#!/usr/bin/env python3
"""Build the poll-only libevent core used by the LeonOS tmux port."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


SOURCES = (
    "buffer.c", "bufferevent.c", "bufferevent_filter.c", "bufferevent_pair.c",
    "bufferevent_ratelim.c", "bufferevent_sock.c", "event.c", "evmap.c", "evthread.c", "evutil.c",
    "evutil_rand.c", "evutil_time.c", "listener.c", "log.c", "poll.c",
    "signal.c", "strlcpy.c",
)


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def resource_headers() -> Path:
    result = subprocess.run(["clang", "-print-resource-dir"], check=True,
                            text=True, capture_output=True)
    headers = Path(result.stdout.strip()) / "include"
    if not headers.is_dir():
        raise SystemExit(f"clang resource headers are missing: {headers}")
    return headers


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    args = parser.parse_args()

    source = args.source.resolve()
    port = args.port.resolve()
    work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    for required in (source / "event.c", source / "include/event2/event.h",
                     port / "include/event2/event-config.h",
                     port / "include/evconfig-private.h"):
        if not required.exists():
            raise SystemExit(f"required libevent input is missing: {required}")
    if work_dir.exists():
        shutil.rmtree(work_dir)
    objects = work_dir / "objects"
    objects.mkdir(parents=True)
    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-fPIC",
        "-mno-red-zone", "-mgeneral-regs-only", "-ffunction-sections",
        "-fdata-sections", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE", "-nostdinc",
        "-isystem", str(resource_headers()),
        "-I" + str(port / "include"), "-I" + str(source / "include"),
        "-I" + str(source), "-I" + str(args.picolibc_prefix.resolve() / "include"),
        "-I" + str(args.leonos_libc_include.resolve()),
        "-I" + str(args.leonos_include.resolve()),
    ]
    outputs: list[Path] = []
    for name in SOURCES:
        source_file = source / name
        object_file = objects / name.removesuffix(".c").__add__(".o")
        run(["clang", *flags, "-c", str(source_file), "-o", str(object_file)])
        outputs.append(object_file)
    compat = port / "leonos_compat.c"
    compat_object = objects / "leonos_compat.o"
    run(["clang", *flags, "-c", str(compat), "-o", str(compat_object)])
    outputs.append(compat_object)
    output.parent.mkdir(parents=True, exist_ok=True)
    run(["llvm-ar", "rcs", str(output), *map(str, outputs)])
    revision = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                              check=True, text=True, capture_output=True).stdout.strip()
    args.stamp.parent.mkdir(parents=True, exist_ok=True)
    args.stamp.write_text(json.dumps({
        "libevent_commit": revision,
        "libevent_version": "2.1.12-stable",
        "port_sha256": hashlib.sha256(
            (port / "include/event2/event-config.h").read_bytes() +
            (port / "include/evconfig-private.h").read_bytes()
        ).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
