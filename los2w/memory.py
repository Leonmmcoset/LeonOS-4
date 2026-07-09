"""Guest memory helpers over Unicorn."""

from __future__ import annotations

import struct

from unicorn import Uc

from . import constants as C
from .errors import EFAULT, GuestFault, neg


class GuestMemory:
    def __init__(self, uc: Uc):
        self.uc = uc
        self.alloc_next = C.USER_MMAP_BASE
        self.allocations: dict[int, int] = {}

    def map_user_space(self) -> None:
        self.uc.mem_map(C.USER_BASE, C.USER_TOP - C.USER_BASE)

    @staticmethod
    def align_down(value: int, align: int = C.PAGE_SIZE) -> int:
        return value & ~(align - 1)

    @staticmethod
    def align_up(value: int, align: int = C.PAGE_SIZE) -> int:
        return (value + align - 1) & ~(align - 1)

    def range_ok(self, addr: int, size: int = 1) -> bool:
        return C.USER_BASE <= addr and size >= 0 and addr + size <= C.USER_TOP

    def read(self, addr: int, size: int) -> bytes:
        if size < 0 or not self.range_ok(addr, size):
            raise GuestFault(f"guest read outside user range addr=0x{addr:x} size={size}", errno=EFAULT)
        try:
            return bytes(self.uc.mem_read(addr, size))
        except Exception as exc:  # Unicorn raises version-specific exception subclasses.
            raise GuestFault(f"guest read failed addr=0x{addr:x} size={size}: {exc}", errno=EFAULT) from exc

    def write(self, addr: int, data: bytes | bytearray | memoryview) -> None:
        data = bytes(data)
        if not self.range_ok(addr, len(data)):
            raise GuestFault(f"guest write outside user range addr=0x{addr:x} size={len(data)}", errno=EFAULT)
        try:
            self.uc.mem_write(addr, data)
        except Exception as exc:
            raise GuestFault(f"guest write failed addr=0x{addr:x} size={len(data)}: {exc}", errno=EFAULT) from exc

    def read_u64(self, addr: int) -> int:
        return struct.unpack("<Q", self.read(addr, 8))[0]

    def write_u64(self, addr: int, value: int) -> None:
        self.write(addr, struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF))

    def read_u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def write_u32(self, addr: int, value: int) -> None:
        self.write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def read_cstr(self, addr: int, max_len: int = 4096) -> str:
        if not addr:
            return ""
        out = bytearray()
        for offset in range(max_len):
            ch = self.read(addr + offset, 1)[0]
            if ch == 0:
                break
            out.append(ch)
        return out.decode("utf-8", "replace")

    def write_cstr_data(self, addr: int, text: str) -> int:
        data = text.encode("utf-8", "replace") + b"\0"
        self.write(addr, data)
        return len(data)

    def allocate(self, size: int, fixed: int | None = None) -> int:
        size = self.align_up(size)
        if size <= 0 or size > C.USER_TOP - C.USER_BASE:
            return neg(12)
        if fixed is not None:
            start = self.align_down(fixed)
            if not self.range_ok(start, size):
                return neg(12)
            self.allocations[start] = size
            return start
        start = self.align_up(self.alloc_next)
        if start < C.USER_MMAP_BASE:
            start = C.USER_MMAP_BASE
        if start + size >= C.USER_STACK_TOP - C.USER_STACK_PAGES * C.PAGE_SIZE:
            return neg(12)
        self.alloc_next = start + size
        self.allocations[start] = size
        return start

    def free(self, addr: int, size: int) -> int:
        if addr in self.allocations:
            del self.allocations[addr]
        return 0
