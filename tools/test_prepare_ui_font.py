#!/usr/bin/env python3
"""Regression tests for the generated LeonOS UI fonts."""

from __future__ import annotations

import struct
import unittest
from pathlib import Path

from prepare_ui_font import pixel_glyph, psf_ascii_glyphs


ROOT = Path(__file__).resolve().parents[1]
PIXEL_FONT = ROOT / "system" / "fonts" / "system.psf"


class Win95PixelFontTests(unittest.TestCase):
    def test_ascii_outlines_keep_the_source_pixel_weight(self) -> None:
        """Each source PSF pixel must become exactly one outline contour."""
        glyphs = psf_ascii_glyphs(PIXEL_FONT)

        for codepoint, bitmap in glyphs.items():
            with self.subTest(codepoint=f"U+{codepoint:04X}"):
                outline = pixel_glyph(bitmap, 1500, -500, 1000)
                contour_count = struct.unpack_from(">h", outline, 0)[0]
                source_pixels = sum(row.bit_count() for row in bitmap)
                self.assertEqual(contour_count, source_pixels)


if __name__ == "__main__":
    unittest.main()
