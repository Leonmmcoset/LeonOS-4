#!/usr/bin/env python3
"""Run official sudo/visudo with root and nonroot callers in a private namespace."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host-root-only", action="store_true", help="bwrap forces NNP, so it can only test root entry")
args = parser.parse_args()
work = ROOT / "build/auth-upstream"
stage = work / "root"
recipe = json.loads((work / "sudo-build.json").read_text())
binary = work / "sudo-runtime.elf"
subprocess.run([*recipe["compiler"], "-static", "-O2", "-g",
                str(ROOT / "tools/tests/sudo_runtime_probe.c"), "-o", str(binary)], check=True)
if not args.host_root_only:
    files = []
    for path in sorted(stage.rglob("*")):
        if path.is_file() and (".so" in path.name or str(path.relative_to(stage)) in
                ("usr/bin/sudo", "usr/sbin/visudo", "bin/su", "sbin/unix_chkpwd", "sbin/unix_update")):
            files += ["--file", str(path), "/" + str(path.relative_to(stage))]
    files += ["--file", str(ROOT / "build/musl/sysroot/lib/libc.so"), "/lib/ld-musl-x86_64.so.1",
              "--file", str(ROOT / "build/userland/busybox.elf"), "/bin/sh"]
    subprocess.run(["python3", "tools/test_linux_reference.py", "--probe-binary", str(binary),
                    "--case", "sudo_runtime", "--log", str(work / "sudo-runtime-linux-6.12.log"),
                    *files], cwd=ROOT, check=True)
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix="sudo-runtime-", dir=work) as directory:
    etc = Path(directory) / "etc"
    etc.mkdir()
    (etc / "passwd").write_text("root:x:0:0::/root:/bin/sh\n")
    (etc / "group").write_text("root:x:0:\n")
    (etc / "nsswitch.conf").write_text("passwd: files\ngroup: files\nsudoers: files\n")
    command = ["unshare", "--map-auto", "--map-root-user", "bwrap", "--uid", "0", "--gid", "0",
               "--cap-add", "ALL", "--unshare-pid",
               "--unshare-net", "--die-with-parent", "--ro-bind", "/", "/", "--proc", "/proc",
               "--tmpfs", "/usr/bin",
               "--dev", "/dev", "--tmpfs", "/tmp", "--tmpfs", "/run", "--tmpfs", "/var",
               "--dir", "/var/lib/sudo",
               "--bind", str(etc), "/etc", "--ro-bind", str(stage / "usr/lib/sudo"), "/usr/lib/sudo",
               "--ro-bind", str(stage / "lib/security"), "/lib/security",
               "--ro-bind", str(ROOT / "build/musl/sysroot/lib/libc.so"),
               str(Path("/lib/ld-musl-x86_64.so.1").resolve())]
    for path in [*(stage / "lib").glob("libpam*.so*"), *(stage / "lib").glob("libcrypt.so*")]:
        command += ["--ro-bind", str(path), str((Path("/lib") / path.name).resolve())]
    command += ["--ro-bind", str(ROOT / "build/userland/busybox.elf"), str(Path("/bin/busybox").resolve())]
    command += ["--ro-bind", str(ROOT / "build/userland/busybox.elf"), str(Path("/bin/sh").resolve())]
    for path in ("usr/bin/sudo", "usr/sbin/visudo"):
        command += ["--ro-bind", str(stage / path), str((Path("/") / path).resolve())]
    with (work / "sudo-runtime-linux.log").open("w") as log:
        subprocess.run([*command, str(binary), "--root-only"], stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=60)
print(f"PASS isolated Linux sudo entry subset: {work / 'sudo-runtime-linux.log'}")
