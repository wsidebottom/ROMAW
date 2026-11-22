# +===================================================================
#   File:      check.py
#
#   Summary:   Cross-check C++ StuffIt output with an independent archive decoder.
#
#   Classes:   None.
#
#   Functions: main, crc16.
#
#   Origin:    ROMAW regression fixture for the StuffIt bridge.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Run from WSL; generated fixtures remain under the selected build directory."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def crc16(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/stuffit-regression")
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)

    def run(command):
        subprocess.run([str(value) for value in command], check=True, cwd=ROOT)

    run([sys.executable, ROOT / "stuffit/build.py", "--native", "--output-dir", out])
    run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Istuffit/include",
            "-Istuffit/third_party/stuffit-rs/stuffit-ffi/include",
            "stuffit/src/archive.cpp",
            "stuffit/tests/archive_test.cpp",
            out / "libstuffit_ffi.a",
            "-ldl",
            "-lpthread",
            "-lm",
            "-o",
            out / "archive-test",
        ]
    )
    archive = out / "fixture.sit"
    run([out / "archive-test", archive])
    raw = archive.read_bytes()
    visited = set()

    def check_headers(offset):
        while offset:
            assert offset not in visited
            visited.add(offset)
            assert raw[offset : offset + 4] == b"\xa5" * 4
            assert crc16(raw[offset : offset + 32]) == int.from_bytes(raw[offset + 32 : offset + 34], "big")
            if raw[offset + 9] & 0x40:
                check_headers(int.from_bytes(raw[offset + 34 : offset + 38], "big"))
            offset = int.from_bytes(raw[offset + 22 : offset + 26], "big")

    check_headers(int.from_bytes(raw[94:98], "big"))
    assert len(visited) == 14
    run(["lsar", "-t", archive])
    extracted = out / "extracted"
    run(["unar", "-f", "-e", "MacRoman", "-o", extracted, archive])
    folder = extracted / "Folder"
    assert (folder / "Empty").is_dir() and not any((folder / "Empty").iterdir())
    assert (folder / "Zero").read_bytes() == b""
    assert (folder / "café").read_bytes() == bytes((i * 73 + i // 11) & 255 for i in range(8193))
    sidecar = (folder / "café.rsrc").read_bytes()
    magic, version = struct.unpack_from(">II", sidecar)
    assert (magic, version) == (0x00051607, 0x00020000)
    parts = {}
    for i in range(int.from_bytes(sidecar[24:26], "big")):
        kind, offset, size = struct.unpack_from(">III", sidecar, 26 + i * 12)
        assert offset <= len(sidecar) and size <= len(sidecar) - offset and kind not in parts
        parts[kind] = sidecar[offset : offset + size]
    assert parts[2] == bytes((i * 31 + i // 17) & 255 for i in range(4097))
    assert parts[9][:10] == b"APPLTEST\x44\x00"
    for size in (0, 1, 2, 31, 32, 255, 256, 4095, 4096, 8193):
        state = size + 1

        def pattern(length):
            nonlocal state
            result = bytearray()
            for _ in range(length):
                state ^= (state << 13) & 0xFFFFFFFF
                state ^= state >> 17
                state ^= (state << 5) & 0xFFFFFFFF
                result.append(state & 255)
            return bytes(result)

        # unar omits the physical empty data file for resource-only entries.
        # The bridge read-back and lsar still validate that fork's zero length.
        data_file = folder / f"Random{size}"
        assert (data_file.read_bytes() if data_file.exists() else b"") == pattern(size)
        resource = (folder / f"Random{size}.rsrc").read_bytes()
        for i in range(int.from_bytes(resource[24:26], "big")):
            kind, offset, length = struct.unpack_from(">III", resource, 26 + i * 12)
            if kind == 2:
                assert resource[offset : offset + length] == pattern(size * 2 + 3)
                break
        else:
            raise AssertionError("Independent decoder lost the resource fork")
    print("Independent StuffIt extraction: exact data/resource forks, Finder codes/flags and all header CRCs passed")


if __name__ == "__main__":
    main()
