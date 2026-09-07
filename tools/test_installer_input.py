"""Regression tests for installer mouse initialization and window IPC."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallerInputTests(unittest.TestCase):
    def test_window_creation_is_retried_before_present(self):
        with tempfile.TemporaryDirectory(prefix="leonos-windowd-") as tmp:
            executable = str(Path(tmp) / "windowd-announce")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-idirafter", "userland/libc/include", "-idirafter", "include",
                "tools/tests/windowd_announce_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_disconnected_ipc_clients_are_not_reported_as_would_block(self):
        with tempfile.TemporaryDirectory(prefix="leonos-ipc-disconnect-") as tmp:
            executable = str(Path(tmp) / "ipc-disconnect")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                "-O1", "-g", "-include", "sys/un.h",
                "-idirafter", "userland/libc/include",
                "tools/tests/unix_ipc_disconnect_test.c", "userland/libc/src/unix_ipc.c",
                "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_mouse_coordinates_survive_evdev_routing(self):
        with tempfile.TemporaryDirectory(prefix="leonos-pointer-") as tmp:
            executable = str(Path(tmp) / "pointer")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-Ikernel/ntclks/include",
                "-idirafter", "userland/libc/include",
                "tools/tests/pointer_routing_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_descriptor_receive_preserves_next_frame(self):
        with tempfile.TemporaryDirectory(prefix="leonos-ipc-") as tmp:
            executable = str(Path(tmp) / "ipc-frames")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                "-O1", "-g", "-include", "sys/un.h",
                "-idirafter", "userland/libc/include",
                "tools/tests/unix_ipc_frame_test.c", "userland/libc/src/unix_ipc.c",
                "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_input_before_fetch_reply_preserves_descriptor(self):
        with tempfile.TemporaryDirectory(prefix="leonos-wind-") as tmp:
            executable = str(Path(tmp) / "wind-reply")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-idirafter", "userland/libc/include", "-idirafter", "include",
                "tools/tests/wind_reply_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_mouse_parameters_reach_auxiliary_port(self):
        with tempfile.TemporaryDirectory(prefix="leonos-mouse-") as tmp:
            executable = str(Path(tmp) / "mouse-init")
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-Ikernel/ntclks/include",
                "tools/tests/mouse_init_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
