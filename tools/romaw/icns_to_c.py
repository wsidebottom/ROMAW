#!/usr/bin/env python3
# +===================================================================
#   File:      icns_to_c.py
#
#   Summary:   Compile indexed ICNS UI assets to native QuickDraw descriptors.
#
#   Classes:   None declared.
#
#   Functions: decode, representations, ui_image, generate, main
#
#   Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Compile indexed ICNS UI assets to native QuickDraw descriptors.

Assets contain indexed source frames at 1, 4 and/or 8 bpp,
with identical dimensions. Missing color depths use the nearest lower image.
Missing monochrome is derived from the lowest color depth by luminance.
Palette order is part of the UI recoloring ABI.
Multi-size and RGB/PNG ICNS files use their smallest frame at classic display depths.
"""

import argparse
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import mac_icons


def decode(data):
    frames = mac_icons.decode(data)
    dimensions = frames[0]["size"], frames[0]["height"]
    images = {}
    for frame in frames:
        w, h, depth = frame["size"], frame["height"], frame["depth"]
        if (w, h) != dimensions or depth not in (1, 4, 8) or depth in images:
            raise ValueError("expected unique indexed images of matching dimensions")
        stride = (w * depth + 7) // 8
        mask_stride = (w + 7) // 8
        rows = []
        opacity = []
        for y in range(h):
            row = []
            mask = []
            for x in range(w):
                bit = x * depth
                value = (frame["image"][y * stride + bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
                if value >= len(frame["palette"]):
                    raise ValueError("pixel outside palette")
                row.append(value)
                mask.append(int(bool(frame["opacity"][y * mask_stride + x // 8] & (128 >> (x % 8)))))
            rows.append(row)
            opacity.append(mask)
        images[depth] = (
            rows,
            [tuple(channel * 257 for channel in color) for color in frame["palette"]],
            opacity if any(0 in row for row in opacity) else None,
        )
    return dimensions, images


def representations(images):
    """Return available images and source depths for the three UI visuals."""
    images = dict(images)
    if 1 not in images:
        rows, palette, opacity = images[min(images)]
        # QuickDraw monochrome convention: zero is white, one is black.
        mono = [int(299 * r + 587 * g + 114 * b < 32768 * 1000) for r, g, b in palette]
        images[1] = (
            [[mono[pixel] for pixel in row] for row in rows],
            [(65535, 65535, 65535), (0, 0, 0)],
            opacity,
        )
    sources = [max(depth for depth in images if depth <= requested) for requested in (1, 4, 8)]
    return images, sources


def ui_image(data):
    try:
        return decode(data)
    except ValueError:
        frames = mac_icons.decode(data)
        size = min(frame["size"] for frame in frames)
        selected = []
        for depth in (1, 4, 8):
            frame = mac_icons.best_frame(frames, size, depth)
            if (frame["size"], frame["height"], frame["depth"]) != (size, size, depth):
                frame = mac_icons.classic_frame(frame, size, depth)
            selected.append(frame)
        indexed = mac_icons.encode(selected)
        return decode(indexed)


def generate(data, name, vertical_thumb=False, arrow_direction=None):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
        raise ValueError("invalid C image name")
    (w, h), images = ui_image(data)
    if vertical_thumb:
        images = {
            depth: (
                mac_icons.vertical_scroll_thumb(rows),
                palette,
                mac_icons.vertical_scroll_thumb(mask) if mask is not None else None,
            )
            for depth, (rows, palette, mask) in images.items()
        }
        w, h = h, w
    if arrow_direction is not None:
        images = {
            depth: (
                mac_icons.scroll_arrow(rows, arrow_direction),
                palette,
                (mac_icons.scroll_arrow(mask, arrow_direction) if mask is not None else None),
            )
            for depth, (rows, palette, mask) in images.items()
        }
        if arrow_direction in ("up", "down"):
            w, h = h, w
    images, sources = representations(images)
    output = ["/* Generated from indexed ICNS; do not edit. */"]
    strides = {}
    for depth in sorted(images):
        rows, palette, opacity = images[depth]
        stride = ((w * depth + 15) // 16) * 2
        strides[depth] = stride
        output.append(f"static char {name}_{depth}_bits[] = {{")
        for row in rows:
            packed = bytearray(stride)
            for x, value in enumerate(row):
                packed[x * depth // 8] |= value << (8 - depth - x * depth % 8)
            output.append("  " + ", ".join(str(v) for v in packed) + ",")
        output.append("};")
        if opacity is not None:
            output.append(f"static char {name}_{depth}_mask[] = {{")
            for row in opacity:
                packed = bytearray(((w + 15) // 16) * 2)
                for x, value in enumerate(row):
                    packed[x // 8] |= value << (7 - x % 8)
                output.append("  " + ", ".join(str(v) for v in packed) + ",")
            output.append("};")
    output.extend(
        [
            f"static pixel_image_desc_t {name}_desc = {{",
            f"  {{ 0, 0, {h}, {w} }},",
            "  {",
        ]
    )
    for depth in sources:
        mask = f"{name}_{depth}_mask" if images[depth][2] is not None else "NULL"
        mask_stride = ((w + 15) // 16) * 2 if images[depth][2] is not None else 0
        output.append(f"    {{ {name}_{depth}_bits, {strides[depth]}, {depth}, {mask}, {mask_stride} }},")
    output.extend(
        [
            "  }",
            "};",
            f"pixel_image_t *{name};",
            "void",
            f"image_{name}_init (void)",
            "{",
            f"  {name} = image_init (&{name}_desc);",
        ]
    )
    for visual, depth in enumerate(sources):
        output.append("  {")
        output.append("    static const RGBColor colors[] = {")
        for rgb in images[depth][1]:
            output.append("      { " + ", ".join(f"CWC (0x{v:04X})" for v in rgb) + " },")
        output.extend(
            [
                "    };",
                f"    image_update_palette ({name}, {visual}, colors, {len(images[depth][1]) - 1});",
                "  }",
            ]
        )
    output.append("}")
    return "\n".join(output) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--scroll-thumb", action="store_true")
    parser.add_argument("--scroll-arrow", action="store_true")
    args = parser.parse_args()
    try:
        data = args.input.read_bytes()
        output = generate(data, args.input.stem)
        if args.scroll_thumb:
            output += generate(data, args.input.stem + "_vertical", vertical_thumb=True)
        if args.scroll_arrow:
            for direction in ("left", "up", "down"):
                output += generate(data, args.input.stem + "_" + direction, arrow_direction=direction)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{args.input}: {error}\n")
    sys.stdout.write(output)


if __name__ == "__main__":
    main()
