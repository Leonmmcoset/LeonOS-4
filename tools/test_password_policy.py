#!/usr/bin/env python3
"""Exercise target PAM and passwd in a disposable Linux account namespace."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--linux-reference", action="store_true")
    args = parser.parse_args()
    work = ROOT / "build/auth-upstream"
    stage = work / "root"
    recipe = json.loads((work / "linux-pam-build.json").read_text())
    binary = work / "password-policy-runtime.elf"
    subprocess.run([*recipe["compiler"], "-O2", "-g", "-I" + str(stage / "usr/include"),
                    str(ROOT / "tools/tests/password_policy_runtime_probe.c"),
                    "-L" + str(stage / "lib"), "-lpam", "-o", str(binary)], check=True)
    if args.linux_reference:
        command = ["python3", "tools/test_linux_reference.py", "--probe-binary", str(binary),
                   "--case", "password_policy", "--log", str(work / "password-policy-linux-6.12.log")]
        files = [(path, "/" + str(path.relative_to(stage)))
                 for path in (stage / "lib").rglob("*.so*") if path.is_file()]
        files += [(stage / "usr/bin/passwd", "/usr/bin/passwd"),
                  (ROOT / "build/musl/sysroot/lib/libc.so", "/lib/ld-musl-x86_64.so.1")]
        files += [(path, "/sbin/" + path.name) for path in (stage / "sbin").iterdir() if path.is_file()]
        for source, destination in files:
            command += ["--file", str(source.resolve()), destination]
        subprocess.run(command, cwd=ROOT, check=True)
        return
    with tempfile.TemporaryDirectory(prefix="password-policy-", dir=work) as directory:
        etc = Path(directory) / "etc"
        etc.mkdir()
        loader = ROOT / "build/musl/sysroot/lib/libc.so"
        command = ["bwrap", "--unshare-user", "--uid", "0", "--gid", "0", "--unshare-pid",
                   "--unshare-net", "--die-with-parent", "--ro-bind", "/", "/",
                   "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
                   "--bind", str(etc), "/etc", "--ro-bind", str(stage / "lib/security"), "/lib/security",
                   "--ro-bind", str(stage / "usr/bin/passwd"), "/usr/bin/passwd",
                   "--ro-bind", str(loader), str(Path("/lib/ld-musl-x86_64.so.1").resolve())]
        for library in (stage / "lib").glob("*.so*"):
            if library.is_file():
                command += ["--ro-bind", str(library), str((Path("/lib") / library.name).resolve())]
        for helper in (stage / "sbin").iterdir():
            if helper.is_file():
                command += ["--ro-bind", str(helper), str((Path("/sbin") / helper.name).resolve())]
        command += [str(binary), "--root-only"]
        log = work / "password-policy-host.log"
        with log.open("w") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                    env={**os.environ, "LC_ALL": "C"}, timeout=100)
        print(log.read_text(), end="")
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
