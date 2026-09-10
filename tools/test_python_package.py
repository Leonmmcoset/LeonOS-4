#!/usr/bin/env python3
"""Check the pinned Python payload and execute its real command aliases."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def verify(root: Path) -> None:
    from package_python import ARCHIVE_SHA256, COMMANDS, VERSION, digest

    manifest = json.loads((root / ".leonos-package.json").read_text())
    assert manifest["archive_sha256"] == ARCHIVE_SHA256
    assert manifest["version"] == VERSION
    for name, expected in manifest["upstream_files"].items():
        assert digest(root / name) == expected, name
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    for name in COMMANDS:
        binary = root / "usr/bin" / name
        assert binary.read_bytes()[:4] == b"\x7fELF", name
        assert subprocess.check_output([binary, "--version"], env=env, text=True).strip() == "Python " + VERSION
        output = subprocess.check_output([binary, root / "usr/share/examples/python/hello.py"], env=env, text=True)
        assert "Sum: 55" in output and "Fibonacci: [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]" in output
    for alias in ("python", "python3"):
        link = root / "usr/bin" / alias
        assert link.is_symlink() and link.readlink().as_posix() == "python3.14", alias
    script = """import sys, encodings, json, ssl, sqlite3, zlib, bz2, lzma, ctypes, decimal, hashlib
assert sys.version_info[:3] == (3, 14, 7)
assert sqlite3.connect(':memory:').execute('select 42').fetchone() == (42,)
assert json.loads(json.dumps({'answer': 42}))['answer'] == 42
assert bz2.decompress(bz2.compress(b'python')) == b'python'
assert lzma.decompress(lzma.compress(b'python')) == b'python'
assert hashlib.sha256(b'python').hexdigest() == '11a4a60b518bf24989d481468076e5d5982884626aed9faeb35b8576fcd223e1'
print(sys.prefix)
print('PYTHON_STDLIB_OK')
"""
    output = subprocess.check_output([root / "usr/bin/python3", "-c", script], env=env, text=True)
    assert str(root / "opt/python") in output and "PYTHON_STDLIB_OK" in output
    subprocess.run([root / "usr/bin/python3", "-m", "pip", "--version"], env=env, check=True)
    print(f"PASS Python {VERSION}: {len(manifest['upstream_files'])} file hashes, aliases, stdlib, pip")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    verify(parser.parse_args().root.resolve())
