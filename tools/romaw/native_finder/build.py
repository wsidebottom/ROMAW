# +===================================================================
#   File:      build.py
#
#   Summary:   Generate resource-backed Finder artwork/layouts and link its native archive.
#
#   Classes:   None declared.
#
#   Functions: run, main
#
#   Origin:    Existing repository source in executor/src/tools/native_finder; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Generate resource-backed Finder artwork/layouts and link its native archive.

Maintained resources stay in finder/resources, system/resources and bitmaps/. All generated tables,
objects and Windows-only test executables go to the selected output directory;
the enclosing ROMAW build owns the final executable and its application icon.
"""

import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import sys
from collections import Counter

from pixpat import decode_pixpat

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "finder"
TESTS = ROOT / "system/tests/native-finder"
UI_SOURCE = ROOT / "classic-ui/src/classic_ui.c"
OUT = ROOT / "system/obj/native-finder/windows-x64"
sys.path.insert(0, str(ROOT / "tools"))
from build_version import version_string, write_header
from resource_catalog import load_label_menu, load_scope, native_path, read_rez, resource_path

FINDER_RESOURCES = load_scope(ROOT / "finder/resources", "finder")[1]


def run(args, timeout=None):
    """Run a host tool from the workspace root and propagate failures/timeouts."""
    subprocess.run([str(arg) for arg in args], check=True, cwd=ROOT, timeout=timeout)


def main():
    """Regenerate inputs, compile the frontend and optionally run Windows fixtures."""
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=OUT,
        help="Generated frontend objects (defaults to system/obj/native-finder/windows-x64)",
    )
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--test", action="store_true")
    parser.add_argument(
        "--generate-only", action="store_true", help="Generate resource/artwork inputs without compiling"
    )
    args = parser.parse_args()
    OUT = args.output_dir.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / "tools/romaw"))
    import system_resources

    # STR# 3000 retains the seven native column/sort names in classic order.
    # Append native type/creator columns without changing the sort selectors.
    text_resources = read_rez(FINDER_RESOURCES / "text_resources.r")
    view_strings = next(
        entry["payload"] for entry in text_resources if entry["type_hex"] == b"STR#".hex() and entry["id"] == 3000
    )
    count = int.from_bytes(view_strings[:2], "big")
    names = []
    offset = 2
    for _ in range(count):
        length = view_strings[offset]
        offset += 1
        names.append(view_strings[offset : offset + length].decode("mac_roman"))
        offset += length
    if offset != len(view_strings):
        raise ValueError("Invalid Finder STR# 3000 presentation names")
    if len(names) != 7:
        raise ValueError("Unexpected Finder STR# 3000 presentation names")
    (OUT / "view_menu.inc").write_text(
        "/* Generated from Finder STR# 3000, columns 1..7. */\n"
        "static const wchar_t *classic_view_columns[]={"
        + ",".join("L" + json.dumps(name) for name in names + ["Type", "Creator"])
        + "};\n"
    )

    # The classic Blue Tiles resource supplies every pixel and palette color.
    records = system_resources.read_resources(ROOT / "system/resources/system")
    pattern = next(data for kind, rid, _, _, data in records if kind == b"ppat" and rid == 17)
    width, height, pixels = decode_pixpat(pattern)
    background = Counter(pixels).most_common(1)[0][0]
    (OUT / "desktop_pattern.inc").write_text(
        "/* Generated from ROMAW System 'ppat' 17 (Blue Tiles). */\n"
        f"static constexpr int desktop_pattern_width = {width};\n"
        f"static constexpr int desktop_pattern_height = {height};\n"
        f"static constexpr unsigned desktop_pattern_background = 0x{background:06x};\n"
        "static constexpr unsigned desktop_pattern_pixels[] = {\n"
        + "\n".join(
            "  " + ", ".join(f"0x{p:06x}" for p in pixels[start : start + 8]) + ","
            for start in range(0, len(pixels), 8)
        )
        + "\n};\n"
    )
    if args.test:
        run([sys.executable, TESTS / "test_pixpat.py"])
    if os.name == "nt":
        compiler = Path(os.environ.get("FINDER_TOOLCHAIN", "C:/msys64/mingw64/bin"))
        os.environ["PATH"] = str(compiler) + os.pathsep + os.environ["PATH"]
        cc, cxx, rc = [compiler / name for name in ("gcc.exe", "g++.exe", "windres.exe")]
    else:
        cc, cxx, rc = ["x86_64-w64-mingw32-" + name for name in ("gcc", "g++", "windres")]
    run(
        [
            sys.executable,
            ROOT / "tools/generate-folder-icons.py",
            "--output",
            OUT,
            "--windows-output",
            OUT / "image-file-icons",
        ]
    )
    # Asset order is consumed by main.cpp: base icons, labels, then notice/menu
    # artwork. Change both the generator and consumers when adding a new slot.
    names = [
        "-4000_genericDocumentIconResource.icns",
        "-3999_genericFolderIconResource.icns",
        "type_APPL.icns",
        "-3993_trashIconResource.icns",
        "ROMAW_finder.icns",
    ]
    names = [ROOT / "bitmaps" / name for name in names]
    names += [ROOT / "bitmaps/label-menu.png"] * 7
    names += [
        ROOT / "bitmaps/stop.icns",
        ROOT / "bitmaps/warning.icns",
        ROOT / "bitmaps/apple.icns",
    ]
    # Append rather than shifting the established document/label/notice slots.
    full_trash_asset = len(names)
    names.append(ROOT / "bitmaps/-3984_fullTrashIconResource.icns")
    assets = [f"static constexpr unsigned full_trash_asset = {full_trash_asset};"]
    unique_assets = {}
    asset_indices = []
    for i, name in enumerate(names):
        if name not in unique_assets:
            unique_assets[name] = i
            b = name.read_bytes()
            assets.append(f"static const unsigned char asset_{i}[]={{" + ",".join(map(str, b)) + "};")
        asset_indices.append(unique_assets[name])
    assets += ["static const struct { const unsigned char *data; size_t size; } assets[]={"]
    assets += [f"{{asset_{i},sizeof asset_{i}}}," for i in asset_indices]
    assets += ["};"]
    # Embed font bytes so normal launches do not require font installation or
    # depend on the current working directory.
    for name in ("Chicago", "Geneva"):
        b = (ROOT / "fonts" / (name + ".ttf")).read_bytes()
        assets.append("static const unsigned char font_" + name.lower() + "[]={" + ",".join(map(str, b)) + "};")
    (OUT / "assets.inc").write_text("\n".join(assets) + "\n")
    # The editable classic Finder view resource is authoritative for copy geometry.
    source = (FINDER_RESOURCES / "resources.r").read_text()
    view = re.search(r"resource 'fvew' \(8751,.*?(?=\nresource |\Z)", source, re.S)
    if not view:
        raise ValueError("Finder copy view fvew 8751 is missing")
    pattern = r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\},\s*0x[0-9A-Fa-f]+,\s*0x[0-9A-Fa-f]+,\s*(\d+),\s*\d+,"
    layout = ["struct FinderCopyBounds { int x,y,width,height; };"]
    bounds = re.search(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}, /\* content bounds", view[0])
    if not bounds:
        raise ValueError("Copy content bounds missing")
    top, left, bottom, right = map(int, bounds.groups())
    layout += [f"static constexpr int copy_width={right-left},copy_height={bottom-top};"]
    for match in re.finditer(pattern, view[0]):
        top, left, bottom, right, identifier = map(int, match.groups())
        layout += [
            f"static constexpr FinderCopyBounds copy_rect_{identifier}={{{left},{top},{right-left},{bottom-top}}};"
        ]
    (OUT / "copy_layout.inc").write_text("\n".join(layout) + "\n")
    # Keep native menu strings/order and Get Info geometry in sync with Rez.
    interface = (FINDER_RESOURCES / "interface.r").read_text()
    menus = ["struct FinderMenuEntry { const char *command; const wchar_t *text; }; "]
    for identifier in (1252, 1253, 1255):
        block = re.search(
            r"resource 'fmnu' \(" + str(identifier) + r",.*?(?=\nresource |\Z)",
            interface,
            re.S,
        )
        entries = re.findall(r"'(.{4})',\s*0x[0-9A-Fa-f]+,\s*(\d+),\s*\d+,\s*\"([^\"]*)\";", block[0])
        if not entries:
            raise ValueError(f"Missing Finder menu {identifier}")
        menus.append(f"static const FinderMenuEntry finder_menu_{identifier}[]={{")
        for command, key, text in entries:
            text = text.replace(r"\0xC9", "...")
            if int(key):
                text += r"\tCtrl+" + chr(int(key))
            menus.append('{"' + command + '", L"' + text + '"},')
        menus.append("};")
    menus += ["struct FinderInfoField { RECT bounds; int id; const wchar_t *label; };"]
    field_pattern = pattern.replace(r"(\d+),", r"(-?\d+),") + r"\s*\{(.*?)\n        \};"
    for identifier, expected in ((11261, 14), (11511, 14), (17011, 19), (17261, 29)):
        info = re.search(
            r"resource 'fvew' \(" + str(identifier) + r",.*?(?=\nresource |\Z)",
            source,
            re.S,
        )[0]
        top, left, bottom, right = map(
            int,
            re.search(r"\{ (\d+), (\d+), (\d+), (\d+) \}, /\* content bounds", info).groups(),
        )
        menus += [
            f"static constexpr int info_width_{identifier}={right-left}, info_height_{identifier}={bottom-top+24};",
            f"static const FinderInfoField finder_info_{identifier}[]={{",
        ]
        matches = list(re.finditer(field_pattern, info, re.S))
        if len(matches) != expected:
            raise ValueError(f"Get Info {identifier} requires all {expected} resource views")
        for match in matches:
            top, left, bottom, right, field_id = map(int, match.groups()[:5])
            if top >= 57:
                top += 24
                bottom += 24
            label = re.search(r'Text \{ "([^"\n]*)" \}', match.group(6))
            text = label[1] if label else ""
            if identifier == 17261 and field_id == -1:
                if text == "Memory Requirements":
                    continue
                if left == 78 and right == 234:
                    field_id, text, top = 17304, "Memory Requirements", top - 7
                elif re.search(r"Words \{ \{ 331 \}", match.group(6)):
                    text = "K"
            menus.append(f'{{{{{left},{top},{right},{bottom}}},{field_id},L"{text}"}},')
        menus += [
            '{{8,57,69,69},-1,L"Type:"},',
            '{{71,57,234,69},32001,L""},',
            '{{8,69,69,81},-1,L"Creator:"},',
            '{{71,69,234,81},32002,L""},',
        ]
        menus.append("};")
    (OUT / "interface_layout.inc").write_text("\n".join(menus) + "\n")
    alert = re.search(r"resource 'ALRT' \(6011,.*?(?=\nresource |\Z)", interface, re.S)[0]
    alert_height, alert_width = map(int, re.search(r"\{ 0, 0, (\d+), (\d+) \}", alert).groups())
    items = re.search(r"resource 'DITL' \(6011,.*?(?=\nresource |\Z)", interface, re.S)[0]
    rectangles = re.findall(r"\{ (\d+), (\d+), (\d+), (\d+) \}", items)
    notice = [f"static constexpr int alert_width={alert_width}, alert_height={alert_height};"]
    for name, coordinates in zip(("button", "cancel", "icon", "text"), rectangles):
        top, left, bottom, right = map(int, coordinates)
        notice.append(f"static constexpr RECT alert_{name}={{{left},{top},{right},{bottom}}};")
    (OUT / "alert_layout.inc").write_text("\n".join(notice) + "\n")
    find = ["struct FindMenuChoice { int value; const wchar_t *text; };"]
    for rid, mode, fields in (
        (11014, "simple", ("label", "value", "toggle", "cancel", "submit")),
        (
            11015,
            "expanded",
            (
                "label",
                "field",
                "relation",
                "value",
                "rule1",
                "search",
                "scope",
                "batch",
                "rule2",
                "toggle",
                "cancel",
                "submit",
            ),
        ),
    ):
        view = re.search(rf"resource 'fvew' \({rid},.*?(?=\nresource |\Z)", source, re.S)[0]
        height, width = map(int, re.search(r"\{ 0, 0, (\d+), (\d+) \}, /\* content bounds", view).groups())
        find.append(f"static constexpr int find_{mode}_width={width}, find_{mode}_height={height};")
        matches = list(re.finditer(field_pattern, view, re.S))
        if len(matches) != len(fields):
            raise ValueError(f"Find {rid} requires all {len(fields)} resource views")
        for field, match in zip(fields, matches):
            top, left, bottom, right = map(int, match.groups()[:4])
            find.append(f"static constexpr RECT find_{mode}_{field}={{{left},{top},{right},{bottom}}};")
            label = re.search(r'Text \{ "([^"\n]*)" \}', match.group(6))
            if label:
                find.append(f"static const wchar_t find_{mode}_{field}_text[]=L{json.dumps(label[1])};")
    for rid in (11030, 11031, 11032, 11035, 11036, 11037, 11038, 11052, 11056):
        menu = re.search(rf"resource 'fmnu' \({rid},.*?(?=\nresource |\Z)", interface, re.S)[0]
        find.append(f"static constexpr FindMenuChoice find_menu_{rid}[]={{")
        for value, label in re.findall(r'(-?\d+),\s*0x[\dA-Fa-f]+,\s*0,\s*0,\s*"([^"]*)";', menu):
            if label == "-":
                continue
            label = re.sub(r"\\0x([A-Fa-f0-9]{2})", lambda m: bytes([int(m[1], 16)]).decode("mac_roman"), label)
            find.append(f"{{{value},L{json.dumps(label)}}},")
        find.append("};")
    (OUT / "find_layout.inc").write_text("\n".join(find) + "\n")
    about_view = re.search(r"resource 'fvew' \(10001,.*?(?=\nresource |\Z)", source, re.S)[0]
    about = ["static constexpr int about_width=378, about_header_height=84, about_row_height=18;"]
    for index, match in enumerate(re.finditer(pattern.replace(r"(\d+),", r"(-?\d+),"), about_view), 1):
        top, left, bottom, right, identifier = map(int, match.groups())
        about.append(f"static constexpr RECT about_rect_{index}={{{left},{top},{right},{bottom}}};")
    (OUT / "about_layout.inc").write_text("\n".join(about) + "\n")

    sys.path.insert(0, str(ROOT / "tools"))
    import mac_icons

    # Preserve the classic 16-pixel SICN frame, including its transparent border.
    lock_frame = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps/lock_solid.icns").read_bytes()), 16)
    if lock_frame["size"] != 16 or lock_frame["height"] != 16:
        raise ValueError("The volume lock artwork must contain a 16x16 frame")
    lock_pixels = [channel for row in mac_icons.rgba(lock_frame) for pixel in row for channel in pixel]
    (OUT / "volume_status.inc").write_text(
        "/* Generated from bitmaps/lock_solid.icns. */\n"
        "static const unsigned char volume_lock_rgba[]={" + ",".join(map(str, lock_pixels)) + "};\n"
    )
    disclosure = ["/* Generated from bitmaps/arrow_open.icns and bitmaps/3061.icns. */"]
    for state, filename in (("closed", "arrow_open.icns"), ("open", "3061.icns")):
        frame = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps" / filename).read_bytes()), 16)
        if frame["size"] != 16 or frame["height"] != 16:
            raise ValueError(f"Disclosure icon {filename} must contain a 16x16 frame")
        pixels = [channel for row in mac_icons.rgba(frame) for pixel in row for channel in pixel]
        disclosure.append(f"static const unsigned char disclosure_{state}_rgba[]={{{','.join(map(str, pixels))}}};")
    (OUT / "disclosure.inc").write_text("\n".join(disclosure) + "\n")

    # Share System 7's actual label numbering, colors and recolored swatch art.
    icon_source = (ROOT / "ui/icon.c").read_text()
    label_table = re.search(r"PRIVATE const label_info_t labels\[8\] = \{(.*?)\n\};", icon_source, re.S)
    values = [int(x, 16) for x in re.findall(r"CWC\s*\(\s*0x([0-9a-fA-F]+)\s*\)", label_table[1])]
    if len(values) != 21:
        raise ValueError("Cannot read System 7 label palette")
    palette = [[65535] * 3] + [values[i : i + 3] for i in range(0, 21, 3)]
    label_menu = load_label_menu(native_path("label_menu"))
    check_rows = label_menu["check_bitmap"]
    menu_layout = [
        f"static constexpr int label_{key}={value};" for key, value in label_menu.items() if isinstance(value, int)
    ]
    menu_layout.append(
        "static const unsigned short label_check_bitmap[]={"
        + ",".join(str(int(row.replace(".", "0").replace("#", "1"), 2)) for row in check_rows)
        + "};"
    )
    menu_layout.append(
        "static const wchar_t *label_menu_names[]={"
        + ",".join("L" + json.dumps(item["name"]) for item in label_menu["labels"])
        + "};"
    )
    (OUT / "label_menu.inc").write_text("\n".join(menu_layout) + "\n")
    label_art = [
        "static const unsigned short classic_label_rgb[8][3]={"
        + ",".join("{" + ",".join(map(str, p)) + "}" for p in palette)
        + "};"
    ]
    label_art.append(
        "static const unsigned char label_menu_rgb[8][3]={"
        + ",".join("{" + ",".join(map(str, item["rgb"])) + "}" for item in label_menu["labels"])
        + "};"
    )
    entries = [
        entry
        for entry in read_rez(FINDER_RESOURCES / "artwork.r")
        if entry["type"] == "cicn" and entry["id"] == label_menu["icon_resource"]
    ]
    if len(entries) != 1:
        raise ValueError("The label menu must reference one Finder cicn resource")
    entry = entries[0]
    if "payload" in entry:
        label_icon = entry["payload"]
    else:
        label_source = resource_path(FINDER_RESOURCES, entry["file"]).read_bytes()
        label_icon = mac_icons.resource(label_source, b"cicn") if entry.get("icon") else label_source
    # Finder 7 CODE 38:00a2 changes one ColorSpec in each loaded CIcon. Keep
    # the shared resource's pixels/mask and change only its Rez-selected tint slot.
    for i in range(1, 8):
        tinted = mac_icons.tint_color_icon(label_icon, label_menu["icon_color_index"], label_menu["labels"][i]["rgb"])
        frame = mac_icons.color_icon_frame(tinted)
        if (frame["size"], frame["height"]) != (32, 32):
            raise ValueError("The label-menu CIcon must contain 32x32 artwork")
        pixels = [channel for row in mac_icons.rgba(frame) for pixel in row for channel in pixel]
        label_art.append(f"static const unsigned char label_{i}_rgba[]={{" + ",".join(map(str, pixels)) + "};")
    (OUT / "label_art.inc").write_text("\n".join(label_art) + "\n")
    chrome = []
    for name in ("close", "zoom", "grow", "close_clicked"):
        tile = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps" / f"wind_{name}.icns").read_bytes()), 16)
        pixels = [v for row in mac_icons.rgba(tile) for pixel in row for v in pixel]
        chrome += [f"static const unsigned char chrome_{name}_rgba[]={{" + ",".join(map(str, pixels)) + "};"]
    (OUT / "chrome_art.inc").write_text("\n".join(chrome) + "\n")
    # Use the same pixel rotations as ROMAW's classic Control Manager.
    # Store single-image Windows resource payloads, retaining the ICNS mask.
    scrollbar = []
    scroll_frames = []
    for state in ("active", "inactive"):
        frame = mac_icons.best_frame(
            mac_icons.decode((ROOT / "bitmaps" / f"wind_scroll_arrow_{state}.icns").read_bytes()),
            16,
        )
        for direction in ("up", "down", "left", "right"):
            scroll_frames.append(mac_icons.scroll_arrow(mac_icons.rgba(frame), direction))
    thumb = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps/wind_scroll_thumb.icns").read_bytes()), 16)
    scroll_frames += [
        mac_icons.rgba(thumb),
        mac_icons.vertical_scroll_thumb(mac_icons.rgba(thumb)),
    ]
    grow = mac_icons.best_frame(mac_icons.decode((ROOT / "bitmaps/wind_grow.icns").read_bytes()), 16)
    scroll_frames.append(mac_icons.rgba(grow))
    clicked = mac_icons.best_frame(
        mac_icons.decode((ROOT / "bitmaps/wind_scroll_arrow_clicked.icns").read_bytes()),
        16,
    )
    for direction in ("up", "down", "left", "right"):
        scroll_frames.append(mac_icons.scroll_arrow(mac_icons.rgba(clicked), direction))
    for i, rows in enumerate(scroll_frames):
        mask = bytearray(32)
        for y, row in enumerate(rows):
            for x, pixel in enumerate(row):
                if pixel[3]:
                    mask[y * 2 + x // 8] |= 128 >> (x % 8)
        frame = dict(
            size=16,
            height=16,
            depth=32,
            palette=[],
            opacity=bytes(mask),
            image=bytes(c for row in rows for r, g, b, a in row for c in (b, g, r, a)),
        )
        payload = mac_icons.encode_ico([frame])[22:]
        scrollbar.append(f"static const unsigned char scrollbar_{i}[]={{" + ",".join(map(str, payload)) + "};")
    scrollbar.append("static const struct { const unsigned char *data; unsigned size; } scrollbar_resources[]={")
    scrollbar += [f"{{scrollbar_{i},sizeof scrollbar_{i}}}," for i in range(len(scroll_frames))]
    scrollbar.append("};")
    from bitmaps import read_bitmap

    width, height, depth, track = read_bitmap((ROOT / "bitmaps/scrollbar_track.png").read_bytes())
    if (width, height, depth) != (8, 8, 1):
        raise ValueError("Scrollbar track must remain an 8x8 monochrome bitmap")
    scrollbar.append("static const WORD scrollbar_track_pattern[]={" + ",".join(map(str, track)) + "};")
    (OUT / "scrollbar_art.inc").write_text("\n".join(scrollbar) + "\n")
    write_header(OUT / "generated/build_version.h")
    if args.generate_only:
        print(f"Generated native Finder resource inputs: {OUT}")
        return
    run([sys.executable, ROOT / "stuffit/build.py", "--output-dir", OUT / "stuffit"])
    run([sys.executable, ROOT / "archives/build.py", "--output-dir", OUT / "archives"])
    # The archive has no standalone entry point. ROMAW supplies startup,
    # shared metadata parsing and the final link; fixtures link only what they use.
    flags = ["-O0", "-g"] if args.debug else ["-O2", "-DNDEBUG"]
    include = [
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "finder-metadata/include"),
        "-I" + str(ROOT / "disk-images/include"),
        "-I" + str(ROOT / "stuffit/include"),
        "-I" + str(ROOT / "archives/include"),
        "-I" + str(ROOT / "stuffit/third_party/stuffit-rs/stuffit-ffi/include"),
        "-I" + str(OUT),
    ]
    flags += [
        "-DUNICODE",
        "-D_UNICODE",
        "-DNOMINMAX",
        "-DWIN32_LEAN_AND_MEAN",
        "-D_WIN32_WINNT=0x0601",
        "-D_WIN32_IE=0x0600",
        "-Wall",
        "-Wextra",
        "-ffunction-sections",
        "-fdata-sections",
    ]
    link = ["-static", "-static-libgcc", "-static-libstdc++", "-Wl,--gc-sections"]
    if not args.debug:
        link.append("-s")
    objects = []
    for source in (
        SOURCE / "main.cpp",
        SOURCE / "files.cpp",
        SOURCE / "stuffit.cpp",
        SOURCE / "archive.cpp",
        SOURCE / "archive_picker.cpp",
        ROOT / "stuffit/src/archive.cpp",
        SOURCE / "info.cpp",
        SOURCE / "recycle.cpp",
        SOURCE / "clipboard.cpp",
        SOURCE / "actions.cpp",
        SOURCE / "file_associations.cpp",
        SOURCE / "classic.cpp",
        SOURCE / "text.cpp",
        SOURCE / "romaw.cpp",
        SOURCE / "image_mount.cpp",
        SOURCE / "image_copy.cpp",
        SOURCE / "settings.cpp",
        SOURCE / "drag.cpp",
        SOURCE / "views.cpp",
        SOURCE / "desktop_icons.cpp",
    ):
        stem = "stuffit-bridge" if source == ROOT / "stuffit/src/archive.cpp" else source.stem
        obj = OUT / (stem + "-integrated.o")
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                *include,
                "-c",
                source,
                "-o",
                obj,
            ]
        )
        objects.append(obj)
    ar = str(cxx).replace("g++", "ar")
    # Replacing an archive also removes members retired from its source list.
    # Plain ar rcs against an existing archive would silently retain them.
    archive = OUT / "libfinder.a"
    pending = OUT / "libfinder.a.tmp"
    pending.unlink(missing_ok=True)
    run([ar, "rcs", pending, *objects])
    pending.replace(archive)
    # ROMAW owns its executable icon. Embed only the DPI/common-controls
    # manifest here, avoiding duplicate RT_GROUP_ICON and RT_ICON IDs.
    manifest = OUT / "finder-integrated.rc"
    manifest.write_text(
        f'1 24 "{(native_path("application_manifest")).as_posix()}"\n'
        "1 VERSIONINFO\n FILEVERSION 0,0,0,0\n PRODUCTVERSION 0,0,0,0\n"
        'BEGIN\n BLOCK "StringFileInfo"\n BEGIN\n BLOCK "040904b0"\n BEGIN\n'
        'VALUE "FileDescription", "ROMAW\\0"\n'
        f'VALUE "FileVersion", "{version_string()}\\0"\n'
        'VALUE "ProductName", "ROMAW\\0"\n'
        f'VALUE "ProductVersion", "{version_string()}\\0"\n'
        'END\n END\n BLOCK "VarFileInfo"\n BEGIN\n VALUE "Translation", 0x409, 1200\n END\n END\n'
    )
    run([rc, "-i", manifest, "-o", OUT / "finder-integrated-res.o"])
    # These fixtures execute Windows code (including under WSL interop), not
    # Linux substitutes for the filesystem, drawing or command-line APIs.
    if args.test:
        image_resources = OUT / "image-file-icons-test-res.o"
        run(
            [
                rc,
                "-I" + str(OUT / "image-file-icons"),
                "-i",
                OUT / "image-file-icons/resources.rc",
                "-o",
                image_resources,
            ]
        )
        image_test = OUT / "file-associations-test.exe"
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "file_associations_test.cpp",
                SOURCE / "file_associations.cpp",
                image_resources,
                *link,
                "-ladvapi32",
                "-lshell32",
                "-o",
                image_test,
            ]
        )
        test_artwork = str(OUT / "image-file-icons")
        if sys.platform != "win32":
            test_artwork = subprocess.check_output(["wslpath", "-w", test_artwork], text=True).strip()
        run([image_test, test_artwork], timeout=120)
    if args.test:
        parser = OUT / "desktop-icons-parser.o"
        run([cc, "-std=c11", *include, "-c", ROOT / "finder-metadata/src/icons.c", "-o", parser])
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "desktop_icons_test.cpp",
                SOURCE / "desktop_icons.cpp",
                SOURCE / "files.cpp",
                ROOT / "files/desktop_database.cpp",
                parser,
                *link,
                "-lshell32",
                "-lshlwapi",
                "-lole32",
                "-luuid",
                "-o",
                OUT / "desktop-icons-test.exe",
            ]
        )
        run([OUT / "desktop-icons-test.exe"], timeout=120)
    if args.test:
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "views_test.cpp",
                SOURCE / "views.cpp",
                *link,
                "-o",
                OUT / "views-test.exe",
            ]
        )
        run([OUT / "views-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                *include,
                "-municode",
                TESTS / "launcher_test.cpp",
                OUT / "romaw-integrated.o",
                *link,
                "-lole32",
                "-luuid",
                "-o",
                OUT / "integrated-launch-test.exe",
            ]
        )
        run([OUT / "integrated-launch-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "image_mount_test.cpp",
                OUT / "image_mount-integrated.o",
                *link,
                "-o",
                OUT / "image-mount-test.exe",
            ]
        )
        run([OUT / "image-mount-test.exe"], timeout=120)
    if args.test:
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-DFINDER_TESTING",
                *include,
                TESTS / "label_menu_test.cpp",
                SOURCE / "classic.cpp",
                SOURCE / "text.cpp",
                UI_SOURCE,
                OUT / "finder-integrated-res.o",
                *link,
                "-lcomctl32",
                "-luxtheme",
                "-lgdi32",
                "-o",
                OUT / "label-menu-test.exe",
            ]
        )
        run([OUT / "label-menu-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-DFINDER_TESTING",
                *include,
                TESTS / "scrollbars_test.cpp",
                SOURCE / "classic.cpp",
                SOURCE / "text.cpp",
                UI_SOURCE,
                OUT / "finder-integrated-res.o",
                *link,
                "-lcomctl32",
                "-luxtheme",
                "-lgdi32",
                "-o",
                OUT / "scrollbars-test.exe",
            ]
        )
        run([OUT / "scrollbars-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "volume_status_test.cpp",
                SOURCE / "classic.cpp",
                SOURCE / "text.cpp",
                UI_SOURCE,
                OUT / "finder-integrated-res.o",
                *link,
                "-lcomctl32",
                "-luxtheme",
                "-lgdi32",
                "-o",
                OUT / "volume-status-test.exe",
            ]
        )
        run([OUT / "volume-status-test.exe"], timeout=120)
        type_map = OUT / "info-filetypemap.o"
        run([cc, "-std=c11", *include, "-c", ROOT / "files/filetypemap.c", "-o", type_map])
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                *include,
                TESTS / "info_test.cpp",
                SOURCE / "info.cpp",
                ROOT / "files/filetypedb.cpp",
                type_map,
                "-static",
                "-o",
                OUT / "info-test.exe",
            ]
        )
        run([OUT / "info-test.exe"], timeout=120)

        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                "-DFINDER_TESTING",
                TESTS / "conflicts_test.cpp",
                SOURCE / "classic.cpp",
                SOURCE / "text.cpp",
                UI_SOURCE,
                OUT / "finder-integrated-res.o",
                *link,
                "-lcomctl32",
                "-luxtheme",
                "-lgdi32",
                "-o",
                OUT / "conflicts-test.exe",
            ]
        )
        run([OUT / "conflicts-test.exe"], timeout=120)

        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "actions_test.cpp",
                SOURCE / "actions.cpp",
                SOURCE / "files.cpp",
                SOURCE / "views.cpp",
                *link,
                "-lcomctl32",
                "-lshell32",
                "-lshlwapi",
                "-lole32",
                "-luuid",
                "-o",
                OUT / "actions-test.exe",
            ]
        )
        run([OUT / "actions-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                "-DFINDER_TESTING",
                *include,
                TESTS / "files_test.cpp",
                SOURCE / "files.cpp",
                *link,
                "-lshell32",
                "-lole32",
                "-luuid",
                "-o",
                OUT / "files-test.exe",
            ]
        )
        run([OUT / "files-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                "-DFINDER_RECYCLE_TESTING",
                *include,
                TESTS / "recycle_test.cpp",
                SOURCE / "recycle.cpp",
                SOURCE / "files.cpp",
                *link,
                "-lshell32",
                "-lshlwapi",
                "-lole32",
                "-luuid",
                "-o",
                OUT / "recycle-test.exe",
            ]
        )
        run([OUT / "recycle-test.exe"], timeout=120)
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                "-DFINDER_CLIPBOARD_TESTING",
                *include,
                TESTS / "clipboard_test.cpp",
                SOURCE / "clipboard.cpp",
                SOURCE / "files.cpp",
                *link,
                "-lgdi32",
                "-lshell32",
                "-lole32",
                "-luuid",
                "-o",
                OUT / "clipboard-test.exe",
            ]
        )
        run([OUT / "clipboard-test.exe"], timeout=120)
    if args.test:
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                TESTS / "settings_test.cpp",
                SOURCE / "settings.cpp",
                SOURCE / "drag.cpp",
                *link,
                "-lgdi32",
                "-lole32",
                "-lcomctl32",
                "-luuid",
                "-o",
                OUT / "settings-test.exe",
            ]
        )
        run([OUT / "settings-test.exe"], timeout=120)
    if args.test:
        run(
            [
                cxx,
                "-std=c++17",
                *flags,
                "-UNDEBUG",
                *include,
                "-DFINDER_TESTING",
                TESTS / "drag_test.cpp",
                SOURCE / "drag.cpp",
                *link,
                "-lgdi32",
                "-luser32",
                "-lcomctl32",
                "-o",
                OUT / "drag-test.exe",
            ]
        )
        run([OUT / "drag-test.exe"], timeout=120)
    if args.test:
        run(
            [
                cc,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                *include,
                TESTS.parent / "classic_ui_test.c",
                UI_SOURCE,
                "-static",
                "-o",
                OUT / "classic-ui-test.exe",
            ]
        )
        run([OUT / "classic-ui-test.exe"], timeout=120)
    print(f"Built native ROMAW frontend: {OUT / 'libfinder.a'}")


if __name__ == "__main__":
    main()
