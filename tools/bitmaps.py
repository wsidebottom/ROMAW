# +===================================================================
#   File:      bitmaps.py
#
#   Summary:   Read indexed PNG/BMP artwork and generate packed native bitmap initializers.
#
#   Classes:   None.
#
#   Functions: read_png, read_bitmap, initializer, main.
#
#   Origin:    Executor shared bitmap asset build support.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Compile editable indexed PNG/BMP files without changing their palette indices."""

import argparse
import io
from pathlib import Path
import struct


def read_png(data):
    """Pack indexed PNG samples directly, preserving palette indices and native bit depth."""
    if not 33 <= len(data) <= 32 * 1024 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid indexed PNG extent or signature")
    depth, color_type = data[24:26]
    if depth not in (1, 4, 8) or color_type != 3:
        raise ValueError("Native bitmap PNGs must retain an indexed palette at 1, 4 or 8 bits per pixel")
    from PIL import Image

    try:
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.size
            if (
                image.format != "PNG"
                or not 1 <= width <= 4096
                or not 1 <= height <= 4096
                or image.mode != "P"
                or image.n_frames != 1
                or "transparency" in image.info
            ):
                raise ValueError("Expected a single opaque indexed PNG with valid bitmap dimensions")
            colors = len(image.getpalette() or []) // 3
            if not 1 <= colors <= 1 << depth:
                raise ValueError("Invalid indexed PNG palette extent")
            indices = image.tobytes()
    except (OSError, SyntaxError, Image.DecompressionBombError) as error:
        raise ValueError("Invalid indexed PNG bitmap") from error
    stride = (width * depth + 7) // 8
    pixels = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            value = indices[y * width + x]
            if value >= colors:
                raise ValueError("PNG pixel exceeds palette")
            bit = x * depth
            pixels[y * stride + bit // 8] |= value << (8 - depth - bit % 8)
    return width, height, depth, bytes(pixels)


def read_bitmap(data):
    """Return width, height, depth and tightly packed top-down indexed rows."""
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return read_png(data)
    if not 54 <= len(data) <= 32 * 1024 * 1024 or data[:2] != b"BM":
        raise ValueError("Invalid BMP extent or signature")
    length, reserved, offset = struct.unpack_from("<III", data, 2)
    header, width, height, planes, depth, compression = struct.unpack_from("<IiiHHI", data, 14)
    if (
        length != len(data)
        or reserved
        or header != 40
        or not 1 <= width <= 4096
        or not 1 <= abs(height) <= 4096
        or planes != 1
        or depth not in (1, 4, 8)
        or compression
    ):
        raise ValueError("Expected an uncompressed indexed Windows BMP")
    colors = struct.unpack_from("<I", data, 46)[0] or (1 << depth)
    if not 1 <= colors <= 1 << depth or not 54 + colors * 4 <= offset <= len(data):
        raise ValueError("Invalid BMP palette or pixel offset")
    stride = ((width * depth + 31) // 32) * 4
    packed = (width * depth + 7) // 8
    if stride * abs(height) > len(data) - offset:
        raise ValueError("Truncated BMP pixel rows")
    rows = []
    for y in range(abs(height)):
        row = y if height < 0 else height - y - 1
        start = offset + row * stride
        pixels = data[start : start + packed]
        for x in range(width):
            value = pixels[x * depth // 8] >> (8 - depth - x * depth % 8) & ((1 << depth) - 1)
            if value >= colors:
                raise ValueError("BMP pixel exceeds palette")
        rows.append(pixels)
    return width, abs(height), depth, b"".join(rows)


def initializer(data, expected=None, crop=None, rotate=None):
    """Emit native byte initializers from indexed artwork without file-row padding."""
    if data[:4] == b"icns":
        import mac_icons

        width, height, depth = 16, 16, 1
        family = mac_icons.resource(data, b"ics#")
        # CopyBits uses white at index zero; transparent ICNS pixels must not
        # expose the arbitrary image bits stored outside the shared mask.
        pixels = bytes(image & mask for image, mask in zip(family[:32], family[32:]))
    else:
        width, height, depth, pixels = read_bitmap(data)
    if crop is not None:
        left, top, right, bottom = crop
        if not 0 <= left < right <= width or not 0 <= top < bottom <= height:
            raise ValueError("Bitmap crop exceeds the source bounds")
        source_stride = (width * depth + 7) // 8
        target_stride = ((right - left) * depth + 7) // 8
        cropped = bytearray(target_stride * (bottom - top))
        for y in range(bottom - top):
            for x in range(right - left):
                bit = (left + x) * depth
                value = pixels[(top + y) * source_stride + bit // 8] >> (8 - depth - bit % 8) & ((1 << depth) - 1)
                bit = x * depth
                cropped[y * target_stride + bit // 8] |= value << (8 - depth - bit % 8)
        width, height, pixels = right - left, bottom - top, bytes(cropped)
    if rotate is not None:
        if rotate not in ("clockwise", "counterclockwise"):
            raise ValueError("Expected a clockwise or counterclockwise quarter turn")
        source_stride = (width * depth + 7) // 8
        target_stride = (height * depth + 7) // 8
        rotated = bytearray(target_stride * width)
        for y in range(width):
            for x in range(height):
                source_x, source_y = (y, height - 1 - x) if rotate == "clockwise" else (width - 1 - y, x)
                bit = source_x * depth
                value = pixels[source_y * source_stride + bit // 8] >> (8 - depth - bit % 8) & ((1 << depth) - 1)
                bit = x * depth
                rotated[y * target_stride + bit // 8] |= value << (8 - depth - bit % 8)
        width, height, pixels = height, width, bytes(rotated)
    if expected is not None and (width, height, depth) != tuple(expected):
        raise ValueError("Bitmap dimensions/depth do not match the native drawing layout")
    stride = (width * depth + 7) // 8
    lines = ["/* Generated from shared bitmaps/; do not edit. */"]
    lines += [
        "    " + ", ".join(str(value) for value in pixels[y * stride : (y + 1) * stride]) + "," for y in range(height)
    ]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--expect", nargs=3, type=int, metavar=("WIDTH", "HEIGHT", "DEPTH"))
    parser.add_argument("--crop", nargs=4, type=int, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    parser.add_argument("--rotate", choices=("clockwise", "counterclockwise"), help="Rotate after cropping")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(initializer(args.source.read_bytes(), args.expect, args.crop, args.rotate), encoding="utf-8")


if __name__ == "__main__":
    main()
