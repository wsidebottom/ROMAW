#!/usr/bin/env python3
# +===================================================================
#   File:      test-appledouble-normalization.py
#
#   Summary:   Regression for resource-first Retro68 sidecars that crash Tiger.
#
#   Classes:   NormalizationTests
#
#   Functions: sidecar
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Regression for resource-first Retro68 sidecars that crash Tiger."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "normalizer", Path(__file__).resolve().parents[2] / "tools/normalize-appledouble.py"
)
normalizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(normalizer)


def sidecar(records):
    offset = 26 + 12 * len(records)
    header = struct.pack(">II16sH", 0x51607, 0x20000, b"test header", len(records))
    table, data = [], []
    for kind, value in records:
        table.append(struct.pack(">III", kind, offset, len(value)))
        data.append(value)
        offset += len(value)
    return header + b"".join(table) + b"".join(data)


class NormalizationTests(unittest.TestCase):
    def test_resource_first_layout_and_all_payloads(self):
        records = [(2, bytes(range(256)) * 500), (9, b"APPLEFnd" + bytes(24))]
        original = sidecar(records)
        result = normalizer.normalize(original)
        self.assertEqual(
            result[:26],
            struct.pack(">II16sH", 0x51607, 0x20000, b"Mac OS X        ", 2),
        )
        self.assertEqual(struct.unpack_from(">III", result, 26), (9, 50, 32))
        self.assertEqual(struct.unpack_from(">III", result, 38), (2, 82, 128000))
        self.assertEqual(dict(normalizer.entries(result)), dict(records))
        self.assertEqual(normalizer.normalize(result), result)

    def test_legacy_entries_excluded_from_native_output(self):
        records = [
            (2, b"resource"),
            (8, bytes(range(16))),
            (9, bytes(range(16))),
            (0x80000001, b"private\0\xff"),
        ]
        result = normalizer.normalize(sidecar(records))
        actual = dict(normalizer.entries(result))
        self.assertEqual(actual[9], bytes(range(16)) + bytes(16))
        self.assertEqual(set(actual), {9, 2})
        self.assertEqual(actual[2], b"resource")
        self.assertEqual(struct.unpack_from(">III", result, 26), (9, 50, 32))
        self.assertEqual(struct.unpack_from(">III", result, 38), (2, 82, 8))

    def test_empty_resource_offset_is_materialized(self):
        data = bytearray(sidecar([(9, bytes(32)), (2, b"")]))
        struct.pack_into(">I", data, 42, 512)
        result = normalizer.normalize(data)
        self.assertEqual(struct.unpack_from(">III", result, 38), (2, 82, 0))
        self.assertEqual(len(result), 82)

    def test_malformed_or_extended_input_rejected(self):
        valid = sidecar([(2, b"resource"), (9, bytes(32))])
        overlap = bytearray(valid)
        struct.pack_into(">I", overlap, 42, 50)
        for data in (
            b"dsst" + bytes(512),
            valid[:20],
            valid[:-1],
            overlap,
            sidecar([(9, bytes(32)), (9, bytes(32))]),
            sidecar([(2, b"resource")]),
            sidecar([(9, bytes(31))]),
            sidecar([(9, bytes(32) + b"ATTR" + bytes(32))]),
        ):
            with self.subTest(length=len(data)):
                with self.assertRaises(ValueError):
                    normalizer.normalize(data)

    def test_new_destination_only_and_source_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source, destination = Path(directory) / "source", Path(directory) / "out"
            original = sidecar([(2, b"resource"), (9, bytes(32))])
            source.write_bytes(original)
            normalizer.write_normalized(source, destination)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(destination.read_bytes(), normalizer.normalize(original))
            with self.assertRaises(ValueError):
                normalizer.write_normalized(source, destination)
            with self.assertRaises(ValueError):
                normalizer.write_normalized(source, source)


if __name__ == "__main__":
    unittest.main()
