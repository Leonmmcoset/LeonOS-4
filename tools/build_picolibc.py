#!/usr/bin/env python3
"""Build Picolibc for LeonOS with the checked-in x86_64 cross file."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WINDOWS_MESON_DIR = ROOT / "buildsystem/deps/picolibc-meson"


def run_meson(arguments: list[str]) -> None:
    meson = shutil.which("meson")
    if meson:
        subprocess.run([meson, *arguments], check=True)
        return
    if WINDOWS_MESON_DIR.is_dir():
        environment = os.environ.copy()
        existing = environment.get("PYTHONPATH")
        environment["PYTHONPATH"] = str(WINDOWS_MESON_DIR) + (
            os.pathsep + existing if existing else ""
        )
        subprocess.run([sys.executable, "-m", "mesonbuild.mesonmain", *arguments],
                       check=True, env=environment)
        return
    raise SystemExit(
        "Meson is required for Picolibc. Install meson >= 0.61 or run "
        "py -m pip install --target buildsystem/deps/picolibc-meson meson"
    )


def picolibc_revision(source: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


def reject_os_fallback_members(archive: Path) -> None:
    """Ensure LeonOS never ships Picolibc's linker-script heap fallback."""
    result = subprocess.run(
        ["llvm-ar", "t", str(archive)], check=True, text=True, capture_output=True
    )
    fallback_members = [
        member for member in result.stdout.splitlines()
        if member.startswith("libos_fallback_")
    ]
    if fallback_members:
        raise SystemExit(
            "Picolibc os-fallback objects leaked into the LeonOS archive: "
            + ", ".join(fallback_members)
        )


def remove_unsupported_tls_member(archive: Path) -> None:
    """Keep the archive consistent with LeonOS's TLS-disabled ABI.

    Picolibc's x86 machine source list includes ``tls.c.o`` even when its
    thread-local-storage option is disabled.  Leaving that member available
    would advertise an ABI that the LeonOS dynamic loader deliberately
    rejects, and it references the linker-provided TLS layout symbols.
    """
    members = subprocess.run(
        ["llvm-ar", "t", str(archive)], check=True, text=True, capture_output=True
    ).stdout.splitlines()
    if "tls.c.o" in members:
        subprocess.run(["llvm-ar", "d", str(archive), "tls.c.o"], check=True)


def generated_cross_file(
    source: Path, destination: Path, compile_flags: list[str], linker_flags: list[str]
) -> Path:
    """Create a build-owned cross file so presets never edit tracked input."""
    text = source.read_text(encoding="utf-8")
    c_args = ", ".join(repr(flag) for flag in (compile_flags or ["-O2"]))
    c_link_args = ", ".join(
        repr(flag if not flag.startswith("--") else f"-Wl,{flag}")
        for flag in linker_flags
    )
    old_args = "c_args = ['-target', 'x86_64-unknown-none', '-O2', '-std=c11', '-ffreestanding', '-nostdlibinc', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-mno-red-zone']"
    pic = "'-fPIC', '-fPIE'" if "-fPIC" in compile_flags else "'-fno-pic', '-fno-pie'"
    new_args = "c_args = ['-target', 'x86_64-unknown-none', " + c_args + ", '-std=c11', '-ffreestanding', '-nostdlibinc', '-fno-stack-protector', " + pic + ", '-mno-red-zone']"
    if old_args not in text:
        raise SystemExit("unsupported Picolibc cross-file c_args format")
    text = text.replace(old_args, new_args, 1)
    old_link_args = "c_link_args = ['-target', 'x86_64-unknown-none', '-nostdlib', '-fno-pic', '-fno-pie', '-mno-red-zone']"
    link_pic = "'-fPIC', '-fPIE'" if "-fPIC" in compile_flags else "'-fno-pic', '-fno-pie'"
    new_link_args = "c_link_args = ['-target', 'x86_64-unknown-none', '-nostdlib', " + link_pic + ", '-mno-red-zone'" + (", " + c_link_args if c_link_args else "") + "]"
    if old_link_args not in text:
        raise SystemExit("unsupported Picolibc cross-file c_link_args format")
    text = text.replace(old_link_args, new_link_args, 1)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding="utf-8", newline="\n")
    return destination


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--cross-file", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    parser.add_argument("--compile-flag", action="append", default=[])
    parser.add_argument("--linker-flag", action="append", default=[])
    args = parser.parse_args()

    source = args.source.resolve()
    cross_file = args.cross_file.resolve()
    build_dir = args.build_dir.resolve()
    prefix = args.prefix.resolve()
    archive = args.archive.resolve()
    stamp = args.stamp.resolve()
    if not (source / "meson.build").is_file():
        raise SystemExit(f"Picolibc source tree not found: {source}")
    if not cross_file.is_file():
        raise SystemExit(f"Picolibc cross file not found: {cross_file}")

    # Meson ``setup --wipe`` removes every file inside ``build_dir`` before
    # it parses the cross file.  Keep this generated input beside the build
    # directory so a reconfiguration cannot delete it between generation and
    # Meson's argument parsing.
    effective_cross_file = generated_cross_file(
        cross_file,
        build_dir.parent / f"{build_dir.name}.leonos-x86_64.generated.ini",
        args.compile_flag,
        args.linker_flag,
    )
    options = [
        "-Db_staticpic=true" if "-fPIC" in args.compile_flag else "-Db_staticpic=false",
        "-Dmultilib=false",
        "-Dtests=false",
        "-Dsemihost=false",
        # In Picolibc, true splits fallback syscalls into libos-fallback.a;
        # false merges them into libc.a. Keep them separate so LeonOS's
        # syscall implementations are always used by regular applications.
        "-Dos-fallback=true",
        "-Dpicocrt=false",
        "-Dpicocrt-lib=false",
        "-Dthread-local-storage=false",
        "-Dnewlib-global-errno=true",
        "-Dsingle-thread=true",
        "-Dposix-console=true",
        "-Denable-malloc=true",
        "-Dinternal-heap=0",
        "-Dio-long-long=true",
        # Fastfetch and other POSIX ports use ordinary %f through the public
        # printf/vsnprintf ABI. Picolibc still emits all variants, so selecting
        # the double-aware default does not require an upstream source change.
        "-Dformat-default=double",
        "-Dmb-capable=true",
        "-Dstack-protector-guard=global",
        "-Dspecsdir=none",
    ]
    config = {
        # Schema 6 forces a clean Meson reconfiguration for old build trees
        # that merged os-fallback/sbrk objects into libc.a.
        "schema": 6,
        "cross_file_sha256": hashlib.sha256(effective_cross_file.read_bytes()).hexdigest(),
        "options": options,
    }
    config_path = build_dir / ".leonos-picolibc-config.json"
    previous = None
    if config_path.is_file():
        try:
            previous = json.loads(config_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            previous = None

    setup = ["setup"]
    if (build_dir / "meson-private/coredata.dat").exists():
        setup.append("--reconfigure" if previous == config else "--wipe")
    setup += [
        str(build_dir),
        str(source),
        "--cross-file", str(effective_cross_file),
        "--prefix", str(prefix),
        *options,
    ]
    run_meson(setup)
    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    run_meson(["compile", "-C", str(build_dir)])
    run_meson(["install", "-C", str(build_dir)])

    if not archive.is_file():
        raise SystemExit(f"Picolibc install did not produce {archive}")
    reject_os_fallback_members(archive)
    remove_unsupported_tls_member(archive)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(
        json.dumps(
            {
                "picolibc_commit": picolibc_revision(source),
                "archive": str(archive),
                "cross_file": str(cross_file),
                "effective_cross_file": str(effective_cross_file),
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
