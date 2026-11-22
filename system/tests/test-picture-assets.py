# +===================================================================
#   File:      test-picture-assets.py
#
#   Summary:   Verify PNG/RGB BMP reading and Macintosh PICT resource generation.
#
#   Classes:   PictureAssetsTest.
#
#   Functions: Script unittest entry point.
#
#   Origin:    Executor raster picture conversion regression fixture.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

from pathlib import Path
import io
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pictures
from PIL import Image


class PictureAssetsTest(unittest.TestCase):
    def test_png_preserves_rgb_pixels_and_generated_picture_bytes(self):
        rgb = bytes((255, 0, 0, 0, 255, 0))
        for mode in ("RGB", "RGBA", "P"):
            image = Image.frombytes("RGB", (2, 1), rgb).convert(mode)
            output = io.BytesIO()
            image.save(output, format="PNG")
            data = output.getvalue()
            self.assertEqual(pictures.read_image(data), (2, 1, rgb))
            self.assertEqual(pictures.resource(data, -3, 8), pictures.resource(pictures.write_bmp(2, 1, rgb), -3, 8))

    def test_invalid_truncated_and_oversized_png_inputs_fail(self):
        output = io.BytesIO()
        Image.new("RGB", (2, 1), "red").save(output, format="PNG")
        for data in (b"not a PNG", output.getvalue()[:33], output.getvalue()[:45]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                pictures.read_png(data)
        output = io.BytesIO()
        Image.new("RGB", (4096, 1), "red").save(output, format="PNG")
        with self.assertRaises(ValueError):
            pictures.read_png(output.getvalue())

    def test_rgb_bmp_channel_order_padding_and_top_down_rows(self):
        rgb = bytes((255, 0, 0, 0, 255, 0))
        bmp = pictures.write_bmp(1, 2, rgb)
        self.assertEqual(bmp[54:], b"\0\xff\0\0\0\0\xff\0")
        self.assertEqual(pictures.read_bmp(bmp), (1, 2, rgb))
        top_down = bytearray(bmp)
        struct.pack_into("<i", top_down, 22, -2)
        top_down[54:] = bmp[58:] + bmp[54:58]
        self.assertEqual(pictures.read_bmp(top_down), (1, 2, rgb))
        rgba = bytearray(bmp)
        struct.pack_into("<H", rgba, 28, 32)
        rgba[57] = rgba[61] = 0x80
        self.assertEqual(pictures.read_bmp(rgba), (1, 2, rgb))

    def test_pict_frame_and_raw_rgb_bitmap_records(self):
        rgb = bytes((255, 0, 0, 0, 255, 0))
        pict = pictures.resource(pictures.write_bmp(2, 1, rgb), -3, 8)
        self.assertEqual(struct.unpack_from(">H4h", pict), (len(pict), -3, 8, -2, 10))
        self.assertEqual(pict[10:14], b"\0\x11\x02\xff")
        self.assertEqual(struct.unpack_from(">HIH", pict, 52), (0x009A, 0xFF, 0x8008))
        self.assertEqual(struct.unpack_from(">HH", pict, 68), (0, 2))
        self.assertEqual(pict[122:-2], rgb)
        self.assertEqual(pict[-2:], b"\0\xff")

    def test_large_and_odd_length_pictures(self):
        large = pictures.resource(pictures.write_bmp(200, 200, bytes(200 * 200 * 3)))
        self.assertEqual(large[:2], b"\0\0")
        odd = pictures.resource(pictures.write_bmp(1, 1, b"\x12\x34\x56"))
        self.assertEqual(odd[122:], b"\x12\x34\x56\0\0\xff")

    def test_invalid_and_truncated_bmp_inputs_fail(self):
        bmp = pictures.write_bmp(1, 2, bytes(6))
        for length in range(len(bmp)):
            shortened = bytearray(bmp[:length])
            if length >= 6:
                struct.pack_into("<I", shortened, 2, length)
            with self.subTest(length=length), self.assertRaises(ValueError):
                pictures.read_bmp(shortened)
        for offset, fmt, value in ((10, "I", 0), (18, "i", 0), (22, "i", 0), (28, "H", 8), (30, "I", 1)):
            corrupt = bytearray(bmp)
            struct.pack_into("<" + fmt, corrupt, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pictures.read_bmp(corrupt)
        with self.assertRaises(ValueError):
            pictures.resource(bmp, 32767, 0)

    def test_all_shared_pictures_are_png_backed(self):
        from resource_catalog import load_scope, resource_path
        from importlib.util import module_from_spec, spec_from_file_location
        from unittest.mock import patch

        spec = spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
        builder = module_from_spec(spec)
        spec.loader.exec_module(builder)
        count = 0
        for scope in ("system", "finder"):
            manifest, directory = load_scope(
                ROOT / ("finder/resources" if scope == "finder" else "system/resources/system"), scope
            )
            # Finder's typed layout resources have a separate native compiler.
            entries = [entry for entry in manifest["resources"] if entry["type"] == "PICT"]
            with patch.object(builder, "load_scope", return_value=({"resources": entries}, directory)):
                records = {(kind, rid): data for kind, rid, _, _, data in builder.read_resources(directory)}
            for entry in manifest["resources"]:
                if entry["type"] == "PICT":
                    source = resource_path(directory, entry["file"])
                    self.assertEqual(source.suffix, ".png")
                    width, height, _ = pictures.read_png(source.read_bytes())
                    top, left = entry["picture_origin"]
                    data = records[b"PICT", entry["id"]]
                    self.assertEqual(struct.unpack_from(">4h", data, 2), (top, left, top + height, left + width))
                    count += 1
        self.assertEqual(count, 24)
        self.assertFalse(list((ROOT / "bitmaps").glob("*.pict")))
        self.assertFalse(list((ROOT / "bitmaps").glob("PICT-*.bmp")))


if __name__ == "__main__":
    unittest.main()
