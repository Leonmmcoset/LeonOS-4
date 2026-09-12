#!/usr/bin/env python3
"""Exercise kernel memory implementation with host physical-page fixtures."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LinuxMemoryTests(unittest.TestCase):
    def test_memory_ownership_and_elf_page_boundaries(self):
        cases = (("paging_protection", []),
                 ("elf_file_page", ["kernel/ntclks/page_cache.c"]))
        for name, sources in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="leonos-mm-") as tmp:
                executable = str(Path(tmp) / name)
                subprocess.run([
                    "cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
                    "-Wl,--gc-sections", "-Iinclude", "-Iinclude/uapi", "-Ikernel/ntclks/include",
                    f"tools/tests/{name}_test.c", *sources, "-o", executable,
                ], cwd=ROOT, check=True)
                subprocess.run([executable], cwd=ROOT, check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
