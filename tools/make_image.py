#!/usr/bin/env python3
"""Create a LeonOS GPT disk with a FAT32 ESP and an exFAT runtime root."""
from __future__ import annotations

import argparse
import fcntl
import os
import shutil
import struct
import subprocess
import tempfile
import uuid
import zlib
from contextlib import contextmanager
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SECTOR_SIZE = 512
ESP_FIRST_SECTOR = 2048
GPT_HEADER_SIZE = 92
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_TABLE_SIZE = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE
GPT_ENTRY_TABLE_SECTORS = GPT_ENTRY_TABLE_SIZE // SECTOR_SIZE
GPT_PRIMARY_HEADER_LBA = 1
GPT_PRIMARY_ENTRIES_LBA = 2
EFI_SYSTEM_PARTITION_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
LINUX_FILESYSTEM_GUID = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4")
MICROSOFT_BASIC_DATA_GUID = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def temporary_output(path: Path) -> Path:
    """Return a same-directory temporary path suitable for atomic replacement."""
    return path.with_name(f".{path.name}.{os.getpid()}.tmp")


@contextmanager
def image_lock(raw: Path):
    """Reject concurrent image writers before they can race on final outputs."""
    lock_path = raw.with_name(f".{raw.name}.lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    handle = lock_path.open("a+", encoding="utf-8")
    try:
        try:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise SystemExit(
                f"another VMDK build is already writing {raw}; wait for it to finish"
            ) from error
        yield
    finally:
        fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        handle.close()


def gpt_header(current_lba: int, backup_lba: int, first_usable_lba: int,
               last_usable_lba: int, disk_guid: uuid.UUID,
               entries_lba: int, entries_crc: int) -> bytes:
    """Build one CRC-protected GPT header for the fixed LeonOS table layout."""
    header = bytearray(GPT_HEADER_SIZE)
    struct.pack_into(
        "<8sIIIIQQQQ16sQIII", header, 0,
        b"EFI PART", 0x00010000, GPT_HEADER_SIZE, 0, 0,
        current_lba, backup_lba, first_usable_lba, last_usable_lba,
        disk_guid.bytes_le, entries_lba, GPT_ENTRY_COUNT, GPT_ENTRY_SIZE,
        entries_crc,
    )
    struct.pack_into("<I", header, 16, zlib.crc32(header) & 0xFFFFFFFF)
    return bytes(header)


def write_gpt(image: Path, partitions: list[tuple[uuid.UUID, int, int, str]]) -> None:
    """Write a standard primary and backup GPT without host partition tools."""
    image_size = image.stat().st_size
    total_sectors = image_size // SECTOR_SIZE
    if image_size % SECTOR_SIZE or total_sectors < 2 * GPT_ENTRY_TABLE_SECTORS + 3:
        raise ValueError("GPT image size must contain aligned primary and backup tables")
    if len(partitions) > GPT_ENTRY_COUNT:
        raise ValueError("too many GPT partitions")

    first_usable_lba = GPT_PRIMARY_ENTRIES_LBA + GPT_ENTRY_TABLE_SECTORS
    backup_header_lba = total_sectors - 1
    backup_entries_lba = backup_header_lba - GPT_ENTRY_TABLE_SECTORS
    last_usable_lba = backup_entries_lba - 1
    entries = bytearray(GPT_ENTRY_TABLE_SIZE)
    for index, (type_guid, first_lba, last_lba, name) in enumerate(partitions):
        if first_lba < first_usable_lba or last_lba > last_usable_lba or first_lba > last_lba:
            raise ValueError(f"GPT partition {index + 1} is outside the usable LBA range")
        encoded_name = name.encode("utf-16le")
        if len(encoded_name) > 72:
            raise ValueError(f"GPT partition {index + 1} name is too long")
        struct.pack_into(
            "<16s16sQQQ72s", entries, index * GPT_ENTRY_SIZE,
            type_guid.bytes_le, uuid.uuid4().bytes_le, first_lba, last_lba,
            0, encoded_name.ljust(72, b"\0"),
        )

    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk_guid = uuid.uuid4()
    primary_header = gpt_header(
        GPT_PRIMARY_HEADER_LBA, backup_header_lba, first_usable_lba,
        last_usable_lba, disk_guid, GPT_PRIMARY_ENTRIES_LBA, entries_crc,
    )
    backup_header = gpt_header(
        backup_header_lba, GPT_PRIMARY_HEADER_LBA, first_usable_lba,
        last_usable_lba, disk_guid, backup_entries_lba, entries_crc,
    )
    protective_mbr = bytearray(SECTOR_SIZE)
    struct.pack_into(
        "<B3sB3sII", protective_mbr, 446, 0,
        b"\0\x02\0", 0xEE, b"\xff\xff\xff", 1,
        min(total_sectors - 1, 0xFFFFFFFF),
    )
    protective_mbr[510:512] = b"\x55\xaa"

    with image.open("r+b") as stream:
        stream.seek(0)
        stream.write(protective_mbr)
        stream.seek(GPT_PRIMARY_HEADER_LBA * SECTOR_SIZE)
        stream.write(primary_header)
        stream.seek(GPT_PRIMARY_ENTRIES_LBA * SECTOR_SIZE)
        stream.write(entries)
        stream.seek(backup_entries_lba * SECTOR_SIZE)
        stream.write(entries)
        stream.seek(backup_header_lba * SECTOR_SIZE)
        stream.write(backup_header)


def make_boot_tree(staging: Path, destination: Path) -> None:
    """Stage only files GRUB and the LeonOS loader need before the root mounts."""
    copy_file(staging / "EFI/BOOT/BOOTX64.EFI", destination / "EFI/BOOT/BOOTX64.EFI")
    copy_file(staging / "loader.elf", destination / "loader.elf")
    shutil.copytree(staging / "grub", destination / "grub", dirs_exist_ok=True)
    copy_file(staging / "system/kernel.sys", destination / "system/kernel.sys")
    copy_file(staging / "system/middlelayer.sys", destination / "system/middlelayer.sys")


def make_root_tree(staging: Path, destination: Path, language: str) -> None:
    """Stage the normal writable root without duplicating ESP-only boot files."""
    shutil.copytree(staging, destination, dirs_exist_ok=True)
    shutil.rmtree(destination / "EFI", ignore_errors=True)
    shutil.rmtree(destination / "grub", ignore_errors=True)
    for name in ("loader.elf", "kernel.sys", "middlelayer.sys"):
        (destination / "system" / name).unlink(missing_ok=True)
    (destination / "loader.elf").unlink(missing_ok=True)
    locale = destination / "system/config/locale.conf"
    locale.parent.mkdir(parents=True, exist_ok=True)
    locale.write_text(f"lang={language}\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create LeonOS 4 GPT FAT32-ESP/exFAT-root VMDK")
    parser.add_argument("--out", default="build/images/leonos4.vmdk")
    parser.add_argument("--raw", default="build/images/leonos4.raw")
    parser.add_argument("--esp-tree", default="build/esp")
    parser.add_argument("--root-image")
    parser.add_argument("--root-fs", choices=("exfat", "ext2"), default="exfat",
                        help="Runtime root filesystem; ext2 is retained for compatibility regression builds")
    parser.add_argument("--esp-image", default="build/images/esp.fat")
    parser.add_argument("--default-language", choices=("en", "zh"), default="en",
                        help="Language seed written into this VMDK root filesystem")
    parser.add_argument("--size-mib", type=int, default=512)
    parser.add_argument("--esp-size-mib", type=int, default=128)
    args = parser.parse_args()

    raw = ROOT / args.raw
    out = ROOT / args.out
    esp_tree = ROOT / args.esp_tree
    root_image = ROOT / (args.root_image or f"build/images/root.{args.root_fs}")
    esp_image = ROOT / args.esp_image
    if not esp_tree.is_dir():
        raise SystemExit(f"ESP staging tree does not exist: {esp_tree}")
    if args.esp_size_mib < 128 or args.size_mib <= args.esp_size_mib + 128:
        raise SystemExit("VMDK needs a 128 MiB+ FAT32 ESP and at least 128 MiB runtime root space")

    for path in (raw, out, root_image, esp_image):
        path.parent.mkdir(parents=True, exist_ok=True)
    raw_temp = temporary_output(raw)
    out_temp = temporary_output(out)
    root_temp = temporary_output(root_image)
    esp_temp = temporary_output(esp_image)
    temporary_files = (raw_temp, out_temp, root_temp, esp_temp)

    with image_lock(raw):
        try:
            for path in temporary_files:
                path.unlink(missing_ok=True)

            run(["truncate", "-s", f"{args.size_mib}M", str(raw_temp)])
            total_sectors = raw_temp.stat().st_size // SECTOR_SIZE
            esp_sectors = args.esp_size_mib * 1024 * 1024 // SECTOR_SIZE
            esp_last = ESP_FIRST_SECTOR + esp_sectors - 1
            root_first = (esp_last + 1 + 2047) & ~2047
            root_last = total_sectors - 2048
            if root_last <= root_first or root_last - root_first + 1 < 262144:
                raise SystemExit("VMDK root partition is smaller than the 128 MiB minimum")
            write_gpt(raw_temp, [
                (EFI_SYSTEM_PARTITION_GUID, ESP_FIRST_SECTOR, esp_last, "LEONOS4_ESP"),
                (MICROSOFT_BASIC_DATA_GUID if args.root_fs == "exfat" else LINUX_FILESYSTEM_GUID,
                 root_first, root_last, "LEONOS4_ROOT"),
            ])

            with tempfile.TemporaryDirectory(prefix="leonos-vmdk-") as temp_dir:
                temp = Path(temp_dir)
                boot_tree = temp / "esp"
                root_tree = temp / "root"
                make_boot_tree(esp_tree, boot_tree)
                make_root_tree(esp_tree, root_tree, args.default_language)

                run(["truncate", "-s", str(esp_sectors * SECTOR_SIZE), str(esp_temp)])
                run(["mkfs.fat", "-F", "32", "-s", "2", "-n", "LEONOS4ESP", str(esp_temp)])
                for item in sorted(boot_tree.iterdir()):
                    run(["mcopy", "-s", "-i", str(esp_temp), str(item), "::/"])

                root_bytes = (root_last - root_first + 1) * SECTOR_SIZE
                run(["truncate", "-s", str(root_bytes), str(root_temp)])
                if args.root_fs == "exfat":
                    # exFAT volume labels are limited to 11 UTF-16 units;
                    # the exact LEONOS4_ROOT identity is retained in GPT.
                    run(["mkfs.exfat", "-q", "-s", "512", "-L", "LEONOS4ROOT", str(root_temp)])
                    run(["python3", "tools/populate_exfat.py", "--image", str(root_temp),
                         "--tree", str(root_tree)])
                else:
                    # The kernel supports the stable classic ext2 subset only. Explicitly
                    # disable modern ext4 extensions rather than relying on host defaults.
                    run([
                        "mke2fs", "-q", "-t", "ext2", "-F", "-b", "4096", "-I", "128",
                        "-O", "^has_journal,^resize_inode,^dir_index,^metadata_csum,^64bit",
                        "-d", str(root_tree), str(root_temp),
                    ])

            run(["dd", f"if={esp_temp}", f"of={raw_temp}", "bs=512",
                 f"seek={ESP_FIRST_SECTOR}", "conv=notrunc", "status=none"])
            run(["dd", f"if={root_temp}", f"of={raw_temp}", "bs=512",
                 f"seek={root_first}", "conv=notrunc", "status=none"])
            run(["qemu-img", "convert", "-f", "raw", "-O", "vmdk", str(raw_temp), str(out_temp)])
            for source, destination in ((esp_temp, esp_image), (root_temp, root_image),
                                        (raw_temp, raw), (out_temp, out)):
                os.replace(source, destination)
        finally:
            for path in temporary_files:
                path.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
