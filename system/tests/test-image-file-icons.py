#!/usr/bin/env python3
# +===================================================================
#   File:      test-image-file-icons.py
#
#   Summary:   Verify Finder type fallbacks and Windows image-file icons against shared ICNS art.
#
#   Classes:   Icon.
#
#   Functions: main.
#
#   Origin:    Executor image-file mapping regression fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
import ctypes
import importlib.util
import json
import subprocess
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mac_icons


class Icon(ctypes.Structure):
    _fields_ = [("planes", ctypes.c_ubyte * 2240), ("present", ctypes.c_uint)]


def windows_frames(data):
    """Independently decode every generated ICO size and color depth."""
    reserved, kind, count = struct.unpack_from("<HHH", data)
    assert reserved == 0 and kind == 1 and count > 0
    result = []
    for index in range(count):
        width, height, _, _, planes, depth, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + index * 16)
        width, height = width or 256, height or 256
        assert planes == 1 and depth in (1, 4, 8, 24, 32)
        header, w, h, dib_planes, bits, compression = struct.unpack_from("<IiiHHI", data, offset)
        assert (header, w, h, dib_planes, bits, compression) == (40, width, height * 2, 1, depth, 0)
        assert offset + length <= len(data)
        colors = struct.unpack_from("<I", data, offset + 32)[0] if depth <= 8 else 0
        palette = [tuple(reversed(data[offset + 40 + i * 4 : offset + 43 + i * 4])) for i in range(colors)]
        pixels = offset + 40 + colors * 4
        stride = ((width * depth + 31) // 32) * 4
        mask = pixels + stride * height
        mask_stride = ((width + 31) // 32) * 4
        alpha_used = depth == 32 and any(
            data[pixels + y * stride + x * 4 + 3] for y in range(height) for x in range(width)
        )
        rows = []
        for y in range(height):
            row = height - y - 1
            raster = []
            for x in range(width):
                if depth <= 8:
                    bit = x * depth
                    color = data[pixels + row * stride + bit // 8] >> (8 - depth - bit % 8) & ((1 << depth) - 1)
                    rgb, alpha = palette[color], 255
                else:
                    position = pixels + row * stride + x * (depth // 8)
                    rgb = tuple(reversed(data[position : position + 3]))
                    alpha = data[position + 3] if alpha_used else 255
                if data[mask + row * mask_stride + x // 8] & (128 >> (x % 8)):
                    alpha = 0
                raster.append(rgb + (alpha,))
            rows.append(raster)
        result.append((width, height, depth, rows))
    return result


def main():
    spec = importlib.util.spec_from_file_location("builder", ROOT / "tools/generate-folder-icons.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    wanted = {
        ".hfs": "file_hfs.icns",
        ".hda": "file_hfs.icns",
        ".hfv": "file_hfs.icns",
        ".dmg": "file_dmg.icns",
        ".iso": "file_iso.icns",
        ".cdr": "file_iso.icns",
        ".img": "file_img.icns",
        ".image": "file_img.icns",
        ".toast": "file_toast.icns",
        ".dsk": "file_dsk.icns",
    }
    wanted_types = {
        "dfil": "type_dfil_2.icns",
        "DFIL": "type_dfil_1.icns",
        "FNDR": "mac.icns",
        "ZSYS": "mac.icns",
        "APPL": "type_APPL.icns",
        "TEXT": "-4000_genericDocumentIconResource.icns",
        "PICT": "type_PICT.icns",
        "kfil": "type_kfil.icns",
        "FFIL": "type_ffil_1.icns",
        "ffil": "type_ffil_2.icns",
        "sfil": "type_sfil.icns",
        "tfil": "type_tfil.icns",
        "pref": "type_pref.icns",
        "INIT": "type_INIT.icns",
        "shlb": "type_INIT.icns",
    }
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        builder.generate(work)
        builder.generate_windows(work)
        library = work / "icons.so"
        subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-std=c17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(ROOT / "finder-metadata/include"),
                "-I" + str(work),
                str(ROOT / "finder-metadata/src/specialicons.c"),
                str(ROOT / "finder-metadata/src/icons.c"),
                "-o",
                str(library),
            ],
            check=True,
        )
        shared = ctypes.CDLL(str(library))
        lookup = shared.finder_image_extension_icon
        lookup.argtypes = [ctypes.c_char_p, ctypes.POINTER(Icon)]
        lookup.restype = ctypes.c_int
        manifest = json.loads((work / "mapping.json").read_text())
        resources = (work / "resources.rc").read_text()
        assert {row["resource_id"] for row in manifest} == set(range(101, 107))
        for row in manifest:
            assert f'{row["resource_id"]} ICON "{row["icon"]}"' in resources
        assert {extension: row["source"] for row in manifest for extension in row["extensions"]} == wanted
        for extension, filename in wanted.items():
            data = (ROOT / "bitmaps" / filename).read_bytes()
            expected = b"".join(
                builder.family_resource(data, kind) for kind in (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8")
            )
            for suffix in [extension, extension.upper()]:
                icon = Icon()
                assert lookup(suffix.encode(), ctypes.byref(icon)) == 1
                assert icon.present == 63 and bytes(icon.planes) == expected, suffix
        for extension in [b"", b".txt", b".hfsx", b".dmg.bak", b".is", None]:
            assert lookup(extension, ctypes.byref(Icon())) == 0, extension
        type_lookup = shared.finder_type_icon
        type_lookup.argtypes = [ctypes.c_uint32, ctypes.POINTER(Icon)]
        type_lookup.restype = ctypes.c_int
        for file_type, filename in wanted_types.items():
            expected = b"".join(
                builder.family_resource((ROOT / "bitmaps" / filename).read_bytes(), kind)
                for kind in (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8")
            )
            icon = Icon()
            code = int.from_bytes(file_type.encode("ascii"), "big")
            assert type_lookup(code, ctypes.byref(icon)) == 1
            assert icon.present == 63 and bytes(icon.planes) == expected, file_type
            assert type_lookup(code, None) == 0
        for unknown in (0, 0x4466696C, 0x74657874, 0x42494E41):
            icon = Icon()
            ctypes.memset(ctypes.byref(icon), 0xA5, ctypes.sizeof(icon))
            before = bytes(icon)
            assert type_lookup(unknown, ctypes.byref(icon)) == 0
            assert bytes(icon) == before
        # A real BNDL/FREF mapping must provide its own artwork ahead of APPL's
        # generic fallback, including a nonstandard local icon number.
        from romaw.system_resources import build_fork

        bundle = b"TEST\0\0\0\1FREF\0\0\0\0\0\x80ICN#\0\0\0\7\1\x2c"
        art = (ROOT / "bitmaps/-4000_genericDocumentIconResource.icns").read_bytes()
        planes = [
            (kind, builder.family_resource(art, kind))
            for kind in (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8")
        ]
        records = [(b"BNDL", 128, 0, None, bundle), (b"FREF", 128, 0, None, b"APPL\0\7\0")]
        records += [(kind, 300, 0, None, plane) for kind, plane in planes]
        fork = build_fork(records)
        parse = shared.finder_parse_icon
        parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(Icon)]
        parse.restype = ctypes.c_int
        bundled = Icon()
        assert parse(
            fork, len(fork), int.from_bytes(b"TEST", "big"), int.from_bytes(b"APPL", "big"), ctypes.byref(bundled)
        )
        assert bytes(bundled.planes) == b"".join(plane for _, plane in planes)
        generic = Icon()
        assert type_lookup(int.from_bytes(b"APPL", "big"), ctypes.byref(generic))
        assert bytes(bundled.planes) != bytes(generic.planes)
        for row in manifest:
            source = mac_icons.decode((ROOT / "bitmaps" / row["source"]).read_bytes())
            windows = windows_frames((work / row["icon"]).read_bytes())
            assert len(windows) == len(source), row["key"]
            for (width, height, depth, pixels), frame in zip(windows, source):
                assert (width, height, depth) == (frame["size"], frame["height"], frame["depth"])
                assert pixels == mac_icons.rgba(frame), (row["key"], width, height, depth)
    print(
        f"PASS: {len(wanted_types)} generic types, all {len(wanted)} extensions, "
        "Finder planes and every Windows icon size/depth match requested ICNS files"
    )


if __name__ == "__main__":
    main()
