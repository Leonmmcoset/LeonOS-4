#!/usr/bin/env python3
"""Create the build-only LeonOS LiteHTML source overlay.

The upstream submodule remains untouched.  This copies it into the generated
build tree and applies the small freestanding/no-RTTI compatibility patch used
by the browser.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--stamp", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    patch = args.patch.resolve()
    stamp = args.stamp.resolve()
    if not (source / "src").is_dir():
        raise SystemExit(f"LiteHTML source tree is missing: {source}")
    if not patch.is_file():
        raise SystemExit(f"LiteHTML compatibility patch is missing: {patch}")

    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output)
    with patch.open("rb") as patch_input:
        result = subprocess.run(
            ["patch", "-p1", "--forward", "--batch"],
            cwd=output,
            stdin=patch_input,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
    if result.returncode != 0:
        raise SystemExit("failed to apply LeonOS LiteHTML patch:\n" + result.stdout)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text("LeonOS LiteHTML overlay v1\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
