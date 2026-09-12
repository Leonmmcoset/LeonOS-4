#!/usr/bin/env python3
"""Run the built Linux binaries on the host and check their real runtime data."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--musl", required=True, type=Path)
    parser.add_argument("--ncurses", required=True, type=Path)
    parser.add_argument("--vim", required=True, type=Path)
    args = parser.parse_args()
    musl, ncurses, vim = args.musl.resolve(), args.ncurses.resolve(), args.vim.resolve()
    env = {**os.environ, "TERM": "xterm-256color",
           "TERMINFO": str(ncurses / "share/terminfo"),
           "VIMRUNTIME": str(vim / "share/vim/vim91")}
    for binary in (vim / "bin/vim", ncurses / "bin/infocmp", ncurses / "bin/tput"):
        headers = subprocess.check_output(["readelf", "-l", "-d", str(binary)], text=True)
        assert "INTERP" not in headers and "(NEEDED)" not in headers, binary
    version = subprocess.check_output([str(vim / "bin/vim"), "--version"], text=True)
    assert "+timers" in version and "+multi_byte" in version, version
    terminfo = subprocess.check_output([str(ncurses / "bin/infocmp"), "xterm-256color"], env=env)
    assert b"colors#0x100" in terminfo or b"colors#256" in terminfo
    with tempfile.TemporaryDirectory(prefix="leonos-terminal-test-") as directory:
        work = Path(directory)
        probe = work / "ncurses-test"
        subprocess.run([
            "clang", "--target=x86_64-linux-musl", "--gcc-toolchain=/nonexistent",
            f"--sysroot={musl}", "--rtlib=compiler-rt", "-fuse-ld=lld", "-static",
            "-I" + str(ncurses / "include"), str(ROOT / "tools/tests/ncurses_runtime_test.c"),
            "-L" + str(ncurses / "lib"), "-lncursesw", "-ltinfow", "-o", str(probe),
        ], check=True)
        subprocess.run([str(probe)], env=env, check=True, timeout=30)
        source = work / "input.txt"
        source.write_text("before\n")
        subprocess.run([str(vim / "bin/vim"), "-n", "-es", str(source),
                        "+runtime defaults.vim", "+syntax on", "+%s/before/after/", "+wq"],
                       env={**env, "HOME": str(work)}, check=True, timeout=30)
        assert source.read_text() == "after\n"
    print("PASS Linux host: static Vim editing, runtime syntax, timers enabled; static ncurses")


if __name__ == "__main__":
    main()
