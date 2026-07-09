"""ELF64 loader for LeonOS user applications."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from elftools.elf.elffile import ELFFile

from . import constants as C
from .errors import GuestFault
from .memory import GuestMemory


@dataclass
class LoadedELF:
    path: Path
    entry: int
    low: int
    high: int


class ELFLoader:
    def __init__(self, memory: GuestMemory):
        self.memory = memory

    def load(self, path: str | Path) -> LoadedELF:
        elf_path = Path(path)
        with elf_path.open("rb") as fp:
            elf = ELFFile(fp)
            if elf.elfclass != 64:
                raise GuestFault("only ELF64 is supported")
            header = elf.header
            if header["e_machine"] != "EM_X86_64":
                raise GuestFault(f"unsupported ELF machine {header['e_machine']}")
            if header["e_type"] != "ET_EXEC":
                raise GuestFault(f"unsupported ELF type {header['e_type']}")
            entry = int(header["e_entry"])
            if not (C.USER_BASE <= entry < C.USER_TOP):
                raise GuestFault(f"ELF entry outside LeonOS user range: 0x{entry:x}")

            low = C.USER_TOP
            high = C.USER_BASE
            for segment in elf.iter_segments():
                if segment["p_type"] != "PT_LOAD":
                    continue
                vaddr = int(segment["p_vaddr"])
                filesz = int(segment["p_filesz"])
                memsz = int(segment["p_memsz"])
                if memsz < filesz:
                    raise GuestFault("ELF PT_LOAD memsz is smaller than filesz")
                if vaddr < C.USER_BASE or vaddr + memsz > C.USER_TOP:
                    raise GuestFault(f"ELF segment outside user range vaddr=0x{vaddr:x} memsz=0x{memsz:x}")
                data = segment.data()
                if len(data) != filesz:
                    raise GuestFault("short ELF segment read")
                if filesz:
                    self.memory.write(vaddr, data)
                if memsz > filesz:
                    self.memory.write(vaddr + filesz, b"\0" * (memsz - filesz))
                low = min(low, vaddr)
                high = max(high, vaddr + memsz)

        if low == C.USER_TOP:
            raise GuestFault("ELF has no PT_LOAD segments")
        return LoadedELF(path=elf_path, entry=entry, low=low, high=high)
