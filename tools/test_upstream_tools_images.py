#!/usr/bin/env python3
"""Check actual production ext2 images, including both installer payload roots."""
from pathlib import Path
import hashlib
from storage_tools import COMPAT_LINKS
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / "build/esp"
IMAGES = ((ROOT / "build/live/root.ext2", ("",)),
          (ROOT / "build/install/root.fat", ("", "/install/root")))


def debugfs(image, command):
    return subprocess.check_output(["debugfs", "-R", command, str(image)], stderr=subprocess.DEVNULL)


class ImageToolsTests(unittest.TestCase):
    def test_embedded_tools_and_library_bytes_match_staging(self):
        paths = ("bin/busybox", "usr/sbin/fdisk", "usr/sbin/sfdisk", "bin/mount", "bin/umount",
                 "bin/lsblk", "usr/sbin/blkid", "usr/sbin/fsck", "usr/sbin/mkfs.ext2",
                 "usr/sbin/fsck.ext2", "usr/sbin/mkfs.fat", "usr/sbin/fsck.fat",
                 "usr/sbin/mkfs.exfat", "usr/sbin/fsck.exfat", "usr/sbin/leonos-grub-installer",
                 "usr/lib/libfdisk.so.1", "usr/lib/libuuid.so.1", "usr/lib/libsmartcols.so.1",
                 "usr/lib/libmount.so.1", "usr/lib/libblkid.so.1")
        for image, prefixes in IMAGES:
            for prefix in prefixes:
                for name in paths:
                    with self.subTest(image=image.name, root=prefix or "/", path=name):
                        source = STAGE / name
                        self.assertTrue(source.is_file(), str(source))
                        actual = source.resolve().relative_to(STAGE)
                        binary = debugfs(image, f"cat {prefix}/{actual}")
                        self.assertEqual(hashlib.sha256(binary).digest(),
                                         hashlib.sha256(source.read_bytes()).digest(), name)

    def test_legacy_paths_resolve_to_official_tools(self):
        links = COMPAT_LINKS
        for image, prefixes in IMAGES:
            for prefix in prefixes:
                for name, target in links.items():
                    with self.subTest(image=image.name, root=prefix or "/", path=name):
                        stat = debugfs(image, f"stat {prefix}/{name}").decode()
                        self.assertIn("Type: symlink", stat)
                        self.assertIn(f'"{target}"', stat)


if __name__ == "__main__":
    unittest.main()
