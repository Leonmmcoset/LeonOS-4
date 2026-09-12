#!/usr/bin/env python3
"""Execute staged musl PAM/sudo code in an isolated Linux user namespace."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def configure_tests(directory: Path, modules: str) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    permit = f"{modules}/pam_permit.so"
    deny = f"{modules}/pam_deny.so"
    debug = f"{modules}/pam_debug.so"
    configs = {
        "permit": "".join(f"{kind} required {permit}\n" for kind in ("auth", "account", "password", "session")),
        "deny": f"auth required {deny}\n",
        "other": f"auth required {deny}\n",
        "required": f"auth required {deny}\nauth sufficient {permit}\n",
        "sufficient": f"auth sufficient {permit}\nauth required {deny}\n",
        "jump": f"auth [success=1 default=die] {permit}\nauth required {deny}\nauth required {permit}\n",
        "include": "auth include required\n",
        "substack": f"auth substack sufficient\nauth required {deny}\n",
        "missing-module": f"auth required {modules}/pam_not_installed.so\n",
        "phase-failure": f"auth required {debug} auth=success cred=cred_err\n"
            f"account required {debug} acct=acct_expired\nsession required {debug} open_session=session_err\n",
        "unix": f"auth required {modules}/pam_unix.so nodelay\naccount required {modules}/pam_unix.so\n"
            f"password required {modules}/pam_unix.so sha512\n",
    }
    for name, value in configs.items():
        (directory / name).write_text(value)


def account_fixture(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "passwd").write_text("root:!:0:0::/root:/bin/sh\npam_fixture:x:1000:1000::/tmp:/bin/sh\npam_other:x:1001:1001::/tmp:/bin/sh\n")
    (directory / "group").write_text("root:x:0:\npam_fixture:x:1000:\npam_other:x:1001:\n")
    # Public test hash only; actual pam_unix password updates generate their own salt.
    password_hash = subprocess.check_output(["openssl", "passwd", "-6", "-salt", "pamfixture", "-stdin"],
                                            input="r\n", text=True).strip()
    (directory / "shadow").write_text(f"root:!:1::::::\npam_fixture:{password_hash}:1:0:99999:7:::\npam_other:{password_hash}:1:0:99999:7:::\n")
    (directory / "shadow").chmod(0o600)
    (directory / "shells").write_text("/bin/sh\n")
    (directory / "nsswitch.conf").write_text("passwd: files\ngroup: files\nshadow: files\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=ROOT / "build/auth-upstream")
    parser.add_argument("--entropy-failure", action="store_true")
    args = parser.parse_args()
    work = args.work.resolve()
    stage = work / "root"
    recipe = json.loads((work / "linux-pam-build.json").read_text())
    musl = ROOT / "build/musl/sysroot"
    binary = work / ("pam-entropy-failure.elf" if args.entropy_failure else "pam-runtime.elf")
    command = [*recipe["compiler"], "-O2", "-g", "-DPAM_TEST_ENTROPY_FAILURE",
               "-I" + str(stage / "usr/include"),
               str(ROOT / "tools/tests/pam_runtime_probe.c"), "-L" + str(stage / "lib"),
               "-lpam", "-Wl,-rpath,/lib", "-o", str(binary)]
    subprocess.run(command, check=True)
    configure_tests(work / "pam.d", "/lib/security")
    with tempfile.TemporaryDirectory(prefix="auth-reference-", dir=work) as tmp:
        fixture = Path(tmp)
        etc = fixture / "etc"
        account_fixture(etc)
        shutil.copytree(work / "pam.d", etc / "pam.d")
        loader = str(musl / "lib/libc.so")
        libraries = ":".join(map(str, (stage / "lib", stage / "usr/lib/sudo", musl / "lib")))
        isolated = ["bwrap", "--unshare-user", "--uid", "0", "--gid", "0", "--unshare-pid",
                    "--unshare-net", "--die-with-parent", "--ro-bind", "/", "/",
                    "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
                    "--bind", str(etc), "/etc", "--ro-bind", str(stage / "lib/security"), "/lib/security"]
        isolated += ["--ro-bind", loader, str(Path("/lib/ld-musl-x86_64.so.1").resolve())]
        for library in (stage / "lib").glob("libcrypt.so*"):
            isolated += ["--ro-bind", str(library), str((Path("/lib") / library.name).resolve())]
        if args.entropy_failure:
            isolated += ["--ro-bind", "/dev/null", "/dev/urandom"]
        for helper in (stage / "sbin").iterdir():
            if helper.is_file():
                isolated += ["--ro-bind", str(helper), str((Path("/sbin") / helper.name).resolve())]
        env = {**os.environ, "LD_LIBRARY_PATH": libraries}
        logfile = work / ("pam-entropy-failure-linux.log" if args.entropy_failure else "host-runtime.log")
        with logfile.open("w") as output:
            subprocess.run([*isolated, loader, "--library-path", libraries, str(binary), "/etc/pam.d",
                            "unix-no-entropy" if args.entropy_failure else "unix"],
                           env=env, stdout=output, stderr=subprocess.STDOUT, check=True, timeout=60)
        print(logfile.read_text(), end="")
    print(f"PASS isolated Linux reference; this is not LeonOS guest evidence: {logfile}")


if __name__ == "__main__":
    main()
