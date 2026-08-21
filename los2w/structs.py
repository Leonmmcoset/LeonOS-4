"""Little-endian LeonOS ABI structure helpers."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from . import constants as C


def zbytes(text: str | bytes, size: int) -> bytes:
    if isinstance(text, str):
        data = text.encode("utf-8", "replace")
    else:
        data = text
    return data[: max(0, size - 1)] + b"\0" * max(0, size - len(data[: max(0, size - 1)]))


def fixed_text(data: bytes) -> str:
    end = data.find(b"\0")
    if end >= 0:
        data = data[:end]
    return data.decode("utf-8", "replace")


def ipv4_to_u32(addr: str) -> int:
    parts = [int(p) for p in addr.split(".")]
    if len(parts) != 4 or any(p < 0 or p > 255 for p in parts):
        return 0
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]


def u32_to_ipv4(value: int) -> str:
    return ".".join(str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))


@dataclass
class GuiCreate:
    width: int
    height: int
    title_ptr: int
    text_ptr: int
    flags: int

    FORMAT = "<IIQQI4x"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "GuiCreate":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE])[:5])


@dataclass
class GuiPresent:
    window_id: int
    width: int
    height: int
    stride: int
    pixels_ptr: int

    FORMAT = "<IIIIQ"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "GuiPresent":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))


@dataclass
class GuiFetch:
    window_id: int
    capacity_width: int
    capacity_height: int
    stride: int
    out_width: int
    out_height: int
    pixels_ptr: int

    FORMAT = "<IIIIIIQ"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "GuiFetch":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.window_id,
            self.capacity_width,
            self.capacity_height,
            self.stride,
            self.out_width,
            self.out_height,
            self.pixels_ptr,
        )


@dataclass
class GuiAppEvent:
    window_id: int = 0
    type: int = 0
    x: int = 0
    y: int = 0
    dx: int = 0
    dy: int = 0
    width: int = 0
    height: int = 0
    buttons: int = 0
    keycode: int = 0
    pressed: int = 0
    reserved: int = 0

    FORMAT = "<IIiiiiIIBBBB"
    SIZE = struct.calcsize(FORMAT)

    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.window_id,
            self.type,
            self.x,
            self.y,
            self.dx,
            self.dy,
            self.width,
            self.height,
            self.buttons,
            self.keycode,
            self.pressed,
            self.reserved,
        )

    @classmethod
    def unpack(cls, data: bytes) -> "GuiAppEvent":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))


def pack_stat(kind: int, size: int) -> bytes:
    return struct.pack("<IIQ", kind, 0, size)


def pack_dir_entry(kind: int, name: str) -> bytes:
    return struct.pack("<I", kind) + zbytes(name, C.FS_NAME_LEN)


DIR_ENTRY_SIZE = 4 + C.FS_NAME_LEN


@dataclass
class DirList:
    path_ptr: int
    capacity: int
    count: int
    entries_ptr: int

    FORMAT = "<QIIQ"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "DirList":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.path_ptr, self.capacity, self.count, self.entries_ptr)


def pack_user(uid: int = 1, role: int = C.AUTH_ROLE_ADMIN, username: str = "los2w", home: str = "/users/los2w") -> bytes:
    return (
        struct.pack("<IIII", uid, role, 0, 0)
        + zbytes(username, 32)
        + zbytes(home, 96)
    )


def pack_auth_status(user_count: int = 1, has_admin: int = 1) -> bytes:
    return struct.pack("<IIII", user_count, has_admin, 0, 0)


def pack_time_info(unix_seconds: int, uptime_ms: int, parts) -> bytes:
    return struct.pack(
        "<QQIIIIIIII",
        unix_seconds,
        uptime_ms,
        parts.tm_year,
        parts.tm_mon,
        parts.tm_mday,
        parts.tm_hour,
        parts.tm_min,
        parts.tm_sec,
        1,
        0,
    )


@dataclass
class TimeSync:
    timeout_ms: int
    status: int
    server_ip: int
    valid: int
    unix_seconds: int
    server: str

    FORMAT = f"<IIIIQ{C.NET_HOSTNAME_LEN}s"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "TimeSync":
        timeout_ms, status, server_ip, valid, unix_seconds, server = struct.unpack(
            cls.FORMAT, data[: cls.SIZE]
        )
        return cls(timeout_ms, status, server_ip, valid, unix_seconds, fixed_text(server))

    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.timeout_ms,
            self.status,
            self.server_ip,
            self.valid,
            self.unix_seconds,
            zbytes(self.server, C.NET_HOSTNAME_LEN),
        )


def pack_net_config() -> bytes:
    mac = b"\x52\x54\x00\x12\x34\x56" + b"\0\0"
    return struct.pack(
        "<IIIIIIII8s",
        C.NET_CONFIG_FLAG_PRESENT | C.NET_CONFIG_FLAG_ACTIVE | C.NET_CONFIG_FLAG_DHCP,
        C.NET_CONFIG_SOURCE_DHCP,
        ipv4_to_u32("127.0.0.1"),
        ipv4_to_u32("255.0.0.0"),
        ipv4_to_u32("127.0.0.1"),
        ipv4_to_u32("127.0.0.1"),
        ipv4_to_u32("127.0.0.1"),
        3600,
        mac,
    )


@dataclass
class NetDNS:
    name: str
    timeout_ms: int
    status: int
    address_count: int
    addresses: tuple[int, int, int, int]

    SIZE = C.NET_HOSTNAME_LEN + 12 + C.NET_DNS_MAX_ADDRESSES * 4

    @classmethod
    def unpack(cls, data: bytes) -> "NetDNS":
        name = fixed_text(data[: C.NET_HOSTNAME_LEN])
        offset = C.NET_HOSTNAME_LEN
        timeout_ms, status, address_count = struct.unpack("<III", data[offset : offset + 12])
        offset += 12
        addresses = struct.unpack("<IIII", data[offset : offset + 16])
        return cls(name, timeout_ms, status, address_count, addresses)

    def pack(self) -> bytes:
        return (
            zbytes(self.name, C.NET_HOSTNAME_LEN)
            + struct.pack("<III", self.timeout_ms, self.status, self.address_count)
            + struct.pack("<IIII", *self.addresses)
        )


@dataclass
class SocketOpen:
    domain: int
    type: int
    protocol: int
    timeout_ms: int
    status: int
    socket: int

    FORMAT = "<IIIIIi"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "SocketOpen":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.domain, self.type, self.protocol, self.timeout_ms, self.status, self.socket)


@dataclass
class SocketConnect:
    socket: int
    host: str
    port: int
    timeout_ms: int
    status: int
    remote_ip: int
    local_ip: int
    local_port: int

    SIZE = 4 + C.NET_HOSTNAME_LEN + 24

    @classmethod
    def unpack(cls, data: bytes) -> "SocketConnect":
        socket_id = struct.unpack("<i", data[:4])[0]
        host = fixed_text(data[4 : 4 + C.NET_HOSTNAME_LEN])
        offset = 4 + C.NET_HOSTNAME_LEN
        port, timeout_ms, status, remote_ip, local_ip, local_port = struct.unpack("<IIIIII", data[offset : offset + 24])
        return cls(socket_id, host, port, timeout_ms, status, remote_ip, local_ip, local_port)

    def pack(self) -> bytes:
        return (
            struct.pack("<i", self.socket)
            + zbytes(self.host, C.NET_HOSTNAME_LEN)
            + struct.pack("<IIIIII", self.port, self.timeout_ms, self.status, self.remote_ip, self.local_ip, self.local_port)
        )


@dataclass
class SocketIO:
    socket: int
    buffer_ptr: int
    length: int
    timeout_ms: int
    status: int
    transferred: int

    FORMAT = "<i4xQIIII"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "SocketIO":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.socket, self.buffer_ptr, self.length, self.timeout_ms, self.status, self.transferred)


@dataclass
class SocketClose:
    socket: int
    status: int

    FORMAT = "<iI"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "SocketClose":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.socket, self.status)


@dataclass
class ConnectionList:
    capacity: int
    count: int
    entries_ptr: int

    FORMAT = "<IIQ"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "ConnectionList":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.capacity, self.count, self.entries_ptr)


def pack_connection_info(socket_id: int, state: int, remote_ip: int, remote_port: int, tx: int, rx: int) -> bytes:
    return struct.pack(
        "<iIIIIIIIIII",
        socket_id,
        1,
        state,
        C.NET_STATUS_OK,
        ipv4_to_u32("127.0.0.1"),
        remote_ip,
        0,
        remote_port,
        0,
        tx,
        rx,
    )


CONNECTION_INFO_SIZE = struct.calcsize("<iIIIIIIIIII")


@dataclass
class TextLayout:
    text_ptr: int
    byte_len: int
    capacity: int
    count: int
    total_cells: int
    total_px: int
    glyphs_ptr: int

    FORMAT = "<QIIIII4xQ"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "TextLayout":
        return cls(*struct.unpack(cls.FORMAT, data[: cls.SIZE]))

    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.text_ptr,
            self.byte_len,
            self.capacity,
            self.count,
            self.total_cells,
            self.total_px,
            self.glyphs_ptr,
        )


def pack_text_glyph(codepoint: int, byte_offset: int, byte_len: int, cell_width: int) -> bytes:
    return struct.pack("<IIIII", codepoint, byte_offset, byte_len, cell_width, cell_width * 8)


TEXT_GLYPH_SIZE = struct.calcsize("<IIIII")


@dataclass
class NetHttpGet:
    host: str
    path: str
    port: int
    timeout_ms: int
    status: int
    remote_ip: int
    http_status: int
    response_len: int
    response: bytes

    SIZE = C.NET_HOSTNAME_LEN + C.NET_HTTP_PATH_LEN + 24 + C.NET_HTTP_RESPONSE_MAX

    @classmethod
    def unpack(cls, data: bytes) -> "NetHttpGet":
        host = fixed_text(data[: C.NET_HOSTNAME_LEN])
        offset = C.NET_HOSTNAME_LEN
        path = fixed_text(data[offset : offset + C.NET_HTTP_PATH_LEN])
        offset += C.NET_HTTP_PATH_LEN
        port, timeout_ms, status, remote_ip, http_status, response_len = struct.unpack("<IIIIII", data[offset : offset + 24])
        offset += 24
        response = data[offset : offset + C.NET_HTTP_RESPONSE_MAX]
        return cls(host, path, port, timeout_ms, status, remote_ip, http_status, response_len, response)

    def pack(self) -> bytes:
        return (
            zbytes(self.host, C.NET_HOSTNAME_LEN)
            + zbytes(self.path, C.NET_HTTP_PATH_LEN)
            + struct.pack("<IIIIII", self.port, self.timeout_ms, self.status, self.remote_ip, self.http_status, self.response_len)
            + self.response[: C.NET_HTTP_RESPONSE_MAX].ljust(C.NET_HTTP_RESPONSE_MAX, b"\0")
        )
