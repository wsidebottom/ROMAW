#!/usr/bin/env python3
# +===================================================================
#   File:      test-icon-file-decoding.py
#
#   Summary:   Generate independent icon fixtures and build the shared Windows decoder regression executable.
#
#   Classes:   None declared.
#
#   Functions: main, frame, flatten.
#
#   Origin:    Original Executor ICO/ICNS regression fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
import struct
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mac_icons
from romaw import system_resources


def frame(size, depth):
    """Create asymmetric pixels, transparent corners and partial alpha."""
    rows = []
    for y in range(size):
        rows.append(
            [
                (255 if x < size // 2 else 0, 255 if y < size // 2 else 0, 96, (0, 96, 255)[(x + y) % 3])
                for x in range(size)
            ]
        )
    source = mac_icons.decode_png(mac_icons.png(rows), size, size)
    if depth == 24:
        source = dict(source, depth=24, image=bytes(b for i, b in enumerate(source["image"]) if i % 4 != 3))
    return source if depth >= 24 else mac_icons.classic_frame(source, size, depth)


def flatten(rows):
    return bytes(channel for row in rows for pixel in row for channel in pixel)


def main():
    work = ROOT / "build/icon-file-decoding"
    work.mkdir(parents=True, exist_ok=True)
    cases = {}
    for depth in (1, 4, 8, 24, 32):
        frames = [frame(size, depth) for size in (16, 32)]
        cases[f"dib{depth}.ico"] = (mac_icons.encode_ico(frames), frames)
    frames = [frame(size, 32) for size in (16, 32)]
    payload = mac_icons.png(mac_icons.rgba(frames[1]))
    cases["png.ico"] = (
        struct.pack("<HHHBBBBHHII", 0, 1, 1, 32, 32, 0, 0, 1, 32, len(payload), 22) + payload,
        [frames[1]],
    )
    cases["exact.icns"] = (mac_icons.encode(frames), frames)
    cases["png.icns"] = (mac_icons.icns_family([mac_icons.icns_chunk(b"icp5", payload)]), [frames[1]])
    # Real classic planes exercise the portable path with no PNG dependency.
    source = (ROOT / "bitmaps/-4000_genericDocumentIconResource.icns").read_bytes()
    classic = [
        mac_icons.classic_frame(mac_icons.best_frame(mac_icons.decode(source), size), size, depth)
        for size in (16, 32)
        for depth in (1, 4, 8)
    ]
    chunks = []
    for kind in (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8"):
        chunks.append(mac_icons.icns_chunk(kind, mac_icons.resource(mac_icons.encode(classic), kind)))
    cases["classic.icns"] = (mac_icons.icns_family(chunks), classic)
    for name, (data, expected) in cases.items():
        (work / name).write_bytes(data)
        # The resource tool must accept ICO and ICNS as equivalent source art.
        decoded = mac_icons.decode(data)
        for size in (16, 32):
            wanted = flatten(mac_icons.resized_rows(mac_icons.best_frame(expected, size), size))
            actual = flatten(mac_icons.resized_rows(mac_icons.best_frame(decoded, size), size))
            assert wanted == actual, name
            (work / f"{name}.{size}.rgba").write_bytes(wanted)
        for kind in (b"ICN#", b"ics#", b"icl4", b"icl8", b"ics4", b"ics8"):
            assert mac_icons.resource(data, kind)
        for length in (0, 1, 5, len(data) - 1):
            try:
                mac_icons.decode(data[:length])
            except ValueError:
                pass
            else:
                raise AssertionError(f"Truncated source accepted: {name}, {length}")
    (work / "cases.txt").write_text("\n".join(cases) + "\n")
    # Feed a Rez catalog entry through the real resource compiler, using an ICO
    # beside the shared ICNS artwork rather than a private resource directory.
    source_path = ROOT / "bitmaps/test-source.ico"
    assert system_resources.resource_path(ROOT / "system/resources/system", "bitmaps/test-source.ico") == source_path
    entry = dict(type_hex=b"icl8".hex(), id=300, attributes=0, name_hex=None, file="bitmaps/test-source.ico", icon=True)
    directory = ROOT / "system/resources/system"
    original_read = Path.read_bytes
    with patch.object(system_resources, "load_scope", return_value=({"resources": [entry]}, directory)):
        with patch.object(
            Path, "read_bytes", lambda path: cases["dib8.ico"][0] if path == source_path else original_read(path)
        ):
            records = system_resources.read_resources(directory)
    assert records == [(b"icl8", 300, 0, None, mac_icons.resource(cases["dib8.ico"][0], b"icl8"))]
    compiler = "x86_64-w64-mingw32-g++"
    subprocess.run(
        [
            "x86_64-w64-mingw32-gcc",
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I" + str(ROOT / "finder-metadata/include"),
            "-c",
            str(ROOT / "finder-metadata/src/icons.c"),
            "-o",
            str(work / "icons.o"),
        ],
        check=True,
    )
    subprocess.run(
        [
            compiler,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-static",
            "-I" + str(ROOT / "finder-metadata/include"),
            str(ROOT / "finder-metadata/tests/icon_file_test.cpp"),
            str(ROOT / "finder-metadata/src/iconfiles.cpp"),
            str(work / "icons.o"),
            "-lole32",
            "-luuid",
            "-o",
            str(work / "icon-file-test.exe"),
        ],
        check=True,
    )
    print(
        "PASS: ICO/ICNS resource sources; run build/icon-file-decoding/icon-file-test.exe with its fixture directory on Windows"
    )


if __name__ == "__main__":
    main()
