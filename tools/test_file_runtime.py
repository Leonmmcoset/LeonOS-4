#!/usr/bin/env python3
"""Run the packaged musl file/libmagic with a matching database and short reads."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    recipe = json.loads((ROOT / "build/auth-upstream/linux-pam-build.json").read_text())
    loader = ROOT / "build/musl/sysroot/lib/libc.so"
    app = ROOT / "build/userland/file.elf"
    database = ROOT / "build/userland/magic.mgc"
    libraries = ":".join(str(ROOT / path) for path in (
        "build/system/lib", "build/musl/sysroot/lib", "build/auth-upstream/root/lib",
        "build/auth-upstream/root/usr/lib"))
    with tempfile.TemporaryDirectory(prefix="leonos-file-") as temp:
        work = Path(temp)
        shim = work / "short-read.so"
        subprocess.run([*recipe["compiler"], "-shared", "-fPIC", "-O2",
                        str(ROOT / "tools/tests/file_short_read.c"), "-o", str(shim)], check=True)
        sample = work / "plain.txt"
        sample.write_text("Hello from LeonOS\n")
        command = [str(loader), "--library-path", libraries, str(app), "-m", str(database)]
        for short in (False, True):
            env = os.environ.copy()
            env.pop("MAGIC", None)
            env.pop("LD_PRELOAD", None)
            if short:
                env["LD_PRELOAD"] = str(shim)
            result = subprocess.run([*command, str(app), str(sample)], env=env,
                                    capture_output=True, text=True, errors="replace", timeout=20)
            assert result.returncode == 0, (short, result.stderr[:1200], result.stdout[:1200])
            assert "ELF 64-bit" in result.stdout and "ASCII text" in result.stdout, result.stdout
            assert not result.stderr, result.stderr
            print(f"PASS file ELF/text and compiled magic; short_reads={short}")
        result = subprocess.run([*command, "-E", str(work / "absent")],
                                capture_output=True, text=True, timeout=20)
        assert result.returncode != 0 and "No such file" in result.stdout + result.stderr
        print("PASS file still reports genuinely missing input")


if __name__ == "__main__":
    main()
