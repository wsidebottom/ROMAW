# +===================================================================
#   File:      test_layout.py
#
#   Summary:   Original regressions for the classic agent's native PEF packaging rules.
#
#   Classes:   LayoutTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Original regressions for the classic agent's native PEF packaging rules."""

from pathlib import Path
import struct
import unittest

from build import validate_layout


class LayoutTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[2]
        self.container = bytearray((root / "build/classic-remote/Classic Remote").read_bytes())

    def test_writable_executable_image(self):
        validate_layout(self.container)
        self.assertEqual(self.container[64], 6)
        self.assertEqual(self.container[65], 1)

    def test_readonly_bss_regression(self):
        self.container[64] = 0
        with self.assertRaisesRegex(ValueError, "Read-only code"):
            validate_layout(self.container)

    def test_bad_sharing(self):
        self.container[65] = 0
        with self.assertRaisesRegex(ValueError, "instantiated"):
            validate_layout(self.container)

    def test_payload_and_memory_bounds(self):
        for field, value in ((8, 1), (16, 0xFFFFFFFF), (20, 0xFFFFFFFF)):
            with self.subTest(field=field):
                container = self.container.copy()
                struct.pack_into(">I", container, 40 + field, value)
                with self.assertRaises(ValueError):
                    validate_layout(container)


if __name__ == "__main__":
    unittest.main()
