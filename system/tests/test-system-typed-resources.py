# +===================================================================
#   File:      test-system-typed-resources.py
#
#   Summary:   Verify typed System conversion, editable bitmap inputs and malformed fields.
#
#   Classes:   TypedSystemResourcesTests.
#
#   Functions: unittest entry point.
#
#   Origin:    Byte hashes captured from the maintained pre-conversion System
#              resource declarations and independent synthetic field fixtures.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import resource_catalog as catalog
from rez_system_types import indexed_pixels, pattern_bitmap

spec = importlib.util.spec_from_file_location("system_builder", ROOT / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
SOURCE = ROOT / "system/resources/system"


class TypedSystemResourcesTests(unittest.TestCase):
    def test_all_55_converted_payloads_and_metadata_match_original(self):
        original = json.loads((ROOT / "system/tests/system-typed-resources.json").read_text(encoding="utf-8"))
        self.assertEqual(len(original), 55)
        current = {(k.decode("mac_roman"), i): (a, n, p) for k, i, a, n, p in builder.read_resources(SOURCE)}
        for record in original:
            with self.subTest(kind=record["type"], identifier=record["id"]):
                attrs, name, payload = current[record["type"], record["id"]]
                self.assertEqual(attrs, record["attributes"])
                self.assertEqual(None if name is None else name.hex(), record["name_hex"])
                # A concurrent native-localization port replaces only INTL 1's
                # executable RTS; this conversion must preserve its 330 data bytes.
                if (record["type"], record["id"]) == ("INTL", 1) and len(payload) != 332:
                    from native_code_resources import validate_intl1_localization

                    validate_intl1_localization(payload)
                    payload = payload[:330] + struct.pack(">H", 20085)
                self.assertEqual(len(payload), record["length"])
                self.assertEqual(hashlib.sha256(payload).hexdigest(), record["sha256"])

    def test_system_rez_has_no_literal_hex_blocks(self):
        for path in SOURCE.glob("*.r"):
            self.assertNotRegex(path.read_text(encoding="utf-8"), r'\$"', path.name)

    def test_monochrome_png_is_the_editable_pattern_input(self):
        original = next(p for k, i, a, n, p in builder.read_resources(SOURCE) if (k, i) == (b"PAT ", 16))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            shutil.copytree(ROOT / "bitmaps/system-patterns", directory / "system-patterns")
            path = directory / "system-patterns/PAT-16.png"
            with Image.open(path) as image:
                changed = image.convert("RGB")
            pixel = changed.getpixel((0, 0))
            changed.putpixel((0, 0), (0, 0, 0) if pixel == (255, 255, 255) else (255, 255, 255))
            changed.save(path)
            with patch.object(catalog, "ICONS", directory):
                entries = catalog.read_rez(SOURCE / "interface.r")
            payload = next(e["payload"] for e in entries if (e["type"], e["id"]) == ("PAT ", 16))
            self.assertEqual(payload, bytes((original[0] ^ 128,)) + original[1:])

    def test_indexed_png_controls_pixels_and_exact_color_table(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            shutil.copytree(ROOT / "bitmaps/system-patterns", directory / "system-patterns")
            path = directory / "system-patterns/ppat-16.png"
            with Image.open(path) as image:
                changed = image.copy()
            palette = changed.getpalette()
            red = 255 - palette[0]
            palette[0] = red
            changed.putpalette(palette)
            changed.save(path, bits=4)
            with patch.object(catalog, "ICONS", directory):
                entries = catalog.read_rez(SOURCE / "payloads.r")
            payload = next(e["payload"] for e in entries if (e["type"], e["id"]) == ("ppat", 16))
            color_table = struct.unpack_from(">I", payload, 70)[0]
            self.assertEqual(struct.unpack_from(">H", payload, color_table + 10)[0], red * 257)

    def fixture(self, source):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.r"
            path.write_text(source, encoding="utf-8")
            return catalog.read_rez(path)

    def test_help_items_and_alignment_bytes_are_explicit(self):
        item = self.fixture("resource 'DITL' (1) { { {0,0,10,20}, HelpItem {disabled,1,-6043}; } };")[0]["payload"]
        self.assertEqual(item[-6:], b"\x81\x04\0\1\xe8e")
        item = self.fixture("resource 'DITL' (1) { { {0,0,10,20}, RadioButton {enabled,\"Off\",7}; } };")[0]["payload"]
        self.assertEqual(item[-5:], b"\x03Off\x07")
        with self.assertRaisesRegex(ValueError, "already aligned"):
            self.fixture("resource 'DITL' (1) { { {0,0,10,20}, Button {enabled,\"OK\",7}; } };")

    def test_array_extents_and_native_definition_types_are_checked(self):
        for source in (
            "resource 'movp' (0) { { 1; } };",
            "resource 'LDEF' (0) { P_cdef0 };",
            "resource 'INTL' (0) { NumberFormat { \"..\" } };",
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.fixture(source)

    def test_invalid_bitmap_inputs_are_rejected(self):
        for image in (
            Image.new("RGB", (9, 8)),
            Image.new("RGB", (8, 8), "red"),
            Image.new("RGBA", (8, 8), (0, 0, 0, 0)),
        ):
            stream = io.BytesIO()
            image.save(stream, format="PNG")
            with self.assertRaises(ValueError):
                pattern_bitmap(stream.getvalue())
        stream = io.BytesIO()
        Image.new("RGB", (8, 8)).save(stream, format="PNG")
        with self.assertRaisesRegex(ValueError, "indexed PNG"):
            indexed_pixels(stream.getvalue(), 8, 8, 4, 4, 2)


if __name__ == "__main__":
    unittest.main()
