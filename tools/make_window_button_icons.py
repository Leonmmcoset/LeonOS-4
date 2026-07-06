#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path


BMP_SIZE = 16
BLACK = (0, 0, 0, 255)
TRANSPARENT = (0, 0, 0, 0)


def canvas() -> list[list[tuple[int, int, int, int]]]:
    return [[TRANSPARENT for _ in range(BMP_SIZE)] for _ in range(BMP_SIZE)]


def put(pixels: list[list[tuple[int, int, int, int]]], x: int, y: int,
        color: tuple[int, int, int, int] = BLACK) -> None:
    if 0 <= x < BMP_SIZE and 0 <= y < BMP_SIZE:
        pixels[y][x] = color


def rect_outline(pixels: list[list[tuple[int, int, int, int]]],
                 x: int, y: int, w: int, h: int) -> None:
    for xx in range(x, x + w):
        put(pixels, xx, y)
        put(pixels, xx, y + h - 1)
    for yy in range(y, y + h):
        put(pixels, x, yy)
        put(pixels, x + w - 1, yy)


def line_h(pixels: list[list[tuple[int, int, int, int]]],
           x: int, y: int, w: int, h: int = 1) -> None:
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            put(pixels, xx, yy)


def line_diag_down(pixels: list[list[tuple[int, int, int, int]]],
                   x: int, y: int, length: int) -> None:
    for i in range(length):
        put(pixels, x + i, y + i)
        put(pixels, x + i + 1, y + i)
        put(pixels, x + i, y + i + 1)
        put(pixels, x + i + 1, y + i + 1)


def line_diag_up(pixels: list[list[tuple[int, int, int, int]]],
                 x: int, y: int, length: int) -> None:
    for i in range(length):
        put(pixels, x + i, y - i)
        put(pixels, x + i + 1, y - i)
        put(pixels, x + i, y - i + 1)
        put(pixels, x + i + 1, y - i + 1)


def icon_minimize() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    line_h(pixels, 3, 12, 10, 2)
    return pixels


def icon_maximize() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    rect_outline(pixels, 3, 3, 10, 9)
    line_h(pixels, 4, 4, 8, 1)
    return pixels


def icon_restore() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    rect_outline(pixels, 5, 3, 8, 7)
    line_h(pixels, 6, 4, 6, 1)
    rect_outline(pixels, 3, 6, 8, 7)
    line_h(pixels, 4, 7, 6, 1)
    return pixels


def icon_close() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    line_diag_down(pixels, 4, 4, 8)
    line_diag_up(pixels, 4, 11, 8)
    return pixels


ICONS = {
    "window-button-minimize.bmp": icon_minimize,
    "window-button-maximize.bmp": icon_maximize,
    "window-button-restore.bmp": icon_restore,
    "window-button-close.bmp": icon_close,
}


def write_bmp(path: Path, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    image_size = BMP_SIZE * BMP_SIZE * 4
    pixel_offset = 14 + 40
    file_size = pixel_offset + image_size
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    dib = struct.pack("<IiiHHIIiiII", 40, BMP_SIZE, -BMP_SIZE, 1, 32, 0,
                      image_size, 2835, 2835, 0, 0)
    data = bytearray()
    for row in pixels:
        for r, g, b, a in row:
            data.extend((b, g, r, a))
    path.write_bytes(header + dib + data)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LeonOS window button BMP icons")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, draw in ICONS.items():
        write_bmp(out_dir / name, draw())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
