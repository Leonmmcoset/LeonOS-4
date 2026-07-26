"""LeonOS path and file descriptor emulation."""

from __future__ import annotations

import io
import os
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

from . import constants as C
from . import structs
from .errors import EACCES, EBADF, EEXIST, EINVAL, EISDIR, ENOENT, ENOTDIR, neg


@dataclass
class DirectoryFD:
    entries: list[tuple[int, str]]
    index: int = 0


@dataclass
class VirtualFD:
    data: io.BytesIO
    writable: bool = False
    path: str | None = None


class GuestFS:
    DISPLAY_CONFIG_PATH = "0:/system/config/display.conf"

    def __init__(self, root: str | Path, *, language: str = "en", ui_theme: str = "metro", logger=None):
        self.root = Path(root).resolve()
        self.cwd = "0:/"
        self.language = language
        self.ui_theme = "win95" if ui_theme == "win95" else "metro"
        self._display_config = self._load_display_config()
        self.logger = logger
        self.fds: dict[int, BinaryIO | DirectoryFD | VirtualFD | object] = {
            0: VirtualFD(io.BytesIO(), False),
            1: object(),
            2: object(),
            3: object(),
        }
        self.next_fd = 4

    def log(self, text: str) -> None:
        if self.logger:
            self.logger.write(text)

    def set_language(self, language: str) -> None:
        self.language = "zh" if language == "zh" else "en"

    def set_ui_theme(self, ui_theme: str) -> None:
        self.ui_theme = "win95" if ui_theme == "win95" else "metro"
        self._display_config = self._with_display_theme(self._display_config, self.ui_theme)

    @staticmethod
    def _with_display_theme(data: bytes, ui_theme: str) -> bytes:
        lines = data.decode("utf-8", "replace").splitlines()
        output: list[str] = []
        found = False
        for line in lines:
            if line.strip().startswith("theme="):
                if not found:
                    output.append(f"theme={ui_theme}")
                    found = True
                continue
            output.append(line)
        if not found:
            output.append(f"theme={ui_theme}")
        return ("\n".join(output) + "\n").encode("utf-8")

    def _load_display_config(self) -> bytes:
        try:
            data = self.host_path(self.DISPLAY_CONFIG_PATH).read_bytes()
        except OSError:
            data = b""
        return self._with_display_theme(data, self.ui_theme)

    @classmethod
    def _is_display_config(cls, guest: str) -> bool:
        return guest.lower() == cls.DISPLAY_CONFIG_PATH

    def _open_virtual_display_config(self, flags: int) -> int:
        write_mode = flags & C.O_ACCMODE
        writable = write_mode != C.O_RDONLY
        if flags & C.O_TRUNC and writable:
            data = io.BytesIO()
        else:
            data = io.BytesIO(self._display_config)
        if flags & C.O_APPEND:
            data.seek(0, os.SEEK_END)
        return self._alloc_fd(VirtualFD(data, writable, self.DISPLAY_CONFIG_PATH))

    def guest_abs(self, path: str) -> str:
        path = (path or "").replace("\\", "/")
        if path.startswith("/"):
            path = "0:" + path
        if len(path) >= 3 and path[1] == ":" and path[2] == "/":
            drive = path[:2]
            rest = path[3:]
        elif len(path) == 2 and path[1] == ":":
            drive = path
            rest = ""
        else:
            base = self.cwd
            if not base.endswith("/"):
                base += "/"
            return self.guest_abs(base + path)
        if drive != "0:":
            raise ValueError("only drive 0: is supported")
        parts: list[str] = []
        for part in rest.split("/"):
            if not part or part == ".":
                continue
            if part == "..":
                if parts:
                    parts.pop()
                continue
            parts.append(part)
        return "0:/" + "/".join(parts)

    def host_path(self, guest_path: str) -> Path:
        guest = self.guest_abs(guest_path)
        rel = guest[3:]
        host = (self.root / Path(*[p for p in rel.split("/") if p])).resolve()
        try:
            host.relative_to(self.root)
        except ValueError as exc:
            raise PermissionError("path escapes selected root") from exc
        return host

    def _alloc_fd(self, value) -> int:
        fd = self.next_fd
        self.next_fd += 1
        self.fds[fd] = value
        return fd

    def open(self, path: str, flags: int, mode: int = 0) -> int:
        try:
            guest = self.guest_abs(path)
            host = self.host_path(guest)
        except (ValueError, PermissionError):
            return neg(EACCES)
        if self._is_display_config(guest):
            return self._open_virtual_display_config(flags)
        if guest.lower() == "0:/system/config/locale.conf" and not host.exists():
            text = "lang=zh\n" if self.language == "zh" else "lang=en\n"
            return self._alloc_fd(VirtualFD(io.BytesIO(text.encode("ascii")), False))
        write_mode = flags & C.O_ACCMODE
        if host.is_dir():
            if write_mode != C.O_RDONLY:
                return neg(EISDIR)
            try:
                entries = []
                for child in sorted(host.iterdir(), key=lambda p: p.name.lower()):
                    kind = C.FS_TYPE_DIR if child.is_dir() else C.FS_TYPE_FILE
                    entries.append((kind, child.name))
            except OSError:
                return neg(EACCES)
            return self._alloc_fd(DirectoryFD(entries))
        if not host.exists() and not (flags & C.O_CREAT):
            return neg(ENOENT)
        try:
            if write_mode == C.O_RDONLY:
                py_mode = "rb"
            elif write_mode == C.O_WRONLY:
                py_mode = "ab" if flags & C.O_APPEND else "wb" if flags & C.O_TRUNC else "r+b"
            else:
                py_mode = "a+b" if flags & C.O_APPEND else "w+b" if flags & C.O_TRUNC else "r+b"
            if flags & C.O_CREAT and not host.exists():
                py_mode = "w+b" if write_mode == C.O_RDWR else "wb"
            fp = open(host, py_mode)
            if flags & C.O_APPEND:
                fp.seek(0, os.SEEK_END)
            return self._alloc_fd(fp)
        except FileExistsError:
            return neg(EEXIST)
        except FileNotFoundError:
            return neg(ENOENT)
        except PermissionError:
            return neg(EACCES)
        except IsADirectoryError:
            return neg(EISDIR)
        except OSError:
            return neg(EINVAL)

    def read(self, fd: int, count: int) -> bytes | int:
        value = self.fds.get(fd)
        if value is None:
            return neg(EBADF)
        if isinstance(value, DirectoryFD):
            if value.index >= len(value.entries):
                return b""
            kind, name = value.entries[value.index]
            value.index += 1
            data = structs.pack_dir_entry(kind, name)
            return data[:count]
        if isinstance(value, VirtualFD):
            return value.data.read(count)
        if fd in (0, 3):
            return b""
        if fd in (1, 2):
            return neg(EBADF)
        try:
            return value.read(count)
        except OSError:
            return neg(EINVAL)

    def write(self, fd: int, data: bytes) -> int:
        value = self.fds.get(fd)
        if fd in (1, 2):
            text = data.decode("utf-8", "replace").rstrip("\n")
            if text:
                self.log(text)
            return len(data)
        if value is None or isinstance(value, DirectoryFD):
            return neg(EBADF)
        if isinstance(value, VirtualFD):
            if not value.writable:
                return neg(EBADF)
            return value.data.write(data)
        if fd in (0, 3):
            return neg(EBADF)
        try:
            return value.write(data)
        except OSError:
            return neg(EINVAL)

    def close(self, fd: int) -> int:
        if fd in (0, 1, 2, 3):
            return 0
        value = self.fds.pop(fd, None)
        if value is None:
            return neg(EBADF)
        if hasattr(value, "close"):
            try:
                if isinstance(value, VirtualFD) and value.path == self.DISPLAY_CONFIG_PATH:
                    self._display_config = value.data.getvalue()
                value.close()
            except OSError:
                return neg(EINVAL)
        return 0

    def lseek(self, fd: int, offset: int, whence: int) -> int:
        value = self.fds.get(fd)
        if value is None or isinstance(value, DirectoryFD):
            return neg(EBADF)
        if isinstance(value, VirtualFD):
            stream = value.data
        else:
            stream = value
        try:
            return stream.seek(offset, whence)
        except OSError:
            return neg(EINVAL)

    def stat_path(self, path: str) -> tuple[int, int] | int:
        try:
            guest = self.guest_abs(path)
            host = self.host_path(guest)
        except (ValueError, PermissionError):
            return neg(EACCES)
        if self._is_display_config(guest):
            return (C.FS_TYPE_FILE, len(self._display_config))
        if guest.lower() == "0:/system/config/locale.conf" and not host.exists():
            return (C.FS_TYPE_FILE, len("lang=zh\n" if self.language == "zh" else "lang=en\n"))
        try:
            st = host.stat()
        except FileNotFoundError:
            return neg(ENOENT)
        except OSError:
            return neg(EACCES)
        return (C.FS_TYPE_DIR if host.is_dir() else C.FS_TYPE_FILE, st.st_size)

    def list_dir(self, path: str) -> list[tuple[int, str]] | int:
        try:
            host = self.host_path(path)
        except (ValueError, PermissionError):
            return neg(EACCES)
        if not host.exists():
            return neg(ENOENT)
        if not host.is_dir():
            return neg(ENOTDIR)
        try:
            entries = [
                (C.FS_TYPE_DIR if child.is_dir() else C.FS_TYPE_FILE, child.name)
                for child in sorted(host.iterdir(), key=lambda p: p.name.lower())
            ]
            if self.guest_abs(path).lower() == "0:/system/config" and not any(name.lower() == "display.conf" for _, name in entries):
                entries.append((C.FS_TYPE_FILE, "display.conf"))
            return entries
        except OSError:
            return neg(EACCES)

    def fstat(self, fd: int) -> tuple[int, int] | int:
        value = self.fds.get(fd)
        if value is None:
            return neg(EBADF)
        if isinstance(value, DirectoryFD):
            return (C.FS_TYPE_DIR, len(value.entries) * structs.DIR_ENTRY_SIZE)
        if isinstance(value, VirtualFD):
            pos = value.data.tell()
            value.data.seek(0, os.SEEK_END)
            size = value.data.tell()
            value.data.seek(pos)
            return (C.FS_TYPE_FILE, size)
        if fd in (0, 1, 2, 3):
            return (C.FS_TYPE_DEVICE, 0)
        try:
            st = os.fstat(value.fileno())
            return (C.FS_TYPE_FILE, st.st_size)
        except OSError:
            return neg(EBADF)

    def mkdir(self, path: str) -> int:
        try:
            self.host_path(path).mkdir()
            return 0
        except FileExistsError:
            return neg(EEXIST)
        except FileNotFoundError:
            return neg(ENOENT)
        except PermissionError:
            return neg(EACCES)
        except OSError:
            return neg(EINVAL)

    def rmdir(self, path: str) -> int:
        try:
            self.host_path(path).rmdir()
            return 0
        except FileNotFoundError:
            return neg(ENOENT)
        except NotADirectoryError:
            return neg(ENOTDIR)
        except OSError:
            return neg(EINVAL)

    def unlink(self, path: str) -> int:
        try:
            self.host_path(path).unlink()
            return 0
        except FileNotFoundError:
            return neg(ENOENT)
        except IsADirectoryError:
            return neg(EISDIR)
        except PermissionError:
            return neg(EACCES)
        except OSError:
            return neg(EINVAL)

    def rename(self, old: str, new: str) -> int:
        try:
            self.host_path(old).replace(self.host_path(new))
            return 0
        except FileNotFoundError:
            return neg(ENOENT)
        except PermissionError:
            return neg(EACCES)
        except OSError:
            return neg(EINVAL)

    def chdir(self, path: str) -> int:
        try:
            guest = self.guest_abs(path)
            host = self.host_path(guest)
        except (ValueError, PermissionError):
            return neg(EACCES)
        if not host.exists():
            return neg(ENOENT)
        if not host.is_dir():
            return neg(ENOTDIR)
        self.cwd = guest if guest.endswith("/") else guest + "/"
        return 0
