#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "boot" / "grub" / "themes" / "leonos98" / "background.tga"

Color = tuple[int, int, int]

BLUE = (0, 0, 170)
BRIGHT_BLUE = (0, 0, 255)
CYAN = (85, 255, 255)
WHITE = (255, 255, 255)
GRAY = (170, 170, 170)
BLACK = (0, 0, 0)


FONT: dict[str, list[int]] = {
    " ": [0, 0, 0, 0, 0, 0, 0, 0],
    "!": [0x18, 0x18, 0x18, 0x18, 0x18, 0, 0x18, 0],
    ".": [0, 0, 0, 0, 0, 0x18, 0x18, 0],
    ":": [0, 0x18, 0x18, 0, 0, 0x18, 0x18, 0],
    "/": [0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0],
    "-": [0, 0, 0, 0x7e, 0, 0, 0, 0],
    "_": [0, 0, 0, 0, 0, 0, 0xff, 0],
    "0": [0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0],
    "1": [0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0],
    "2": [0x3c, 0x66, 0x06, 0x1c, 0x30, 0x66, 0x7e, 0],
    "3": [0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0],
    "4": [0x0c, 0x1c, 0x3c, 0x6c, 0x7e, 0x0c, 0x0c, 0],
    "5": [0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0],
    "6": [0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0],
    "7": [0x7e, 0x66, 0x0c, 0x18, 0x18, 0x18, 0x18, 0],
    "8": [0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0],
    "9": [0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0],
}

LETTERS = {
    "A": [0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0],
    "B": [0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0],
    "C": [0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0],
    "D": [0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0],
    "E": [0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x7e, 0],
    "F": [0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0],
    "G": [0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3e, 0],
    "H": [0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0],
    "I": [0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0],
    "J": [0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x6c, 0x38, 0],
    "K": [0x66, 0x6c, 0x78, 0x70, 0x78, 0x6c, 0x66, 0],
    "L": [0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0],
    "M": [0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0],
    "N": [0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0],
    "O": [0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0],
    "P": [0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0],
    "Q": [0x3c, 0x66, 0x66, 0x66, 0x6a, 0x6c, 0x36, 0],
    "R": [0x7c, 0x66, 0x66, 0x7c, 0x78, 0x6c, 0x66, 0],
    "S": [0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0],
    "T": [0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0],
    "U": [0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0],
    "V": [0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0],
    "W": [0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0],
    "X": [0x66, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x66, 0],
    "Y": [0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x18, 0],
    "Z": [0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0],
}
FONT.update(LETTERS)


class Canvas:
    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height * 3)

    def set(self, x: int, y: int, color: Color) -> None:
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return
        r, g, b = color
        i = (y * self.width + x) * 3
        self.pixels[i] = b
        self.pixels[i + 1] = g
        self.pixels[i + 2] = r

    def rect(self, x: int, y: int, w: int, h: int, color: Color) -> None:
        x0 = max(0, x)
        y0 = max(0, y)
        x1 = min(self.width, x + w)
        y1 = min(self.height, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        bgr = bytes((color[2], color[1], color[0]))
        row = bgr * (x1 - x0)
        for yy in range(y0, y1):
            start = (yy * self.width + x0) * 3
            self.pixels[start : start + len(row)] = row

    def text(self, x: int, y: int, text: str, fg: Color = WHITE, bg: Color | None = None, scale: int = 2) -> None:
        cx = x
        for ch in text.upper():
            glyph = FONT.get(ch, FONT[" "])
            if bg is not None:
                self.rect(cx, y, 8 * scale, 8 * scale, bg)
            for gy, bits in enumerate(glyph):
                for gx in range(8):
                    if bits & (0x80 >> gx):
                        self.rect(cx + gx * scale, y + gy * scale, scale, scale, fg)
            cx += 8 * scale

    def text_center(self, y: int, text: str, fg: Color = WHITE, bg: Color | None = None, scale: int = 2) -> None:
        x = (self.width - len(text) * 8 * scale) // 2
        self.text(x, y, text, fg, bg, scale)


def draw_box(c: Canvas, x: int, y: int, cols: int, rows: int) -> None:
    cw = 16
    ch = 16
    w = cols * cw
    h = rows * ch
    c.rect(x, y, w, h, BLUE)
    c.text(x, y, "+" + "-" * (cols - 2) + "+", WHITE, BLUE)
    for row in range(1, rows - 1):
        c.text(x, y + row * ch, "|" + " " * (cols - 2) + "|", WHITE, BLUE)
    c.text(x, y + (rows - 1) * ch, "+" + "-" * (cols - 2) + "+", WHITE, BLUE)


def write_tga(path: Path, width: int = 1024, height: int = 768) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    c = Canvas(width, height)
    c.rect(0, 0, width, height, BLUE)
    c.rect(0, 0, width, 28, GRAY)
    c.text_center(6, "LeonOS 4 Boot Manager", BLACK, GRAY, 2)
    c.text_center(54, "ReactOS-style text setup screen", CYAN, BLUE, 2)

    box_x = 128
    box_y = 128
    draw_box(c, box_x, box_y, 48, 22)
    c.text(box_x + 48, box_y + 32, "Select an operating system to start:", WHITE, BLUE, 2)
    c.text(box_x + 48, box_y + 272, "ENTER=Boot   E=Edit   C=Command Line", GRAY, BLUE, 2)

    c.text(160, 600, "LeonOS 4 uses GRUB Multiboot2, UEFI, FAT32 ESP, SATA VMDK.", WHITE, BLUE, 2)
    c.text(160, 628, "Default entry starts ntclks kernel and desktop.elf window server.", WHITE, BLUE, 2)
    c.text(160, 684, "Copyright (c) LeonOS Project", GRAY, BLUE, 2)

    with path.open("wb") as f:
        f.write(bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0]))
        f.write(width.to_bytes(2, "little"))
        f.write(height.to_bytes(2, "little"))
        f.write(bytes([24, 0x20]))
        f.write(c.pixels)


def main() -> int:
    write_tga(OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
