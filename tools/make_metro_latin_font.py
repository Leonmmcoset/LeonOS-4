#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


WIDTH = 8
HEIGHT = 16
SOURCE_HEIGHT = HEIGHT + 2
DESCENDER_BOTTOM = HEIGHT - 2
FIRST_CODEPOINT = 32
GLYPH_COUNT = 95
TALL_CODEPOINTS = frozenset(ord(character) for character in "ABCDEFGHIJKLMNOPQRSTUVWXYZbdfhiklt")


def glyph_top(image: Image.Image) -> int:
    for row in range(image.height):
        if any(image.getpixel((column, row)) >= 96 for column in range(WIDTH)):
            return row
    return image.height


def glyph_bounds(image: Image.Image) -> tuple[int, int]:
    top = glyph_top(image)
    for row in range(image.height - 1, -1, -1):
        if any(image.getpixel((column, row)) >= 96 for column in range(WIDTH)):
            return top, row
    return image.height, -1


def render_glyph(font: ImageFont.FreeTypeFont, codepoint: int) -> Image.Image:
    glyph = Image.new("L", (WIDTH, SOURCE_HEIGHT))
    ImageDraw.Draw(glyph).text((0, 0), chr(codepoint), font=font, fill=255)
    return glyph


def normalize_glyph(glyph: Image.Image, target_top: int, target_bottom: int) -> Image.Image:
    source_top, source_bottom = glyph_bounds(glyph)
    output = Image.new("L", (WIDTH, HEIGHT))
    if source_bottom < source_top:
        return output
    target_height = target_bottom - target_top + 1
    source = glyph.crop((0, source_top, WIDTH, source_bottom + 1))
    normalized = source.resize((WIDTH, target_height), Image.Resampling.LANCZOS)
    output.paste(normalized, (0, target_top))
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the LeonOS Metro Latin grayscale font")
    parser.add_argument("--font", required=True, help="TrueType font used for rasterization")
    parser.add_argument("--out", required=True, help="Output MSF1 font path")
    parser.add_argument("--size", type=int, default=13, help="TrueType point size")
    args = parser.parse_args()

    font = ImageFont.truetype(args.font, args.size)
    cap_bounds = glyph_bounds(render_glyph(font, ord("H")))
    cap_bottom = cap_bounds[1]
    tall_top = min(glyph_top(render_glyph(font, codepoint)) for codepoint in TALL_CODEPOINTS)
    output = bytearray(struct.pack("<4sBBBB", b"MSF1", WIDTH, HEIGHT,
                                   FIRST_CODEPOINT, GLYPH_COUNT))
    for codepoint in range(FIRST_CODEPOINT, FIRST_CODEPOINT + GLYPH_COUNT):
        glyph = render_glyph(font, codepoint)
        source_top, source_bottom = glyph_bounds(glyph)
        if source_bottom >= HEIGHT:
            glyph = normalize_glyph(glyph, source_top, DESCENDER_BOTTOM)
        elif codepoint in TALL_CODEPOINTS:
            glyph = normalize_glyph(glyph, tall_top, cap_bottom)
        else:
            glyph = glyph.crop((0, 0, WIDTH, HEIGHT))
        output.extend(glyph.tobytes())

    target = Path(args.out)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
