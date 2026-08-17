"""Unicorn-powered LeonOS ELF runner."""

from __future__ import annotations

import time
import struct
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_HOOK_INTR, UC_MODE_64, Uc, UcError
from unicorn.x86_const import UC_X86_REG_R10, UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RDX, UC_X86_REG_RFLAGS, UC_X86_REG_RIP, UC_X86_REG_RSI, UC_X86_REG_RSP

from . import constants as C
from .config import HostConfig
from .elf_loader import ELFLoader, LoadedELF
from .errors import GuestFault, UnsupportedABI
from .fs import GuestFS
from .logging import LogBuffer
from .memory import GuestMemory
from .net import NetworkManager
from .syscalls import SyscallDispatcher


DEFAULT_INSTRUCTION_SLICE = 250000


class LeonOSEmulator:
    def __init__(
        self,
        elf_path: str | Path,
        root_dir: str | Path,
        argv: list[str] | None = None,
        *,
        config: HostConfig | None = None,
        gui=None,
        logger: LogBuffer | None = None,
        pid: int = 100,
        ppid: int = 0,
        process_manager=None,
        envp: list[str] | None = None,
        argv0: str | None = None,
    ):
        self.elf_path = Path(elf_path)
        self.root_dir = Path(root_dir)
        self.argv = [argv0 or self.elf_path.name] + list(argv or [])
        self.envp: list[str] = list(envp or [])
        self.config = config or HostConfig(root_dir=str(root_dir), last_elf=str(elf_path))
        self.pid = int(pid)
        self.ppid = int(ppid)
        self.process_manager = process_manager
        self.logger = logger or LogBuffer()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.memory = GuestMemory(self.uc)
        self.memory.map_user_space()
        self.loaded: LoadedELF = ELFLoader(self.memory, self.root_dir).load(self.elf_path)
        self.fs = GuestFS(self.root_dir, language=self.config.language,
                          ui_theme=self.config.ui_theme, logger=self.logger)
        if gui is None:
            from .gui import GUIManager

            gui = GUIManager(logger=self.logger, ui_theme=self.config.ui_theme,
                             allow_theme_changes=self.config.guest_admin)
        self.gui = gui
        if hasattr(self.gui, "configure_appearance"):
            self.gui.configure_appearance(self.config.ui_theme, self.config.guest_admin)
        if hasattr(self.gui, "set_theme_change_callback"):
            self.gui.set_theme_change_callback(self.fs.set_ui_theme)
        if hasattr(self.gui, "set_present_callback"):
            self.gui.set_present_callback(self._stop_current_slice)
        self.net = NetworkManager(logger=self.logger)
        self.dispatcher = SyscallDispatcher(
            memory=self.memory, fs=self.fs, gui=self.gui, net=self.net, logger=self.logger,
            config=self.config, pid=self.pid, execve_callback=self._execve,
            wait_callback=self._wait4,
        )
        self.stop_requested = False
        self.fault_message: str | None = None
        self._pending_exec: tuple[str, list[str], list[str]] | None = None
        self.children: dict[int, "LeonOSEmulator"] = {}
        self._install_hooks()
        self._prepare_entry()
        self.logger.write(
            f"[los2w] loaded elf={self.elf_path} root={self.root_dir} "
            f"entry=0x{self.loaded.entry:x} image=0x{self.loaded.low:x}-0x{self.loaded.high:x}"
        )

    @property
    def exit_code(self) -> int | None:
        return self.dispatcher.exit_code

    def _install_hooks(self) -> None:
        self.uc.hook_add(UC_HOOK_INTR, self._hook_interrupt)

    def _stop_current_slice(self) -> None:
        try:
            self.uc.emu_stop()
        except UcError:
            pass

    def _hook_interrupt(self, uc, intno, _user_data) -> None:
        if intno != 0x80:
            self.fault_message = f"unsupported interrupt int=0x{intno:x}"
            uc.emu_stop()
            return
        try:
            self.dispatcher.dispatch_from_uc(uc)
        except UnsupportedABI as exc:
            self.fault_message = str(exc)
            self.logger.write(f"[los2w] unsupported ABI: {exc}")
            self.log_registers()
            uc.emu_stop()
        except GuestFault as exc:
            self.fault_message = str(exc)
            self.logger.write(f"[los2w] guest fault: {exc}")
            self.log_registers()
            uc.emu_stop()
        except Exception as exc:
            self.fault_message = f"internal los2w syscall error: {exc}"
            self.logger.write(f"[los2w] internal syscall error: {exc}")
            self.log_registers()
            uc.emu_stop()

    def _prepare_entry(self) -> None:
        argv_base, envp_base = self._write_exec_vectors(self.argv, self.envp)
        self.uc.reg_write(UC_X86_REG_RIP, self.loaded.entry)
        self.uc.reg_write(UC_X86_REG_RSP, envp_base)
        self.uc.reg_write(UC_X86_REG_RDI, len(self.argv))
        self.uc.reg_write(UC_X86_REG_RSI, argv_base)
        self.uc.reg_write(UC_X86_REG_RDX, envp_base)
        if self.loaded.dynamic_launch:
            launch = self.loaded.dynamic_launch
            launch_addr = self.memory.allocate(0x400)
            if launch_addr < 0:
                raise GuestFault("unable to allocate dynamic launch record")
            path = launch.main_path.encode("utf-8", "replace")[:259] + b"\0"
            data = struct.pack("<QQQQQII16s", launch.main_base, launch.main_entry,
                               launch.main_phdr, launch.interp_base, launch.interp_entry,
                               launch.abi_major, 0, launch.random[:16].ljust(16, b"\0"))
            data += path.ljust(260, b"\0")
            self.memory.write(launch_addr, data)
            self.uc.reg_write(UC_X86_REG_R8, launch_addr)
        self.uc.reg_write(UC_X86_REG_RFLAGS, 0x202)

    def _execve(self, path: str, argv: list[str], envp: list[str], _uc=None) -> int:
        if not path:
            return -2
        guest = self.fs.guest_abs(path)
        try:
            host = self.fs.host_path(guest)
        except Exception:
            return -13
        if not host.is_file():
            return -2
        args = list(argv) or [guest]
        if self.process_manager is None:
            return -38
        try:
            child = self.process_manager.spawn(
                str(host), args[1:] if args and len(args) > 1 else [], ppid=self.pid,
                envp=list(envp), argv0=args[0] if args else guest,
            )
            self.children[child.pid] = child
            self.logger.write(f"[los2w] execve parent={self.pid} child={child.pid} path={guest}")
            return child.pid
        except GuestFault as exc:
            self.logger.write(f"[los2w] execve failed path={guest}: {exc}")
            return -(exc.errno or 2)

    def _wait4(self, pid: int, status_ptr: int, options: int, _rusage: int) -> int:
        if self.process_manager:
            return self.process_manager.wait4(self, pid, status_ptr, options)
        child = self.children.get(pid)
        if child and child.exit_code is not None:
            if status_ptr:
                self.memory.write_u64(status_ptr, (int(child.exit_code) & 0xff) << 8)
            self.children.pop(pid, None)
            return pid
        return -10

    def _apply_pending_exec(self) -> None:
        if not self._pending_exec:
            return
        path, argv, envp = self._pending_exec
        self._pending_exec = None
        # Reinitialize the current process in-place so its PID and open host
        # services remain stable while its address space becomes a new image.
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.memory = GuestMemory(self.uc)
        self.memory.map_user_space()
        self.elf_path = Path(path)
        self.argv = argv
        self.envp = envp
        self.loaded = ELFLoader(self.memory, self.root_dir).load(self.elf_path)
        self.dispatcher.memory = self.memory
        self.dispatcher.exit_code = None
        self.dispatcher.pid = self.pid
        self._install_hooks()
        self._prepare_entry()
        self.logger.write(f"[los2w] execve pid={self.pid} elf={self.elf_path}")

    def _write_exec_vectors(self, argv: list[str], envp: list[str]) -> tuple[int, int]:
        strings = bytearray()
        offsets: list[int] = []
        for item in argv + envp:
            offsets.append(len(strings))
            strings += item.encode("utf-8", "replace") + b"\0"
        sp = C.USER_STACK_TOP
        strings_base = (sp - len(strings)) & ~0xF
        argv_base = (strings_base - (len(argv) + 1) * 8) & ~0xF
        envp_base = (argv_base - (len(envp) + 1) * 8) & ~0xF
        stack_low = C.USER_STACK_TOP - C.USER_STACK_PAGES * C.PAGE_SIZE
        if envp_base < stack_low:
            raise GuestFault("argv/envp does not fit in LeonOS stack")
        self.memory.write(strings_base, strings)
        for idx, _item in enumerate(argv):
            self.memory.write_u64(argv_base + idx * 8, strings_base + offsets[idx])
        self.memory.write_u64(argv_base + len(argv) * 8, 0)
        for idx, _item in enumerate(envp):
            self.memory.write_u64(envp_base + idx * 8, strings_base + offsets[len(argv) + idx])
        self.memory.write_u64(envp_base + len(envp) * 8, 0)
        return argv_base, envp_base

    def stop(self) -> None:
        self.stop_requested = True
        try:
            self.uc.emu_stop()
        except UcError:
            pass

    def run(self, *, max_seconds: float | None = None, smoke: bool = False) -> int | None:
        start = time.monotonic()
        while not self.stop_requested and self.exit_code is None:
            if max_seconds is not None and time.monotonic() - start >= max_seconds:
                break
            if smoke and getattr(self.gui, "present_count", 0) > 0:
                return self.exit_code
            self.run_step()
        if self.fault_message:
            raise GuestFault(self.fault_message)
        return self.exit_code

    def run_step(self, instruction_count: int = DEFAULT_INSTRUCTION_SLICE, timeout_us: int = 0, *, pump_events: bool = True) -> bool:
        if self.stop_requested or self.exit_code is not None:
            return False
        if self.fault_message:
            raise GuestFault(self.fault_message)
        if pump_events and self.gui:
            self.gui.process_events()
        rip = self.uc.reg_read(UC_X86_REG_RIP)
        try:
            self.uc.emu_start(rip, 0, timeout=max(0, int(timeout_us)), count=max(1, int(instruction_count)))
        except UcError as exc:
            self.fault_message = f"unicorn execution error at rip=0x{self.uc.reg_read(UC_X86_REG_RIP):x}: {exc}"
            self.logger.write(f"[los2w] {self.fault_message}")
            self.log_registers()
            raise GuestFault(self.fault_message) from exc
        if pump_events and self.gui:
            self.gui.process_events()
        if self._pending_exec and self.exit_code is None:
            self._apply_pending_exec()
            return True
        if self.fault_message:
            raise GuestFault(self.fault_message)
        return not self.stop_requested and self.exit_code is None

    def log_registers(self) -> None:
        regs = self.register_snapshot()
        self.logger.write(" ".join(f"{name}=0x{value:x}" for name, value in regs.items()))

    def register_snapshot(self) -> dict[str, int]:
        return {
            "rip": self.uc.reg_read(UC_X86_REG_RIP),
            "rax": self.uc.reg_read(UC_X86_REG_RAX),
            "rdi": self.uc.reg_read(UC_X86_REG_RDI),
            "rsi": self.uc.reg_read(UC_X86_REG_RSI),
            "rdx": self.uc.reg_read(UC_X86_REG_RDX),
            "r10": self.uc.reg_read(UC_X86_REG_R10),
            "r8": self.uc.reg_read(UC_X86_REG_R8),
            "r9": self.uc.reg_read(UC_X86_REG_R9),
        }


