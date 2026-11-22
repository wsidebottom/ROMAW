# +===================================================================
#   File:      test-edited-icons.py
#
#   Summary:   Edited ICNS files remain usable as classic System and shared Finder icons.
#
#   Classes:   EditedIconsTest
#
#   Functions: png_icns
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Edited ICNS files remain usable as classic System and shared Finder icons."""

import importlib.util
import json
import re
import sys
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mac_icons


def png_icns(rows):
    payload = mac_icons.png(rows)
    size = len(rows)
    if size not in mac_icons.PNG_TYPES:
        return mac_icons.encode([mac_icons.decode_png(payload, size, size)])
    return mac_icons.icns_family([mac_icons.icns_chunk(mac_icons.PNG_TYPES[size], payload)])


class EditedIconsTest(unittest.TestCase):
    def test_label_png_preserves_pixels_when_converted_to_rez_cicon(self):
        data = (ROOT / "bitmaps/label-menu.png").read_bytes()
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        source = mac_icons.decode(data)[0]
        self.assertEqual((source["size"], source["height"]), (32, 32))
        target = mac_icons.color_icon_frame(mac_icons.resource(data, b"cicn"))
        # Transparent CIcon pixels use palette index zero; compare visible RGB
        # and every alpha value rather than hidden transparent RGB channels.
        visible = lambda frame: [pixel if pixel[3] else (0, 0, 0, 0) for row in mac_icons.rgba(frame) for pixel in row]
        self.assertEqual(visible(target), visible(source))
        self.assertFalse((ROOT / "bitmaps/cicn-301.icns").exists())

    def test_standalone_png_rejects_invalid_dimensions(self):
        data = (ROOT / "bitmaps/label-menu.png").read_bytes()
        for invalid in (
            data[:20],
            data[:16] + struct.pack(">2I", 0, 32) + data[24:],
            data[:16] + struct.pack(">2I", 1025, 32) + data[24:],
        ):
            with self.assertRaises(ValueError):
                mac_icons.decode(invalid)

    def test_small_lock_resources_do_not_expose_transparent_icns_bits(self):
        for name in ("lock_solid.icns", "lock_grey.icns"):
            data = (ROOT / "bitmaps" / name).read_bytes()
            family = mac_icons.resource(data, b"ics#")
            expected = bytes(pixel & mask for pixel, mask in zip(family[:32], family[32:]))
            self.assertEqual(mac_icons.resource(data, b"SICN"), expected)

    def test_label_generator_reuses_one_rez_icon_for_all_menu_colors(self):
        from resource_catalog import load_label_menu, native_path

        menu = load_label_menu(native_path("label_menu"))
        source = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps/label-menu.png").read_bytes()), 32)
        opacity = bytes(pixel[3] for row in mac_icons.rgba(source) for pixel in row)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools/romaw/native_finder/build.py"),
                    "--output-dir",
                    str(output),
                    "--generate-only",
                ],
                check=True,
                capture_output=True,
            )
            art = (output / "label_art.inc").read_text()
            for i in range(1, 8):
                values = re.search(rf"label_{i}_rgba\[\]=\{{([^}}]+)\}}", art)[1]
                pixels = bytes(map(int, values.split(",")))
                self.assertEqual(pixels[3::4], opacity)
                self.assertEqual(
                    {tuple(pixels[j : j + 3]) for j in range(0, len(pixels), 4) if pixels[j + 3]},
                    {tuple(menu["labels"][i]["rgb"])},
                )
            assets = (output / "assets.inc").read_text()
            self.assertEqual(assets.count("{asset_5,sizeof asset_5}"), 7)

    def test_label_tint_changes_only_one_color_spec(self):
        source = (ROOT / "bitmaps/label-menu.png").read_bytes()
        base = mac_icons.color_icon(source)
        frame = mac_icons.color_icon_frame(base)
        menu = __import__("resource_catalog").load_label_menu(ROOT / "finder/resources/label-menu.r")
        for label in menu["labels"][1:]:
            tinted = mac_icons.tint_color_icon(base, menu["icon_color_index"], label["rgb"])
            target = mac_icons.color_icon_frame(tinted)
            self.assertEqual(target["image"], frame["image"])
            self.assertEqual(target["opacity"], frame["opacity"])
            for index, color in enumerate(frame["palette"]):
                self.assertEqual(target["palette"][index], tuple(label["rgb"]) if index == 255 else color)
            # Black source pixels must tint too; RGB-based replacement missed them.
            self.assertEqual(
                {pixel[:3] for row in mac_icons.rgba(target) for pixel in row if pixel[3]}, {tuple(label["rgb"])}
            )
            changed = [index for index, (a, b) in enumerate(zip(base, tinted)) if a != b]
            slot = 82 + 128 + 128 + 8 + 255 * 8 + 2
            self.assertTrue(all(slot <= index < slot + 6 for index in changed))
        self.assertEqual(base, mac_icons.color_icon(source))

    def test_color_icon_reader_and_tint_reject_malformed_data(self):
        base = mac_icons.color_icon((ROOT / "bitmaps/label-menu.png").read_bytes())
        for length in range(len(base)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                mac_icons.color_icon_frame(base[:length])
        for index, rgb in ((256, (0, 0, 0)), (-1, (0, 0, 0)), (255, (256, 0, 0)), (255, (0, 0))):
            with self.subTest(index=index, rgb=rgb), self.assertRaises(ValueError):
                mac_icons.tint_color_icon(base, index, rgb)

    def test_down_disclosure_icon_rotates_arrow_open_clockwise(self):
        source = mac_icons.decode((ROOT / "bitmaps/arrow_open.icns").read_bytes())
        target = mac_icons.decode((ROOT / "bitmaps/3061.icns").read_bytes())
        self.assertEqual(
            [(f["size"], f["height"], f["depth"]) for f in target],
            [(f["size"], f["height"], f["depth"]) for f in source],
        )
        for original, rotated in zip(source, target):
            self.assertEqual(rotated["palette"], original["palette"])
            self.assertEqual(mac_icons.rgba(rotated), mac_icons.scroll_arrow(mac_icons.rgba(original), "down"))

    def test_huge_members_preserve_source_and_share_the_48_pixel_mask(self):
        rows = [[(255, 0, 0, 255)] * 16 for _ in range(16)]
        rows[0][0] = (0, 0, 0, 0)
        original = png_icns(rows)
        completed = mac_icons.complete_huge(original)
        members = dict(mac_icons.icns_members(completed))
        for kind, payload in mac_icons.icns_members(original):
            self.assertEqual(members[kind], payload)
        for kind, length in ((b"ich#", 576), (b"ich4", 1152), (b"ich8", 2304)):
            self.assertEqual(len(members[kind]), length)
            self.assertEqual(members[kind], mac_icons.resource(original, kind))
        frames = mac_icons.decode(completed)
        huge = [frame for frame in frames if frame["size"] == 48]
        self.assertEqual({frame["depth"] for frame in huge}, {1, 4, 8})
        for frame in huge:
            self.assertEqual(frame["opacity"], members[b"ich#"][288:])
            self.assertEqual(mac_icons.rgba(frame)[0][0][3], 0)
            self.assertEqual(mac_icons.rgba(frame)[0][3][3], 255)
        self.assertEqual(mac_icons.complete_huge(completed), completed)

    def test_shared_icons_follow_huge_family_policy(self):
        names = (ROOT / "bitmaps/no-huge-icons.txt").read_text().splitlines()
        no_huge = set(names)
        self.assertEqual(len(no_huge), len(names))
        self.assertTrue(all((ROOT / "bitmaps" / name).is_file() for name in no_huge))
        for path in sorted((ROOT / "bitmaps").glob("*.icns")):
            with self.subTest(icon=path.name):
                members = dict(mac_icons.icns_members(path.read_bytes()))
                if path.name in no_huge:
                    self.assertNotIn(b"ih32", members)
                    self.assertNotIn(b"h8mk", members)
                    self.assertFalse(
                        any(frame["size"] == frame["height"] == 48 for frame in mac_icons.decode(path.read_bytes()))
                    )
                for kind, length in ((b"ich#", 576), (b"ich4", 1152), (b"ich8", 2304)):
                    if path.name in no_huge:
                        self.assertNotIn(kind, members)
                    else:
                        self.assertEqual(len(members[kind]), length)

    def test_removing_huge_members_preserves_other_frames(self):
        data = png_icns([[(255, 0, 0, 255)] * 16 for _ in range(16)])
        self.assertEqual(mac_icons.without_huge(mac_icons.complete_huge(data)), data)
        frame = mac_icons.classic_frame(mac_icons.decode(data)[0], 48, 8)
        combined = mac_icons.icns_family(
            [mac_icons.icns_chunk(kind, payload) for kind, payload in mac_icons.icns_members(data)]
            + [
                mac_icons.icns_chunk(kind, payload)
                for kind, payload in mac_icons.icns_members(mac_icons.encode([frame]))
                if kind == b"exfr"
            ]
        )
        self.assertEqual(mac_icons.without_huge(combined), data)

    def test_huge_completion_cli_keeps_small_ui_assets_small(self):
        original = png_icns([[(255, 0, 0, 255)] * 16 for _ in range(16)])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "wind_zoom.icns"
            path.write_bytes(mac_icons.complete_huge(original))
            for _ in range(2):
                subprocess.run(
                    [sys.executable, str(ROOT / "tools/complete-icon-depths.py"), "--huge", str(path)],
                    check=True,
                    capture_output=True,
                )
                self.assertEqual(path.read_bytes(), original)

    def read_palette_fixture(self, source, depth=1):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "palette.r"
            path.write_text(source, encoding="utf-8")
            return mac_icons.read_classic_palette(path, depth)

    def test_rez_palette_preserves_every_eight_bit_component(self):
        expected = [(i, 255 - i, (i * 73) % 256) for i in range(256)]
        rows = [f"{i}, {r * 257}, {g * 257}, {b * 257};" for i, (r, g, b) in enumerate(expected)]
        # Explicit indices, rather than physical row order, identify colours.
        source = "resource 'clut' (8) { 0, 0, { " + " ".join(reversed(rows)) + " } };"
        self.assertEqual(self.read_palette_fixture(source, 8), expected)
        for depth in (1, 4, 8):
            palette = mac_icons.read_classic_palette(ROOT / "bitmaps/mac-palettes.r", depth)
            self.assertEqual(len(palette), 1 << depth)
            self.assertTrue(all(len(colour) == 3 and all(0 <= c <= 255 for c in colour) for colour in palette))

    def test_rez_device_palette_uses_record_order(self):
        source = "resource 'clut' (1) { 0, 0x8000, { 0, 0, 0, 0; 0, 65535, 65535, 65535; } };"
        self.assertEqual(self.read_palette_fixture(source), [(0, 0, 0), (255, 255, 255)])

    def test_rez_palette_rejects_invalid_sizes_flags_and_indices(self):
        bodies = [
            "0, 0, { 0, 0, 0, 0; }",
            "0, 1, { 0, 0, 0, 0; 1, 0, 0, 0; }",
            "0, 0, { 0, 0, 0, 0; 0, 0, 0, 0; }",
            "0, 0, { 0, 0, 0, 0; 2, 0, 0, 0; }",
        ]
        for body in bodies:
            with self.subTest(body=body):
                with self.assertRaises(ValueError):
                    self.read_palette_fixture("resource 'clut' (1) { " + body + " };")
        with self.assertRaises(ValueError):
            self.read_palette_fixture("resource 'clut' (4) { 0, 0, {} };")
        with self.assertRaises(ValueError):
            self.read_palette_fixture("resource 'clut' (1) { 0, 0, {} };", 2)
        payload = struct.pack(">IHh8H", 0, 0, 1, 0, 65535, 65535, 65535, 1, 0, 0, 0)
        for size in range(len(payload)):
            with self.assertRaises(ValueError):
                self.read_palette_fixture("data 'clut' (1) { $\"" + payload[:size].hex() + '" };')
        with self.assertRaises(ValueError):
            self.read_palette_fixture("data 'clut' (1) { $\"" + (payload + b"x").hex() + '" };')

    def test_color_icon_keeps_transparent_and_opaque_white_pixels(self):
        rows = [[(0, 0, 0, 0)] * 32 for _ in range(32)]
        rows[0][1] = (255, 255, 255, 255)
        rows[0][2] = (255, 0, 0, 255)
        payload = mac_icons.resource(png_icns(rows), b"cicn")
        self.assertEqual(struct.unpack_from(">H4h", payload, 4), (0x8020, 0, 0, 32, 32))
        self.assertEqual(struct.unpack_from(">4H", payload, 30), (0, 8, 1, 8))
        self.assertEqual(struct.unpack_from(">H4h", payload, 54), (4, 0, 0, 32, 32))
        self.assertEqual(struct.unpack_from(">H4h", payload, 68), (4, 0, 0, 32, 32))
        self.assertEqual(payload[82:210], bytes([0x60]) + bytes(127))
        table = 82 + 128 + 128
        count = struct.unpack_from(">H", payload, table + 6)[0] + 1
        pixels = table + 8 + count * 8
        self.assertEqual(len(payload), pixels + 1024)
        for x, expected in [(1, (65535, 65535, 65535)), (2, (65535, 0, 0))]:
            index = payload[pixels + x]
            self.assertEqual(struct.unpack_from(">3H", payload, table + 8 + index * 8 + 2), expected)

    def test_standard_indexed_icns_members(self):
        mono = bytes([0x55] * 128)
        mask = bytes([0xAA] * 128)
        color = bytes([0x77] * 512)
        data = mac_icons.icns_family(
            [
                mac_icons.icns_chunk(b"ICN#", mono + mask),
                mac_icons.icns_chunk(b"icl4", color),
            ]
        )
        self.assertEqual(mac_icons.resource(data, b"ICN#"), mono + mask)
        self.assertEqual(mac_icons.resource(data, b"icl4"), color)
        self.assertEqual(mac_icons.decode(data)[1]["opacity"], mask)

    def test_finder_colour_art_is_not_clipped_by_mono_mask(self):
        spec = importlib.util.spec_from_file_location("folder_icons", ROOT / "tools/generate-folder-icons.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        for size, kind in [(32, b"ICN#"), (16, b"ics#")]:
            source = mac_icons.decode(png_icns([[(255, 0, 0, 255)] * size for _ in range(size)]))[0]
            mono = mac_icons.classic_frame(source, size, 1)
            colour = mac_icons.classic_frame(source, size, 8)
            mono["opacity"] = bytes(size * size // 8)
            data = mac_icons.encode([mono, colour])
            result = builder.family_resource(data, kind)
            self.assertEqual(result[size * size // 8 :], bytes([255] * (size * size // 8)))
            self.assertEqual(
                result[: size * size // 8],
                mac_icons.resource(data, kind)[: size * size // 8],
            )

    def test_lossless_classic_frames(self):
        for kind, (size, depth, paired) in mac_icons.KINDS.items():
            pixels = bytes([0x55] * (size * size * depth // 8))
            mask = bytes([0xAA] * (size * size // 8))
            frame = dict(
                size=size,
                height=size,
                depth=depth,
                palette=mac_icons.classic_palette(depth),
                image=pixels,
                opacity=mask,
            )
            data = mac_icons.encode([frame])
            expected = bytes(pixel & alpha for pixel, alpha in zip(pixels, mask)) if kind == b"SICN" else pixels
            self.assertEqual(mac_icons.resource(data, kind), expected + (mask if paired else b""))

    def test_png_and_rgba_generate_missing_variants(self):
        rows = [[(255, 0, 0, 255)] * 64 for _ in range(64)]
        rows[0][0] = (0, 0, 255, 0)
        png = png_icns(rows)
        rgba = mac_icons.encode(mac_icons.decode(png))
        for kind, (size, depth, paired) in mac_icons.KINDS.items():
            result = mac_icons.resource(png, kind)
            self.assertEqual(result, mac_icons.resource(rgba, kind))
            self.assertEqual(
                len(result),
                size * size * depth // 8 + (size * size // 8 if paired else 0),
            )
            if paired:
                mask = result[size * size // 8 :]
                self.assertEqual(mask[0], 0x7F)
                self.assertTrue(all(x == 255 for x in mask[1:]))
        self.assertEqual(mac_icons.image(png, 32)[0][0][3], 0)
        self.assertEqual(mac_icons.image(png, 32)[-1][-1], (255, 0, 0, 255))

    def test_changed_indexed_palette_is_remapped(self):
        palette = list(mac_icons.classic_palette(4))
        palette[3] = mac_icons.classic_palette(4)[7]
        frame = dict(
            size=32,
            height=32,
            depth=4,
            palette=palette,
            image=bytes([0x33] * 512),
            opacity=bytes([255] * 128),
        )
        red = 7
        self.assertEqual(
            mac_icons.resource(mac_icons.encode([frame]), b"icl4"),
            bytes([red * 17] * 512),
        )

    def test_alpha_threshold_and_native_alpha(self):
        rows = [[(0, 0, 0, 255)] * 32 for _ in range(32)]
        rows[0][:4] = [(0, 0, 0, a) for a in (0, 127, 128, 255)]
        data = png_icns(rows)
        self.assertEqual(mac_icons.image(data, 32)[0][:4], rows[0][:4])
        mask = mac_icons.resource(data, b"ICN#")[128:]
        self.assertEqual(mask[0], 0x3F)

    def test_png_directory_and_payload_validation(self):
        data = bytearray(png_icns([[(0, 0, 0, 255)] * 32 for _ in range(32)]))
        data[8:12] = b"icp4"
        with self.assertRaisesRegex(ValueError, "dimensions"):
            mac_icons.decode(bytes(data))
        with self.assertRaises(ValueError):
            mac_icons.decode(bytes(data[:30]))

    def test_edited_system_family_builds(self):
        spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        edited = png_icns([[(0, 255, 0, 255)] * 32 for _ in range(32)])
        original_read = Path.read_bytes

        def read(path):
            return edited if path.name == "type_pref.icns" else original_read(path)

        with patch.object(Path, "read_bytes", read):
            records = builder.read_resources(ROOT / "system/resources/system")
        modified = [(kind, payload) for kind, rid, _, _, payload in records if rid == -3971 and kind in mac_icons.KINDS]
        self.assertEqual(len(modified), 6)
        for kind, payload in modified:
            self.assertEqual(payload, mac_icons.resource(edited, kind))
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(Path, "read_bytes", read):
                builder.generate(ROOT / "system/resources/system", Path(temporary) / "system.inc")
            self.assertTrue((Path(temporary) / "system.inc").stat().st_size > 0)

    def test_complete_depths_preserves_edits_and_odd_width_masks(self):
        for size in (13, 14, 16, 32, 48):
            rows = [[(255, 0, 0, 255)] * size for _ in range(size)]
            rows[0][0] = (0, 0, 0, 0)
            original = png_icns(rows)
            completed = mac_icons.complete_depths(original)
            frames = mac_icons.decode(completed)
            self.assertEqual({f["depth"] for f in frames}, {1, 4, 8, 32})
            self.assertEqual(mac_icons.rgba(frames[0]), rows)
            for frame in frames:
                self.assertEqual(mac_icons.rgba(frame)[0][0][3], 0)
                self.assertEqual(mac_icons.rgba(frame)[-1][-1][3], 255)
            self.assertEqual(mac_icons.complete_depths(completed), completed)

    def test_split_small_icons_preserve_rez_order(self):
        spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        directory = ROOT / "system/resources/system"
        catalog = builder.load_scope(directory)[0]
        entry = next(
            entry for entry in catalog["resources"] if entry["type_hex"] == b"SICN".hex() and entry["id"] == -15744
        )
        self.assertEqual(len(entry["files"]), 6)
        replacements = {}
        expected = bytearray()
        for index, filename in enumerate(entry["files"]):
            pixels = bytes([index * 17] * 32)
            frame = dict(
                size=16,
                height=16,
                depth=1,
                palette=mac_icons.classic_palette(1),
                image=pixels,
                opacity=bytes([255] * 32),
            )
            replacements[(ROOT / "bitmaps" / filename).resolve()] = mac_icons.encode([frame])
            expected.extend(pixels)
        original_read = Path.read_bytes

        def read(path):
            if path in replacements:
                return replacements[path]
            return original_read(path)

        with patch.object(Path, "read_bytes", read):
            records = builder.read_resources(directory)
        payload = next(data for kind, rid, _, _, data in records if kind == b"SICN" and rid == -15744)
        self.assertEqual(len(payload), 192)
        self.assertEqual(payload, bytes(expected))

    def test_expanding_ui_palette_preserves_indices(self):
        palette = list(mac_icons.classic_palette(4))
        palette[3] = (12, 34, 56)
        frame = dict(
            size=16,
            height=16,
            depth=4,
            palette=palette,
            image=bytes([0x33] * 128),
            opacity=bytes([255] * 32),
        )
        data = mac_icons.complete_depths(mac_icons.encode([frame]))
        expanded = next(f for f in mac_icons.decode(data) if f["depth"] == 8)
        self.assertEqual(expanded["image"], bytes([3] * 256))
        self.assertEqual(expanded["palette"][:16], palette)
        self.assertEqual(mac_icons.rgba(expanded), mac_icons.rgba(frame))


if __name__ == "__main__":
    unittest.main()
