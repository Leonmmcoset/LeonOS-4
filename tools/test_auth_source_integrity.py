#!/usr/bin/env python3
"""Verify archive and extracted-source integrity without any network access."""
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

from fetch_auth_upstream import fetch


class SourceIntegrity(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="auth-source-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "official.tar.xz"
        with tarfile.open(self.archive, "w:xz") as archive:
            for path, contents in (("official/COPYING", b"license"), ("official/main.c", b"int main(void){return 0;}")):
                member = tarfile.TarInfo(path)
                member.size = len(contents)
                member.mode = 0o644
                archive.addfile(member, io.BytesIO(contents))
            link = tarfile.TarInfo("official/alias.c")
            link.type = tarfile.SYMTYPE
            link.linkname = "./main.c"
            archive.addfile(link)
        self.entry = {"url": "https://example.invalid/official.tar.xz", "version": "1",
            "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            "directory": "official", "license_file": "COPYING"}

    def fetch(self):
        return fetch("fixture", self.entry, self.root, self.root / "sources")

    def test_verified_reuse(self):
        self.assertEqual(self.fetch(), self.fetch())

    def test_archive_tampering(self):
        self.archive.write_bytes(b"replaced")
        with self.assertRaises(ValueError):
            self.fetch()

    def test_source_tampering(self):
        source = self.fetch()
        (source / "main.c").write_text("injected")
        with self.assertRaises(ValueError):
            self.fetch()

    def test_injected_file(self):
        source = self.fetch()
        (source / "config.h").write_text("injected")
        with self.assertRaises(ValueError):
            self.fetch()

    def test_replaced_symlink(self):
        source = self.fetch()
        (source / "main.c").unlink()
        (source / "main.c").symlink_to(self.archive)
        with self.assertRaises(ValueError):
            self.fetch()


if __name__ == "__main__":
    unittest.main()
