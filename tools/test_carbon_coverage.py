# +===================================================================
#   File:      test_carbon_coverage.py
#
#   Summary:   Synthetic binding audits: no Apple reference files are needed.
#
#   Classes:   CoverageTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Synthetic binding audits: no Apple reference files are needed."""

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("carbon_coverage", Path(__file__).with_name("carbon-coverage.py"))
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


class CoverageTests(unittest.TestCase):
    def test_trace_is_evidence_of_calls_only(self):
        trace = "trace: PPC service CarbonLib:Fallback r3=0\ntrace: PPC service InterfaceLib:Other r3=0\n"
        self.assertEqual(coverage.trace_symbols(trace), {"Fallback"})

    def test_multiline_native_entries_and_duplicates(self):
        source = '{"Native", function,\n (1 | (5 << 3)), 0x1234},'
        self.assertEqual(coverage.native_entries(source), {"Native": {"offset": 0x1234, "library": None}})
        for invalid in ("", source + source):
            with self.assertRaises(ValueError):
                coverage.native_entries(invalid)

    def test_formatted_corefoundation_macro_invocations(self):
        source = (
            "#define CARBON_CF_KEY(name, index, slot, literal) CARBON_CF_SINGLETON(name, index, slot)\n"
            '    CARBON_CF_SINGLETON("First", 0xd6, 0x3160) CARBON_CF_SINGLETON(\n'
            '        "Second", 0x39, 0x3168)\n'
            "    CARBON_CF_KEY_ALIAS(\n"
            '        "Third", 0x14c, 0x316c, 0x1)\n'
        )
        rows = coverage.native_entries(source)
        self.assertEqual(set(rows), {"First", "Second", "Third"})
        self.assertEqual(rows["Second"]["data_offset"], 0x3168)
        self.assertEqual(rows["Third"]["library"], "CoreFoundationLib")
        with self.assertRaisesRegex(ValueError, "Unparsed CoreFoundation singleton"):
            coverage.native_entries(source.replace("0x3168", "invalid"))

    def test_direct_ppc_entries_have_explicit_null_library(self):
        source = '{"LMGetMemTop", function, 4, 0xa7250, NULL, ROMLIB_CARBON_PPC_ONLY},'
        self.assertEqual(coverage.native_entries(source), {"LMGetMemTop": {"offset": 0xA7250, "library": None}})
        for flag in ("UNKNOWN_FLAG", "ROMLIB_CARBON_COMPANION_ONLY", "NULL"):
            with self.assertRaises(ValueError):
                coverage.native_entries(source.replace("ROMLIB_CARBON_PPC_ONLY", flag))

    def test_nqd_macro_binds_only_the_companion_export(self):
        source = (
            "NQD_TOOLBOX(NQDNewRgn, 0x0d8, 0x30, uint32_t, 0x1234)\n"
            "NQD_NATIVE(NQDSetPt, carbon_SetPt, 0x5678)\n"
            "NQD_OFFSCREEN(NQDLockPixels, 0x3d8, 0x40001, uint8_t, 0x9abc)"
        )
        native = coverage.native_entries(source)
        reference = {"exports": []}
        companions = {
            "NQD": {
                "exports": [
                    {"name": "NQDNewRgn", "symbol_class": 2, "code_offset": 0x1234},
                    {"name": "NQDSetPt", "symbol_class": 2, "code_offset": 0x5678},
                    {"name": "NQDLockPixels", "symbol_class": 2, "code_offset": 0x9ABC},
                ]
            }
        }
        report = coverage.audit(reference, native, companions=companions)
        self.assertEqual(report["native_archive_exports"], 0)
        self.assertEqual(report["libraries"]["NQD"]["native_archive_exports"], 3)
        with self.assertRaisesRegex(ValueError, "Unparsed NQD Toolbox binding"):
            coverage.native_entries(source.replace("0x1234", "offset"))
        with self.assertRaisesRegex(ValueError, "Unparsed NQD native binding"):
            coverage.native_entries(source.replace("0x5678", "offset"))

    def test_extension_satisfies_import_without_counting_as_reference_coverage(self):
        source = '{"Optional", fn, 4, 0xffffffff, NULL, ROMLIB_CARBON_PPC_ONLY | ROMLIB_CARBON_EXTENSION},'
        native = coverage.native_entries(source)
        imports = [{"name": "Optional", "symbol_class": 2, "weak": True}]
        reference = {"exports": [{"name": "Original", "symbol_class": 2, "code_offset": 0x1234}]}
        report = coverage.audit(reference, native, imports)
        self.assertEqual(report["native_archive_exports"], 0)
        self.assertEqual(report["native_companion_only_exports"], 0)
        self.assertEqual(report["native_extension_exports"], ["Optional"])
        self.assertEqual(report["pending_reference_exports"], 1)
        self.assertEqual(report["application_native_imports"], 1)
        self.assertEqual(report["application_pending_weak"], 0)
        self.assertFalse(report["application"][0]["in_reference"])
        for invalid in (source.replace("0xffffffff", "0x4321"), source.replace("NULL", '"CoreFoundationLib"')):
            with self.assertRaises(ValueError):
                coverage.native_entries(invalid)
        reference["exports"][0]["name"] = "Optional"
        with self.assertRaises(ValueError):
            coverage.audit(reference, native, imports)

    def test_audit_distinguishes_data_weak_and_missing_reference(self):
        reference = {
            "exports": [
                {"name": "Native", "symbol_class": 2, "code_offset": 0x1234},
                {"name": "Fallback", "symbol_class": 2},
                {"name": "Data", "symbol_class": 1},
            ]
        }
        imports = [
            {"name": name, "symbol_class": kind, "weak": weak}
            for name, kind, weak in [
                ("Native", 2, False),
                ("Fallback", 2, False),
                ("Data", 1, False),
                ("NewerOptional", 2, True),
            ]
        ]
        result = coverage.audit(
            reference, {"Native": {"offset": 0x1234, "library": None}}, imports, {"Fallback", "Data"}
        )
        self.assertEqual(result["native_archive_exports"], 1)
        self.assertEqual(result["pending_reference_exports"], 2)
        self.assertEqual(result["application_native_imports"], 1)
        self.assertEqual(result["application_pending_strong"], 2)
        self.assertEqual(result["application_pending_weak"], 1)
        self.assertEqual(result["application_other_native_calls_observed"], 1)
        self.assertFalse(result["claims_full_compatibility"])
        self.assertFalse(result["application"][2]["in_reference"])
        for bad in ({"Unknown": 0}, {"Native": 0}, {"Data": 0}):
            with self.assertRaises(ValueError):
                coverage.audit(reference, {name: {"offset": offset, "library": None} for name, offset in bad.items()})

    def test_reexports_require_matching_import_identity(self):
        flagged = '{"CFArrayApplyFunction", fn, (4 << 3), 0x1c1, "CoreFoundationLib", ROMLIB_CARBON_PPC_ONLY},'
        self.assertEqual(
            coverage.native_entries(flagged),
            {"CFArrayApplyFunction": {"offset": 0x1C1, "library": "CoreFoundationLib"}},
        )
        with self.assertRaises(ValueError):
            coverage.native_entries(flagged.replace("ROMLIB_CARBON_PPC_ONLY", "UNKNOWN_FLAG"))
        with self.assertRaises(ValueError):
            coverage.native_entries(
                '{"Native", fn, 4, 0x1234},' + flagged.replace("ROMLIB_CARBON_PPC_ONLY", "UNKNOWN_FLAG")
            )
        native = coverage.native_entries('{"PLstrcmp", function, 0x1234, 0xbf7, "StdCLib"},')
        reference = {
            "exports": [{"name": "PLstrcmp", "symbol_class": 2, "section": -3, "offset": 0xBF7}],
            "imports": [{"index": 0xBF7, "name": "PLstrcmp", "library": "StdCLib", "symbol_class": 2}],
        }
        result = coverage.audit(reference, native)
        self.assertEqual(result["native_reexport_exports"], 1)
        self.assertEqual(result["native_code_exports"], 0)
        self.assertEqual(result["unverified_companion_libraries"], ["StdCLib"])
        for field, wrong in (("library", "Other"), ("name", "Other"), ("index", 0), ("symbol_class", 1)):
            original = reference["imports"][0][field]
            reference["imports"][0][field] = wrong
            with self.assertRaises(ValueError):
                coverage.audit(reference, native)
            reference["imports"][0][field] = original
        reference["exports"][0]["section"] = 1
        with self.assertRaises(ValueError):
            coverage.audit(reference, native)

    def test_unbound_reexport_provider_requires_companion_audit(self):
        reference = {
            "exports": [{"name": "Fallback", "symbol_class": 2, "section": -3, "offset": 7}],
            "imports": [{"index": 7, "name": "Fallback", "symbol_class": 2, "library": "CompanionLib"}],
        }
        result = coverage.audit(reference, {})
        self.assertEqual(result["unverified_companion_libraries"], ["CompanionLib"])
        self.assertEqual(result["pending_reference_exports"], 1)
        companion = {"exports": [{"name": "Fallback", "symbol_class": 2, "code_offset": 32}]}
        result = coverage.audit(reference, {}, companions={"CompanionLib": companion})
        self.assertEqual(result["unverified_companion_libraries"], [])
        self.assertEqual(result["libraries"]["CompanionLib"]["native_archive_exports"], 0)
        self.assertEqual(result["libraries"]["CompanionLib"]["pending_reference_exports"], 1)
        reference["imports"].clear()
        with self.assertRaisesRegex(ValueError, "Missing reference re-export origin: Fallback"):
            coverage.audit(reference, {})

    def test_reexport_missing_from_saved_companion_is_reported(self):
        native = {"NewExport": {"library": "CompanionLib", "offset": 7}}
        reference = {
            "exports": [{"name": "NewExport", "symbol_class": 2, "section": -3, "offset": 7}],
            "imports": [{"name": "NewExport", "symbol_class": 2, "library": "CompanionLib", "index": 7}],
        }
        companion = {"exports": [{"name": "OlderExport", "symbol_class": 2, "code_offset": 16}]}
        result = coverage.audit(reference, native, companions={"CompanionLib": companion})
        self.assertEqual(result["native_archive_exports"], 1)
        self.assertEqual(result["libraries"]["CompanionLib"]["native_archive_exports"], 0)
        self.assertEqual(result["libraries"]["CompanionLib"]["pending_reference_exports"], 1)
        self.assertEqual(result["unverified_companion_reexports"], [{"library": "CompanionLib", "name": "NewExport"}])

    def test_data_reexports_require_data_in_both_libraries(self):
        source = '    CARBON_DATA("Epoch", 0xc9, "CoreFoundationLib", 0x41cd27e4, 0x00000000),'
        native = coverage.native_entries(source)
        imports = [{"name": "Epoch", "symbol_class": 1, "weak": False}]
        original = {"name": "Epoch", "symbol_class": 1}
        reference = {
            "exports": [dict(original, section=-3, offset=0xC9)],
            "imports": [dict(original, index=0xC9, library="CoreFoundationLib")],
        }
        companions = {"CoreFoundationLib": {"exports": [original]}}
        report = coverage.audit(reference, native, imports, companions=companions)
        self.assertEqual(report["native_data_exports"], 1)
        self.assertEqual(report["application_native_imports"], 1)
        self.assertEqual(report["application_other_native_calls_observed"], 0)
        for row in (reference["exports"][0], reference["imports"][0], original):
            row["symbol_class"] = 2
            with self.assertRaises(ValueError):
                coverage.audit(reference, native, imports, companions=companions)
            row["symbol_class"] = 1
        with self.assertRaises(ValueError):
            coverage.native_entries(source + "\n" + source)
        with self.assertRaises(ValueError):
            coverage.native_entries(source.replace("0x00000000", "numeric_word"))

    def test_callback_reexports_are_data_records(self):
        source = (
            '    CARBON_CALLBACK_DATA("Callbacks", 0xe3, "CoreFoundationLib", '
            "ROMLIB_CARBON_DATA_TYPE_CALLBACKS, 0x3234),"
        )
        self.assertEqual(
            coverage.native_entries(source),
            {"Callbacks": {"offset": 0xE3, "library": "CoreFoundationLib", "data": True, "data_offset": 0x3234}},
        )
        with self.assertRaises(ValueError):
            coverage.native_entries(source.replace("TYPE_CALLBACKS", "UNKNOWN"))

    def test_shared_corefoundation_key_slots(self):
        source = 'CARBON_CF_KEY("Key", 0x100, 0x200, "Key text")\nCARBON_CF_KEY_ALIAS("Alias", 0x101, 0x204, 0x200)'
        native = coverage.native_entries(source)
        self.assertEqual(
            native["Key"], {"offset": 0x100, "data_offset": 0x200, "data": True, "library": "CoreFoundationLib"}
        )
        self.assertEqual(
            native["Alias"], {"offset": 0x101, "data_offset": 0x204, "data": True, "library": "CoreFoundationLib"}
        )
        with self.assertRaises(ValueError):
            coverage.native_entries(source.replace('"Key text"', "invalid"))

    def test_system_allocator_reexport_checks_the_shared_data_offset(self):
        native = coverage.native_entries(
            '    CARBON_SYSTEM_ALLOCATOR_DATA("Allocator", 0x15d, "CoreFoundationLib", 0x3164),'
        )
        reference = {
            "exports": [{"name": "Allocator", "section": -3, "symbol_class": 1, "offset": 0x15D}],
            "imports": [{"name": "Allocator", "index": 0x15D, "library": "CoreFoundationLib", "symbol_class": 1}],
        }
        row = {"name": "Allocator", "section": 1, "symbol_class": 1, "offset": 0x3164}
        companions = {"CoreFoundationLib": {"exports": [row]}}
        result = coverage.audit(reference, native, companions=companions)
        self.assertEqual(result["native_data_exports"], 1)
        row["offset"] += 4
        with self.assertRaises(ValueError):
            coverage.audit(reference, native, companions=companions)

    def test_companion_only_exports_keep_distinct_scope_and_require_metadata(self):
        source = (
            '{"Private", fn, 4, 0x461fc, "CoreFoundationLib", ROMLIB_CARBON_PPC_ONLY | ROMLIB_CARBON_COMPANION_ONLY},'
        )
        native = coverage.native_entries(source)
        self.assertTrue(native["CoreFoundationLib", "Private"]["companion_only"])
        reference = {"exports": [{"name": "Private", "symbol_class": 2, "code_offset": 99}]}
        companion = {
            "exports": [
                {"name": "Private", "symbol_class": 2, "code_offset": 0x461FC},
                {"name": "Data", "symbol_class": 1},
            ]
        }
        imports = [{"name": "Private", "symbol_class": 2, "weak": False}]
        result = coverage.audit(reference, native, imports, companions={"CoreFoundationLib": companion})
        self.assertEqual(result["native_archive_exports"], 0)
        self.assertEqual(result["native_archive_total_exports"], 1)
        self.assertEqual(result["native_companion_only_exports"], 1)
        self.assertEqual(result["pending_reference_exports"], 1)
        self.assertEqual(result["application_native_imports"], 0)
        self.assertEqual(result["unverified_companion_libraries"], [])
        self.assertEqual(
            result["libraries"]["CoreFoundationLib"],
            {"reference_exports": 2, "native_archive_exports": 1, "pending_reference_exports": 1},
        )
        both = coverage.native_entries(source + '{"Private", main_fn, 4, 0x63},')
        both_result = coverage.audit(reference, both, imports, companions={"CoreFoundationLib": companion})
        self.assertEqual(both_result["native_archive_exports"], 1)
        self.assertEqual(both_result["native_archive_total_exports"], 2)
        self.assertEqual(both_result["native_companion_only_exports"], 1)
        self.assertEqual(both_result["application_native_imports"], 1)
        with self.assertRaisesRegex(ValueError, "Duplicate native binding"):
            coverage.native_entries(source + '{"Private", main_fn, 4, 0x63, "CoreFoundationLib"},')
        with self.assertRaises(ValueError):
            coverage.audit(reference, native)
        for field, value in (("symbol_class", 1), ("code_offset", 0)):
            old = companion["exports"][0][field]
            companion["exports"][0][field] = value
            with self.assertRaises(ValueError):
                coverage.audit(reference, native, companions={"CoreFoundationLib": companion})
            companion["exports"][0][field] = old
        with self.assertRaises(ValueError):
            coverage.native_entries('{"Private", fn, 4, 0x461fc, ROMLIB_CARBON_COMPANION_ONLY},')


if __name__ == "__main__":
    unittest.main()
