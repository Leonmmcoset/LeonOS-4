"""Focused regressions for desktop/terminal runtime stalls."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeResponsivenessTests(unittest.TestCase):
    def test_window_repaints_reuse_live_shared_memory(self):
        with tempfile.TemporaryDirectory(prefix="leonos-wind-surface-") as tmp:
            executable = str(Path(tmp) / "wind-surface")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-idirafter", "userland/libc/include", "-idirafter", "include",
                "tools/tests/wind_surface_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_signal_frame_uses_destination_address_space(self):
        with tempfile.TemporaryDirectory(prefix="leonos-signal-") as tmp:
            executable = str(Path(tmp) / "signal")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-Ikernel/ntclks/include",
                "tools/tests/signal_address_space_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_metadata_reads_are_cached_until_storage_changes(self):
        with tempfile.TemporaryDirectory(prefix="leonos-metadata-") as tmp:
            executable = str(Path(tmp) / "metadata")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-Ikernel/ntclks/include",
                "tools/tests/osmlayer_read_cache_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
