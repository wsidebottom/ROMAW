#!/usr/bin/env python3
# +===================================================================
#   File:      test-bitmap-assets.py
#
#   Summary:   Verify extracted artwork bytes, native index packing and PNG/BMP input validation.
#
#   Classes:   BitmapAssetsTest.
#
#   Functions: Script unittest entry point.
#
#   Origin:    Regression checks for Executor inline artwork extraction.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

import hashlib
import io
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import bitmaps
import mac_icons
from PIL import Image

# Expected native artwork bytes, independent of image-file encoding.
EXPECTED = {
    "color_picker_normal.png": (13, 22, 1, "391886f730902a2ded07e139951bf8bec8dc33215cb79cf86158ddcbc765c971"),
    "color_picker_lower_highlighted.png": (
        13,
        22,
        1,
        "d5094220205d58fa83a3f31a3311060827b8b760e03dde66c5d4b3fada1e2549",
    ),
    "color_picker_upper_highlighted.png": (
        13,
        22,
        1,
        "ea94b19098ac17a748fe5665cfa754c6c877f8a4c5d569e8550a8a2bba0f973c",
    ),
    "color_wheel_4.png": (208, 208, 4, "a3f8ced8e279e16388e4d9a9aa130b8db4e223a0b988fe0912eaf862863f52e9"),
    "color_wheel_8.png": (208, 208, 8, "447f5c11fc625da88457b6778342534b4ed14585d14e934c3e2cb258cf0e4aa9"),
    "alarm.icns": (16, 16, 1, "a4b09666239bb9dc89b375fd4d196ac7c5e362f5451b28a5d973109c2a6e7728"),
    "scrollbar_track.png": (8, 8, 1, "b05163bb0e8ecf940f4e8897f0835bc32956f656b6960f3e5dbd983833a21545"),
}


