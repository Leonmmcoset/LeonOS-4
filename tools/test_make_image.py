#!/usr/bin/env python3
"""Unit tests for the host-independent LeonOS GPT image writer."""

from __future__ import annotations

import struct
import tempfile
import unittest
import uuid
import zlib
from pathlib import Path

from make_image import (
    EFI_SYSTEM_PARTITION_GUID,
    GPT_ENTRY_TABLE_SIZE,
    GPT_PRIMARY_ENTRIES_LBA,
    GPT_PRIMARY_HEADER_LBA,
    LINUX_FILESYSTEM_GUID,
    SECTOR_SIZE,
    write_gpt,
)


HEADER_FORMAT = "<8sIIIIQQQQ16sQIII"


def read_header(image: bytes, lba: int) -> tuple[object, ...]:
    return struct.unpack_from(HEADER_FORMAT, image, lba * SECTOR_SIZE)


def assert_header_crc(test: unittest.TestCase, image: bytes, lba: int) -> None:
    header = bytearray(image[lba * SECTOR_SIZE:lba * SECTOR_SIZE + 92])
    stored_crc = struct.unpack_from("<I", header, 16)[0]
    struct.pack_into("<I", header, 16, 0)
    test.assertEqual(stored_crc, zlib.crc32(header) & 0xFFFFFFFF)


class GptWriterTests(unittest.TestCase):
    def test_writes_valid_primary_and_backup_gpt(self) -> None:
        with tempfile.TemporaryDirectory(prefix="leonos-gpt-test-") as directory:
            image_path = Path(directory) / "disk.raw"
            image_size = 64 * 1024 * 1024
            with image_path.open("wb") as image:
                image.truncate(image_size)
            total_sectors = image_size // SECTOR_SIZE
            esp_last = 34815
            root_first = 34816
            root_last = total_sectors - 2048
            write_gpt(image_path, [
                (EFI_SYSTEM_PARTITION_GUID, 2048, esp_last, "LEONOS4_ESP"),
                (LINUX_FILESYSTEM_GUID, root_first, root_last, "LEONOS4_ROOT"),
            ])

            image = image_path.read_bytes()
            self.assertEqual(image[510:512], b"\x55\xaa")
            self.assertEqual(image[450], 0xEE)

            primary = read_header(image, GPT_PRIMARY_HEADER_LBA)
            self.assertEqual(primary[0], b"EFI PART")
            self.assertEqual(primary[1], 0x00010000)
            self.assertEqual(primary[2], 92)
            self.assertEqual(primary[5], GPT_PRIMARY_HEADER_LBA)
            self.assertEqual(primary[6], total_sectors - 1)
            self.assertEqual(primary[10], GPT_PRIMARY_ENTRIES_LBA)
            self.assertEqual(primary[11:13], (128, 128))
            assert_header_crc(self, image, GPT_PRIMARY_HEADER_LBA)

            entries_offset = primary[10] * SECTOR_SIZE
            entries = image[entries_offset:entries_offset + GPT_ENTRY_TABLE_SIZE]
            self.assertEqual(primary[13], zlib.crc32(entries) & 0xFFFFFFFF)
            first = struct.unpack_from("<16s16sQQQ72s", entries, 0)
            second = struct.unpack_from("<16s16sQQQ72s", entries, 128)
            self.assertEqual(first[0], EFI_SYSTEM_PARTITION_GUID.bytes_le)
            self.assertNotEqual(first[1], uuid.UUID(int=0).bytes_le)
            self.assertEqual(first[2:4], (2048, esp_last))
            self.assertEqual(first[5].decode("utf-16le").rstrip("\0"), "LEONOS4_ESP")
            self.assertEqual(second[0], LINUX_FILESYSTEM_GUID.bytes_le)
            self.assertEqual(second[2:4], (root_first, root_last))
            self.assertEqual(second[5].decode("utf-16le").rstrip("\0"), "LEONOS4_ROOT")

            backup_lba = total_sectors - 1
            backup = read_header(image, backup_lba)
            self.assertEqual(backup[0], b"EFI PART")
            self.assertEqual(backup[5:7], (backup_lba, GPT_PRIMARY_HEADER_LBA))
            self.assertEqual(backup[9], primary[9])
            self.assertEqual(backup[13], primary[13])
            backup_entries = image[backup[10] * SECTOR_SIZE:backup[10] * SECTOR_SIZE + GPT_ENTRY_TABLE_SIZE]
            self.assertEqual(backup_entries, entries)
            assert_header_crc(self, image, backup_lba)


if __name__ == "__main__":
    unittest.main()
