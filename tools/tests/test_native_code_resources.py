#!/usr/bin/env python3
# +===================================================================
#   File:      test_native_code_resources.py
#
#   Summary:   Check native resource audits, embedded hook extents and resource parsing.
#
#   Classes:   NativeCodeResourceTests.
#
#   Functions: unittest script entry point.
#
#   Origin:    ROMAW native resource conversion regression fixtures.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from finder_guest_resources import FINDER_HANDLERS, finder_guest_payload
from native_code_resources import EXECUTABLE_TYPES, audit_native_resources, intl1_localization_payload
from resource_catalog import read_rez


class NativeCodeResourceTests(unittest.TestCase):
    def resource(self, kind, identity, payload):
        return kind, identity, 0, None, payload

    def test_all_unknown_executable_types_are_rejected(self):
        for kind in EXECUTABLE_TYPES:
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "Unreviewed executable"):
                audit_native_resources([self.resource(kind, 12345, b"\x4e\x75")], {})

    def test_embedded_rts_is_rejected_even_though_intl_is_data(self):
        with self.assertRaisesRegex(ValueError, "localization extent"):
            audit_native_resources([self.resource(b"INTL", 1, bytes(330) + b"\x4e\x75")], {})

    def test_embedded_native_entry_and_extent_are_checked(self):
        data = bytes(330) + intl1_localization_payload()
        entries = audit_native_resources([self.resource(b"INTL", 1, data)], {})
        self.assertEqual([(item[2], item[3]) for item in entries], [(330, 16)])
        damaged = bytearray(data)
        damaged[330] ^= 1
        with self.assertRaisesRegex(ValueError, "executable INTL"):
            audit_native_resources([self.resource(b"INTL", 1, damaged)], {})

    def test_finder_handler_entry_is_also_audited(self):
        records = [self.resource(b"hcsl", 0, finder_guest_payload(b"hcsl", 0))]
        entries = audit_native_resources(records, FINDER_HANDLERS)
        self.assertEqual([(entry[2], entry[3]) for entry in entries], [(0, 14), (26, 15)])

    def test_modified_standalone_body_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unexpected executable"):
            audit_native_resources([self.resource(b"WDEF", 128, b"\x4e\x75")], FINDER_HANDLERS)

    def test_full_system_requires_its_declared_entry_set(self):
        with self.assertRaisesRegex(ValueError, "Missing System"):
            audit_native_resources([], FINDER_HANDLERS, require_system=True)

    def test_opcode_shaped_data_is_not_misclassified(self):
        self.assertEqual(audit_native_resources([self.resource(b"DATA", 0, b"\x4e\x75\x4e\x56")], {}), [])

    def test_data_prefix_and_native_entry_are_parsed_without_binary_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "catalog.r"
            path.write_text("data 'INTL' (1) { $\"" + "00" * 330 + '" $$intl1Code() };', encoding="utf-8")
            data = read_rez(path)[0]["payload"]
            self.assertEqual(data, bytes(330) + intl1_localization_payload())
            path.write_text("data 'INTL' (0) { $\"" + "00" * 330 + '" $$intl1Code() };', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Native localization requires INTL 1"):
                read_rez(path)


if __name__ == "__main__":
    unittest.main()