class ProcessManager:
    """Small round-robin process table for los2w guest programs."""

    def __init__(self, *, root_dir, config=None, gui=None, logger=None):
        self.root_dir = Path(root_dir)
        self.config = config
        self.gui = gui
        self.logger = logger or LogBuffer()
        self.processes: dict[int, LeonOSEmulator] = {}
        self.next_pid = 100
        self.current_pid: int | None = None

    def _stop_current_slice(self) -> None:
        if self.current_pid is None:
            return
        process = self.processes.get(self.current_pid)
        if process:
            try:
                process.uc.emu_stop()
            except UcError:
                pass

    def spawn(self, elf_path, argv=None, *, ppid=0, envp=None, argv0=None) -> LeonOSEmulator:
        pid = self.next_pid
        self.next_pid += 1
        process = LeonOSEmulator(elf_path, self.root_dir, argv, config=self.config,
                                 gui=self.gui, logger=self.logger, pid=pid,
                                 ppid=ppid, process_manager=self, envp=envp,
                                 argv0=argv0)
        self.processes[pid] = process
        if self.gui and hasattr(self.gui, "set_present_callback"):
            self.gui.set_present_callback(self._stop_current_slice)
        return process

    def run_step(self, instruction_count=50000) -> bool:
        alive = False
        for pid, process in list(self.processes.items()):
            if process.exit_code is not None or process.stop_requested:
                continue
            self.current_pid = pid
            process.run_step(instruction_count, pump_events=False)
            alive = alive or process.exit_code is None
        self.current_pid = None
        return alive

    def wait4(self, parent, pid: int, status_ptr: int, options: int = 0) -> int:
        candidates = [p for p in self.processes.values() if p.ppid == parent.pid and (pid <= 0 or p.pid == pid)]
        for child in candidates:
            if child.exit_code is not None:
                if status_ptr:
                    parent.memory.write_u64(status_ptr, (int(child.exit_code) & 0xff) << 8)
                self.processes.pop(child.pid, None)
                return child.pid
        return 0 if options & 1 else -10

    def stop(self) -> None:
        for process in self.processes.values():
            process.stop()
