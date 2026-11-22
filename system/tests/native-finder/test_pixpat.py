# +===================================================================
#   File:      test_pixpat.py
#
#   Summary:   Exercise indexed PixPat layout, palette conversion and malformed resources.
#
#   Classes:   PixPatTests
#
#   Functions: sample
#
#   Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Exercise indexed PixPat layout, palette conversion and malformed resources."""

from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/romaw/native_finder"))
sys.path.insert(0, str(ROOT / "tools/romaw"))
from pixpat import decode_pixpat
from system_resources import read_resources


def sample():
    data = bytearray(108)
    struct.pack_into(">HII", data, 0, 1, 28, 78)
    struct.pack_into(">Hhhhh", data, 32, 0x8003, 5, 7, 7, 10)
    struct.pack_into(">HHHH", data, 58, 0, 4, 1, 4)
    struct.pack_into(">I", data, 70, 84)
    data[78:84] = bytes.fromhex("0100ff1010ff")
    struct.pack_into(">IHH", data, 84, 0, 0, 1)
    struct.pack_into(">HHHHHHHH", data, 92, 0, 65535, 0, 0, 1, 0, 65535, 0)
    return data


class PixPatTests(unittest.TestCase):
    def test_stride_bounds_and_palette(self):
        data = sample()
        self.assertEqual(
            decode_pixpat(data),
            (3, 2, [0xFF0000, 0x00FF00, 0xFF0000, 0x00FF00, 0xFF0000, 0x00FF00]),
        )
        struct.pack_into(">H", data, 94, 0x3333)
        self.assertEqual(decode_pixpat(data)[2][0], 0x330000)

    def test_device_palette_uses_entry_order(self):
        data = sample()
        struct.pack_into(">H", data, 88, 0x8000)
        struct.pack_into(">H", data, 92, 65535)
        struct.pack_into(">H", data, 100, 65535)
        self.assertEqual(decode_pixpat(data)[2][0], 0xFF0000)

    def test_truncations(self):
        data = sample()
        for end in range(len(data)):
            with self.subTest(end=end), self.assertRaises(ValueError):
                decode_pixpat(data[:end])

    def test_invalid_maps_and_pixels(self):
        for offset, fmt, value in (
            (2, ">I", 0),
            (6, ">I", 29),
            (32, ">H", 0x8001),
            (38, ">h", 32767),
            (44, ">H", 1),
            (60, ">H", 16),
            (70, ">I", 80),
            (90, ">H", 256),
            (100, ">H", 0),
            (78, ">B", 0x20),
        ):
            data = sample()
            struct.pack_into(fmt, data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_pixpat(data)

    def test_romaw_blue_tiles(self):
        resources = read_resources(ROOT / "system/resources/system")
        pattern = next(data for kind, rid, _, _, data in resources if kind == b"ppat" and rid == 17)
        width, height, pixels = decode_pixpat(pattern)
        self.assertEqual((width, height), (64, 64))
        self.assertEqual(len(pixels), width * height)
        self.assertGreater(len(set(pixels)), 2)


if __name__ == "__main__":
    unittest.main()
