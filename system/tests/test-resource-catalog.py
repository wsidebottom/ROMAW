# +===================================================================
#   File:      test-resource-catalog.py
#
#   Summary:   Verify the shared catalog preserves fork identities and relocated sources.
#
#   Classes:   ResourceCatalogTests
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Verify the shared catalog preserves fork identities and relocated sources."""

from pathlib import Path
import sys
import hashlib
import struct
import tempfile
import shutil
import unittest
from unittest import mock
import re

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import resource_catalog as catalog


class ResourceCatalogTests(unittest.TestCase):
    def alert_records(self, entry):
        payload = entry["payload"]
        count = struct.unpack_from(">H", payload)[0]
        offset, records = 2, {}
        for _ in range(count):
            identity, length = struct.unpack_from(">hH", payload, offset)
            records[identity] = payload[offset + 4 : offset + 4 + length]
            offset += 4 + length
        self.assertEqual(offset, len(payload))
        self.assertEqual(len(records), count)
        return records

    def test_system_error_tables_keep_edited_alerts_and_native_procedures(self):
        entries = catalog.read_rez(ROOT / "system/resources/system/syserr.r")
        self.assertEqual([entry["id"] for entry in entries], [0, 2])
        tables = {entry["id"]: self.alert_records(entry) for entry in entries}
        self.assertEqual(struct.unpack(">5h", tables[0][32767]), (-20107, 0, -16397, -21103, -22001))
        self.assertEqual(tables[0][-23000], b"Quit \0")
        self.assertEqual(tables[2][-23000], b"Restart \0\0")
        self.assertNotIn(40, tables[0])
        self.assertEqual(struct.unpack(">6h", tables[2][20003]), (0, 0, -16401, -21109, 0, -24301))
        from mac_icons import resource

        self.assertEqual(tables[2][-16400][8:], resource((ROOT / "bitmaps/stop.icns").read_bytes(), b"ICON"))
        for records in tables.values():
            for identity, payload in records.items():
                if -22000 < identity <= -21000:
                    self.assertEqual(struct.unpack(">i", payload)[0], identity)
                if identity >= 0 or identity in (-10, -11, -13):
                    for reference in struct.unpack(">" + "h" * (len(payload) // 2), payload):
                        if reference:
                            self.assertIn(reference, records)

    def test_system_error_icon_reads_shared_artwork(self):
        source = ROOT / "system/resources/system/syserr.r"
        original = catalog.read_rez(source)[0]["payload"]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            shutil.copytree(ROOT / "bitmaps/syserr", directory / "syserr")
            shutil.copyfile(ROOT / "bitmaps/stop.icns", directory / "stop.icns")
            shutil.copyfile(ROOT / "bitmaps/bomb.icns", directory / "bomb.icns")
            from mac_icons import best_frame, decode, encode

            frames = decode((directory / "bomb.icns").read_bytes())
            icon = best_frame(frames, 32, 1)
            icon["image"] = bytes((icon["image"][0] ^ 128,)) + icon["image"][1:]
            (directory / "bomb.icns").write_bytes(encode(frames))
            with mock.patch.object(catalog, "ICONS", directory):
                changed = catalog.read_rez(source)[0]["payload"]
            differences = [index for index, pair in enumerate(zip(original, changed)) if pair[0] != pair[1]]
            self.assertEqual(len(differences), 1)
            self.assertEqual(original[differences[0]] ^ changed[differences[0]], 0x80)
        with mock.patch.object(catalog, "ICONS", directory), self.assertRaises(FileNotFoundError):
            catalog.read_rez(source)

    def test_system_error_fallback_rejects_bad_lengths_and_duplicate_ids(self):
        for source in (
            "resource 'DSAT' (0) { { 40, 8, Alert {50, 150, 31, 0, 151}; } };",
            "resource 'DSAT' (0) { { 1, 4, Procedure {0}; 1, 4, Procedure {0}; } };",
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                catalog.read_rez(self.fixture(source) / "catalog.r")

    def test_picture_declarations_preserve_frame_origins(self):
        for extension in ("png", "bmp"):
            directory = self.fixture(f"data 'PICT' (7, purgeable) {{ $$pict(\"PICT-7.{extension}\", -4, 12) }};")
            entry = catalog.read_rez(directory / "catalog.r")[0]
            self.assertEqual(entry["file"], f"PICT-7.{extension}")
            self.assertEqual(entry["picture_origin"], (-4, 12))
        for source in (
            "data 'TEST' (7) { $$pict(\"PICT-7.png\", 0, 0) };",
            "data 'PICT' (7) { $$pict(\"PICT-7.pict\", 0, 0) };",
            "data 'PICT' (7) { $$pict(\"PICT-7.png\", 32768, 0) };",
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                catalog.read_rez(self.fixture(source) / "catalog.r")
        with self.assertRaises(ValueError):
            catalog.resource_path(directory, "../../escape.png")

    def test_all_sources_resolve(self):
        total = 0
        for scope, count in (("system", 259), ("finder", 251)):
            manifest, directory = catalog.load_scope(
                catalog.FINDER_DIRECTORY if scope == "finder" else catalog.DIRECTORY, scope
            )
            self.assertEqual(len(manifest["resources"]), count)
            total += count
            for entry in manifest["resources"]:
                for name in entry.get("files", [entry.get("file")]):
                    self.assertTrue(catalog.resource_path(directory, name).is_file(), name)
        self.assertEqual(total, 510)
        for name, path in {
            "application_manifest": ROOT / "platform/os/cygwin32/ROMAW.manifest",
            "label_menu": ROOT / "finder/resources/label-menu.r",
        }.items():
            self.assertEqual(catalog.native_path(name), path)
            self.assertTrue(path.is_file())
        self.assertFalse((ROOT / "finder7/resources").exists())

    def test_finder_code_resources_have_source_only_replacements(self):
        manifest, directory = catalog.load_scope(catalog.FINDER_DIRECTORY, "finder")
        identities = {(bytes.fromhex(entry["type_hex"]), entry["id"]) for entry in manifest["resources"]}
        removed = {(b"CDEF", 128), (b"WDEF", 128), (b"gest", 128), (b"hcsl", 0)}
        self.assertTrue(removed <= identities)
        self.assertFalse(list((directory / "../guest").glob("*.bin")))
        for name in ("CDEF-128", "WDEF-128", "gest-128", "hcsl-0"):
            self.assertTrue((directory / "../guest" / (name + ".c")).is_file())

    def test_colliding_ids_remain_separate(self):
        system = catalog.load_scope(catalog.DIRECTORY, "system")[0]["resources"]
        finder = catalog.load_scope(catalog.FINDER_DIRECTORY, "finder")[0]["resources"]
        key = lambda entry: (entry["type_hex"], entry["id"])
        collisions = {key(entry) for entry in system} & {key(entry) for entry in finder}
        self.assertEqual(
            collisions,
            {
                ("43444546", 128),
                ("57444546", 128),
                ("67657374", 128),
                ("6863736c", 0),
            },
        )

    def fixture(self, source):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        (directory / "catalog.r").write_text(source, encoding="utf-8")
        return directory

    def test_scopes_have_independent_rez_roots(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for scope in ("system", "finder"):
                source = catalog.load_scope(
                    catalog.FINDER_DIRECTORY if scope == "finder" else catalog.DIRECTORY, scope
                )[1]
                shutil.copytree(source, directory / scope)
            before = (directory / "system/catalog.r").read_bytes()
            finder = directory / "finder/catalog.r"
            finder.write_text(finder.read_text() + "\ndata 'TEST' (1) { $\"12AB\" };\n")
            self.assertEqual(len(catalog.load_scope(directory, "finder")[0]["resources"]), 252)
            self.assertEqual((directory / "system/catalog.r").read_bytes(), before)
            self.assertEqual(len(catalog.load_scope(directory, "system")[0]["resources"]), 259)

    def test_names_comments_empty_and_nested_typed_bodies(self):
        directory = self.fixture(
            r"""// Header comment
            data 'TEST' (-32768, "a//b\0x8E\0x22\0x5C", sysheap, protected) { $"00 ff" $"01" };
            data 'ZERO' (32767, "") { $"" };
            resource 'fmnu' (12, purgeable) { { "a}b", 1; { 2; } }; };
        """
        )
        entries = catalog.read_rez(directory / "catalog.r")
        self.assertEqual(entries[0]["name_hex"], b'a//b\x8e"\\'.hex())
        self.assertEqual(entries[0]["attributes"], 72)
        self.assertEqual(entries[0]["payload"], b"\x00\xff\x01")
        self.assertEqual(entries[1]["payload"], b"")
        self.assertEqual(entries[1]["name_hex"], "")
        self.assertNotIn("payload", entries[2])

    def test_includes_and_asset_order(self):
        directory = self.fixture('#include "other.r"\n')
        (directory / "other.r").write_text(
            'data \'SICN\' (1) { $$read("first.icns") $$read("second.icns") };\n'
            "data 'CURS' (2) { $$read(\"arrow.cur\") };\n"
        )
        entries = catalog.read_rez(directory / "catalog.r")
        self.assertEqual(len(entries), 2)
        self.assertEqual(entries[0]["files"], ["first.icns", "second.icns"])
        self.assertTrue(entries[0]["icon"])
        self.assertNotIn("icon", entries[1])

    def test_duplicate_identity_across_includes(self):
        directory = self.fixture('#include "other.r"\n#include "other.r"\n')
        (directory / "other.r").write_text("data 'TEST' (1) { $\"AB\" };")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            catalog.read_rez(directory / "catalog.r")

    def test_typed_field_includes_preserve_headers_and_expand_relative_to_fragment(self):
        directory = self.fixture(
            'resource \'STR \' (1, "First", purgeable) {\n#include "fields/outer.rinc"\n};\n'
            'resource \'STR \' (2, "Second", locked) {\n#include "fields/outer.rinc"\n};\n'
        )
        (directory / "fields").mkdir()
        (directory / "fields/outer.rinc").write_text('/* Shared fields. */\n#include "inner.rinc"\n')
        (directory / "fields/inner.rinc").write_text('"#include \\"not-a-file\\""\n')
        entries = catalog.read_rez(directory / "catalog.r")
        self.assertEqual([e["id"] for e in entries], [1, 2])
        self.assertEqual([e["attributes"] for e in entries], [32, 16])
        self.assertEqual([e["name_hex"] for e in entries], [b"First".hex(), b"Second".hex()])
        self.assertEqual(entries[0]["payload"], b'\x15#include "not-a-file"')
        self.assertEqual(entries[0]["payload"], entries[1]["payload"])

    def test_typed_field_includes_reject_cycles_escape_missing_and_invalid_fields(self):
        directory = self.fixture("resource 'STR ' (1) {\n#include \"fields.rinc\"\n};\n")
        fragment = directory / "fields.rinc"
        with self.assertRaises(FileNotFoundError):
            catalog.read_rez(directory / "catalog.r")
        for fields, message in (
            ('#include "fields.rinc"\n', "Cyclic"),
            ('#include "catalog.r"\n', "Cyclic"),
            ('#include "../outside.rinc"\n', "escapes"),
            ('"valid" garbage\n', "trailing"),
        ):
            fragment.write_text(fields)
            with self.subTest(fields=fields), self.assertRaisesRegex(ValueError, message):
                catalog.read_rez(directory / "catalog.r")

    def test_macroman_asset_paths(self):
        directory = self.fixture("data 'ICON' (1) { $$read(\"ÃƒÆ’Ã¢â‚¬Â°toile.icns\") };")
        entry = catalog.read_rez(directory / "catalog.r")[0]
        self.assertEqual(entry["file"], "ÃƒÆ’Ã¢â‚¬Â°toile.icns")

    def test_maintained_dialogs_and_strings_use_typed_sources(self):
        for path in (catalog.DIRECTORY / "system/interface.r", catalog.FINDER_DIRECTORY / "text_resources.r"):
            source = path.read_text(encoding="utf-8")
            for kind in ("ALRT", "DLOG", "STR ", "STR#", "vers", "MACS"):
                self.assertNotIn("data '" + kind + "'", source)
        source = (catalog.DIRECTORY / "system/interface.r").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"data 'DITL'", source)), 0)
        for identifier in (-6044, -6043, -6042, -4063, -4062):
            self.assertIn(f"resource 'DITL' ({identifier}", source)
        self.assertNotIn("data 'vers'", (catalog.DIRECTORY / "system/payloads.r").read_text(encoding="utf-8"))

    def test_typed_native_label_menu(self):
        path = catalog.native_path("label_menu")
        menu = catalog.load_label_menu(path)
        self.assertEqual(menu["frame_width"], 143)
        self.assertEqual(menu["canvas_height"], 145)
        self.assertEqual(menu["sprite_y"], -8)
        self.assertEqual(menu["check_x"], 3)
        self.assertEqual(menu["icon_resource"], 301)
        self.assertEqual(menu["icon_color_index"], 255)
        self.assertEqual(menu["labels"][3], {"name": "In Progress", "rgb": [255, 0, 153]})
        self.assertEqual(len(menu["labels"]), 8)
        self.assertEqual(menu["check_bitmap"][0], "........#")
        self.assertEqual(len(menu["check_bitmap"]), 8)
        # The template is a source declaration, not a second resource instance.
        self.assertEqual(len(catalog.read_rez(path)), 1)

    def test_label_cicn_resources_share_one_artwork_source(self):
        manifest, directory = catalog.load_scope(catalog.FINDER_DIRECTORY, "finder")
        entries = [entry for entry in manifest["resources"] if entry["type"] == "cicn" and 301 <= entry["id"] <= 307]
        self.assertEqual({entry["id"] for entry in entries}, set(range(301, 308)))
        self.assertEqual(
            {catalog.resource_path(directory, entry["file"]).name for entry in entries}, {"label-menu.png"}
        )
        self.assertTrue(all(entry.get("icon") for entry in entries))
        for identifier in range(302, 308):
            self.assertFalse((catalog.ICONS / f"cicn-{identifier}.icns").exists())

    def test_label_menu_rejects_invalid_fields_and_counts(self):
        source = (catalog.native_path("label_menu")).read_text()
        cases = [
            source.replace("255, /* icon_color_index", "256, /* icon_color_index"),
            source.replace("301, /* icon_resource", "-32769, /* icon_resource"),
            source.replace("143,", "0,", 1),
            source.replace("136,", "256,", 1),
            source.replace('"Hot", 221, 0, 0;', '"Hot", 256, 0, 0;'),
            source.replace('"Hot", 221, 0, 0;', ""),
            source.replace('"Hot", 221, 0, 0;', '"Hot", 221, 0, 0; "Extra", 0, 0, 0;'),
            source.replace('"........#";', '".......#";'),
            source.replace('"........#";', '"........x";'),
            source.replace('"........#";', ""),
            source.replace("resource 'lMnu' (0)", "resource 'lMnu' (1)"),
            source + "\nresource 'STR ' (1) { \"extra\" };",
        ]
        for value in cases:
            with self.subTest(source=value):
                directory = self.fixture(value)
                with self.assertRaises(ValueError):
                    catalog.load_label_menu(directory / "catalog.r")

    def test_label_menu_bounds_on_literal_resource_data(self):
        source = catalog.read_rez(catalog.native_path("label_menu"))[0]["payload"]
        # Literal data takes the decoder path too: do not trust lengths because
        # the normal source happens to be a checked typed declaration.
        for size in range(len(source)):
            directory = self.fixture("data 'lMnu' (0) { $\"" + source[:size].hex() + '" };')
            with self.assertRaises(ValueError):
                catalog.load_label_menu(directory / "catalog.r")
        directory = self.fixture("data 'lMnu' (0) { $\"" + (source + b"x").hex() + '" };')
        with self.assertRaises(ValueError):
            catalog.load_label_menu(directory / "catalog.r")

    def test_typed_colour_table_payload(self):
        directory = self.fixture(
            "resource 'clut' (4) { 0x89ABCDEF, 0x8000, { " "0, 65535, 0, 0x1234; 7, 0, 0xABCD, 65535; } };"
        )
        payload = catalog.read_rez(directory / "catalog.r")[0]["payload"]
        self.assertEqual(payload.hex(), "89abcdef800000010000ffff0000123400070000abcdffff")
        directory = self.fixture("resource 'clut' (1) { 0, 0, {} };")
        self.assertEqual(catalog.read_rez(directory / "catalog.r")[0]["payload"].hex(), "000000000000ffff")
        for body in ("0, 0, { 0, 65536, 0, 0; }", "0, 0, { 0, 0, 0; }", "0, 65536, {}"):
            directory = self.fixture("resource 'clut' (1) { " + body + " };")
            with self.assertRaises(ValueError):
                catalog.read_rez(directory / "catalog.r")

    def test_typed_text_payloads(self):
        directory = self.fixture(
            r"""resource 'STR ' (1) { "A\0x8E" "B" };
            resource 'STR#' (2) { { ""; "one"; "two"; } };
            resource 'vers' (1) { 7, 0x55, final, 0, verUS, "7.5.5", "System" };
            resource 'MACS' (0) { "Finder" };
        """
        )
        entries = catalog.read_rez(directory / "catalog.r")
        self.assertEqual(entries[0]["payload"], b"\x03A\x8eB")
        self.assertEqual(entries[1]["payload"], b"\x00\x03\x00\x03one\x03two")
        self.assertEqual(entries[2]["payload"], bytes.fromhex("075580000000") + b"\x057.5.5\x06System")
        self.assertEqual(entries[3]["payload"], b"\x06Finder")

    def test_typed_dialog_payloads(self):
        directory = self.fixture(
            """resource 'ALRT' (1) {
                { 1, 2, 3, 4 }, -1,
                { OK, visible, silent; Cancel, invisible, sound1;
                  OK, visible, sound2; Cancel, visible, sound3; }, centerMainScreen
            };
            resource 'DLOG' (1) { { 1, 2, 3, 4 }, dBoxProc, visible, goAway, -1, -2, "Hi", noAutoCenter };
            resource 'DLOG' (2) { { 1, 2, 3, 4 }, dBoxProc, invisible, noGoAway, 0, -2, "" };
            resource 'DITL' (1) { {
                { 1, 2, 3, 4 }, Button { enabled, "OK" };
                { -1, -2, 3, 4 }, Icon { disabled, -3 };
                { 0, 0, 0, 0 }, UserItem { enabled };
            } };
        """
        )
        entries = catalog.read_rez(directory / "catalog.r")
        self.assertEqual(entries[0]["payload"].hex(), "0001000200030004ffff496f280a")
        self.assertEqual(
            entries[1]["payload"].hex(), "0001000200030004000101000100ffffffff fffe 02486900 0000".replace(" ", "")
        )
        self.assertEqual(entries[2]["payload"].hex(), "000100020003000400010000000000000000fffe00")
        self.assertEqual(
            entries[3]["payload"].hex(),
            (
                "0002 00000000 0001000200030004 04024f4b "
                "00000000 fffffffe00030004 a002fffd "
                "00000000 0000000000000000 0000"
            ).replace(" ", ""),
        )

    def test_typed_layout_rejects_malformed_fields(self):
        cases = [
            "resource 'STR ' (1) { 12 };",
            "resource 'STR ' (1) { \"" + "x" * 256 + '" };',
            'resource \'STR#\' (1) { { "a", "b" } };',
            "resource 'STR ' (1) { \"ok\" garbage };",
            'resource \'vers\' (1) { 256, 0, final, 0, 0, "", "" };',
            "resource 'DLOG' (1) { { 0, 0, 0, 32768 }, 0, visible, noGoAway, 0, 1, \"\" };",
            "resource 'DITL' (1) { { { 0, 0, 1, 1 }, Icon { enabled, 32768 }; } };",
            "resource 'DITL' (1) { { { 0, 0, 1, 1 }, UserItem { enabled, 1 }; } };",
        ]
        for source in cases:
            with self.subTest(source=source):
                directory = self.fixture(source)
                with self.assertRaises(ValueError):
                    catalog.read_rez(directory / "catalog.r")

    def test_invalid_declarations_and_includes(self):
        cases = [
            "data 'TEST' (1) { $\"00\" }; data 'TEST' (1) { $\"01\" };",
            "data 'TEST' (32768) { $\"00\" };",
            "data 'TEST' (1, preload, preload) { $\"00\" };",
            "data 'TEST' (1, unknown) { $\"00\" };",
            "data 'TEST' (1) { $\"0\" };",
            "data 'TEST' (1) { $\"00\" }",
            "data 'TEST' (1) { $\"00\" ;",
            'data \'TEST\' (1) { $$read("a.icns") $$read("b.icns") };',
            '#include "../outside.r"',
            '#include "catalog.r"',
            "ignored garbage",
        ]
        for source in cases:
            with self.subTest(source=source):
                directory = self.fixture(source)
                with self.assertRaises(ValueError):
                    catalog.read_rez(directory / "catalog.r")
        with self.assertRaises(ValueError):
            catalog.resource_path(catalog.DIRECTORY / "finder", "../../outside.bin")
        with self.assertRaises(ValueError):
            catalog.resource_path(catalog.DIRECTORY / "finder", "bitmaps/../../outside.icns")


if __name__ == "__main__":
    unittest.main()
