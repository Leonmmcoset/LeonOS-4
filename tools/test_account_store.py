#!/usr/bin/env python3
"""Run production account transactions only in a disposable root namespace."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/auth-upstream"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", action="store_true", help="Build a static musl guest probe")
    args = parser.parse_args()
    flags = ["-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread",
             "tools/tests/account_store_test.c", "userland/auth/account_store.c",
             "-Wl,--wrap=fsync,--wrap=renameat,--wrap=write"]
    binary = WORK / ("account-store-runtime.elf" if args.target else "account-store-test")
    if args.target:
        compiler = json.loads((WORK / "linux-pam-build.json").read_text())["compiler"]
        flags += ["-static"]
    else:
        compiler = ["clang"]
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    subprocess.run([*compiler, *flags, "-o", str(binary)], cwd=ROOT, check=True)
    if not args.target:
        subprocess.run(["bwrap", "--unshare-user", "--uid", "0", "--gid", "0",
                        "--unshare-pid", "--unshare-net", "--die-with-parent",
                        "--ro-bind", "/", "/", "--proc", "/proc", "--dev", "/dev",
                        "--tmpfs", "/tmp", str(binary)], check=True, timeout=90)
    print(binary)


if __name__ == "__main__":
    main()
