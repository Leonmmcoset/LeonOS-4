#!/usr/bin/env python3
"""Verify the actual ISO root payloads against the packaged toolchain manifest."""
import json
from pathlib import Path
import subprocess
import tempfile

from package_musl_gcc import ROOT, COMMANDS, digest


def main():
    package = ROOT / "build/musl-gcc/root"
    manifest = json.loads((package / ".leonos-package.json").read_text())
    for image_name, source, prefixes in (
        ("leonos4.iso", "build/live/root.ext2", ("",)),
        ("leonos4-installer.iso", "build/install/root.fat", ("", "/install/root")),
    ):
        with tempfile.TemporaryDirectory(prefix="leonos-gcc-iso-") as directory:
            work = Path(directory)
            image = work / "root.ext2"
            subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(ROOT / "build/images" / image_name),
                            "-extract", "/install/root.fat", str(image)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            assert digest(image) == digest(ROOT / source), image_name
            for index, prefix in enumerate(prefixes):
                extracted = work / str(index)
                extracted.mkdir()
                subprocess.run(["debugfs", "-R", f"rdump {prefix}/opt {extracted}", image], check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for name, expected in manifest["upstream_files"].items():
                    assert digest(extracted / name) == expected, (image_name, prefix, name)
                for name in COMMANDS:
                    content = subprocess.check_output(["debugfs", "-R", f"cat {prefix}/bin/{name}", image], stderr=subprocess.DEVNULL)
                    assert content == (package / "bin" / name).read_bytes(), (prefix, name)
                print(f"PASS {image_name} {prefix or '/'}: {len(manifest['upstream_files'])} upstream files and {len(COMMANDS)} aliases")


if __name__ == "__main__":
    main()
