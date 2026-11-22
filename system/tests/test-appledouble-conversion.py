#!/usr/bin/env python3
# +===================================================================
#   File:      test-appledouble-conversion.py
#
#   Summary:   Check migration safety using original, disposable AppleDouble fixtures.
#
#   Classes:   ConversionTest
#
#   Functions: apple
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Check migration safety using original, disposable AppleDouble fixtures."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "conversion", Path(__file__).resolve().parents[2] / "tools/convert-appledouble.py"
)
conversion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(conversion)


def apple(resource=b"resource", offset=82):
    header = struct.pack(">II16sH", 0x00051607, 0x00020000, b"", 2)
    descriptors = struct.pack(">IIIIII", 9, 50, 32, 2, offset, len(resource))
    return header + descriptors + b"APPLTEST" + bytes(24) + resource


class ConversionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.apps = self.directory / "Apps"
        self.apps.mkdir()
        self.backup = self.directory / "backup"

    def put(self, name, data):
        path = self.apps / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_forks_directory_metadata_and_empty_forks(self):
        data = apple(b"\x00\xffresource")
        self.put(".AppleDouble/App", data)
        self.put("App", b"data fork")
        self.put(".AppleDouble/.Parent", apple(b"directory"))
        self.put("%Empty", apple(b"", 512))
        self.put("%ordinary", b"ordinary percent file")
        conversion.convert(self.apps, self.backup)
        self.assertEqual((self.apps / "._App").read_bytes(), data)
        self.assertEqual((self.apps / "App").read_bytes(), b"data fork")
        self.assertEqual((self.directory / "._Apps").read_bytes(), apple(b"directory"))
        self.assertEqual((self.apps / "._Empty").read_bytes(), apple(b"", 512))
        self.assertTrue((self.apps / "%ordinary").is_file())
        self.assertFalse((self.apps / ".AppleDouble").exists())
        self.assertEqual((self.backup / ".AppleDouble/App").read_bytes(), data)

    def test_conflict_is_non_destructive_and_can_be_backed_up(self):
        original = apple(b"old")
        desired = apple(b"new")
        source = self.put(".AppleDouble/App", desired)
        target = self.put("._App", original)
        with self.assertRaises(ValueError):
            conversion.convert(self.apps, self.backup)
        self.assertEqual(source.read_bytes(), desired)
        self.assertEqual(target.read_bytes(), original)
        self.assertFalse(self.backup.exists())
        conversion.convert(self.apps, self.backup, replace_conflicts=True)
        self.assertEqual(target.read_bytes(), desired)
        self.assertEqual((self.backup / "existing-targets/Apps/._App").read_bytes(), original)

    def test_truncated_resource_prevents_all_changes(self):
        source = self.put(".AppleDouble/A", apple())
        self.put(".AppleDouble/Z", apple()[:-1])
        with self.assertRaises(ValueError):
            conversion.convert(self.apps, self.backup)
        self.assertTrue(source.is_file())
        self.assertFalse((self.apps / "._A").exists())
        self.assertFalse(self.backup.exists())


if __name__ == "__main__":
    unittest.main()
