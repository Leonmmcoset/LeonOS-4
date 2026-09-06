"""Regression checks for the procfs-backed task manager data path."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ProcfsTaskmgrTests(unittest.TestCase):
    def test_proc_pid_paths_start_after_proc_slash(self):
        source = (ROOT / "kernel/ntclks/procfs.c").read_text()
        self.assertIn("const char *p = path + 6;", source)

    def test_procfs_exports_cpu_runtime_stats(self):
        source = (ROOT / "kernel/ntclks/procfs.c").read_text()
        libc = (ROOT / "userland/libc/src/procsys.c").read_text()
        self.assertIn('proc_text_eq(path, "/proc/stat")', source)
        self.assertIn('ps_read_file("/proc/stat"', libc)

    def test_taskmgr_presents_initial_snapshot(self):
        source = (ROOT / "userland/apps/taskmgr/main.c").read_text()
        init = source.index("int main(void)")
        loop = source.index("for (;;) {", init)
        self.assertLess(source.index("refresh_all();", init), loop)
        self.assertLess(source.index("present_taskmgr(", init), loop)

    def test_forkpty_validates_child_setup_and_fork_failure(self):
        source = (ROOT / "userland/libc/src/libc.c").read_text()
        forkpty = source[source.index("pid_t forkpty("):]
        self.assertIn("*master = -1;", forkpty)
        self.assertIn("if (pid < 0)", forkpty)
        self.assertIn("setsid() < 0", forkpty)
        self.assertIn("dup2(sfd, 0) < 0", forkpty)
        self.assertIn("dup2(sfd, 1) < 0", forkpty)
        self.assertIn("dup2(sfd, 2) < 0", forkpty)

    def test_pty_hangup_preserves_pending_canonical_input(self):
        source = (ROOT / "kernel/ntclks/pty.c").read_text()
        start = source.index("int pty_destroy(")
        end = source.index("/**", start)
        destroy = source[start:end]
        self.assertIn("pty_commit_canonical_input(session);", destroy)


if __name__ == "__main__":
    unittest.main()
