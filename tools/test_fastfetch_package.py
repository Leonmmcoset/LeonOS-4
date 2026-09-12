#!/usr/bin/env python3
"""Verify the pinned binary, cache failures, and upstream logo/config support."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from package_fastfetch import BINARY_SHA256, BINARY_URL, CACHE, ROOT, digest, obtain_binary, package


class FastfetchPackageTests(unittest.TestCase):
    def test_disabled_component_prunes_config_and_payload(self):
        sys.path.insert(0, str(ROOT))
        from build import build_graph
        from buildsystem.core.state import BuildPaths
        with tempfile.TemporaryDirectory(prefix="fastfetch-prune-", dir=ROOT / "build") as directory:
            paths = BuildPaths(Path(directory))
            config = paths.root / "config"
            config.write_text("CONFIG_LEON_COMPONENT_APP_FASTFETCH_BUILD=n\n")
            owned = ("etc/fastfetch/config.jsonc", "usr/share/licenses/fastfetch/LICENSE",
                     "usr/lib/leonos/apps/fastfetch/fastfetch.elf")
            for name in (*owned, "etc/unrelated.conf"):
                path = paths.staging / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            graph = build_graph(paths, config)
            self.assertNotIn("esp:fastfetch:config", graph.targets)
            graph.targets["staging-prune"].action(SimpleNamespace(detail=lambda text: None))
            for name in owned:
                self.assertFalse((paths.staging / name).exists())
            self.assertTrue((paths.staging / "etc/unrelated.conf").is_file())

    def test_package_and_reject_changed_input(self):
        with tempfile.TemporaryDirectory(prefix="fastfetch-test-", dir=ROOT / "build") as directory:
            work = Path(directory)
            cache, output, stamp = work / "cache", work / "fastfetch", work / "stamp.json"
            obtain_binary(cache, CACHE)
            package(cache, output, stamp)
            self.assertEqual(digest(output), BINARY_SHA256)
            self.assertEqual(json.loads(stamp.read_text())["sha256"], BINARY_SHA256)
            self.assertEqual(output.stat().st_mode & 0o777, 0o755)
            obtain_binary(cache, None)
            changed = work / "changed"
            changed.write_bytes(cache.read_bytes() + b"changed")
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                obtain_binary(cache, changed)
            self.assertEqual(digest(cache), BINARY_SHA256)
            self.assertEqual(digest(output), BINARY_SHA256)
            with patch("package_fastfetch.subprocess.run", wraps=subprocess.run) as network:
                obtain_binary(cache, None)
                self.assertTrue(all(call.args[0][0] != "curl" for call in network.call_args_list))

    def test_download_verification_and_failure_cleanup(self):
        real_run = subprocess.run
        fixture = CACHE.read_bytes()
        with tempfile.TemporaryDirectory(prefix="fastfetch-download-", dir=ROOT / "build") as directory:
            work = Path(directory)
            cache = work / "cache"
            failure = None
            def download(command, **kwargs):
                if command[0] != "curl":
                    return real_run(command, **kwargs)
                self.assertEqual(command[-1], BINARY_URL)
                self.assertEqual(command[command.index("--proxy") + 1], "http://127.0.0.1:12334")
                target = Path(command[command.index("--output") + 1])
                target.write_bytes(fixture if failure is None else b"incomplete download")
                if failure == "network":
                    raise subprocess.CalledProcessError(22, command)
                return subprocess.CompletedProcess(command, 0)
            with patch("package_fastfetch.subprocess.run", side_effect=download):
                for failure, exception in (("network", subprocess.CalledProcessError),
                                           ("checksum", ValueError)):
                    with self.subTest(failure=failure), self.assertRaises(exception):
                        obtain_binary(cache, None)
                    self.assertFalse(cache.exists())
                    self.assertEqual(list(work.iterdir()), [])
                failure = None
                obtain_binary(cache, None)
                self.assertEqual(digest(cache), BINARY_SHA256)
                self.assertEqual(list(work.iterdir()), [cache])
            cache.write_bytes(b"corrupt cache")
            with patch("package_fastfetch.subprocess.run") as network:
                with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                    obtain_binary(cache, None)
                network.assert_not_called()

    def test_upstream_logo_and_config(self):
        binary = str(CACHE)
        logos = subprocess.check_output([binary, "--list-logos"], text=True, timeout=15)
        self.assertRegex(logos, r"\bLeonOS\b")
        config = str(ROOT / "userland/fastfetch/config.jsonc")
        data = json.loads(subprocess.check_output([
            binary, "--config", config, "--structure", "Kernel", "--format", "json"
        ], text=True, timeout=15))
        self.assertEqual(data[0]["result"]["name"], os.uname().sysname)
        self.assertEqual(data[0]["result"]["release"], os.uname().release)
        explicit = subprocess.check_output([
            binary, "--config", "none", "--logo", "LeonOS", "--structure", "Kernel", "--pipe"
        ], text=True, timeout=15)
        configured = subprocess.check_output([
            binary, "--config", config, "--structure", "Kernel", "--pipe"
        ], text=True, timeout=15)
        self.assertEqual(configured, explicit)
        self.assertGreater(len(configured.splitlines()), 1)
        defaults = subprocess.check_output([
            binary, "--config", config, "--pipe"
        ], text=True, timeout=15)
        self.assertIn(f"Kernel: {os.uname().sysname} {os.uname().release}", defaults)
        self.assertIn("OS:", defaults)
        self.assertIn("Memory:", defaults)
        upstream_modules = subprocess.check_output([
            binary, "--config", "none", "--print-structure"
        ], text=True, timeout=15).strip().split(":")
        self.assertEqual(json.loads(Path(config).read_text())["modules"], upstream_modules)


if __name__ == "__main__":
    unittest.main()
