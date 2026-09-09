#!/usr/bin/env python3
"""Validate the shipped ELF dependency closure and retired-libc removal."""
import argparse
from pathlib import Path
import re
import subprocess


def validate(root: Path) -> int:
    """Inspect actual ELF headers without executing target code on the host."""
    count = 0
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        assert "picolibc" not in path.name.lower(), path
        assert path.name not in ("newlib.h", "_newlib_version.h", "ld-leonos.elf", "libleonos.so.1"), path
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        headers = subprocess.check_output(["readelf", "-l", "-d", str(path)],
                                           text=True, stderr=subprocess.DEVNULL)
        assert "ld-leonos" not in headers and "libleonos.so.1" not in headers, path
        payload = root / "install/root" if path.is_relative_to(root / "install/root") else root
        for interpreter in re.findall(r"Requesting program interpreter: ([^\]]+)", headers):
            assert interpreter == "/lib/ld-musl-x86_64.so.1", (path, interpreter)
            assert (payload / interpreter.lstrip("/")).is_file(), (root, interpreter)
        for needed in re.findall(r"\(NEEDED\).*\[([^\]]+)\]", headers):
            assert any((payload / directory / needed).is_file()
                       for directory in ("lib", "system/lib")), (path, needed)
        count += 1
    assert count, f"no ELF files in {root}"
    print(f"PASS {root}: {count} ELF files; musl dependency closure; no retired libc artifacts")
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path)
    for root in parser.parse_args().roots:
        validate(root)
