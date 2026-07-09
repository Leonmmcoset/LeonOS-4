"""Unicorn-powered LeonOS ELF runner."""

from __future__ import annotations

import time
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
    ):
        self.elf_path = Path(elf_path)
        self.root_dir = Path(root_dir)
        self.argv = [self.elf_path.name] + list(argv or [])
        self.envp: list[str] = []
        self.config = config or HostConfig(root_dir=str(root_dir), last_elf=str(elf_path))
        self.logger = logger or LogBuffer()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.memory = GuestMemory(self.uc)
        self.memory.map_user_space()
        self.loaded: LoadedELF = ELFLoader(self.memory).load(self.elf_path)
        self.fs = GuestFS(self.root_dir, language=self.config.language, logger=self.logger)
        if gui is None:
            from .gui import GUIManager

            gui = GUIManager(logger=self.logger)
        self.gui = gui
        if hasattr(self.gui, "set_present_callback"):
            self.gui.set_present_callback(self._stop_current_slice)
        self.net = NetworkManager(logger=self.logger)
        self.dispatcher = SyscallDispatcher(memory=self.memory, fs=self.fs, gui=self.gui, net=self.net, logger=self.logger, config=self.config)
        self.stop_requested = False
        self.fault_message: str | None = None
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
        self.uc.reg_write(UC_X86_REG_RFLAGS, 0x202)

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
        if self.fault_message:
            raise GuestFault(self.fault_message)
        return not self.stop_requested and self.exit_code is None

    def log_registers(self) -> None:
        regs = {
            "rip": self.uc.reg_read(UC_X86_REG_RIP),
            "rax": self.uc.reg_read(UC_X86_REG_RAX),
            "rdi": self.uc.reg_read(UC_X86_REG_RDI),
            "rsi": self.uc.reg_read(UC_X86_REG_RSI),
            "rdx": self.uc.reg_read(UC_X86_REG_RDX),
            "r10": self.uc.reg_read(UC_X86_REG_R10),
            "r8": self.uc.reg_read(UC_X86_REG_R8),
            "r9": self.uc.reg_read(UC_X86_REG_R9),
        }
        self.logger.write(" ".join(f"{name}=0x{value:x}" for name, value in regs.items()))
