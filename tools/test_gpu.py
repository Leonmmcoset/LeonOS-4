"""GPU public ABI boundary tests; included by build.py test svga."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuAbiTests(unittest.TestCase):
    def test_syscall_and_copyout(self):
        for name, source in (("gpu_syscall", "kernel/ntclks/gpu.c"),
                             ("gpu_usercopy", "kernel/ntclks/user/usercopy.c")):
            with self.subTest(name=name), tempfile.TemporaryDirectory(prefix="leonos-gpu-") as tmp:
                executable = str(Path(tmp) / name)
                subprocess.run([
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-Ikernel/ntclks/include", "-Iinclude",
                    f"tools/tests/{name}_test.c", source, "-o", executable,
                ], cwd=ROOT, check=True)
                subprocess.run([executable], cwd=ROOT, check=True, timeout=30)

    def test_sdk_header_matches(self):
        self.assertEqual((ROOT / "include/leonos/gpu.h").read_bytes(),
                         (ROOT / "devtools/include/leonos/gpu.h").read_bytes())

    def test_taskmgr_sampling(self):
        with tempfile.TemporaryDirectory(prefix="leonos-gpu-sampler-") as tmp:
            executable = str(Path(tmp) / "sampler")
            subprocess.run([
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-Iinclude",
                "tools/tests/taskmgr_gpu_sample_test.c", "-o", executable,
            ], cwd=ROOT, check=True)
            subprocess.run([executable], cwd=ROOT, check=True, timeout=30)

    def test_gears_backend(self):
        with tempfile.TemporaryDirectory(prefix="leonos-gears-source-") as tmp:
            upstream = ROOT / "third_party/portablegl/examples/classic/gears.c"
            source = upstream.read_text()
            marker = "#define PORTABLEGL_IMPLEMENTATION"
            self.assertEqual(source.count(marker), 1)
            (Path(tmp) / "gears-upstream.c").write_text(
                source.replace(marker, "/* Test uses the existing PortableGL API. */", 1))
            env = dict(os.environ, GLXGEARS_GENERATED_DIR=tmp)
            subprocess.run(["sh", "userland/apps/glxgears/tests/run_host_tests.sh"],
                           cwd=ROOT, env=env, check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