class BitmapAssetsTest(unittest.TestCase):
    def test_menu_scroll_arrows_rotate_arrow_closed_at_native_size(self):
        data = (ROOT / "bitmaps/arrow_closed.icns").read_bytes()
        down = bitmaps.initializer(data, (11, 6, 1), (5, 2, 11, 13), "clockwise")
        up = bitmaps.initializer(data, (11, 6, 1), (5, 2, 11, 13), "counterclockwise")
        packed_down = bytes(map(int, re.findall(r"\d+", down.split("*/", 1)[1])))
        packed_up = bytes(map(int, re.findall(r"\d+", up.split("*/", 1)[1])))
        self.assertEqual(packed_down, bytes((0xFF, 0xE0, 0x7F, 0xC0, 0x3F, 0x80, 0x1F, 0, 0x0E, 0, 0x04, 0)))
        self.assertEqual(packed_up, b"".join(packed_down[y : y + 2] for y in range(10, -1, -2)))
        self.assertFalse((ROOT / "bitmaps/menu_up_arrow.png").exists())
        self.assertFalse((ROOT / "bitmaps/menu_down_arrow.png").exists())
        with self.assertRaises(ValueError):
            bitmaps.initializer(data, rotate="invalid")

    def test_submenu_arrow_uses_arrow_closed_without_resizing(self):
        data = (ROOT / "bitmaps/arrow_closed.icns").read_bytes()
        generated = bitmaps.initializer(data, (6, 11, 1), (5, 2, 11, 13))
        packed = bytes(map(int, re.findall(r"\d+", generated.split("*/", 1)[1])))
        self.assertEqual(packed, bitmaps.read_bitmap(self.bmp_fixture())[3])
        self.assertFalse((ROOT / "bitmaps/menu_right_arrow.png").exists())

    def test_popup_arrow_uses_the_shared_disclosure_icon_at_native_size(self):
        data = (ROOT / "bitmaps/3061.icns").read_bytes()
        generated = bitmaps.initializer(data, (11, 6, 1), (2, 6, 13, 12))
        packed = bytes(map(int, re.findall(r"\d+", generated.split("*/", 1)[1])))
        self.assertEqual(packed, bytes((0xFF, 0xE0, 0x40, 0x40, 0x20, 0x80, 0x11, 0, 0x0A, 0, 0x04, 0)))
        self.assertFalse((ROOT / "bitmaps/popup_down_arrow.png").exists())
        with self.assertRaises(ValueError):
            bitmaps.initializer(data, crop=(2, 6, 17, 12))

    def bmp_fixture(self):
        rows = bytes((0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80))
        pixels = b"".join(bytes((value, 0, 0, 0)) for value in reversed(rows))
        return (
            struct.pack("<2sIHHI", b"BM", 62 + len(pixels), 0, 0, 62)
            + struct.pack("<IiiHHIIiiII", 40, 6, 11, 1, 1, 0, len(pixels), 0, 0, 2, 0)
            + b"\xff\xff\xff\0\0\0\0\0"
            + pixels
        )

    def test_png_preserves_palette_indices_at_every_native_depth(self):
        for depth, indices, expected, cropped in (
            (1, bytes((1, 0, 1, 0, 1, 0)), b"\xa0\x40", b"\x40\x80"),
            (4, bytes((15, 1, 0, 0, 1, 15)), b"\xf1\0\x01\xf0", b"\x10\x1f"),
            (8, bytes((255, 1, 0, 0, 1, 255)), bytes((255, 1, 0, 0, 1, 255)), bytes((1, 0, 1, 255))),
        ):
            with self.subTest(depth=depth):
                image = Image.frombytes("P", (3, 2), indices)
                # Reversed colors prove the compiler retains indices rather than quantizing RGB.
                image.putpalette([channel for color in reversed(mac_icons.classic_palette(depth)) for channel in color])
                output = io.BytesIO()
                image.save(output, format="PNG", bits=depth)
                self.assertEqual(bitmaps.read_bitmap(output.getvalue()), (3, 2, depth, expected))
                generated = bitmaps.initializer(output.getvalue(), (2, 2, depth), (1, 0, 3, 2))
                self.assertEqual(bytes(map(int, re.findall(r"\d+", generated.split("*/", 1)[1]))), cropped)
                for direction, orientation in (("clockwise", "down"), ("counterclockwise", "up")):
                    generated = bitmaps.initializer(output.getvalue(), (2, 3, depth), rotate=direction)
                    packed = bytes(map(int, re.findall(r"\d+", generated.split("*/", 1)[1])))
                    stride = (2 * depth + 7) // 8
                    rows = [
                        [
                            packed[y * stride + x * depth // 8] >> (8 - depth - x * depth % 8) & ((1 << depth) - 1)
                            for x in range(2)
                        ]
                        for y in range(3)
                    ]
                    self.assertEqual(rows, mac_icons.scroll_arrow([list(indices[:3]), list(indices[3:])], orientation))

    def test_png_rejects_rgb_transparency_truncation_and_wrong_geometry(self):
        output = io.BytesIO()
        Image.new("RGB", (3, 2)).save(output, format="PNG")
        with self.assertRaises(ValueError):
            bitmaps.read_bitmap(output.getvalue())
        data = (ROOT / "bitmaps/color_picker_normal.png").read_bytes()
        for length in (0, 33, 45):
            with self.subTest(length=length), self.assertRaises(ValueError):
                bitmaps.read_png(data[:length])
        output = io.BytesIO()
        image = Image.new("P", (3, 2))
        image.putpalette([255, 255, 255, 0, 0, 0])
        image.save(output, format="PNG", bits=1, transparency=0)
        with self.assertRaises(ValueError):
            bitmaps.read_png(output.getvalue())
        with self.assertRaises(ValueError):
            bitmaps.initializer(data, (8, 8, 1))

    def test_native_bytes_and_dimensions_match_artwork(self):
        self.assertFalse(list((ROOT / "bitmaps").glob("*.bmp")))
        for filename, (width, height, depth, digest) in EXPECTED.items():
            with self.subTest(asset=filename):
                data = (ROOT / "bitmaps" / filename).read_bytes()
                if filename.endswith((".bmp", ".png")):
                    w, h, d, pixels = bitmaps.read_bitmap(data)
                    self.assertEqual((w, h, d), (width, height, depth))
                else:
                    pixels = mac_icons.resource(data, b"SICN")
                self.assertEqual(hashlib.sha256(pixels).hexdigest(), digest)
                generated = bitmaps.initializer(data, (width, height, depth))
                packed = bytes(map(int, re.findall(r"\d+", generated.split("*/", 1)[1])))
                self.assertEqual(packed, pixels)

    def test_top_down_bmp_preserves_the_same_native_rows(self):
        data = bytearray(self.bmp_fixture())
        offset = struct.unpack_from("<I", data, 10)[0]
        original = bitmaps.read_bitmap(data)
        struct.pack_into("<i", data, 22, -11)
        data[offset:] = b"".join(data[i : i + 4] for i in range(len(data) - 4, offset - 1, -4))
        self.assertEqual(bitmaps.read_bitmap(data), original)

    def test_truncations_and_malformed_headers_fail(self):
        source = self.bmp_fixture()
        for length in range(len(source)):
            data = bytearray(source[:length])
            if length >= 6:
                struct.pack_into("<I", data, 2, length)
            with self.subTest(length=length), self.assertRaises(ValueError):
                bitmaps.read_bitmap(data)
        for offset, fmt, value in (
            (10, "I", 0),
            (18, "i", 0),
            (22, "i", 0),
            (26, "H", 2),
            (28, "H", 24),
            (30, "I", 1),
            (46, "I", 1),
        ):
            data = bytearray(source)
            struct.pack_into("<" + fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                bitmaps.read_bitmap(data)
        with self.assertRaises(ValueError):
            bitmaps.initializer(source, (8, 8, 1))


if __name__ == "__main__":
    unittest.main()
