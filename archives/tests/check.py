# +===================================================================
#   File:      check.py
#
#   Summary:   Verify Macintosh archive encoding and extraction against independent fixtures and decoders.
#
#   Classes:   None.
#
#   Functions: main, appledouble, macbinary, binhex.
#
#   Origin:    ROMAW archive regression fixtures using standard Python codecs.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Exercise the production codec before publishing extracted data to any destination."""
import binascii
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/archive-regression"
DATA = bytes((i * 73 + i // 11) & 255 for i in range(4097))
RESOURCE = b"Resource\x90\x90\x90" * 87
FINDER = b"APPLTEST\x44\x01" + bytes(range(10, 32))


def appledouble(resource=None):
    resource = RESOURCE if resource is None else resource
    return (
        struct.pack(">II16sHIIIIII", 0x51607, 0x20000, bytes(16), 2, 9, 50, 32, 2, 82, len(resource))
        + FINDER
        + resource
    )


def macbinary(version):
    header = bytearray(128)
    header[1:6] = b"\x04Test"
    header[65:73] = FINDER[:8]
    header[73] = FINDER[8]
    header[75:81] = FINDER[10:16]
    struct.pack_into(">II", header, 83, len(DATA), len(RESOURCE))
    if version:
        header[101] = FINDER[9]
        header[122:124] = bytes((version, 129))
        if version == 130:
            header[102:106] = b"mBIN"
            header[106:108] = FINDER[24:26]
        struct.pack_into(">H", header, 124, binascii.crc_hqx(header[:124], 0))
    return bytes(header) + DATA + bytes(-len(DATA) % 128) + RESOURCE + bytes(-len(RESOURCE) % 128)


def binhex():
    header = b"\x04Test\0" + FINDER[:10] + struct.pack(">II", len(DATA), len(RESOURCE))
    raw = b"".join(part + struct.pack(">H", binascii.crc_hqx(part, 0)) for part in (header, DATA, RESOURCE))
    encoded = bytearray()
    offset = 0
    while offset < len(raw):
        end = offset + 1
        while end < len(raw) and raw[end] == raw[offset] and end - offset < 255:
            end += 1
        count = end - offset
        literal = bytes((raw[offset],)) if raw[offset] != 0x90 else b"\x90\0"
        encoded += literal + bytes((0x90, count)) if count >= 3 else literal * count
        offset = end
    raw = encoded
    alphabet = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
    bits = count = 0
    text = ""
    for byte in raw:
        bits = (bits << 8) | byte
        count += 8
        while count >= 6:
            count -= 6
            text += alphabet[(bits >> count) & 63]
    if count:
        text += alphabet[(bits << (6 - count)) & 63]
    return ("(This file must be converted with BinHex 4.0)\r\n:" + text + ":\r\n").encode()


def stuffit_classic(name=b"Test"):
    def crc(data):
        value = 0
        for byte in data:
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0xA001 if value & 1 else 0)
        return value

    header = bytearray(112)
    header[2] = len(name)
    header[3 : 3 + len(name)] = name
    header[66:76] = FINDER[:10]
    struct.pack_into(
        ">IIIIHH", header, 84, len(RESOURCE), len(DATA), len(RESOURCE), len(DATA), crc(RESOURCE), crc(DATA)
    )
    struct.pack_into(">H", header, 110, crc(header[:110]))
    archive = bytearray(22)
    archive[:4] = b"SIT!"
    struct.pack_into(">HI", archive, 4, 1, 22 + len(header) + len(RESOURCE) + len(DATA))
    archive[10:15] = b"rLau\x01"
    return archive + header + RESOURCE + DATA


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    def run(args, success=True):
        result = subprocess.run([str(arg) for arg in args], cwd=ROOT, capture_output=True, text=True)
        if (result.returncode == 0) != success:
            raise AssertionError(result.stdout + result.stderr)
        return result

    run([sys.executable, ROOT / "stuffit/build.py", "--native", "--output-dir", OUT / "stuffit"])
    run([sys.executable, ROOT / "archives/build.py", "--native", "--output-dir", OUT])
    probe = OUT / "archive-probe"
    run(
        [
            "g++",
            "-std=c++17",
            "-Iarchives/include",
            "archives/tests/probe.cpp",
            OUT / "libromaw-archives.a",
            OUT / "stuffit/libstuffit_ffi.a",
            "-ldl",
            "-lpthread",
            "-lm",
            "-o",
            probe,
        ]
    )
    fixtures = {
        "binhex": binhex(),
        "stuffit-classic": stuffit_classic(),
        **{f"macbinary-{v}": macbinary(v) for v in (0, 129, 130)},
    }
    carrier = OUT / "classic-icon-carrier.sit"
    carrier.write_bytes(stuffit_classic(b"Icon\r"))
    with tempfile.TemporaryDirectory(dir=OUT) as output:
        run([probe, "decode", carrier, output])
        assert (Path(output) / "Icon%0D").read_bytes() == DATA
        assert (Path(output) / "._Icon%0D").read_bytes()[82:] == RESOURCE
    zipped = io.BytesIO()
    with zipfile.ZipFile(zipped, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Folder/", b"")
        archive.writestr("Folder/Test", DATA)
        archive.writestr("__MACOSX/Folder/._Test", appledouble())
        archive.writestr("Folder/Empty/", b"")
    fixtures["ditto-zip"] = zipped.getvalue()
    # Independently parse the writer's standard header and fork offsets, then
    # exercise our decoder to verify the full secondary FinderInfo as well.
    for name, data, resource in (
        ("café", DATA, RESOURCE),
        ("Long" * 12, b"", b""),
        ("LargeFork", DATA, RESOURCE * 100),
        ("Reordered", DATA, RESOURCE),
        ("PaddedResource", DATA, RESOURCE),
        ("FinderOnly", DATA, b""),
    ):
        source = OUT / "binary-input.zip"
        raw = io.BytesIO()
        mac_metadata = appledouble(resource)
        if name == "Reordered":
            mac_metadata = (
                struct.pack(
                    ">II16xHIIIIIIIII",
                    0x51607,
                    0x20000,
                    3,
                    2,
                    62,
                    len(resource),
                    9,
                    62 + len(resource),
                    32,
                    4,
                    94 + len(resource),
                    14,
                )
                + resource
                + FINDER
                + b"Finder comment"
            )
        elif name == "PaddedResource":
            # A resource-fork rewrite appended the working fork after obsolete bytes.
            stale = bytes(range(256)) * 280
            mac_metadata = bytearray(mac_metadata[:82] + stale + resource)
            struct.pack_into(">I", mac_metadata, 42, 82 + len(stale))
        elif name == "FinderOnly":
            mac_metadata = struct.pack(">II16xHIII", 0x51607, 0x20000, 1, 9, 38, 32) + FINDER
        with zipfile.ZipFile(raw, "w") as archive:
            archive.writestr(name, data)
            archive.writestr("__MACOSX/._" + name, mac_metadata)
        source.write_bytes(raw.getvalue())
        output = OUT / (name + ".bin")
        run([probe, "macbinary", source, output])
        encoded = output.read_bytes()
        assert encoded[2 : 2 + encoded[1]].decode("mac_roman") == name
        assert encoded[65:73] == FINDER[:8] and encoded[73] == FINDER[8] and encoded[101] == FINDER[9]
        assert int.from_bytes(encoded[124:126], "big") == binascii.crc_hqx(encoded[:124], 0)
        secondary_length = int.from_bytes(encoded[120:122], "big")
        assert secondary_length < 65536
        data_at = 128 + (secondary_length + 127) // 128 * 128
        resource_at = data_at + (len(data) + 127) // 128 * 128
        assert struct.unpack_from(">II", encoded, 83) == (len(data), len(resource))
        assert encoded[data_at : data_at + len(data)] == data
        assert encoded[resource_at : resource_at + len(resource)] == resource
        destination = OUT / ("binary-roundtrip-" + name)
        run([probe, "decode", output, destination])
        assert (destination / name).read_bytes() == data
        metadata = (destination / ("._" + name)).read_bytes()
        parts = {}
        for i in range(int.from_bytes(metadata[24:26], "big")):
            kind, offset, length = struct.unpack_from(">III", metadata, 26 + i * 12)
            parts[kind] = metadata[offset : offset + length]
        assert parts[9] == FINDER and parts[2] == resource
        if name == "Reordered":
            assert parts[4] == b"Finder comment"
        independent = OUT / ("binary-independent-" + name)
        run(["unar", "-f", "-e", "macintosh", "-o", independent, output])
        assert (independent / name).read_bytes() == data
        if resource:
            metadata = (independent / (name + ".rsrc")).read_bytes()
            parts = {}
            for i in range(int.from_bytes(metadata[24:26], "big")):
                kind, offset, length = struct.unpack_from(">III", metadata, 26 + i * 12)
                parts[kind] = metadata[offset : offset + length]
            assert parts[2] == resource
    for name in ("Unsupported😀", "x" * 64, "Folder/"):
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as archive:
            archive.writestr(name, b"")
        source = OUT / "binary-unrepresentable.zip"
        source.write_bytes(raw.getvalue())
        output = OUT / "binary-must-not-create.bin"
        run([probe, "macbinary", source, output], False)
        assert not output.exists()
    broken_zip = bytearray(fixtures["ditto-zip"])
    with zipfile.ZipFile(io.BytesIO(broken_zip)) as archive:
        entry = archive.getinfo("Folder/Test")
        name_length, extra_length = struct.unpack_from("<HH", broken_zip, entry.header_offset + 26)
        broken_zip[entry.header_offset + 30 + name_length + extra_length] ^= 0x20
    broken_zip_path = OUT / "crc-broken.zip"
    broken_zip_path.write_bytes(broken_zip)
    run([probe, "decode", broken_zip_path, OUT / "must-not-create"], False)
    for name, raw in fixtures.items():
        source = OUT / (name + ".archive")
        source.write_bytes(raw)
        extracted = OUT / (name + "-out")
        run([probe, "decode", source, extracted])
        file = extracted / ("Folder/Test" if name == "ditto-zip" else "Test")
        assert file.read_bytes() == DATA
        ad = file.with_name("._" + file.name).read_bytes()
        parts = {}
        for i in range(int.from_bytes(ad[24:26], "big")):
            kind, offset, length = struct.unpack_from(">III", ad, 26 + i * 12)
            parts[kind] = ad[offset : offset + length]
        assert parts[2] == RESOURCE and parts[9][:8] == FINDER[:8]
        assert parts[9][8] == FINDER[8]
        if name != "macbinary-0":
            assert parts[9][9] == FINDER[9]
        if name == "ditto-zip":
            assert ad == appledouble() and (extracted / "Folder/Empty").is_dir()
            generated = OUT / "generated.zip"
            run([probe, "zip", source, generated])
            with zipfile.ZipFile(generated) as archive:
                assert archive.testzip() is None
                assert archive.read("Folder/Test") == DATA
                assert archive.read("__MACOSX/Folder/._Test") == appledouble()
    for name in ("binhex", "macbinary-130"):
        broken = bytearray(fixtures[name])
        broken[len(broken) // 3] ^= 1
        source = OUT / (name + "-broken")
        source.write_bytes(broken)
        # MacBinary has no fork CRC, so corrupt its protected header explicitly.
        if name.startswith("macbinary"):
            broken[65] ^= 1
            source.write_bytes(broken)
        run([probe, "decode", source, OUT / "must-not-create"], False)
        assert not (OUT / "must-not-create").exists()
    for name in ("../escape", "/absolute", "C:/escape", "Folder/../../escape", "NUL.txt", "Folder/bad:stream"):
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as archive:
            archive.writestr(name, b"bad")
        source = OUT / "unsafe.zip"
        source.write_bytes(raw.getvalue())
        run([probe, "decode", source, OUT / "must-not-create"], False)
        assert not (OUT / "must-not-create").exists()
    encoder = OUT / "stuffit-encoder-fixture"
    run(
        [
            "g++",
            "-std=c++17",
            "-Istuffit/include",
            "-Istuffit/third_party/stuffit-rs/stuffit-ffi/include",
            "stuffit/src/archive.cpp",
            "stuffit/tests/archive_test.cpp",
            OUT / "stuffit/libstuffit_ffi.a",
            "-ldl",
            "-lpthread",
            "-lm",
            "-o",
            encoder,
        ]
    )
    sit = OUT / "fixture.sit"
    run([encoder, sit])
    sit_output = OUT / "stuffit-out"
    run([probe, "decode", sit, sit_output])
    assert (sit_output / "Folder/café").read_bytes() == bytes((i * 73 + i // 11) & 255 for i in range(8193))
    sit_raw = sit.read_bytes()
    root = int.from_bytes(sit_raw[94:98], "big")
    offsets = []

    def visit(offset):
        while offset:
            offsets.append(offset)
            if sit_raw[offset + 9] & 0x40:
                visit(int.from_bytes(sit_raw[offset + 34 : offset + 38], "big"))
            offset = int.from_bytes(sit_raw[offset + 22 : offset + 26], "big")

    visit(root)
    file = next(
        offset
        for offset in offsets
        if not sit_raw[offset + 9] & 0x40 and int.from_bytes(sit_raw[offset + 34 : offset + 38], "big")
    )
    full_info = bytearray(sit_raw)
    metadata_offset = file + int.from_bytes(full_info[file + 6 : file + 8], "big") + 4
    full_info[metadata_offset : metadata_offset + 32] = FINDER
    full_source = OUT / "full-finder-info.sit"
    full_source.write_bytes(full_info)
    run([probe, "decode", full_source, OUT / "full-finder-info-out"])
    full_ad = (OUT / "full-finder-info-out/Folder/._café").read_bytes()
    assert full_ad[50:82] == FINDER
    bad = bytearray(sit_raw)
    bad[file + 42] ^= 1
    corrupt = OUT / "crc-broken.sit"
    corrupt.write_bytes(bad)
    run([probe, "decode", corrupt, OUT / "must-not-create"], False)
    struct.pack_into(">I", bad, file + 34, 0xF0000000)
    corrupt.write_bytes(bad)
    run([probe, "decode", corrupt, OUT / "must-not-create"], False)
    bad = bytearray(sit_raw)
    struct.pack_into(">I", bad, root + 34, root)
    corrupt.write_bytes(bad)
    run([probe, "decode", corrupt, OUT / "must-not-create"], False)
    for entries in (("Duplicate", "duplicate"), ("file", "file/child")):
        raw = io.BytesIO()
        with zipfile.ZipFile(raw, "w") as archive:
            for name in entries:
                archive.writestr(name, b"bad")
        corrupt.write_bytes(raw.getvalue())
        run([probe, "decode", corrupt, OUT / "must-not-create"], False)
    raw = io.BytesIO()
    with zipfile.ZipFile(raw, "w") as archive:
        link = zipfile.ZipInfo("link")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        archive.writestr(link, "../escape")
    corrupt.write_bytes(raw.getvalue())
    run([probe, "decode", corrupt, OUT / "must-not-create"], False)
    print(
        "ZIP, StuffIt 1/5, BinHex and MacBinary I/II/III: exact forks, FinderInfo, ditto layout and safe failure checks passed"
    )
    print("MacBinary encoding: independent unar fork checks, full FinderInfo round-trips and filename failures passed")


if __name__ == "__main__":
    main()
