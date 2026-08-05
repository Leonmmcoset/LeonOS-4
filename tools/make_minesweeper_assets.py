#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path


SIZE = 20
TRANSPARENT = (0, 0, 0, 0)
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
RED = (192, 0, 0, 255)
DARK_RED = (128, 0, 0, 255)


def canvas() -> list[list[tuple[int, int, int, int]]]:
    return [[TRANSPARENT for _ in range(SIZE)] for _ in range(SIZE)]


def put(pixels: list[list[tuple[int, int, int, int]]], x: int, y: int,
        color: tuple[int, int, int, int]) -> None:
    if 0 <= x < SIZE and 0 <= y < SIZE:
        pixels[y][x] = color


def rect(pixels: list[list[tuple[int, int, int, int]]], x: int, y: int,
         width: int, height: int, color: tuple[int, int, int, int]) -> None:
    for yy in range(y, y + height):
        for xx in range(x, x + width):
            put(pixels, xx, yy, color)


def mine() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    rect(pixels, 9, 1, 2, 18, BLACK)
    rect(pixels, 1, 9, 18, 2, BLACK)
    rect(pixels, 4, 4, 12, 12, BLACK)
    rect(pixels, 7, 6, 8, 8, BLACK)
    rect(pixels, 8, 6, 4, 3, WHITE)
    rect(pixels, 7, 8, 3, 3, WHITE)
    return pixels


def flag() -> list[list[tuple[int, int, int, int]]]:
    pixels = canvas()
    rect(pixels, 5, 3, 2, 14, BLACK)
    for y in range(4, 11):
        for x in range(7, 17 - (y - 4)):
            put(pixels, x, y, RED if y < 9 else DARK_RED)
    rect(pixels, 2, 17, 10, 2, BLACK)
    rect(pixels, 4, 16, 6, 1, BLACK)
    return pixels


def write_bmp(path: Path, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    image_size = SIZE * SIZE * 4
    header = struct.pack("<2sIHHI", b"BM", 54 + image_size, 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, SIZE, -SIZE, 1, 32, 0,
                      image_size, 2835, 2835, 0, 0)
    data = bytearray()
    for row in pixels:
        for red, green, blue, alpha in row:
            data.extend((blue, green, red, alpha))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + dib + data)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Minesweeper BMP sprites")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()
    out_dir = Path(args.out_dir)
    write_bmp(out_dir / "minesweeper-mine.bmp", mine())
    write_bmp(out_dir / "minesweeper-flag.bmp", flag())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
