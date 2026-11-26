# +===================================================================
#   File:      test-system-resources.py
#
#   Summary:   Every extracted System payload, name and attribute survives rebuilding.
#
#   Classes:   SystemResourcesTest
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Every extracted System payload, name and attribute survives rebuilding."""

import importlib.util
from unittest.mock import patch
import struct
import shlex
import sys
import tempfile
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from mac_resources import ResourceFork

spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
SOURCE = ROOT / "system/resources/system"


class SystemResourcesTest(unittest.TestCase):
    def test_icon_dependencies_preserve_spaces_and_special_characters(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("Ram Disk.icns", "dollar$.cur", "hash#.icns"):
                (root / name).touch()
            makefile = root / "Makefile"
            command = shlex.join(
                [sys.executable, str(ROOT / "tools/romaw/system_resources.py"), "--icon-inputs", str(root)]
            )
            makefile.write_text(
                "inputs = $(shell " + command + ")\nall: $(inputs)\n\t@echo dependencies-ok\n",
                encoding="utf-8",
            )
            result = subprocess.run(["make", "-f", str(makefile)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("dependencies-ok", result.stdout)

    def test_alert_icons_include_their_transparency_masks(self):
        fork = ResourceFork(builder.build_fork(builder.read_resources(SOURCE)))
        for rid, name in [(0, "stop.icns"), (1, "message.icns"), (2, "warning.icns")]:
            data = (ROOT / "bitmaps" / name).read_bytes()
            payload = fork.records[b"cicn", rid][0]
            self.assertEqual(payload, builder.mac_icons.color_icon(data))
            mask = payload[82:210]
            if rid == 2:
                self.assertTrue(any(byte != 255 for byte in mask))
            self.assertTrue(any(mask))
            self.assertEqual(len(fork.records[b"ICON", rid][0]), 128)

    def test_all_resources(self):
        records = builder.read_resources(SOURCE)
        fork = ResourceFork(builder.build_fork(records))
        self.assertEqual(len(records), 259)
        self.assertEqual(len(fork.records), len(records))
        catalog = builder.load_scope(SOURCE)[0]
        self.assertEqual(len(catalog["resources"]), len(records))
        for kind, rid, attrs, name, payload in records:
            actual, flags, reference = fork.records[kind, rid]
            self.assertEqual(actual, payload)
            self.assertEqual(flags, attrs & ~2)
            noff = struct.unpack_from(">H", fork.map, reference + 2)[0]
            if name is None:
                self.assertEqual(noff, 65535)
            else:
                offset = struct.unpack_from(">H", fork.map, 26)[0] + noff
                self.assertEqual(fork.map[offset + 1 : offset + 1 + fork.map[offset]], name)
        self.assertEqual(fork.map_offset + len(fork.map), len(fork.data))

    def test_classic_cursors_keep_hotspots(self):
        fork = ResourceFork(builder.build_fork(builder.read_resources(SOURCE)))
        self.assertEqual({rid for kind, rid in fork.records if kind == b"CURS"}, {1, 2, 3, 4})
        for rid, filename in enumerate(("curs_beam.cur", "curs_cross.cur", "curs_plus.cur", "curs_watch_1.cur"), 1):
            artwork = (ROOT / "bitmaps" / filename).read_bytes()
            payload = fork.get(b"CURS", rid)
            self.assertEqual(payload, builder.mac_icons.windows_cursor(artwork))
            self.assertTrue(all(0 <= point < 16 for point in struct.unpack(">hh", payload[64:])))
            x, y = struct.unpack_from("<HH", artwork, 10)
            self.assertEqual(payload[64:], struct.pack(">hh", y, x))

    def test_windows_cursor_pixel_modes_and_bounds(self):
        cursor = bytearray((ROOT / "bitmaps/curs_plus.cur").read_bytes())
        # First row: opaque black, opaque white, transparent and inverse.
        cursor[22 + 48 + 15 * 4] = 0x50
        cursor[22 + 112 + 15 * 4] = 0x30
        payload = builder.mac_icons.windows_cursor(cursor)
        self.assertEqual(payload[0] & 0xF0, 0x90)
        self.assertEqual(payload[32] & 0xF0, 0xC0)
        for size in range(len(cursor)):
            with self.assertRaises(ValueError):
                builder.mac_icons.windows_cursor(cursor[:size])
        struct.pack_into("<H", cursor, 10, 16)
        with self.assertRaisesRegex(ValueError, "entry"):
            builder.mac_icons.windows_cursor(cursor)

    def test_default_drive_icon_family(self):
        fork = ResourceFork(builder.build_fork(builder.read_resources(SOURCE)))
        for icon_id, filename in (
            (-3995, "-3995_genericHardDiskIconResource.icns"),
            (-3972, "-3972_FileServerIconResource.icns"),
            (-16455, "ROMAW_volume.icns"),
            (-16482, "mac.icns"),
        ):
            artwork = (ROOT / "bitmaps" / filename).read_bytes()
            for kind in (b"ICN#", b"icl4", b"icl8", b"ics#", b"ics4", b"ics8"):
                payload, _, _ = fork.records[kind, icon_id]
                self.assertEqual(payload, builder.mac_icons.resource(artwork, kind))
        self.assertNotEqual(fork.get(b"ICN#", -3995), fork.get(b"ICN#", -4000))

    def test_fonts_and_decompiled_dispatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "system.inc"
            builder.generate(SOURCE, output)
            embedded = bytes.fromhex(
                output.read_text().split("{", 1)[1].split("}", 1)[0].replace("0x", "").replace(",", " ")
            )
            self.assertTrue(builder.FONT_TYPES.isdisjoint(kind for kind, _ in ResourceFork(embedded).records))
            fonts = Path(temporary) / "fonts.rsrc"
            subprocess.run(
                [
                    sys.executable,
                    ROOT / "tools/romaw/font_suitcase.py",
                    ROOT / "fonts",
                    fonts,
                ],
                check=True,
            )
            resource = ResourceFork(fonts.read_bytes())
            self.assertEqual({key[0] for key in resource.records}, {b"NFNT", b"FOND", b"sfnt"})
            self.assertEqual(len(resource.records), 73)
            self.assertEqual(resource.get(b"sfnt", 0), (ROOT / "fonts/Chicago.ttf").read_bytes())
            self.assertEqual(
                {rid for kind, rid in resource.records if kind == b"FOND"},
                {0, 3, 20, 21, 22, 16384},
            )
            for (kind, rid), (payload, _, _) in resource.records.items():
                if kind == b"FOND":
                    count = struct.unpack_from(">H", payload, 52)[0] + 1
                    for i in range(count):
                        _, _, strike = struct.unpack_from(">hhh", payload, 54 + i * 6)
                        self.assertIn((b"NFNT", strike), resource.records)
                elif kind == b"NFNT":
                    first, last = struct.unpack_from(">HH", payload, 2)
                    ow_offset = struct.unpack_from(">H", payload, 16)[0]
                    self.assertEqual(len(payload), 16 + 2 * (ow_offset + last - first + 3))
            dispatch = output.with_name("system_code.inc").read_text()
            for slot, target in builder.CODE_HANDLERS.values():
                self.assertIn(f"({slot}, {target})", dispatch)
            self.assertEqual(dispatch.count("ROMLIB_SYSTEM_DEFINITION"), 16)

    def test_executable_change_requires_review(self):
        with tempfile.TemporaryDirectory() as temporary:
            records = builder.read_resources(SOURCE)
            records = [(k, i, a, n, b"\x4e\x75" if (k, i) == (b"CDEF", 0) else p) for k, i, a, n, p in records]
            with patch.object(builder, "read_resources", return_value=records):
                with self.assertRaisesRegex(ValueError, "Unexpected executable"):
                    builder.generate(SOURCE, Path(temporary) / "output.inc")


if __name__ == "__main__":
    unittest.main()
