#!/usr/bin/env python3
# +===================================================================
#   File:      icns_assets_test.py
#
#   Summary:   Validate shipped ICNS assets, QuickDraw packing and malformed inputs.
#
#   Classes:   IconTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Validate shipped ICNS assets, QuickDraw packing and malformed inputs."""

import importlib.util
from pathlib import Path
import re
import struct
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("icns_to_c", ROOT / "tools/romaw/icns_to_c.py")
ico = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ico)


class IconTests(unittest.TestCase):
    def test_scroll_arrow_directions(self):
        rows = [[1, 0], [0, 1], [1, 1]]
        expected = {
            "right": [[1, 0], [0, 1], [1, 1]],
            "left": [[0, 1], [1, 0], [1, 1]],
            "up": [[0, 1, 1], [1, 0, 1]],
            "down": [[1, 0, 1], [1, 1, 0]],
        }
        for direction, pixels in expected.items():
            self.assertEqual(ico.mac_icons.scroll_arrow(rows, direction), pixels)

    def test_vertical_thumb_pixels_and_mask(self):
        data = ico.mac_icons.encode(
            [
                dict(
                    size=2,
                    height=3,
                    depth=1,
                    palette=[(255, 255, 255), (0, 0, 0)],
                    image=bytes([0x80, 0x40, 0xC0]),
                    opacity=bytes([0xC0, 0x80, 0x40]),
                )
            ]
        )
        output = ico.generate(data, "thumb", vertical_thumb=True)
        for suffix, expected in (
            ("bits", [0xC0, 0, 0xA0, 0]),
            ("mask", [0xA0, 0, 0x60, 0]),
        ):
            match = re.search(rf"thumb_1_{suffix}\[\] = \{{(.*?)\}};", output, re.S)
            self.assertEqual(list(map(int, re.findall(r"\d+", match[1]))), expected)

    def test_multisize_ui_asset(self):
        frames = ico.mac_icons.decode(self.fixture())
        expanded = [ico.mac_icons.classic_frame(frame, size, frame["depth"]) for size in (32, 16) for frame in frames]
        data = ico.mac_icons.encode(expanded)
        dimensions, images = ico.ui_image(data)
        self.assertEqual(dimensions, (16, 16))
        self.assertEqual(set(images), {1, 4, 8})
        self.assertIn("multisize_desc", ico.generate(data, "multisize"))

    def subset(self, depths):
        return ico.mac_icons.encode(
            [
                dict(
                    size=18,
                    height=18,
                    depth=depth,
                    palette=[(i, i, i) for i in range(1 << depth)],
                    image=bytes(((18 * depth + 7) // 8) * 18),
                    opacity=b"\xff" * 54,
                )
                for depth in depths
            ]
        )

    def fixture(self):
        return self.subset((1, 4, 8))

    def test_missing_depths(self):
        cases = [
            ((1,), (1, 1, 1)),
            ((4,), (1, 4, 4)),
            ((8,), (1, 1, 8)),
            ((1, 4), (1, 4, 4)),
            ((1, 8), (1, 1, 8)),
            ((4, 8), (1, 4, 8)),
            ((1, 4, 8), (1, 4, 8)),
        ]
        for depths, expected in cases:
            with self.subTest(depths=depths):
                data = self.subset(depths)
                _, original = ico.decode(data)
                images, sources = ico.representations(original)
                self.assertEqual(tuple(sources), expected)
                for depth in depths:
                    self.assertEqual(images[depth], original[depth])
                output = ico.generate(data, "fallback")
                desc = re.search(r"fallback_desc = \{(.*?)\n\};", output, re.S)[1]
                actual = re.findall(r"fallback_(\d+)_bits, (\d+), (\d+)", desc)
                self.assertEqual([int(entry[2]) for entry in actual], list(expected))
                for source, stride, depth in actual:
                    self.assertEqual(source, depth)
                    self.assertEqual(int(stride), ((18 * int(depth) + 15) // 16) * 2)
                if 1 not in depths:
                    self.assertEqual(images[1][1], [(65535,) * 3, (0,) * 3])
                    self.assertEqual(len(images[1][0]), 18)
                    self.assertTrue(set(sum(images[1][0], [])).issubset({0, 1}))

    def test_monochrome_luminance(self):
        rows = [[0, 1, 2, 3]]
        colors = [(65535,) * 3, (0,) * 3, (65535, 0, 0), (0, 65535, 0)]
        images, sources = ico.representations({4: (rows, colors, None)})
        self.assertEqual(images[1][0], [[0, 1, 1, 0]])
        self.assertEqual(sources, [1, 4, 4])

    def test_assets_and_quickdraw_packing(self):
        assets = [
            ROOT / name
            for name in sorted(
                set(
                    re.findall(
                        r"\bbitmaps/[A-Za-z_][A-Za-z_0-9]*\.icns\b",
                        (ROOT / "Makefile.am").read_text(),
                    )
                )
            )
        ]
        # These use Windows resources or a raw monochrome bitmap, not UI descriptors.
        assets = [path for path in assets if path.name not in {"ROMAW.icns", "alarm.icns", "arrow_closed.icns"}]
        self.assertEqual(len(assets), 9)
        for path in assets:
            with self.subTest(asset=path.name):
                (w, h), images = ico.ui_image(path.read_bytes())
                output = ico.generate(path.read_bytes(), path.stem)
                for depth in images:
                    match = re.search(rf"{path.stem}_{depth}_bits\[\] = \{{(.*?)\}};", output, re.S)
                    packed = bytes(map(int, re.findall(r"\d+", match[1])))
                    stride = ((w * depth + 15) // 16) * 2
                    self.assertEqual(len(packed), stride * h)
                    # Decode each output byte independently, including tail padding.
                    for y, row in enumerate(images[depth][0]):
                        bitstream = "".join(f"{v:08b}" for v in packed[y * stride : (y + 1) * stride])
                        actual = [int(bitstream[x * depth : (x + 1) * depth], 2) for x in range(w)]
                        self.assertEqual(actual, row)
                        self.assertTrue(all(c == "0" for c in bitstream[w * depth :]))
                    opacity = images[depth][2]
                    if opacity is not None:
                        match = re.search(
                            rf"{path.stem}_{depth}_mask\[\] = \{{(.*?)\}};",
                            output,
                            re.S,
                        )
                        packed_mask = bytes(map(int, re.findall(r"\d+", match[1])))
                        mask_stride = ((w + 15) // 16) * 2
                        for y, row in enumerate(opacity):
                            actual = [(packed_mask[y * mask_stride + x // 8] >> (7 - x % 8)) & 1 for x in range(w)]
                            self.assertEqual(actual, row)
                self.assertIn(f"image_{path.stem}_init (void)", output)

    def test_all_truncations(self):
        data = self.fixture()
        for end in range(len(data)):
            with self.assertRaises(ValueError, msg=f"truncation at {end}"):
                ico.decode(data[:end])

    def test_malformed_metadata_and_transparency(self):
        original = self.fixture()
        for position, fmt, value in (
            (0, ">4s", b"bad!"),
            (4, ">I", 8),
            (12, ">I", 0),
            (12, ">I", 0xFFFFFFFF),
            (16, ">H", 0),
            (20, ">H", 2),
            (22, ">H", 3),
            (24, ">H", 2),
        ):
            data = bytearray(original)
            struct.pack_into(fmt, data, position, value)
            with self.subTest(position=position), self.assertRaises(ValueError):
                ico.decode(data)
        frames = ico.mac_icons.decode(original)
        frames[0]["opacity"] = b"\x7f" + frames[0]["opacity"][1:]
        data = ico.mac_icons.encode(frames)
        _, images = ico.decode(data)
        self.assertEqual(images[1][2][0][0], 0)
        self.assertEqual(images[1][2][17][0], 1)
        self.assertIn("masked_1_mask", ico.generate(data, "masked"))
        with self.assertRaises(ValueError):
            ico.generate(original, "bad-name")


if __name__ == "__main__":
    unittest.main()
