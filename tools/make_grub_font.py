#!/usr/bin/env python3
"""Create GRUB's compact, fixed-width pixel-font source from system.psf."""

from __future__ import annotations

import argparse
from pathlib import Path


FIRST_CODEPOINT = 0x20
LAST_CODEPOINT = 0x7E
GLYPH_WIDTH = 8
GLYPH_HEIGHT = 16


def load_ascii_glyphs(path: Path) -> dict[int, bytes]:
    data = path.read_bytes()
    expected_size = 4 + 256 * GLYPH_HEIGHT
    if (len(data) < expected_size or data[:2] != b"\x36\x04" or
            data[3] != GLYPH_HEIGHT):
        raise ValueError(f"unsupported PSF font: {path}")
    glyph_data = data[4:expected_size]
    return {
        codepoint: glyph_data[codepoint * GLYPH_HEIGHT:(codepoint + 1) * GLYPH_HEIGHT]
        for codepoint in range(FIRST_CODEPOINT, LAST_CODEPOINT + 1)
    }


def bdf_name(codepoint: int) -> str:
    if codepoint == 0x20:
        return "space"
    if codepoint == 0x7E:
        return "asciitilde"
    return f"uni{codepoint:04X}"


def bdf_source(glyphs: dict[int, bytes]) -> str:
    lines = [
        "STARTFONT 2.1",
        "FONT -LeonOS-Pixel-Medium-R-Normal--16-160-75-75-C-80-ISO10646-1",
        "SIZE 16 75 75",
        f"FONTBOUNDINGBOX {GLYPH_WIDTH} {GLYPH_HEIGHT} 0 0",
        "STARTPROPERTIES 15",
        "FOUNDRY \"LeonOS\"",
        "FONT_ASCENT 16",
        "FONT_DESCENT 0",
        "FAMILY_NAME \"LeonOS Pixel\"",
        "WEIGHT_NAME \"Regular\"",
        "SLANT \"R\"",
        "SETWIDTH_NAME \"Normal\"",
        "PIXEL_SIZE 16",
        "POINT_SIZE 160",
        "RESOLUTION_X 75",
        "RESOLUTION_Y 75",
        "SPACING \"C\"",
        "AVERAGE_WIDTH 80",
        "CHARSET_REGISTRY \"ISO10646\"",
        "CHARSET_ENCODING \"1\"",
        "ENDPROPERTIES",
        f"CHARS {len(glyphs)}",
    ]
    for codepoint, bitmap in glyphs.items():
        lines.extend((
            f"STARTCHAR {bdf_name(codepoint)}",
            f"ENCODING {codepoint}",
            "SWIDTH 500 0",
            "DWIDTH 8 0",
            "BBX 8 16 0 0",
            "BITMAP",
            *(f"{row:02X}" for row in bitmap),
            "ENDCHAR",
        ))
    lines.append("ENDFONT")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert LeonOS's 8x16 PSF glyphs into a fixed-width BDF font for GRUB",
    )
    parser.add_argument("--source", default="system/fonts/system.psf")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    source = Path(args.source)
    output = Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(bdf_source(load_ascii_glyphs(source)), encoding="ascii", newline="\n")
    print(f"wrote fixed-width GRUB BDF: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
