#!/usr/bin/env python3
"""Exercise production GUI PAM login with real musl PAM DSOs in an isolated host namespace."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    work = ROOT / "build/auth-upstream"
    stage = work / "root"
    recipe = json.loads((work / "linux-pam-build.json").read_text())
    binary = work / "pam-login-case.elf"
    subprocess.run([*recipe["compiler"], "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections,--export-dynamic",
                    "-Iinclude", "-Iinclude/uapi", "-idirafter", "userland/libc/include",
                    "-I" + str(stage / "usr/include"), "tools/tests/pam_login_case_test.c",
                    "userland/auth/standard_accounts.c", "userland/auth/account_store.c",
                    "userland/libc/src/auth_password.c", "-L" + str(stage / "lib"),
                    "-lpam", "-lcrypt", "-pthread", "-o", str(binary)], cwd=ROOT, check=True)
    command = ["bwrap", "--unshare-user", "--uid", "0", "--gid", "0", "--unshare-pid",
               "--unshare-net", "--die-with-parent", "--ro-bind", "/", "/", "--proc", "/proc",
               "--dev", "/dev", "--tmpfs", "/tmp", "--tmpfs", "/etc", "--tmpfs", "/run",
               "--ro-bind", str(stage / "lib/security"), "/lib/security"]
    loader = ROOT / "build/musl/sysroot/lib/libc.so"
    command += ["--ro-bind", str(loader), str(Path("/lib/ld-musl-x86_64.so.1").resolve())]
    for library in (stage / "lib").glob("*.so*"):
        if library.is_file():
            command += ["--ro-bind", str(library.resolve()), str((Path("/lib") / library.name).resolve())]
    for helper in (stage / "sbin").iterdir():
        if helper.is_file():
            command += ["--ro-bind", str(helper.resolve()), str((Path("/sbin") / helper.name).resolve())]
    subprocess.run([*command, str(binary)], check=True, timeout=60)


if __name__ == "__main__":
    main()
