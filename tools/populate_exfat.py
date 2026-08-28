#!/usr/bin/env python3
"""Populate a standard 512-byte-sector exFAT image without mounting it.

The image is expected to have been created by mkfs.exfat.  Keeping this
writer in userspace means normal VMDK builds neither require root privileges
nor rely on a loop device.  It deliberately emits ordinary exFAT directory
sets, allocation-bitmap updates, FAT chains for directories, and NoFatChain
extents for contiguous regular files.
"""
from __future__ import annotations

import argparse
import math
import os
import struct
from pathlib import Path


SECTOR = 512
ENTRY = 32
EOC = 0xFFFFFFFF
FILE = 0x85
STREAM = 0xC0
NAME = 0xC1
BITMAP = 0x81
UPCASE = 0x82
ATTR_DIRECTORY = 0x10
NO_FAT_CHAIN = 0x02


class ExfatError(RuntimeError):
    pass


def checksum_entry_set(data: bytes) -> int:
    value = 0
    for offset, byte in enumerate(data):
        if offset in (2, 3):
            continue
        value = ((value << 15) | (value >> 1)) & 0xFFFF
        value = (value + byte) & 0xFFFF
    return value


def checksum_upcase(data: bytes) -> int:
    value = 0
    for byte in data:
        value = ((value << 31) | (value >> 1)) & 0xFFFFFFFF
        value = (value + byte) & 0xFFFFFFFF
    return value


