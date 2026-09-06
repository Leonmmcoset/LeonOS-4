"""Execute Terminal session and output regressions using host PTYs."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TerminalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="leonos-terminal-")
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.executable = str(Path(cls.tmp.name) / "terminal")
        subprocess.run([
            "cc", "-std=c11", "-D_GNU_SOURCE", "-O1", "-g",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-Iinclude", "-idirafter", "userland/libc/include",
            "tools/tests/terminal_session_test.c", "-lutil",
            "-o", cls.executable,
        ], cwd=ROOT, check=True)

    def run_scenario(self, scenario):
        subprocess.run([self.executable, scenario], cwd=ROOT, check=True, timeout=5)

    def test_child_executes_shell_and_output_reaches_terminal(self):
        self.run_scenario("shell")

    def test_tab_at_right_edge_returns_without_spinning(self):
        self.run_scenario("tab")

    def test_cursor_down_at_full_history_returns_without_spinning(self):
        self.run_scenario("cursor-down")


if __name__ == "__main__":
    unittest.main()
