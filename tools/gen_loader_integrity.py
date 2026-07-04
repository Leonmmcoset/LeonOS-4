#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def c_array(name: str, data: bytes) -> str:
    values = ", ".join(f"0x{byte:02x}" for byte in data)
    return f"static const unsigned char {name}[32] = {{ {values} }};"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate loader built-in image hashes.")
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--middlelayer", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    kernel_hash = hashlib.sha256(args.kernel.read_bytes()).digest()
    middlelayer_hash = hashlib.sha256(args.middlelayer.read_bytes()).digest()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        "\n".join(
            [
                "#ifndef LEONOS_LOADER_INTEGRITY_H",
                "#define LEONOS_LOADER_INTEGRITY_H",
                "",
                "#define LEONOS_LOADER_INTEGRITY_SHA256_LEN 32u",
                c_array("LEONOS_LOADER_KERNEL_SHA256", kernel_hash),
                c_array("LEONOS_LOADER_MIDDLELAYER_SHA256", middlelayer_hash),
                "",
                "#endif",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
