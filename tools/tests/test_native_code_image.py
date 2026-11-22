#!/usr/bin/env python3
# +===================================================================
#   File:      test_native_code_image.py
#
#   Summary:   Validate x64 native image extraction and reject obsolete host ABIs.
#
#   Classes:   NativeCodeImageTests.
#
#   Functions: None; unittest entry point.
#
#   Origin:    ARDI Executor shared native image regression tests.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_code_image import extract_elf, extract_image


class NativeCodeImageTests(unittest.TestCase):
    def elf(self):
        raw = bytearray(256)
        raw[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", raw, 18, 62)
        struct.pack_into("<Q", raw, 40, 64)
        struct.pack_into("<HH", raw, 58, 64, 2)
        struct.pack_into("<IIQQQQIIQQ", raw, 64, 0, 1, 6, 0, 192, 32, 0, 0, 16, 0)
        struct.pack_into("<IIQQQQIIQQ", raw, 128, 0, 4, 0, 0, 224, 24, 0, 0, 8, 24)
        struct.pack_into("<Q", raw, 200, 16)
        struct.pack_into("<QQq", raw, 224, 8, 1, 0)
        return raw

    def pe(self):
        raw = bytearray(640)
        raw[:2] = b"MZ"
        struct.pack_into("<I", raw, 60, 64)
        raw[64:68] = b"PE\0\0"
        struct.pack_into("<HH", raw, 68, 0x8664, 2)
        struct.pack_into("<H", raw, 84, 240)
        struct.pack_into("<H", raw, 88, 0x20B)
        struct.pack_into("<II", raw, 88 + 112 + 5 * 8, 64, 10)
        for at, name, rva, offset, size, flags in (
            (328, b".text", 0, 512, 32, 0x60000000),
            (368, b".reloc", 64, 544, 10, 0x40000000),
        ):
            raw[at : at + len(name)] = name
            struct.pack_into("<IIII", raw, at + 8, size, rva, size, offset)
            struct.pack_into("<I", raw, at + 36, flags)
        struct.pack_into("<Q", raw, 520, 16)
        struct.pack_into("<IIH", raw, 544, 0, 10, 0xA008)
        return raw

    def test_x64_images_and_pointer_relocations(self):
        for extract, raw in ((extract_elf, self.elf()), (extract_image, self.pe())):
            with self.subTest(format=extract.__name__):
                code, fixups, entries = extract(raw, {"entry": 0}, ("entry",))
                self.assertEqual(len(code), 32)
                self.assertEqual(fixups, [(8, 16)])
                self.assertEqual(entries, [0])

    def test_reject_elf32_and_foreign_elf64(self):
        for elf_class, machine in ((1, 3), (2, 183)):
            raw = self.elf()
            raw[4] = elf_class
            struct.pack_into("<H", raw, 18, machine)
            with self.assertRaisesRegex(ValueError, "ABI"):
                extract_elf(raw, {}, ())

    def test_reject_pe32(self):
        raw = self.pe()
        struct.pack_into("<H", raw, 68, 0x14C)
        struct.pack_into("<H", raw, 88, 0x10B)
        with self.assertRaisesRegex(ValueError, "ABI"):
            extract_image(raw, {}, ())

    def test_reject_writable_code(self):
        raw = self.elf()
        struct.pack_into("<Q", raw, 72, 7)
        with self.assertRaisesRegex(ValueError, "writable"):
            extract_elf(raw, {}, ())
        raw = self.pe()
        struct.pack_into("<I", raw, 364, 0xE0000000)
        with self.assertRaisesRegex(ValueError, "writable"):
            extract_image(raw, {}, ())

    def test_reject_external_pointer(self):
        for extract, raw, at in ((extract_elf, self.elf(), 200), (extract_image, self.pe(), 520)):
            struct.pack_into("<Q", raw, at, 128)
            with self.assertRaisesRegex(ValueError, "outside"):
                extract(raw, {"entry": 0}, ("entry",))

    def test_reject_ia32_relocation_in_x64_image(self):
        raw = self.pe()
        struct.pack_into("<H", raw, 552, 0x3008)
        with self.assertRaisesRegex(ValueError, "relocation"):
            extract_image(raw, {}, ())


if __name__ == "__main__":
    unittest.main()
