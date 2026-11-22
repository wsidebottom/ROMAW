#!/usr/bin/env python3
# +===================================================================
#   File:      font_suitcase.py
#
#   Summary:   Build classic bitmap strikes and the default scalable system face.
#
#   Classes:   None declared.
#
#   Functions: strike, font_records
#
#   Origin:    Existing repository source in executor/src/tools; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Build classic Font Manager strikes from the supplied TrueType faces."""

import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from system_resources import build_fork, publish

FAMILIES = {
    0: ("Chicago", {0: "Chicago"}),
    3: ("Geneva", {0: "Geneva"}),
    20: (
        "Times",
        {0: "Times-Roman", 1: "Times-Bold", 2: "Times-Italic", 3: "Times-BoldItalic"},
    ),
    21: ("Helvetica", {0: "Helvetica", 1: "Helvetica-Bold"}),
    22: ("Courier", {0: "Courier", 1: "Courier-Bold"}),
    16384: ("Charcoal", {0: "Charcoal"}),
}
SIZES = (8, 9, 10, 12, 14, 18)
MENU_MARKS = dict(zip(range(0x11, 0x15), "\u2318\u2713\u25c6\uf8ff"))
FIRST, LAST = 0x11, 0xFF


def strike(path, size):
    font = ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.BASIC)
    glyphs = []
    for code in range(FIRST, LAST + 2):
        if code == LAST + 1:
            char = "\u25a1"
        elif code in MENU_MARKS:
            char = MENU_MARKS[code]
        elif code < 32 or code == 127:
            char = " "
        else:
            char = bytes([code]).decode("mac_roman")
        box = font.getbbox(char, mode="1", anchor="ls")
        glyphs.append((char, box, round(font.getlength(char, mode="1"))))

    ascent, descent = font.getmetrics()
    ascent = max(ascent, *(-box[1] for _, box, _ in glyphs))
    descent = max(descent, *(box[3] for _, box, _ in glyphs))
    kern = min(0, *(box[0] for _, box, _ in glyphs))
    locations = [0]
    widths = []
    for _, (left, _, right, _), advance in glyphs:
        locations.append(locations[-1] + right - left)
        widths.append((left - kern) * 256 + advance)
    widths.append(0xFFFF)

    row_words = (locations[-1] + 15) // 16
    height = ascent + descent
    image = Image.new("1", (row_words * 16, height))
    draw = ImageDraw.Draw(image)
    for x, (char, (left, _, _, _), _) in zip(locations, glyphs):
        draw.text((x - left, ascent), char, font=font, anchor="ls", fill=1)

    ow_offset = 5 + row_words * height + len(locations)
    header = struct.pack(
        ">H12h",
        0xB000 if path.stem.startswith("Courier") else 0x9000,
        FIRST,
        LAST,
        max(g[2] for g in glyphs),
        kern,
        -descent,
        max(b - a for a, b in zip(locations, locations[1:])),
        height,
        ow_offset,
        ascent,
        descent,
        0,
        row_words,
    )
    return (
        header
        + image.tobytes()
        + struct.pack(f">{len(locations)}H", *locations)
        + struct.pack(f">{len(widths)}H", *widths)
    )


def font_records(directory):
    yield b"sfnt", 0, 0, None, (directory / "Chicago.ttf").read_bytes()
    # Use negative NFNT IDs for styled strikes and families outside FONT's
    # seven-bit family field. Plain classic IDs remain directly accessible.
    next_id = -1
    for family, (name, faces) in FAMILIES.items():
        associations = []
        for size in SIZES:
            for style, face in faces.items():
                if style or family > 127:
                    resource_id = next_id
                    next_id -= 1
                else:
                    resource_id = family * 128 + size
                data = strike(directory / f"{face}.ttf", size)
                yield b"NFNT", resource_id, 0, None, data
                associations.append((size, style, resource_id))

        header = struct.pack(
            ">H7h3I12h",
            0x6000,
            family,
            FIRST,
            LAST,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            *([0] * 11),
            1,
        )
        table = struct.pack(">H", len(associations) - 1)
        table += b"".join(struct.pack(">hhh", *entry) for entry in associations)
        yield b"FOND", family, 0, name.encode("mac_roman"), header + table


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fonts", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    publish(args.output, build_fork(font_records(args.fonts)))
