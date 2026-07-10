"""LeonOS int 0x80 syscall dispatcher."""

from __future__ import annotations

import struct
import time
import unicodedata
from collections import Counter

from unicorn.x86_const import UC_X86_REG_R10, UC_X86_REG_R8, UC_X86_REG_R9, UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RDX, UC_X86_REG_RIP, UC_X86_REG_RSI

from . import constants as C
from . import structs
from .errors import ECHILD, EFAULT, EINVAL, ENOMEM, ENOSYS, UnsupportedABI, neg


def u64(value: int) -> int:
    return value & 0xFFFFFFFFFFFFFFFF


def s64(value: int) -> int:
    value &= 0xFFFFFFFFFFFFFFFF
    return value - (1 << 64) if value & (1 << 63) else value


class SyscallDispatcher:
    def __init__(self, *, memory, fs, gui, net, logger, config):
        self.memory = memory
        self.fs = fs
        self.gui = gui
        self.net = net
        self.logger = logger
        self.config = config
        self.exit_code: int | None = None
        self.pid = 100
        self.syscall_counts: Counter[str] = Counter()
        self.ioctl_counts: Counter[str] = Counter()
        self.unsupported_syscalls: set[str] = set()
        self.unsupported_ioctls: set[str] = set()

    def dispatch_from_uc(self, uc) -> int:
        number = uc.reg_read(UC_X86_REG_RAX)
        args = (
            uc.reg_read(UC_X86_REG_RDI),
            uc.reg_read(UC_X86_REG_RSI),
            uc.reg_read(UC_X86_REG_RDX),
            uc.reg_read(UC_X86_REG_R10),
            uc.reg_read(UC_X86_REG_R8),
            uc.reg_read(UC_X86_REG_R9),
        )
        ret = self.dispatch(number, args, uc)
        uc.reg_write(UC_X86_REG_RAX, u64(ret))
        return ret

    def dispatch(self, number: int, args: tuple[int, int, int, int, int, int], uc=None) -> int:
        try:
            syscall_name = C.SYSCALL_NAMES.get(number, f"syscall_{number}")
            self.syscall_counts[syscall_name] += 1
            if number == C.SYS_READ:
                return self._read(args[0], args[1], args[2])
            if number == C.SYS_WRITE:
                return self._write(args[0], args[1], args[2])
            if number == C.SYS_OPEN:
                return self.fs.open(self.memory.read_cstr(args[0], C.FS_PATH_LEN), int(args[1]), int(args[2]))
            if number == C.SYS_CLOSE:
                return self.fs.close(int(args[0]))
            if number == C.SYS_STAT:
                return self._stat(args[0], args[1])
            if number == C.SYS_FSTAT:
                return self._fstat(args[0], args[1])
            if number == C.SYS_LSEEK:
                return self.fs.lseek(int(args[0]), s64(args[1]), int(args[2]))
            if number == C.SYS_MMAP:
                return self._mmap(*args)
            if number == C.SYS_MUNMAP:
                return self.memory.free(args[0], args[1])
            if number == C.SYS_IOCTL:
                return self._ioctl(args[0], args[1], args[2])
            if number == C.SYS_SCHED_YIELD:
                self.gui.process_events()
                return 0
            if number == C.SYS_NANOSLEEP:
                if hasattr(self.gui, "idle_sleep"):
                    self.gui.idle_sleep(int(args[0]))
                else:
                    time.sleep(min(max(args[0], 0), 2) / 1000.0)
                return 0
            if number == C.SYS_GETPID:
                return self.pid
            if number == C.SYS_EXECVE:
                path = self.memory.read_cstr(args[0], C.FS_PATH_LEN)
                raise UnsupportedABI(f"multi-process execve is not implemented in v1: {path}")
            if number == C.SYS_EXIT:
                self.exit_code = int(args[0])
                if uc:
                    uc.emu_stop()
                return 0
            if number == C.SYS_WAIT4:
                return neg(ECHILD)
            if number == C.SYS_GETCWD:
                return self._getcwd(args[0], args[1])
            if number == C.SYS_CHDIR:
                return self.fs.chdir(self.memory.read_cstr(args[0], C.FS_PATH_LEN))
            if number == C.SYS_RENAME:
                old = self.memory.read_cstr(args[0], C.FS_PATH_LEN)
                new = self.memory.read_cstr(args[1], C.FS_PATH_LEN)
                return self.fs.rename(old, new)
            if number == C.SYS_MKDIR:
                return self.fs.mkdir(self.memory.read_cstr(args[0], C.FS_PATH_LEN))
            if number == C.SYS_RMDIR:
                return self.fs.rmdir(self.memory.read_cstr(args[0], C.FS_PATH_LEN))
            if number == C.SYS_UNLINK:
                return self.fs.unlink(self.memory.read_cstr(args[0], C.FS_PATH_LEN))
            self.unsupported_syscalls.add(syscall_name)
            raise UnsupportedABI(f"unsupported syscall {syscall_name} ({number})")
        except UnsupportedABI:
            raise
        except Exception as exc:
            self.logger.write(f"[syscall] exception in {C.SYSCALL_NAMES.get(number, number)}: {exc}")
            return neg(EFAULT)

    def _read(self, fd: int, buf: int, count: int) -> int:
        result = self.fs.read(int(fd), int(count))
        if isinstance(result, int):
            return result
        if result:
            self.memory.write(buf, result)
        return len(result)

    def _write(self, fd: int, buf: int, count: int) -> int:
        data = self.memory.read(buf, int(count))
        return self.fs.write(int(fd), data)

    def _stat(self, path_ptr: int, st_ptr: int) -> int:
        result = self.fs.stat_path(self.memory.read_cstr(path_ptr, C.FS_PATH_LEN))
        if isinstance(result, int):
            return result
        kind, size = result
        self.memory.write(st_ptr, structs.pack_stat(kind, size))
        return 0

    def _fstat(self, fd: int, st_ptr: int) -> int:
        result = self.fs.fstat(int(fd))
        if isinstance(result, int):
            return result
        kind, size = result
        self.memory.write(st_ptr, structs.pack_stat(kind, size))
        return 0

    def _mmap(self, addr: int, length: int, prot: int, flags: int, fd: int, offset: int) -> int:
        if not length:
            return neg(EINVAL)
        fixed = addr if flags & C.MAP_FIXED else None
        result = self.memory.allocate(length, fixed=fixed)
        if result < 0:
            return neg(ENOMEM)
        self.memory.write(result, b"\0" * self.memory.align_up(length))
        if not (flags & C.MAP_ANONYMOUS) and s64(fd) >= 0:
            old = self.fs.lseek(int(fd), s64(offset), C.SEEK_SET)
            if old >= 0:
                data = self.fs.read(int(fd), int(length))
                if not isinstance(data, int) and data:
                    self.memory.write(result, data)
        return result

    def _getcwd(self, buf: int, length: int) -> int:
        data = self.fs.cwd.encode("utf-8") + b"\0"
        if not buf or length < len(data):
            return neg(EINVAL)
        self.memory.write(buf, data)
        return buf

    def _ioctl(self, fd: int, request: int, arg: int) -> int:
        name = C.IOCTL_NAMES.get(int(request), f"0x{int(request):x}")
        self.ioctl_counts[name] += 1
        if request == C.IOCTL_LIST_DIR:
            return self._list_dir(arg)
        if request == C.TEXT_IOCTL_LAYOUT_UTF8:
            return self._text_layout_utf8(arg)
        result = self.gui.ioctl(self.memory, int(request), int(arg))
        if result is not None:
            return result
        result = self.net.ioctl(self.memory, int(request), int(arg))
        if result is not None:
            return result
        result = self._system_ioctl(int(request), int(arg))
        if result is not None:
            return result
        self.unsupported_ioctls.add(name)
        raise UnsupportedABI(f"unsupported ioctl {name} fd={fd}")

    def compatibility_snapshot(self) -> dict[str, object]:
        return {
            "syscalls": dict(sorted(self.syscall_counts.items())),
            "ioctls": dict(sorted(self.ioctl_counts.items())),
            "unsupported_syscalls": sorted(self.unsupported_syscalls),
            "unsupported_ioctls": sorted(self.unsupported_ioctls),
        }

    def _list_dir(self, arg: int) -> int:
        query = structs.DirList.unpack(self.memory.read(arg, structs.DirList.SIZE))
        path = self.memory.read_cstr(query.path_ptr, C.FS_PATH_LEN)
        entries = self.fs.list_dir(path)
        if isinstance(entries, int):
            return entries
        count = min(query.capacity, len(entries))
        for idx, (kind, name) in enumerate(entries[:count]):
            self.memory.write(query.entries_ptr + idx * structs.DIR_ENTRY_SIZE, structs.pack_dir_entry(kind, name))
        query.count = count
        self.memory.write(arg, query.pack())
        return 0

    def _text_layout_utf8(self, arg: int) -> int:
        layout = structs.TextLayout.unpack(self.memory.read(arg, structs.TextLayout.SIZE))
        if not layout.text_ptr:
            layout.byte_len = 0
            layout.count = 0
            layout.total_cells = 0
            layout.total_px = 0
            self.memory.write(arg, layout.pack())
            return 0
        data = self.memory.read(layout.text_ptr, layout.byte_len) if layout.byte_len else self.memory.read_cstr(layout.text_ptr, 4096).encode("utf-8")
        glyphs: list[tuple[int, int, int, int]] = []
        offset = 0
        while offset < len(data):
            for size in (1, 2, 3, 4):
                piece = data[offset : offset + size]
                try:
                    char = piece.decode("utf-8")
                    byte_len = size
                    break
                except UnicodeDecodeError:
                    char = "\ufffd"
                    byte_len = 1
            codepoint = ord(char[0]) if char else 0xFFFD
            cell_width = 2 if unicodedata.east_asian_width(char[0] if char else "\ufffd") in ("W", "F") else 1
            glyphs.append((codepoint, offset, byte_len, cell_width))
            offset += max(1, byte_len)
        total_cells = sum(g[3] for g in glyphs)
        capacity = min(layout.capacity, len(glyphs))
        for idx in range(capacity):
            self.memory.write(layout.glyphs_ptr + idx * structs.TEXT_GLYPH_SIZE, structs.pack_text_glyph(*glyphs[idx]))
        layout.count = len(glyphs)
        layout.total_cells = total_cells
        layout.total_px = total_cells * C.FONT_W
        self.memory.write(arg, layout.pack())
        return 0

    def _system_ioctl(self, request: int, arg: int) -> int | None:
        if request == C.INSTALL_IOCTL_LIST_DISKS:
            self.memory.write_u32(arg + 4, 0)
            return 0
        if request == C.AUTH_IOCTL_STATUS:
            self.memory.write(arg, structs.pack_auth_status())
            return 0
        if request == C.AUTH_IOCTL_CURRENT:
            role = C.AUTH_ROLE_ADMIN if getattr(self.config, "guest_admin", True) else C.AUTH_ROLE_USER
            self.memory.write(arg, structs.pack_user(role=role, username=self.config.guest_username, home=self.config.guest_home))
            return 0
        if request == C.TIME_IOCTL_INFO:
            now = int(time.time())
            self.memory.write(arg, structs.pack_time_info(now, int((time.monotonic() - self.gui.start_time) * 1000), time.localtime(now)))
            return 0
        if request == C.TIME_IOCTL_NTP_SYNC:
            sync = structs.TimeSync.unpack(self.memory.read(arg, structs.TimeSync.SIZE))
            server = sync.server or "pool.ntp.org"
            self.memory.write(
                arg,
                structs.TimeSync(
                    timeout_ms=sync.timeout_ms,
                    status=C.NET_STATUS_OK,
                    server_ip=0,
                    valid=1,
                    unix_seconds=int(time.time()),
                    server=server,
                ).pack(),
            )
            self.logger.write(f"[los2w] NTP sync emulated with host clock server={server}")
            return 0
        if request == C.SYSTEM_IOCTL_INFO:
            data = (
                structs.zbytes("LeonOS", 32)
                + structs.zbytes("4 los2w", 32)
                + structs.zbytes("los2w", 32)
                + structs.zbytes(time.strftime("%Y-%m-%d %H:%M:%S"), 32)
                + structs.zbytes("LeonOS compatibility layer", 96)
                + struct.pack("<IIIII", 4, 0, 0, 0, time.localtime().tm_year)
            )
            self.memory.write(arg, data)
            return 0
        if request == C.PERF_IOCTL_INFO:
            uptime = int((time.monotonic() - self.gui.start_time) * 1000)
            self.memory.write(arg, struct.pack("<QQQQQIIII", uptime, 256 * 1024, 128 * 1024, 0, 0, 1, 1, 0, 0))
            return 0
        if request == C.MACHINE_IDENTITY_IOCTL:
            data = (
                struct.pack("<II", 1, 0)
                + structs.zbytes("los2w", 32)
                + structs.zbytes("00000000-0000-0000-0000-000000000000", 37)
                + structs.zbytes("00000000-0000-0000-0000-000000000000", 37)
                + structs.zbytes("00000000-0000-0000-0000-000000000000", 37)
                + structs.zbytes("los2w", 48)
                + struct.pack("<II", 0, 0)
            )
            self.memory.write(arg, data)
            return 0
        return None
