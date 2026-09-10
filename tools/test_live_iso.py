#!/usr/bin/env python3
"""Check the boot media contract and the files in a standalone desktop root."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from make_live_root import make_live_tree, write_fat_root
from make_ext2_root import write_ext2_root
from make_installer_root import share_identical_payload_files

ROOT = Path(__file__).resolve().parents[1]


class LiveRootTests(unittest.TestCase):
    def test_root_has_runtime_shell_and_unmodified_vim(self):
        with tempfile.TemporaryDirectory(prefix="leonos-live-test-") as directory:
            work = Path(directory)
            tree = work / "esp"
            for name, content in {
                "EFI/BOOT/BOOTX64.EFI": b"boot",
                "grub/grub.cfg": b"set root=(hd0,gpt1)",
                "loader.elf": b"loader",
                "system/kernel.sys": b"kernel",
                "system/middlelayer.sys": b"middlelayer",
                "system/osmlayer.manifest": b"fs=exfat\n",
                "system/apps/desktop/desktop.elf": b"desktop",
                "system/apps/terminal/terminal.elf": b"terminal",
                "programs/busybox/busybox.elf": b"shell",
                "bin/sh": b"shell",
                "bin/vim": b"unmodified Linux executable",
                "programs/vim/vim.elf": b"unmodified Linux executable",
                "programs/vim/manifest.ini": b"commands=vim\nterminal=1\n",
                "usr/share/vim/vim91/defaults.vim": b"set nocompatible\n",
                "usr/share/terminfo/x/xterm": b"terminfo",
                "lib/ld-musl-x86_64.so.1": b"musl",
            }.items():
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            vim = tree / "programs/vim/vim.elf"
            output = work / "live"
            make_live_tree(tree, output)
            self.assertEqual((output / "bin/vim").read_bytes(), vim.read_bytes())
            self.assertEqual((output / "programs/vim/vim.elf").read_bytes(), vim.read_bytes())
            self.assertEqual((output / "bin/sh").read_bytes(), b"shell")
            self.assertTrue((output / "usr/share/vim/vim91/defaults.vim").is_file())
            self.assertEqual((output / "usr/share/terminfo/x/xterm").read_bytes(), b"terminfo")
            self.assertTrue((output / "system/apps/terminal/terminal.elf").is_file())
            self.assertIn("fs=ext2", (output / "system/osmlayer.manifest").read_text())
            self.assertFalse((output / "EFI").exists())
            self.assertFalse((output / "system/kernel.sys").exists())
            image = work / "root.fat"
            write_fat_root(output, image)
            check = subprocess.run(["fsck.fat", "-n", str(image)], capture_output=True)
            self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
            extracted = subprocess.check_output(["mtype", "-i", str(image), "::/bin/vim"])
            self.assertEqual(extracted, vim.read_bytes())
            for name, content in (("xt_CONNMARK.h", b"upper"), ("xt_connmark.h", b"lower")):
                header = output / "opt/dyne/include" / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_bytes(content)
            image = work / "root.ext2"
            installed = output / "install/root/bin/vim"
            installed.parent.mkdir(parents=True)
            installed.write_bytes((output / "bin/vim").read_bytes())
            for name in ("system/config/users.db", "install/root/system/config/users.db"):
                seed = output / name
                seed.parent.mkdir(parents=True, exist_ok=True)
                seed.write_bytes(b"")
            share_identical_payload_files(output)
            self.assertEqual(installed.stat().st_ino, (output / "bin/vim").stat().st_ino)
            self.assertNotEqual((output / "system/config/users.db").stat().st_ino,
                                (output / "install/root/system/config/users.db").stat().st_ino)
            write_ext2_root(output, image)
            stats = [subprocess.check_output(["debugfs", "-R", f"stat /{name}", image], text=True)
                     for name in ("bin/vim", "install/root/bin/vim")]
            self.assertEqual(stats[0].splitlines()[0], stats[1].splitlines()[0])
            for name, content in (("xt_CONNMARK.h", b"upper"), ("xt_connmark.h", b"lower")):
                extracted = subprocess.check_output(["debugfs", "-R", f"cat /opt/dyne/include/{name}", image])
                self.assertEqual(extracted, content)

    def test_grub_loads_the_root_from_cd_not_the_first_hard_disk(self):
        config = (ROOT / "boot/grub/live.cfg").read_text()
        self.assertNotIn("(hd", config)
        self.assertIn("search --no-floppy --file /leonos-installer-iso.marker --set=root", config)
        self.assertIn("mode=live startup=desktop", config)
        self.assertIn("module2 /install/root.fat leonos-installer-root", config)
        self.assertIn("module2 /system/kernel.sys leonos-kernel", config)


if __name__ == "__main__":
    unittest.main()
