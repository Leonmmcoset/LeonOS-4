#!/usr/bin/env python3
"""Verify the Python package in both live ISO roots and the installed payload."""
import json
from pathlib import Path
import subprocess
import tempfile

from package_python import COMMANDS, ROOT, digest


def main() -> None:
    package = ROOT / "build/python/root"
    manifest = json.loads((package / ".leonos-package.json").read_text())
    for name, source, prefixes in (
        ("leonos4.iso", "build/live/root.ext2", ("",)),
        ("leonos4-installer.iso", "build/install/root.fat", ("", "/install/root")),
    ):
        with tempfile.TemporaryDirectory(prefix=".image-check-", dir=package.parent) as directory:
            work = Path(directory)
            image = work / "root.ext2"
            subprocess.run(["xorriso", "-osirrox", "on", "-indev", ROOT / "build/images" / name,
                            "-extract", "/install/root.fat", image], check=True, capture_output=True)
            assert digest(image) == digest(ROOT / source), name
            for index, prefix in enumerate(prefixes):
                extracted = work / str(index)
                extracted.mkdir()
                subprocess.run(["debugfs", "-R", f"rdump {prefix}/opt/python {extracted}", image],
                               check=True, capture_output=True)
                for path, expected in manifest["upstream_files"].items():
                    assert digest(extracted / Path(path).relative_to("opt")) == expected, (name, prefix, path)
                launcher = f"{prefix}/usr/bin/python3.14"
                data = subprocess.check_output(["debugfs", "-R", f"cat {launcher}", image],
                                               stderr=subprocess.DEVNULL)
                assert data == (package / "usr/bin/python3.14").read_bytes(), (name, prefix, "python3.14")
                for alias in ("python", "python3"):
                    stat = subprocess.check_output(
                        ["debugfs", "-R", f"stat {prefix}/usr/bin/{alias}", image],
                        stderr=subprocess.DEVNULL, text=True)
                    assert "Fast link dest: \"python3.14\"" in stat, (name, prefix, alias)
                for path in ("usr/share/examples/python/hello.py",
                             "usr/share/licenses/python/package.json",
                             "usr/share/licenses/python/LICENSE.cpython.txt"):
                    data = subprocess.check_output(["debugfs", "-R", f"cat {prefix}/{path}", image],
                                                   stderr=subprocess.DEVNULL)
                    assert data == (package / path).read_bytes(), (name, prefix, path)
                print(f"PASS {name} {prefix or '/'}: Python {manifest['version']}, {len(manifest['upstream_files'])} files, aliases, licenses, example")


if __name__ == "__main__":
    main()
