#!/usr/bin/env python3
"""Build a fresh archive so removed translation units cannot remain linkable."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ar", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("objects", nargs="+", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".archive-", dir=args.output.parent) as work:
        archive = Path(work) / args.output.name
        subprocess.run([args.ar, "rcsD", str(archive), *map(str, args.objects)], check=True)
        archive.replace(args.output)


if __name__ == "__main__":
    main()
