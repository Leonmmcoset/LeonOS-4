"""ELF64 loader for LeonOS ET_EXEC and dynamic ET_DYN applications."""

from __future__ import annotations

import os
import random
from dataclasses import dataclass, field
from pathlib import Path

from elftools.elf.elffile import ELFFile

from . import constants as C
from .errors import GuestFault
from .memory import GuestMemory


PT_LOAD = "PT_LOAD"
PT_INTERP = "PT_INTERP"
ET_EXEC = "ET_EXEC"
ET_DYN = "ET_DYN"
PF_X, PF_W, PF_R = 1, 2, 4


@dataclass
class DynamicLaunch:
    main_base: int
    main_entry: int
    main_phdr: int
    interp_base: int
    interp_entry: int
    abi_major: int = 1
    random: bytes = b""
    main_path: str = ""


@dataclass
class LoadedELF:
    path: Path
    entry: int
    low: int
    high: int
    load_bias: int = 0
    elf_type: str = ET_EXEC
    interp_path: str | None = None
    dynamic_launch: DynamicLaunch | None = None
    segments: list[tuple[int, int, int]] = field(default_factory=list)


class ELFLoader:
    def __init__(self, memory: GuestMemory, root_dir: str | Path | None = None):
        self.memory = memory
        self.root_dir = Path(root_dir).resolve() if root_dir is not None else None

    def _base_for(self, elf: ELFFile, *, preferred: int | None = None) -> int:
        if preferred is not None:
            return preferred
        low = min((int(s["p_vaddr"]) for s in elf.iter_segments() if s["p_type"] == PT_LOAD), default=0)
        # Keep ET_DYN images below the stack and away from the mmap allocator.
        rng = random.SystemRandom()
        # Keep the complete image below the user stack.  The previous broad
        # range could place the interpreter above USER_TOP on small guests.
        max_base = max(0x1000000, C.USER_TOP - 0x1000000)
        slots = max(1, (max_base - 0x1000000) // 0x200000)
        span = max(int(s["p_vaddr"]) + int(s["p_memsz"])
                   for s in elf.iter_segments() if s["p_type"] == PT_LOAD)
        for _ in range(32):
            base = 0x1000000 + rng.randrange(0, slots + 1) * 0x200000
            base -= low & 0x1FFFFF
            start, end = self.memory.align_down(base + low), self.memory.align_up(base + span)
            if not any(start < old + old_size and old < end
                       for old, old_size in self.memory.allocations.items()):
                return base
        # Deterministic fallback: scan the available range for a free window.
        for base in range(0x1000000, max_base + 1, 0x200000):
            base -= low & 0x1FFFFF
            start, end = self.memory.align_down(base + low), self.memory.align_up(base + span)
            if not any(start < old + old_size and old < end
                       for old, old_size in self.memory.allocations.items()):
                return base
        raise GuestFault("unable to find free ASLR range for ELF image")

    def _load_file(self, path: Path, *, base: int | None = None, interpreter: bool = False) -> tuple[LoadedELF, ELFFile]:
        fp = path.open("rb")
        try:
            elf = ELFFile(fp)
            if elf.elfclass != 64 or elf.header["e_machine"] != "EM_X86_64":
                raise GuestFault("only ELF64 x86_64 is supported")
            kind = elf.header["e_type"]
            if kind not in (ET_EXEC, ET_DYN):
                raise GuestFault(f"unsupported ELF type {kind}")
            if interpreter and kind != ET_DYN:
                raise GuestFault("ELF interpreter must be ET_DYN")
            load_segments = [s for s in elf.iter_segments() if s["p_type"] == PT_LOAD]
            if not load_segments:
                raise GuestFault("ELF has no PT_LOAD segments")
            bias = 0 if kind == ET_EXEC else self._base_for(elf, preferred=base)
            low, high = C.USER_TOP, C.USER_BASE
            records: list[tuple[int, int, int]] = []
            for seg in load_segments:
                vaddr = int(seg["p_vaddr"]) + bias
                filesz, memsz = int(seg["p_filesz"]), int(seg["p_memsz"])
                if memsz < filesz or vaddr < C.USER_BASE or vaddr + memsz > C.USER_TOP:
                    raise GuestFault("ELF PT_LOAD outside LeonOS user range")
                start = self.memory.align_down(vaddr)
                size = self.memory.align_up((vaddr - start) + memsz)
                if self.memory.reserve(start, size) < 0:
                    raise GuestFault(f"ELF segment overlaps mapped memory at 0x{start:x}")
                data = seg.data()
                if len(data) != filesz:
                    raise GuestFault("short ELF segment read")
                if filesz:
                    self.memory.write(vaddr, data)
                if memsz > filesz:
                    self.memory.write(vaddr + filesz, b"\0" * (memsz - filesz))
                prot = 0x1 | (0x2 if seg["p_flags"] & PF_W else 0) | (0x4 if seg["p_flags"] & PF_X else 0)
                self.memory.protect(start, size, prot)
                records.append((start, size, prot))
                low, high = min(low, start), max(high, vaddr + memsz)
            entry = int(elf.header["e_entry"]) + bias
            if not (C.USER_BASE <= entry < C.USER_TOP):
                raise GuestFault(f"ELF entry outside LeonOS user range: 0x{entry:x}")
            interp = None
            for seg in elf.iter_segments():
                if seg["p_type"] == PT_INTERP:
                    interp = seg.data().split(b"\0", 1)[0].decode("ascii", "replace")
                    break
            loaded = LoadedELF(path, entry, low, high, bias, kind, interp, segments=records)
            return loaded, elf
        finally:
            fp.close()

    def load(self, path: str | Path) -> LoadedELF:
        main_path = Path(path)
        loaded, elf = self._load_file(main_path)
        if loaded.elf_type != ET_DYN or not loaded.interp_path:
            return loaded
        if loaded.interp_path != "0:/system/lib/ld-leonos.elf":
            raise GuestFault(f"unsupported ELF interpreter: {loaded.interp_path}")
        interp_path = self._guest_to_host(loaded.interp_path)
        interp, _ = self._load_file(interp_path, interpreter=True)
        phdr_vaddr = 0
        with main_path.open("rb") as main_fp:
            main_elf = ELFFile(main_fp)
            for seg in main_elf.iter_segments():
                if seg["p_type"] == "PT_PHDR":
                    phdr_vaddr = int(seg["p_vaddr"]) + loaded.load_bias
                    break
        loaded.dynamic_launch = DynamicLaunch(
            loaded.load_bias, loaded.entry, phdr_vaddr, interp.load_bias, interp.entry,
            1, os.urandom(16), self._guest_path(main_path),
        )
        loaded.entry = interp.entry
        loaded.low = min(loaded.low, interp.low)
        loaded.high = max(loaded.high, interp.high)
        return loaded

    def _guest_path(self, path: Path) -> str:
        # Callers normally pass a path below the selected guest root.  Keep
        # Windows drive prefixes out of argv/startup records when a relative
        # path is supplied, while retaining a useful normalized fallback.
        try:
            path = path.resolve().relative_to(self.root_dir) if self.root_dir else path
        except ValueError:
            pass
        text = path.as_posix().replace("\\", "/")
        if ":/" in text:
            text = text.split(":/", 1)[1]
        return "0:/" + text.lstrip("/")

    def _guest_to_host(self, path: str) -> Path:
        if self.root_dir is None or not path.startswith("0:/"):
            raise GuestFault("dynamic interpreter path requires a guest root")
        target = (self.root_dir / Path(*path[3:].split("/"))).resolve()
        try:
            target.relative_to(self.root_dir)
        except ValueError as exc:
            raise GuestFault("interpreter path escapes guest root") from exc
        return target
