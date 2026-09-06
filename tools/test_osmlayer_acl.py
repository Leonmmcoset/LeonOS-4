"""Exercise the real ACL runtime within its ring-0 stack budget."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OsmlayerAclTests(unittest.TestCase):
    def test_acl_operations_leave_room_for_kernel_storage_stack(self):
        with tempfile.TemporaryDirectory(prefix="leonos-acl-stack-") as tmp:
            executable = str(Path(tmp) / "acl-stack")
            subprocess.run([
                "clang", "-std=c11", "-O2", "-ffreestanding",
                "-fno-stack-protector", "-mno-red-zone", "-mgeneral-regs-only",
                "-fno-pic", "-fno-pie", "-no-pie",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-Iinclude", "tools/tests/osmlayer_acl_stack_test.c",
                "middlelayer/osmlayer/runtime.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
