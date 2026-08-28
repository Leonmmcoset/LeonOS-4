#!/usr/bin/env python3
from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path


def read_build_number(path: Path) -> int:
    try:
        text = path.read_text(encoding="utf-8").strip()
        return int(text) if text else 0
    except FileNotFoundError:
        return 0
    except ValueError:
        return 0


def c_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LeonOS build metadata")
    parser.add_argument("--header", default="include/generated/build_info.h")
    parser.add_argument("--state", default="build/version/build_number.txt")
    parser.add_argument("--major", type=int, default=4)
    parser.add_argument("--minor", type=int, default=6)
    parser.add_argument("--patch", type=int, default=0)
    parser.add_argument("--kernel-name", default="ntclks")
    parser.add_argument("--middlelayer-name", default="osmlayer")
    args = parser.parse_args()

    header = Path(args.header)
    state = Path(args.state)
    state.parent.mkdir(parents=True, exist_ok=True)
    header.parent.mkdir(parents=True, exist_ok=True)

    build_number = read_build_number(state) + 1
    now = datetime.now()
    build_suffix = f"{build_number:04d}"
    version = f"{args.major}.{args.minor}.{args.patch}-{build_suffix}"
    build_time = now.strftime("%Y-%m-%d %H:%M:%S")
    copyright_text = (
        f"(C) LeonMMcoset 2021-{now.year}. Open source as Apache 2.0 license."
    )

    text = f"""#ifndef LEONOS_GENERATED_BUILD_INFO_H
#define LEONOS_GENERATED_BUILD_INFO_H

#define LEONOS_KERNEL_NAME "{c_escape(args.kernel_name)}"
#define LEONOS_KERNEL_VERSION_MAJOR {args.major}
#define LEONOS_KERNEL_VERSION_MINOR {args.minor}
#define LEONOS_KERNEL_VERSION_PATCH {args.patch}
#define LEONOS_BUILD_NUMBER {build_number}
#define LEONOS_KERNEL_VERSION "{c_escape(version)}"
#define LEONOS_MIDDLELAYER_NAME "{c_escape(args.middlelayer_name)}"
#define LEONOS_BUILD_TIME "{c_escape(build_time)}"
#define LEONOS_COPYRIGHT_YEAR {now.year}
#define LEONOS_COPYRIGHT "{c_escape(copyright_text)}"

#endif
"""

    tmp = header.with_suffix(header.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8", newline="\n")
    tmp.replace(header)
    state.write_text(f"{build_number}\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
