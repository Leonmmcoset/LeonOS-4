#!/usr/bin/env python3
"""Build tmux 3.5a with the LeonOS AF_UNIX/PTY portability layer."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


TMUX_COMMIT = "549c35b06165f6ae023115eb76f83f2cbf945395"
COMPAT_SOURCES = (
    "base64.c", "closefrom.c", "err.c", "explicit_bzero.c",
    "freezero.c", "getdtablecount.c", "getdtablesize.c", "getprogname.c",
    "imsg.c", "imsg-buffer.c", "reallocarray.c", "recallocarray.c",
    "strcasestr.c", "strsep.c", "strtonum.c", "unvis.c", "vis.c",
    "cfmakeraw.c", "fdforkpty.c", "fgetln.c", "getopt.c", "getpeereid.c",
    "setproctitle.c",
)


def run(command: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def clang_headers() -> Path:
    result = subprocess.run(["clang", "-print-resource-dir"], check=True,
                            text=True, capture_output=True)
    path = Path(result.stdout.strip()) / "include"
    if not path.is_dir():
        raise SystemExit(f"Clang resource headers are missing: {path}")
    return path


def clang_runtime() -> Path:
    path = Path(subprocess.run(["clang", "-print-resource-dir"], check=True,
                               text=True, capture_output=True).stdout.strip())
    archive = path / "lib/linux/libclang_rt.builtins-x86_64.a"
    if not archive.is_file():
        raise SystemExit(f"Clang x86_64 compiler runtime is missing: {archive}")
    return archive


def revision(source: Path) -> str:
    return subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                          check=True, text=True, capture_output=True).stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--libevent", type=Path, required=True)
    parser.add_argument("--libevent-port", type=Path, required=True)
    parser.add_argument("--libevent-source", type=Path, required=True)
    parser.add_argument("--picolibc-prefix", type=Path, required=True)
    parser.add_argument("--leonos-libc-include", type=Path, required=True)
    parser.add_argument("--leonos-include", type=Path, required=True)
    parser.add_argument("--linker-script", type=Path, required=True)
    parser.add_argument("--leonos-lib", type=Path, required=True)
    parser.add_argument("--dynamic-crt", type=Path, required=True)
    parser.add_argument("--abi-note", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    source = args.source.resolve()
    port = args.port.resolve()
    work = args.work_dir.resolve()
    output = args.output.resolve()
    stamp = args.stamp.resolve()
    if os.name == "nt":
        raise SystemExit("tmux must be built from WSL/Linux, not Windows")
    found_revision = revision(source)
    if found_revision != TMUX_COMMIT:
        raise SystemExit(f"tmux must be pinned to {TMUX_COMMIT}, found {found_revision}")
    for path in (source / "tmux.c", source / "cmd-parse.y", source / "COPYING",
                 port / "leonos_port.c", port / "leonos_termcap.c",
                 port / "include/config.h", port / "include/term.h",
                 args.libevent.resolve(), args.libevent_port.resolve() / "include/event2/event-config.h",
                 args.libevent_source.resolve() / "include/event2/event.h", args.linker_script.resolve(),
                 args.leonos_lib.resolve(), args.dynamic_crt.resolve(), args.abi_note.resolve(),
                 clang_runtime()):
        if not path.exists():
            raise SystemExit(f"required tmux build input is missing: {path}")
    if work.exists():
        shutil.rmtree(work)
    generated = work / "generated"
    objects = work / "objects"
    generated.mkdir(parents=True)
    objects.mkdir()
    run(["bison", "-d", "-o", str(generated / "cmd-parse.c"), str(source / "cmd-parse.y")])

    flags = [
        "-target", "x86_64-unknown-none", *(args.compile_flag or ["-O2"]),
        "-std=gnu11", "-ffreestanding", "-fno-stack-protector", "-fPIC", "-fPIE",
        "-mno-red-zone", "-mgeneral-regs-only", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
        "-Wno-unused-function", "-D_POSIX_C_SOURCE=200809L", "-D_DEFAULT_SOURCE",
        "-DLEONOS_USE_PICOLIBC", "-DHAVE_CONFIG_H", "-include", str(port / "include/config.h"),
        "-Dstat=leonos_tmux_stat", "-Dfstat=leonos_tmux_fstat", "-Dlstat=leonos_tmux_lstat",
        "-Dmkdir=leonos_tmux_mkdir", "-Dunlink=leonos_tmux_unlink",
        "-nostdinc", "-isystem", str(clang_headers()), "-I" + str(port / "include"),
        "-I" + str(generated), "-I" + str(args.libevent_port.resolve() / "include"),
        "-I" + str(args.libevent_source.resolve() / "include"),
        "-I" + str(args.libevent_source.resolve()), "-I" + str(args.picolibc_prefix.resolve() / "include"),
        "-I" + str(args.leonos_libc_include.resolve()), "-I" + str(args.leonos_include.resolve()),
        "-I" + str(source),
    ]
    sources = sorted(source.glob("*.c"))
    sources = [path for path in sources if path.name not in {
        "image.c", "image-sixel.c", "osdep-aix.c", "osdep-cygwin.c",
        "osdep-darwin.c", "osdep-dragonfly.c", "osdep-freebsd.c", "osdep-haiku.c",
        "osdep-hpux.c", "osdep-linux.c", "osdep-netbsd.c", "osdep-openbsd.c",
        "osdep-sunos.c", "osdep-unknown.c",
    }]
    sources.append(source / "osdep-unknown.c")
    sources.append(generated / "cmd-parse.c")
    sources.extend(source / "compat" / name for name in COMPAT_SOURCES)
    sources.extend((port / "leonos_port.c", port / "leonos_termcap.c"))
    object_paths: list[Path] = []
    for source_file in sources:
        if not source_file.exists():
            raise SystemExit(f"tmux source is missing: {source_file}")
        object_file = objects / (source_file.stem + ".o")
        # osdep-unknown and generated parser have unique stems; compat files
        # do not collide with top-level names in this source set.
        run(["clang", *flags, "-c", str(source_file), "-o", str(object_file)])
        object_paths.append(object_file)

    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        "ld.lld", "-nostdlib", "--gc-sections", *args.linker_flag,
        "-pie", "--hash-style=sysv", "--dynamic-linker", "/system/lib/ld-leonos.elf",
        "-z", "relro", "-z", "now", "-z", "max-page-size=0x1000",
        "-T", str(args.linker_script.resolve()), "-o", str(output),
        str(args.dynamic_crt.resolve()), str(args.abi_note.resolve()),
        *map(str, object_paths), "--start-group", str(args.leonos_lib.resolve()),
        str(args.libevent.resolve()), str(clang_runtime()), "--end-group",
    ])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps({
        "tmux_commit": found_revision,
        "tmux_version": "3.5a",
        "libevent": "2.1.12-stable",
        "port_sha256": hashlib.sha256(
            (port / "leonos_port.c").read_bytes() +
            (port / "leonos_termcap.c").read_bytes() +
            (port / "include/config.h").read_bytes()
        ).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
