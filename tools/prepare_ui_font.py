#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path


DEFAULT_FONT = Path("system/fonts/Deng.ttf")
DEFAULT_WIN95_FONT = Path("system/fonts/Deng.ttf")
DEFAULT_PIXEL_SOURCE = Path("system/fonts/system.psf")
PIXEL_FIRST = 0x20
PIXEL_LAST = 0x7E
MAX_RUNTIME_FONT_SIZE = 20 * 1024 * 1024


def be16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def be16s(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def align4(data: bytearray) -> None:
    data.extend(b"\0" * ((-len(data)) & 3))


def checksum(data: bytes) -> int:
    padded = data + b"\0" * ((-len(data)) & 3)
    return sum(struct.unpack(f">{len(padded) // 4}I", padded)) & 0xFFFFFFFF


def font_tables(data: bytes) -> tuple[bytes, list[bytes], dict[bytes, bytes]]:
    if len(data) < 12:
        raise ValueError("source font is not a TrueType glyf font")
    face_offset = 0
    if data[:4] == b"ttcf":
        if len(data) < 16 or be32(data, 8) < 1 or be32(data, 8) > (len(data) - 12) // 4:
            raise ValueError("invalid TrueType font collection")
        face_offset = be32(data, 12)
    if face_offset > len(data) - 12 or be32(data, face_offset) != 0x00010000:
        raise ValueError("source font is not a TrueType glyf font")
    count = be16(data, face_offset + 4)
    if len(data) < face_offset + 12 + count * 16:
        raise ValueError("truncated TrueType table directory")
    order: list[bytes] = []
    tables: dict[bytes, bytes] = {}
    for index in range(count):
        offset = face_offset + 12 + index * 16
        tag, _, table_offset, length = struct.unpack_from(">4sIII", data, offset)
        if table_offset > len(data) or length > len(data) - table_offset:
            raise ValueError(f"truncated TrueType table {tag.decode('ascii', 'replace')}")
        order.append(tag)
        tables[tag] = data[table_offset:table_offset + length]
    return data[face_offset:face_offset + 12], order, tables


def cmap_format4_glyph(table: bytes, codepoint: int) -> int:
    if len(table) < 16 or be16(table, 0) != 4:
        return 0
    length = be16(table, 2)
    segments = be16(table, 6) // 2
    end_offset = 14
    start_offset = end_offset + segments * 2 + 2
    delta_offset = start_offset + segments * 2
    range_offset = delta_offset + segments * 2
    if length > len(table) or range_offset + segments * 2 > length:
        return 0
    for index in range(segments):
        end = be16(table, end_offset + index * 2)
        start = be16(table, start_offset + index * 2)
        if codepoint > end:
            continue
        if codepoint < start:
            return 0
        delta = be16(table, delta_offset + index * 2)
        glyph_range = be16(table, range_offset + index * 2)
        if not glyph_range:
            return (codepoint + delta) & 0xFFFF
        glyph_offset = range_offset + index * 2 + glyph_range + (codepoint - start) * 2
        if glyph_offset + 2 > length:
            return 0
        glyph = be16(table, glyph_offset)
        return (glyph + delta) & 0xFFFF if glyph else 0
    return 0


def ascii_glyph_map(cmap: bytes) -> dict[int, int]:
    if len(cmap) < 4:
        raise ValueError("missing cmap records")
    count = be16(cmap, 2)
    if 4 + count * 8 > len(cmap):
        raise ValueError("truncated cmap records")
    selected: bytes | None = None
    for index in range(count):
        offset = 4 + index * 8
        platform = be16(cmap, offset)
        encoding = be16(cmap, offset + 2)
        subtable_offset = be32(cmap, offset + 4)
        if subtable_offset + 2 > len(cmap):
            continue
        if platform == 3 and encoding == 1 and be16(cmap, subtable_offset) == 4:
            selected = cmap[subtable_offset:]
            break
    if selected is None:
        raise ValueError("missing Windows Unicode cmap format 4")
    return {codepoint: cmap_format4_glyph(selected, codepoint)
            for codepoint in range(PIXEL_FIRST, PIXEL_LAST + 1)}


def psf_ascii_glyphs(path: Path) -> dict[int, bytes]:
    data = path.read_bytes()
    if len(data) < 4 or data[0:2] != b"\x36\x04" or data[3] != 16:
        raise ValueError(f"unsupported pixel source: {path}")
    glyph_data = data[4:4 + 256 * 16]
    if len(glyph_data) != 256 * 16:
        raise ValueError(f"truncated pixel source: {path}")
    return {codepoint: glyph_data[codepoint * 16:(codepoint + 1) * 16]
            for codepoint in range(PIXEL_FIRST, PIXEL_LAST + 1)}


def pixel_glyph(bitmap: bytes, ascender: int, descender: int, advance: int) -> bytes:
    contours: list[list[tuple[int, int]]] = []
    line_height = ascender - descender
    bold_bitmap = bytearray(16)
    for row, bits in enumerate(bitmap):
        expanded = bits | (bits >> 1)
        bold_bitmap[row] |= expanded
        if row + 1 < len(bold_bitmap):
            bold_bitmap[row + 1] |= expanded
    for row, bits in enumerate(bold_bitmap):
        for column in range(8):
            if not bits & (0x80 >> column):
                continue
            x0 = column * advance // 8
            x1 = (column + 1) * advance // 8
            y0 = descender + (15 - row) * line_height // 16
            y1 = descender + (16 - row) * line_height // 16
            contours.append([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    if not contours:
        return struct.pack(">hhhhh", 0, 0, descender, 0, ascender)
    points = [point for contour in contours for point in contour]
    output = bytearray(struct.pack(">hhhhh", len(contours), 0, descender, advance, ascender))
    for index in range(len(contours)):
        output.extend(struct.pack(">H", (index + 1) * 4 - 1))
    output.extend(b"\0\0")
    output.extend(b"\x01" * len(points))
    previous = 0
    for x, _ in points:
        output.extend(struct.pack(">h", x - previous))
        previous = x
    previous = 0
    for _, y in points:
        output.extend(struct.pack(">h", y - previous))
        previous = y
    align4(output)
    return bytes(output)


def ensure_runtime_font_size(path: Path) -> None:
    size = path.stat().st_size
    if size > MAX_RUNTIME_FONT_SIZE:
        raise ValueError(f"runtime font exceeds {MAX_RUNTIME_FONT_SIZE} bytes: {path} ({size} bytes)")


def rebuild_font(header: bytes, order: list[bytes], tables: dict[bytes, bytes]) -> bytes:
    head = bytearray(tables[b"head"])
    head[8:12] = b"\0\0\0\0"
    tables = dict(tables)
    tables[b"head"] = bytes(head)
    output = bytearray(header)
    output.extend(b"\0" * (16 * len(order)))
    offsets_out: dict[bytes, int] = {}
    checksums: dict[bytes, int] = {}
    for tag in order:
        align4(output)
        offsets_out[tag] = len(output)
        payload = tables[tag]
        checksums[tag] = checksum(payload)
        output.extend(payload)
    for index, tag in enumerate(order):
        struct.pack_into(">4sIII", output, 12 + index * 16, tag, checksums[tag], offsets_out[tag], len(tables[tag]))
    adjustment = (0xB1B0AFBA - checksum(bytes(output))) & 0xFFFFFFFF
    struct.pack_into(">I", output, offsets_out[b"head"] + 8, adjustment)
    return bytes(output)


def resolve_font(path: Path) -> Path:
    if path.is_file():
        return path
    raise FileNotFoundError(
        f"missing repository UI font: {path}; restore system/fonts/Deng.ttf or pass --font PATH"
    )


def loca_offsets(data: bytes, glyph_count: int, long_loca: bool) -> list[int]:
    entry_size = 4 if long_loca else 2
    if len(data) < (glyph_count + 1) * entry_size:
        raise ValueError("truncated loca table")
    if long_loca:
        return [be32(data, index * 4) for index in range(glyph_count + 1)]
    return [be16(data, index * 2) * 2 for index in range(glyph_count + 1)]


def build_win95_font(source: Path, pixel_source: Path, target: Path) -> None:
    header, order, tables = font_tables(source.read_bytes())
    required = (b"head", b"hhea", b"maxp", b"cmap", b"loca", b"glyf", b"hmtx")
    if any(tag not in tables for tag in required):
        raise ValueError("source font lacks required TrueType outline tables")
    head = bytearray(tables[b"head"])
    hhea = tables[b"hhea"]
    if len(head) < 54 or len(hhea) < 36 or len(tables[b"maxp"]) < 6:
        raise ValueError("truncated TrueType metrics table")
    if be16(head, 50) != 1:
        raise ValueError("Win95 pixel preparation requires long loca offsets")
    glyph_count = be16(tables[b"maxp"], 4)
    hmetrics_count = be16(hhea, 34)
    ascender = be16s(hhea, 4)
    descender = be16s(hhea, 6)
    if not glyph_count or not hmetrics_count or ascender <= descender:
        raise ValueError("invalid TrueType metrics")
    glyph_ids = ascii_glyph_map(tables[b"cmap"])
    bitmap_glyphs = psf_ascii_glyphs(pixel_source)
    offsets = loca_offsets(tables[b"loca"], glyph_count, True)
    glyf = tables[b"glyf"]
    if offsets[-1] > len(glyf) or any(offsets[index] > offsets[index + 1] for index in range(glyph_count)):
        raise ValueError("invalid loca offsets")
    glyphs = [glyf[offsets[index]:offsets[index + 1]] for index in range(glyph_count)]
    hmtx = bytearray(tables[b"hmtx"])
    pixel_advance = (ascender - descender) // 2
    if pixel_advance <= 0:
        raise ValueError("invalid TrueType line height")
    for codepoint, glyph_id in glyph_ids.items():
        if not glyph_id or glyph_id >= glyph_count or glyph_id >= hmetrics_count:
            raise ValueError(f"pixel glyph U+{codepoint:04X} lacks individual horizontal metrics")
        glyphs[glyph_id] = pixel_glyph(bitmap_glyphs[codepoint], ascender, descender, pixel_advance)
        struct.pack_into(">Hh", hmtx, glyph_id * 4, pixel_advance, 0)
    rebuilt_glyf = bytearray()
    rebuilt_loca = bytearray()
    for glyph in glyphs:
        rebuilt_loca.extend(struct.pack(">I", len(rebuilt_glyf)))
        rebuilt_glyf.extend(glyph)
        align4(rebuilt_glyf)
    rebuilt_loca.extend(struct.pack(">I", len(rebuilt_glyf)))
    tables[b"glyf"] = bytes(rebuilt_glyf)
    tables[b"loca"] = bytes(rebuilt_loca)
    tables[b"hmtx"] = bytes(hmtx)
    head[8:12] = b"\0\0\0\0"
    tables[b"head"] = bytes(head)

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(rebuild_font(header, order, tables))


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare LeonOS Metro and Win95 TrueType UI fonts")
    parser.add_argument("--metro-out", required=True)
    parser.add_argument("--win95-out", required=True)
    parser.add_argument("--font", default=str(DEFAULT_FONT))
    parser.add_argument("--pixel-source", default=str(DEFAULT_PIXEL_SOURCE))
    parser.add_argument("--win95-font", default=str(DEFAULT_WIN95_FONT))
    args = parser.parse_args()

    source = resolve_font(Path(args.font))
    win95_source = Path(args.win95_font)
    if not win95_source.is_file():
        win95_source = source
    pixel_source = Path(args.pixel_source)
    if not pixel_source.is_file():
        raise FileNotFoundError(f"missing Win95 pixel source: {pixel_source}")
    metro_out = Path(args.metro_out)
    metro_out.parent.mkdir(parents=True, exist_ok=True)
    source_data = source.read_bytes()
    metro_header, metro_order, metro_tables = font_tables(source_data)
    if source_data[:4] == b"ttcf":
        metro_out.write_bytes(rebuild_font(metro_header, metro_order, metro_tables))
    else:
        shutil.copyfile(source, metro_out)
    build_win95_font(win95_source, pixel_source, Path(args.win95_out))
    ensure_runtime_font_size(metro_out)
    ensure_runtime_font_size(Path(args.win95_out))
    print(f"prepared {metro_out} from {source} and {args.win95_out} from {win95_source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
