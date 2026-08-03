#!/usr/bin/env python3
"""Generate the compact oschinpt dictionary offset index."""

from __future__ import annotations

import argparse
import os
import struct
from pathlib import Path


MAGIC = b"OSCI"
VERSION = 1
CODE_LENGTH = 8
HEADER_FORMAT = "<4sIII"
ENTRY_FORMAT = "<8sII"


def dictionary_code(line: bytes) -> bytes | None:
    """Return a simple pinyin code from a dictionary entry, if it has one."""
    if not line or line.startswith(b"#"):
        return None
    fields = line.rstrip(b"\r\n").split(b"\t")
    if len(fields) < 2 or not fields[0]:
        return None
    code = fields[1].split(b" ", 1)[0]
    if not code or len(code) >= CODE_LENGTH:
        return None
    if not all(ord("a") <= byte <= ord("z") for byte in code):
        return None
    return code


def build_index(dictionary: Path, output: Path) -> int:
    data = dictionary.read_bytes()
    if len(data) > 0xFFFFFFFF:
        raise ValueError("dictionary is too large for the oschinpt index format")

    entries: list[tuple[bytes, int, int]] = []
    last_code: bytes | None = None
    offset = 0
    for line in data.splitlines(keepends=True):
        end = offset + len(line)
        code = dictionary_code(line)
        if code is not None:
            if entries and last_code == code:
                previous_code, start, _ = entries[-1]
                entries[-1] = (previous_code, start, end)
            else:
                entries.append((code, offset, end))
            last_code = code
        offset = end

    if not entries:
        raise ValueError("dictionary has no indexable simple-pinyin entries")

    payload = bytearray(struct.pack(HEADER_FORMAT, MAGIC, VERSION, len(entries), len(data)))
    for code, start, end in sorted(entries):
        payload.extend(struct.pack(ENTRY_FORMAT, code.ljust(CODE_LENGTH, b"\0"), start, end))

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        temporary.write_bytes(payload)
        os.replace(temporary, output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    print(f"Created {output} ({len(entries)} entries, {len(payload)} bytes)")
    return len(entries)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build_index(args.input, args.output)


if __name__ == "__main__":
    main()