class ExfatVolume:
    def __init__(self, image: Path) -> None:
        self.path = image
        self.handle = image.open("r+b", buffering=0)
        boot = self.read_at(0, SECTOR)
        if boot[3:11] != b"EXFAT   " or boot[510:512] != b"\x55\xaa":
            raise ExfatError(f"not a 512-byte exFAT image: {image}")
        if boot[108] != 9 or boot[110] != 1:
            raise ExfatError("only single-FAT 512-byte-sector exFAT is supported")
        self.volume_sectors = struct.unpack_from("<Q", boot, 72)[0]
        self.fat_offset, self.fat_length, self.heap_offset, self.cluster_count, self.root_cluster = \
            struct.unpack_from("<IIIII", boot, 80)
        self.spc_shift = boot[109]
        if self.spc_shift > 8 or not self.cluster_count or self.root_cluster < 2:
            raise ExfatError("invalid exFAT geometry")
        self.spc = 1 << self.spc_shift
        self.cluster_bytes = self.spc * SECTOR
        if self.heap_offset + self.cluster_count * self.spc > self.volume_sectors:
            raise ExfatError("cluster heap outside volume")
        self.bitmap_cluster = 0
        self.bitmap_length = 0
        self.upcase_cluster = 0
        self.upcase_length = 0
        self.upcase_checksum = 0
        # Child directories need their stream entry updated whenever their
        # FAT chain grows.  The root directory has no parent file entry set.
        self.directory_records: dict[tuple[int, bool], dict[str, int | bool | None]] = {
            (self.root_cluster, False): {
                "parent_first": None,
                "parent_nofat": None,
                "entry_index": None,
                "secondary": None,
                "data_length": self.cluster_bytes,
            }
        }
        self._discover_system_entries()
        self.upcase = self._load_upcase()

    def close(self) -> None:
        self.handle.close()

    def read_at(self, offset: int, length: int) -> bytes:
        self.handle.seek(offset)
        data = self.handle.read(length)
        if len(data) != length:
            raise ExfatError("short image read")
        return data

    def write_at(self, offset: int, data: bytes) -> None:
        self.handle.seek(offset)
        written = self.handle.write(data)
        if written != len(data):
            raise ExfatError("short image write")

    def cluster_offset(self, cluster: int) -> int:
        if cluster < 2 or cluster - 2 >= self.cluster_count:
            raise ExfatError(f"invalid cluster {cluster}")
        return (self.heap_offset + (cluster - 2) * self.spc) * SECTOR

    def fat_entry(self, cluster: int) -> int:
        return struct.unpack("<I", self.read_at((self.fat_offset * SECTOR) + cluster * 4, 4))[0]

    def write_fat_entry(self, cluster: int, value: int) -> None:
        self.write_at((self.fat_offset * SECTOR) + cluster * 4, struct.pack("<I", value))

    def cluster_at(self, first: int, nofat: bool, index: int) -> int:
        if nofat:
            cluster = first + index
            self.cluster_offset(cluster)
            return cluster
        cluster = first
        if index < 0 or index >= self.cluster_count:
            raise ExfatError("FAT chain index out of range")
        seen: set[int] = set()
        for _ in range(index):
            if cluster in seen:
                raise ExfatError("FAT chain loop")
            seen.add(cluster)
            cluster = self.fat_entry(cluster)
            if cluster < 2 or cluster >= EOC:
                raise ExfatError("short or corrupt FAT chain")
        self.cluster_offset(cluster)
        return cluster

    def read_stream(self, first: int, nofat: bool, offset: int, length: int) -> bytes:
        result = bytearray()
        while length:
            cluster_index, cluster_offset = divmod(offset, self.cluster_bytes)
            cluster = self.cluster_at(first, nofat, cluster_index)
            take = min(length, self.cluster_bytes - cluster_offset)
            result += self.read_at(self.cluster_offset(cluster) + cluster_offset, take)
            offset += take
            length -= take
        return bytes(result)

    def write_stream(self, first: int, nofat: bool, offset: int, data: bytes) -> None:
        done = 0
        while done < len(data):
            stream_offset = offset + done
            cluster_index, cluster_offset = divmod(stream_offset, self.cluster_bytes)
            cluster = self.cluster_at(first, nofat, cluster_index)
            take = min(len(data) - done, self.cluster_bytes - cluster_offset)
            self.write_at(self.cluster_offset(cluster) + cluster_offset, data[done:done + take])
            done += take

    def read_dir_entry(self, first: int, nofat: bool, index: int) -> bytes:
        return self.read_stream(first, nofat, index * ENTRY, ENTRY)

    def write_dir_entry(self, first: int, nofat: bool, index: int, data: bytes) -> None:
        if len(data) != ENTRY:
            raise ExfatError("invalid directory entry size")
        self.write_stream(first, nofat, index * ENTRY, data)

    def _discover_system_entries(self) -> None:
        for index in range(self.cluster_bytes // ENTRY):
            entry = self.read_dir_entry(self.root_cluster, False, index)
            if entry[0] == 0:
                break
            if entry[0] == BITMAP and not self.bitmap_cluster:
                self.bitmap_cluster = struct.unpack_from("<I", entry, 20)[0]
                self.bitmap_length = struct.unpack_from("<Q", entry, 24)[0]
            elif entry[0] == UPCASE and not self.upcase_cluster:
                self.upcase_checksum = struct.unpack_from("<I", entry, 4)[0]
                self.upcase_cluster = struct.unpack_from("<I", entry, 20)[0]
                self.upcase_length = struct.unpack_from("<Q", entry, 24)[0]
        if not self.bitmap_cluster or not self.upcase_cluster:
            raise ExfatError("missing allocation bitmap or upcase table")

    def _load_upcase(self) -> list[int]:
        data = self.read_stream(self.upcase_cluster, False, 0, self.upcase_length)
        if checksum_upcase(data) != self.upcase_checksum:
            raise ExfatError("upcase checksum mismatch")
        result: list[int] = []
        offset = 0
        while offset + 2 <= len(data) and len(result) < 65536:
            value = struct.unpack_from("<H", data, offset)[0]
            offset += 2
            if value != 0xFFFF or offset >= len(data):
                result.append(value)
                continue
            count = struct.unpack_from("<H", data, offset)[0]
            offset += 2
            if not count or count > 65536 - len(result):
                raise ExfatError("invalid compressed upcase table")
            result.extend(range(len(result), len(result) + count))
        if len(result) != 65536:
            raise ExfatError("incomplete upcase table")
        return result

    def bitmap_get(self, cluster: int) -> bool:
        bit = cluster - 2
        value = self.read_stream(self.bitmap_cluster, False, bit // 8, 1)[0]
        return bool(value & (1 << (bit & 7)))

    def bitmap_set(self, cluster: int, allocated: bool) -> None:
        bit = cluster - 2
        offset = bit // 8
        value = self.read_stream(self.bitmap_cluster, False, offset, 1)[0]
        if allocated:
            value |= 1 << (bit & 7)
        else:
            value &= ~(1 << (bit & 7))
        self.write_stream(self.bitmap_cluster, False, offset, bytes([value]))

    def allocate(self, count: int, directory: bool = False) -> tuple[int, bool]:
        if count <= 0:
            return 0, False
        run = 0
        first = 0
        for cluster in range(2, self.cluster_count + 2):
            if not self.bitmap_get(cluster):
                run += 1
                if run == count:
                    first = cluster - count + 1
                    break
            else:
                run = 0
        if not first:
            free = [cluster for cluster in range(2, self.cluster_count + 2)
                    if not self.bitmap_get(cluster)]
            if len(free) < count:
                raise ExfatError("exFAT image is full")
            for cluster in free[:count]:
                self.bitmap_set(cluster, True)
            for index, cluster in enumerate(free[:count]):
                self.write_fat_entry(cluster, free[index + 1] if index + 1 < count else EOC)
            return free[0], False
        for cluster in range(first, first + count):
            self.bitmap_set(cluster, True)
        if directory:
            for cluster in range(first, first + count):
                self.write_fat_entry(cluster, cluster + 1 if cluster + 1 < first + count else EOC)
            return first, False
        return first, True

    def name_hash(self, name: list[int]) -> int:
        value = 0
        for codepoint in name:
            upper = self.upcase[codepoint]
            for byte in struct.pack("<H", upper):
                value = ((value << 15) | (value >> 1)) & 0xFFFF
                value = (value + byte) & 0xFFFF
        return value

    def find_in_dir(self, first: int, nofat: bool, text: str) -> tuple[int, dict[str, int]] | None:
        encoded = text.encode("utf-16le")
        wanted = list(struct.unpack(f"<{len(encoded) // 2}H", encoded))
        index = 0
        while index < self.cluster_count * self.cluster_bytes // ENTRY:
            primary = self.read_dir_entry(first, nofat, index)
            if primary[0] == 0:
                return None
            if primary[0] != FILE:
                index += 1
                continue
            count = primary[1]
            entries = [primary] + [self.read_dir_entry(first, nofat, index + step)
                                   for step in range(1, count + 1)]
            if count < 2 or entries[1][0] != STREAM or checksum_entry_set(b"".join(entries)) != struct.unpack_from("<H", primary, 2)[0]:
                raise ExfatError("corrupt file entry set")
            length = entries[1][3]
            units: list[int] = []
            for item in entries[2:]:
                if item[0] != NAME:
                    raise ExfatError("missing filename entry")
                units.extend(struct.unpack_from("<15H", item, 2))
            units = units[:length]
            if len(units) != length:
                raise ExfatError("invalid filename length")
            if len(units) == len(wanted) and all(self.upcase[a] == self.upcase[b] for a, b in zip(units, wanted)):
                return index, {
                    "secondary": count,
                    "directory": bool(struct.unpack_from("<H", primary, 4)[0] & ATTR_DIRECTORY),
                    "first": struct.unpack_from("<I", entries[1], 20)[0],
                    "size": struct.unpack_from("<Q", entries[1], 24)[0],
                    "nofat": bool(entries[1][1] & NO_FAT_CHAIN),
                }
            index += count + 1
        raise ExfatError("directory chain has no end marker")

    def free_span(self, first: int, nofat: bool, count: int) -> int:
        run = 0
        start = 0
        index = 0
        while index < self.cluster_count * self.cluster_bytes // ENTRY:
            try:
                entry = self.read_dir_entry(first, nofat, index)
            except ExfatError:
                if nofat:
                    raise
                self.grow_directory(first, nofat)
                continue
            if not entry[0] & 0x80:
                if not run:
                    start = index
                run += 1
                if run >= count:
                    return start
                if entry[0] == 0:
                    self.ensure_directory_capacity(first, nofat, start + count)
                    return start
            else:
                run = 0
            index += 1
        raise ExfatError("directory is full")

    def grow_directory(self, first: int, nofat: bool) -> None:
        if nofat:
            raise ExfatError("cannot grow a contiguous directory")
        tail = first
        old_clusters = 1
        while True:
            next_cluster = self.fat_entry(tail)
            if next_cluster >= EOC:
                break
            if next_cluster < 2:
                raise ExfatError("corrupt directory FAT chain")
            tail = next_cluster
            old_clusters += 1
        cluster, nofat = self.allocate(1, directory=True)
        if nofat:
            raise ExfatError("directory allocation unexpectedly has NoFatChain")
        self.write_at(self.cluster_offset(cluster), b"\0" * self.cluster_bytes)
        self.write_fat_entry(tail, cluster)

        record = self.directory_records.get((first, False))
        if record is None:
            raise ExfatError("missing directory entry reference")
        if record["parent_first"] is None:
            # The root directory has no stream extension.  Its FAT chain is
            # its authoritative capacity, so there is no entry set to amend.
            record["data_length"] = (old_clusters + 1) * self.cluster_bytes
            return

        parent_first = int(record["parent_first"])
        parent_nofat = bool(record["parent_nofat"])
        entry_index = int(record["entry_index"])
        secondary = int(record["secondary"])
        entries = [self.read_dir_entry(parent_first, parent_nofat, entry_index + index)
                   for index in range(secondary + 1)]
        if entries[0][0] != FILE or entries[1][0] != STREAM:
            raise ExfatError("directory entry reference is corrupt")
        new_length = (old_clusters + 1) * self.cluster_bytes
        stream = bytearray(entries[1])
        struct.pack_into("<Q", stream, 8, new_length)
        struct.pack_into("<Q", stream, 24, new_length)
        entries[1] = bytes(stream)
        primary = bytearray(entries[0])
        struct.pack_into("<H", primary, 2, checksum_entry_set(b"".join(entries)))
        entries[0] = bytes(primary)
        self.write_entry_set(parent_first, parent_nofat, entry_index, b"".join(entries))
        record["data_length"] = new_length

    def ensure_directory_capacity(self, first: int, nofat: bool, entries: int) -> None:
        if entries <= 0:
            return
        target_cluster = ((entries - 1) * ENTRY) // self.cluster_bytes
        while True:
            try:
                self.cluster_at(first, nofat, target_cluster)
                return
            except ExfatError:
                if nofat:
                    raise
                self.grow_directory(first, nofat)

    def make_entry_set(self, name: str, directory: bool, first: int, size: int, nofat: bool) -> bytes:
        units = list(name.encode("utf-16le"))
        if len(units) % 2 or len(units) // 2 > 255:
            raise ExfatError(f"unsupported filename: {name}")
        name_units = list(struct.unpack(f"<{len(units) // 2}H", bytes(units)))
        names = math.ceil(len(name_units) / 15)
        entries = [bytearray(ENTRY) for _ in range(names + 2)]
        entries[0][0] = FILE
        entries[0][1] = names + 1
        struct.pack_into("<H", entries[0], 4, ATTR_DIRECTORY if directory else 0)
        entries[1][0] = STREAM
        entries[1][1] = NO_FAT_CHAIN if nofat else 0
        entries[1][3] = len(name_units)
        struct.pack_into("<H", entries[1], 4, self.name_hash(name_units))
        struct.pack_into("<Q", entries[1], 8, size)
        struct.pack_into("<I", entries[1], 20, first)
        struct.pack_into("<Q", entries[1], 24, size)
        for part in range(names):
            entries[part + 2][0] = NAME
            chunk = name_units[part * 15:(part + 1) * 15]
            entries[part + 2][1] = len(chunk)
            struct.pack_into(f"<{len(chunk)}H", entries[part + 2], 2, *chunk)
        data = b"".join(entries)
        struct.pack_into("<H", entries[0], 2, checksum_entry_set(data))
        return b"".join(entries)

    def write_entry_set(self, directory_first: int, directory_nofat: bool, index: int, data: bytes) -> None:
        for entry_index in range(0, len(data), ENTRY):
            self.write_dir_entry(directory_first, directory_nofat,
                                 index + entry_index // ENTRY, data[entry_index:entry_index + ENTRY])

    def ensure_directory(self, first: int, nofat: bool, name: str) -> tuple[int, bool]:
        found = self.find_in_dir(first, nofat, name)
        if found:
            _, node = found
            if not node["directory"]:
                raise ExfatError(f"path component is a file: {name}")
            return node["first"], bool(node["nofat"])
        cluster, child_nofat = self.allocate(1, directory=True)
        self.write_at(self.cluster_offset(cluster), b"\0" * self.cluster_bytes)
        data = self.make_entry_set(name, True, cluster, self.cluster_bytes, child_nofat)
        entry_index = self.free_span(first, nofat, len(data) // ENTRY)
        self.write_entry_set(first, nofat, entry_index, data)
        self.directory_records[(cluster, child_nofat)] = {
            "parent_first": first,
            "parent_nofat": nofat,
            "entry_index": entry_index,
            "secondary": len(data) // ENTRY - 1,
            "data_length": self.cluster_bytes,
        }
        return cluster, child_nofat

    def put_file(self, first: int, nofat: bool, name: str, source: Path) -> None:
        if self.find_in_dir(first, nofat, name):
            raise ExfatError(f"duplicate staged path: {source}")
        size = source.stat().st_size
        cluster = 0
        file_nofat = False
        if size:
            clusters = math.ceil(size / self.cluster_bytes)
            cluster, file_nofat = self.allocate(clusters, directory=False)
            with source.open("rb") as input_file:
                remaining = size
                offset = 0
                while remaining:
                    data = input_file.read(min(128 * 1024, remaining))
                    if not data:
                        raise ExfatError(f"short source read: {source}")
                    self.write_stream(cluster, file_nofat, offset, data)
                    offset += len(data)
                    remaining -= len(data)
        data = self.make_entry_set(name, False, cluster, size, file_nofat)
        self.write_entry_set(first, nofat, self.free_span(first, nofat, len(data) // ENTRY), data)

    def populate(self, staging: Path) -> None:
        if not staging.is_dir():
            raise ExfatError(f"staging tree does not exist: {staging}")
        directories: dict[Path, tuple[int, bool]] = {Path("."): (self.root_cluster, False)}
        for path in sorted(staging.rglob("*"), key=lambda item: (len(item.relative_to(staging).parts), item.as_posix())):
            relative = path.relative_to(staging)
            parent = directories.get(relative.parent)
            if parent is None:
                raise ExfatError(f"missing parent directory for {relative}")
            if path.is_dir():
                directories[relative] = self.ensure_directory(parent[0], parent[1], path.name)
            elif path.is_file():
                self.put_file(parent[0], parent[1], path.name, path)
            else:
                raise ExfatError(f"unsupported staging entry: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Populate a standard exFAT image")
    parser.add_argument("--image", required=True)
    parser.add_argument("--tree", required=True)
    args = parser.parse_args()
    volume = ExfatVolume(Path(args.image))
    try:
        volume.populate(Path(args.tree))
    finally:
        volume.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ExfatError as error:
        raise SystemExit(f"populate_exfat: {error}")
