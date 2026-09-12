#!/usr/bin/env python3
"""Run the actual target PAM/helper probe on fixed Linux 6.12 with scratch accounts."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_auth_upstream import account_fixture, configure_tests

ROOT = Path(__file__).resolve().parents[1]
work = ROOT / "build/auth-upstream"
runtime = ROOT / "build/musl/sysroot/lib"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--log", type=Path, default=work / "pam-helper-linux-6.12.log")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="pam-helper-reference-", dir=work) as temporary:
    scratch = Path(temporary)
    account_fixture(scratch)
    configure_tests(scratch / "pam.d", "/lib/security")
    files = [(path, "/" + str(path.relative_to(work / "root")))
             for path in sorted((work / "root/lib").glob("**/*.so*")) if path.is_file()]
    files += [(path, "/sbin/" + path.name)
              for path in sorted((work / "root/sbin").iterdir()) if path.is_file()]
    files += [(runtime / "libc.so", "/lib/libc.so"),
              (runtime / "libc.so", "/lib/ld-musl-x86_64.so.1"),
              (runtime / "libmimalloc.so.3", "/lib/libmimalloc.so.3"),
              (ROOT / "build/system/lib/libleonos.so.2", "/lib/libleonos.so.2")]
    files += [(scratch / name, "/etc/" + name) for name in ("passwd", "shadow", "group")]
    files += [(path, "/usr/lib/leonos/tests/pam.d/" + path.name)
              for path in sorted((scratch / "pam.d").iterdir()) if path.is_file()]
    command = ["python3", "tools/test_linux_reference.py", "--probe-binary",
               str(work / "pam-helper-runtime.elf"), "--case", "pam_helper",
               "--log", str(args.log.resolve())]
    for source, destination in files:
        command += ["--file", str(source.resolve()), destination]
    subprocess.run(command, cwd=ROOT, check=True)
