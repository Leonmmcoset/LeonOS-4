"""Storage command ownership shared by build and rootfs staging."""
from pathlib import Path
import shutil

FORMATTER_COMMANDS = tuple(f"usr/sbin/{verb}.{fs}"
                          for fs in ("ext2", "fat", "exfat") for verb in ("mkfs", "fsck"))
UTIL_LINUX_COMMANDS = ("usr/sbin/fdisk", "usr/sbin/sfdisk", "usr/sbin/runuser",
                       "usr/sbin/fsck", "usr/sbin/blkid", "bin/mount", "bin/umount", "bin/lsblk")
UTIL_LINUX_LIBRARIES = tuple(f"usr/lib/lib{name}.so.1"
                           for name in ("fdisk", "smartcols", "uuid", "blkid", "mount"))

COMPAT_LINKS = {"sbin/" + Path(path).name: "../" + path
                for path in (*FORMATTER_COMMANDS, *UTIL_LINUX_COMMANDS) if path.startswith("usr/sbin/")}
COMPAT_LINKS.update({"sbin/umount": "../bin/umount", "usr/sbin/mkfs.vfat": "mkfs.fat",
                     "usr/sbin/mkfs.fat32": "mkfs.fat", "usr/sbin/fsck.vfat": "fsck.fat",
                     "usr/sbin/fsck.fat32": "fsck.fat",
                     "sbin/leonos-grub-installer": "../usr/sbin/leonos-grub-installer"})
for name in ("mkfs.fat32", "mkfs.vfat", "fsck.fat32", "fsck.vfat"):
    COMPAT_LINKS[f"sbin/{name}"] = f"../usr/sbin/{name}"


def stage_filesystems(source: Path, destination: Path) -> None:
    """Copy a built package only after every required command is present."""
    for name in FORMATTER_COMMANDS:
        if not (source / name).is_file():
            raise ValueError(f"missing official filesystem tool: {source / name}")
    for path in source.rglob("*"):
        target = destination / path.relative_to(source)
        # Upstream e2fsprogs installs some archives as 0444. Replace owned
        # files instead of opening old read-only files for in-place writes.
        if target.is_symlink() or (not path.is_dir() and target.is_file()):
            target.unlink()
    shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns(".storage-package.json"))
