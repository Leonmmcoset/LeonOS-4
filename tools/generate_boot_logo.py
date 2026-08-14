#!/usr/bin/env python3
"""Convert the project logo into a freestanding boot-splash bitmap header."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Source PNG logo")
    parser.add_argument("--out", type=Path, required=True, help="Generated C header")
    parser.add_argument("--size", type=int, default=192, help="Square output size in pixels")
    return parser.parse_args()


def paeth(left: int, above: int, upper_left: int) -> int:
    predictor = left + above - upper_left
    left_distance = abs(predictor - left)
    above_distance = abs(predictor - above)
    upper_left_distance = abs(predictor - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_rgba_png(path: Path) -> tuple[int, int, bytearray]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG file")

    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    cursor = 8
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        chunk_type = data[cursor + 4:cursor + 8]
        chunk_data_end = cursor + 8 + length
        if chunk_data_end + 4 > len(data):
            raise SystemExit(f"{path} contains a truncated PNG chunk")
        chunk = data[cursor + 8:chunk_data_end]
        if chunk_type == b"IHDR":
            if length != 13:
                raise SystemExit(f"{path} has an invalid PNG header")
            width, height, bit_depth, color_type, compression, filtering, interlace = \
                struct.unpack(">IIBBBBB", chunk)
            if (not width or not height or bit_depth != 8 or color_type != 6 or
                    compression != 0 or filtering != 0 or interlace != 0):
                raise SystemExit(
                    "boot logo must be a non-interlaced, 8-bit RGBA PNG")
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            break
        cursor = chunk_data_end + 4

    if width is None or height is None:
        raise SystemExit(f"{path} has no PNG header")
    try:
        filtered = zlib.decompress(compressed)
    except zlib.error as exc:
        raise SystemExit(f"{path} has invalid PNG image data: {exc}") from exc

    stride = width * 4
    expected = height * (stride + 1)
    if len(filtered) != expected:
        raise SystemExit(f"{path} has an unsupported PNG data size")

    pixels = bytearray(width * height * 4)
    previous = bytearray(stride)
    source_offset = 0
    target_offset = 0
    for _ in range(height):
        filter_type = filtered[source_offset]
        source_offset += 1
        row = bytearray(filtered[source_offset:source_offset + stride])
        source_offset += stride
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                row[index] = (row[index] + left) & 0xff
            elif filter_type == 2:
                row[index] = (row[index] + above) & 0xff
            elif filter_type == 3:
                row[index] = (row[index] + ((left + above) // 2)) & 0xff
            elif filter_type == 4:
                row[index] = (row[index] + paeth(left, above, upper_left)) & 0xff
            elif filter_type != 0:
                raise SystemExit(f"{path} uses an unsupported PNG filter")
        pixels[target_offset:target_offset + stride] = row
        previous = row
        target_offset += stride
    return width, height, pixels


def visible_bounds(width: int, height: int, pixels: bytearray) -> tuple[int, int, int, int]:
    left, top, right, bottom = width, height, 0, 0
    found = False
    for y in range(height):
        for x in range(width):
            if pixels[(y * width + x) * 4 + 3] == 0:
                continue
            left = min(left, x)
            top = min(top, y)
            right = max(right, x + 1)
            bottom = max(bottom, y + 1)
            found = True
    if not found:
        raise SystemExit("boot logo contains no visible pixels")
    return left, top, right, bottom


def main() -> None:
    args = parse_args()
    if args.size < 32 or args.size > 512:
        raise SystemExit("boot logo size must be between 32 and 512 pixels")

    source_width, source_height, source_pixels = decode_rgba_png(args.input)
    left, top, right, bottom = visible_bounds(source_width, source_height, source_pixels)
    cropped_width = right - left
    cropped_height = bottom - top
    scale_base = max(cropped_width, cropped_height)
    output_width = max(1, (cropped_width * args.size + scale_base // 2) // scale_base)
    output_height = max(1, (cropped_height * args.size + scale_base // 2) // scale_base)
    offset_x = (args.size - output_width) // 2
    offset_y = (args.size - output_height) // 2
    pixels: list[tuple[int, int, int]] = [(255, 255, 255)] * (args.size * args.size)
    for y in range(output_height):
        source_y = top + min(cropped_height - 1, (y * cropped_height) // output_height)
        for x in range(output_width):
            source_x = left + min(cropped_width - 1, (x * cropped_width) // output_width)
            source_index = (source_y * source_width + source_x) * 4
            red, green, blue, alpha = source_pixels[source_index:source_index + 4]
            inverse_alpha = 255 - alpha
            pixels[(offset_y + y) * args.size + offset_x + x] = (
                (red * alpha + 255 * inverse_alpha + 127) // 255,
                (green * alpha + 255 * inverse_alpha + 127) // 255,
                (blue * alpha + 255 * inverse_alpha + 127) // 255,
            )

    lines = [
        "/* Generated from logo.png by tools/generate_boot_logo.py. */\n",
        "#ifndef LEONOS_GENERATED_BOOT_LOGO_H\n",
        "#define LEONOS_GENERATED_BOOT_LOGO_H\n\n",
        "#include <stdint.h>\n\n",
        f"#define LEONOS_BOOT_LOGO_WIDTH {args.size}u\n",
        f"#define LEONOS_BOOT_LOGO_HEIGHT {args.size}u\n\n",
        "static const uint32_t leonos_boot_logo_pixels[LEONOS_BOOT_LOGO_WIDTH * "
        "LEONOS_BOOT_LOGO_HEIGHT] = {\n",
    ]
    for index in range(0, len(pixels), 8):
        row = pixels[index:index + 8]
        values = ", ".join(f"0x00{red:02x}{green:02x}{blue:02x}u" for red, green, blue in row)
        lines.append(f"    {values},\n")
    lines.extend(["};\n\n", "#endif\n"])

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("".join(lines), encoding="ascii")


if __name__ == "__main__":
    main()
