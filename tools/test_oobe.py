"""Host regressions for the OOBE-to-desktop service handoff."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OobeTests(unittest.TestCase):
    def test_taskbar_does_not_wait_for_network_service(self):
        with tempfile.TemporaryDirectory(prefix="leonos-oobe-network-") as tmp:
            executable = str(Path(tmp) / "network")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-idirafter", "userland/libc/include",
                "tools/tests/oobe_network_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_slow_startup_service_does_not_block_desktop(self):
        with tempfile.TemporaryDirectory(prefix="leonos-oobe-startup-") as tmp:
            executable = str(Path(tmp) / "startup")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-idirafter", "userland/libc/include",
                "tools/tests/oobe_startup_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_reboot_discards_session_but_keeps_accounts(self):
        with tempfile.TemporaryDirectory(prefix="leonos-auth-boot-") as tmp:
            executable = str(Path(tmp) / "auth-boot")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-idirafter", "userland/libc/include", "-idirafter", "include",
                "tools/tests/oobe_auth_boot_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            for scenario in ("stale", "missing", "denied"):
                with self.subTest(scenario=scenario):
                    subprocess.run([executable, scenario], cwd=ROOT, check=True, timeout=10)

    def test_closed_window_fetch_finishes_without_timeout(self):
        with tempfile.TemporaryDirectory(prefix="leonos-oobe-window-") as tmp:
            executable = str(Path(tmp) / "window")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "-idirafter", "userland/libc/include",
                "tools/tests/oobe_window_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)

    def test_input_method_requests_after_login(self):
        with tempfile.TemporaryDirectory(prefix="leonos-oobe-") as tmp:
            executable = str(Path(tmp) / "inputm")
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
                "-ftrivial-auto-var-init=zero",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-idirafter", "userland/libc/include", "-idirafter", "include",
                "tools/tests/oobe_inputm_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            for scenario in ("list", "state", "active", "context", "notify", "denied"):
                with self.subTest(scenario=scenario):
                    subprocess.run([executable, scenario], cwd=ROOT, check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
