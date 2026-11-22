# +===================================================================
#   File:      test_images.py
#
#   Summary:   Original HFS+/UDIF fixtures, fork preservation and bounded failure checks.
#
#   Classes:   None declared.
#
#   Functions: u16, u32, u64, fork, hfsplus, classic_hfs, dmg, main
#
#   Origin:    Existing repository source in packages/disk-images/tests; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Original HFS+/UDIF fixtures, fork preservation and bounded failure checks."""

from pathlib import Path
import bz2
import hashlib
import lzma
import plistlib
import struct
import subprocess
import sys
import tempfile
import zlib


def u16(b, o, n):
    struct.pack_into(">H", b, o, n)


def u32(b, o, n):
    struct.pack_into(">I", b, o, n)


def u64(b, o, n):
    struct.pack_into(">Q", b, o, n)


def fork(b, o, size, start, count):
    u64(b, o, size)
    u32(b, o + 12, count)
    u32(b, o + 16, start)
    u32(b, o + 20, count)


def hfsplus(bad_name=False):
    block = 4096
    raw = bytearray(block * 128)
    header = memoryview(raw)[1024:1536]
    u16(header, 0, 0x482B)
    u16(header, 2, 4)
    u32(header, 4, 256)
    u32(header, 32, 1)
    u32(header, 36, 1)
    u32(header, 40, block)
    u32(header, 44, 128)
    u32(header, 48, 100)
    fork(header, 272, block * 2, 4, 2)
    tree = memoryview(raw)[block * 4 : block * 5]
    tree[8] = 1
    u16(tree, 10, 3)
    u16(tree, 14, 1)
    for off, n in [(16, 1), (20, 3), (24, 1), (28, 1), (36, 2)]:
        u32(tree, off, n)
    u16(tree, 32, block)
    u16(tree, 34, 516)
    tree[51] = 0xBC
    for i, off in enumerate([14, 120, 248, 256]):
        u16(tree, block - 2 - 2 * i, off)

    def record(parent, name, directory, identity):
        key = struct.pack(">IH", parent, len(name)) + name.encode("utf-16be")
        body = bytearray(88 if directory else 248)
        u16(body, 0, 1 if directory else 2)
        u32(body, 8, identity)
        if not directory:
            body[48:56] = b"TEXTttxt"
            fork(body, 88, 16, 10, 1)
            fork(body, 168, 4, 11, 1)
        return struct.pack(">H", len(key)) + key + body

    records = [
        record(1, "Mount Test", True, 2),
        record(2, "Folder", True, 17),
        record(2, ".." if bad_name else "Hello", False, 16),
    ]
    leaf = memoryview(raw)[block * 5 : block * 6]
    leaf[8], leaf[9] = 255, 1
    u16(leaf, 10, len(records))
    at = 14
    for i, record in enumerate(records):
        u16(leaf, block - 2 - i * 2, at)
        leaf[at : at + len(record)] = record
        at += len(record)
    u16(leaf, block - 2 - len(records) * 2, at)
    raw[block * 10 : block * 10 + 16] = b"Image mount test"
    raw[block * 11 : block * 11 + 4] = b"RSRC"
    return raw


def classic_hfs(custom_icon=False):
    """Original 64 KiB HFS image: empty extents tree, two catalog leaves."""
    raw = bytearray(512 * 128)
    m = memoryview(raw)[1024:1536]
    u16(m, 0, 0x4244)
    u16(m, 14, 3)
    u16(m, 18, 120)
    u32(m, 20, 512)
    u16(m, 28, 4)
    u32(m, 24, 512)
    u32(m, 30, 32)
    u16(m, 34, 100)
    m[36:47] = b"\x0aMount Test"
    u32(m, 130, 512)
    struct.pack_into(">HH", m, 134, 0, 1)
    u32(m, 146, 1536)
    struct.pack_into(">HH", m, 150, 1, 3)
    for offset, first in [(2048, 0), (2560, 1)]:
        header = memoryview(raw)[offset : offset + 512]
        header[8] = 1
        u16(header, 10, 3)
        u16(header, 32, 512)
        u32(header, 24, first)
        u16(header, 14, int(bool(first)))
        u32(header, 16, first)
        u32(header, 28, first)
        u16(header, 34, 37 if first else 7)
        u32(header, 36, 3 if first else 1)
        for i, at in enumerate([14, 120, 248, 256]):
            u16(header, 510 - i * 2, at)
        header[248] = 0xC0 if first else 0x80

    def record(parent, name, directory, identity):
        name = name.encode("mac_roman")
        key = bytes([(6 + len(name)) | 1, 0]) + struct.pack(">I", parent) + bytes([len(name)]) + name
        key += b"\0" * (len(key) % 2)
        body = bytearray(70 if directory else 102)
        body[0] = 1 if directory else 2
        u32(body, 6 if directory else 20, identity)
        if not directory:
            body[4:12] = b"TEXTttxt"
            u32(body, 26, 16)
            u32(body, 30, 512)
            u32(body, 36, 4)
            u32(body, 40, 512)
            struct.pack_into(">HH", body, 74, 4, 1)
            struct.pack_into(">HH", body, 86, 5, 1)
        return key + body

    def thread(identity, parent, name):
        body = bytearray(46)
        body[0] = 3
        u32(body, 10, parent)
        body[14] = len(name)
        body[15 : 15 + len(name)] = name.encode("mac_roman")
        return bytes([7, 0]) + struct.pack(">I", identity) + b"\0\0" + body

    records = [
        record(1, "Mount Test", True, 2),
        thread(2, 1, "Mount Test"),
        record(2, "Folder", True, 17),
        record(2, "Hello", False, 16),
    ]
    if custom_icon:
        # Original ICN# resource -16455, with an opaque striped custom icon.
        resource = bytearray(566)
        struct.pack_into(">IIII", resource, 0, 256, 516, 260, 50)
        u32(resource, 256, 256)
        resource[260:388] = bytes([0xAA]) * 128
        resource[388:516] = bytes([255]) * 128
        resource[516:532] = resource[:16]
        struct.pack_into(">HHH", resource, 540, 28, 50, 0)
        resource[546:550] = b"ICN#"
        struct.pack_into(">HHhH", resource, 550, 0, 10, -16455, 65535)
        item = bytearray(record(2, "Icon\r", False, 18))
        offset = (item[0] + 2) & ~1
        u32(item, offset + 26, 0)
        u32(item, offset + 30, 0)
        u32(item, offset + 36, len(resource))
        u32(item, offset + 40, 1024)
        struct.pack_into(">HH", item, offset + 74, 0, 0)
        struct.pack_into(">HH", item, offset + 86, 7, 2)
        records.append(item)
        raw[5632 : 5632 + len(resource)] = resource
    records.append(thread(17, 2, "Folder"))
    groups = [records[:-1], records[-1:]] if custom_icon else [records]
    u32(raw, 2560 + 20, len(records))
    u32(raw, 2560 + 28, len(groups))
    for node, group in enumerate(groups, 1):
        leaf = memoryview(raw)[2560 + node * 512 : 3072 + node * 512]
        leaf[8], leaf[9] = 255, 1
        u32(leaf, 0, node + 1 if node < len(groups) else 0)
        u32(leaf, 4, node - 1)
        u16(leaf, 10, len(group))
        at = 14
        for i, item in enumerate(group):
            u16(leaf, 510 - 2 * i, at)
            leaf[at : at + len(item)] = item
            at += len(item)
        u16(leaf, 510 - 2 * len(group), at)
    raw[4096:4112] = b"Image mount test"
    raw[4608:4612] = b"RSRC"
    return raw


def dmg(raw, method, packed):
    table = bytearray(284)
    table[:4] = b"mish"
    struct.pack_into(">IQQQ", table, 4, 1, 0, len(raw) // 512, 0)
    u32(table, 200, 2)
    struct.pack_into(">IIQQQQ", table, 204, method, 0, 0, len(raw) // 512, 0, len(packed))
    struct.pack_into(">IIQ", table, 244, 0xFFFFFFFF, 0, len(raw) // 512)
    xml = plistlib.dumps({"resource-fork": {"blkx": [{"Name": "Mount Test (Apple_HFS : 1)", "Data": bytes(table)}]}})
    trailer = bytearray(512)
    trailer[:4] = b"koly"
    struct.pack_into(">II", trailer, 4, 4, 512)
    u64(trailer, 32, len(packed))
    struct.pack_into(">QQ", trailer, 216, len(packed), len(xml))
    u64(trailer, 492, len(raw) // 512)
    return packed + xml + trailer


def main():
    program = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[2]
    fixture = Path(tempfile.mkdtemp(prefix="images-", dir=root / "build"))
    cache = fixture / "cache"
    cache.mkdir()
    windows = program.suffix == ".exe" and sys.platform != "win32"

    def host(path):
        return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip() if windows else str(path)

    def run(data, label, valid=True):
        image = fixture / label
        image.write_bytes(data)
        before = hashlib.sha256(data).digest()
        cached = set(cache.iterdir())
        result = subprocess.run(
            [str(program), host(image), host(cache)],
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert (result.returncode == 0) == valid, (label, result.stdout, result.stderr)
        assert hashlib.sha256(image.read_bytes()).digest() == before
        if not valid:
            assert set(cache.iterdir()) == cached, label
            return
        kind, name, output = result.stdout.strip().split("|", 2)
        assert kind == "2" and name == "Mount Test", result.stdout
        if windows:
            output = subprocess.check_output(["wslpath", "-u", output], text=True).strip()
        output = Path(output)
        assert (output / "Folder").is_dir()
        assert (output / "Hello").read_bytes() == b"Image mount test"
        sidecar = (output / "._Hello").read_bytes()
        assert struct.unpack_from(">I", sidecar)[0] == 0x51607
        assert sidecar[50:58] == b"TEXTttxt" and sidecar[82:] == b"RSRC"

    raw = hfsplus()
    classic = classic_hfs()
    run(classic, "classic.hfv")
    deep_cache = cache / ("long-cache-" * 10) / ("long-cache-" * 10)
    deep_cache.mkdir(parents=True)
    output = (
        subprocess.check_output(
            [str(program), host(fixture / "classic.hfv"), host(deep_cache)],
            text=True,
            timeout=20,
        )
        .strip()
        .split("|", 2)[2]
    )
    if windows:
        output = subprocess.check_output(["wslpath", "-u", output], text=True).strip()
    assert (Path(output) / "Hello").read_bytes() == b"Image mount test"
    assert (Path(output) / "._Hello").read_bytes()[82:] == b"RSRC"
    header = bytearray(84)
    u32(header, 64, len(classic))
    header[82] = 1
    run(header + classic, "diskcopy.img")
    run(dmg(classic, 0x80000005, zlib.compress(classic)), "classic.dmg")
    broken = bytearray(classic)
    u32(broken, 3072, 1)  # self-referential leaf chain
    run(broken, "classic-cycle.hfv", False)
    broken = bytearray(classic)
    u16(broken, 3072 + 510, 511)
    run(broken, "classic-bounds.hfv", False)
    run(raw, "raw.hfsplus")
    x = bytearray(raw)
    struct.pack_into(">HH", x, 1024, 0x4858, 5)
    run(x, "raw.hfsx")
    apm = bytearray(4096 + len(raw))
    struct.pack_into(">HHI", apm, 0, 0x4552, 2048, len(apm) // 2048)
    struct.pack_into(">HHIII", apm, 2048, 0x504D, 0, 1, 2, len(raw) // 2048)
    apm[2096:2106] = b"Apple_HFS\0"
    apm[4096:] = raw
    run(apm, "partitioned.img")
    hybrid = bytearray(4096 + len(raw))
    struct.pack_into(">HHI", hybrid, 0, 0x4552, 512, len(hybrid) // 512)
    for index, start, blocks, kind in [
        (1, 1, 3, b"Apple_partition_map"),
        (2, 0, len(hybrid) // 512, b"ISO 9660"),
        (3, 8, len(raw) // 512, b"Apple_HFS"),
    ]:
        offset = index * 512
        struct.pack_into(">HHIII", hybrid, offset, 0x504D, 0, 3, start, blocks)
        hybrid[offset + 48 : offset + 48 + len(kind)] = kind
    hybrid[4096:] = raw
    run(hybrid, "hybrid-self-partition.iso")
    # A hybrid's ISO partition points at the whole disc, including its APM.
    # Both filesystems must mount without recursively reopening that map.
    hybrid = bytearray(131072 + len(raw))
    struct.pack_into(">HHI", hybrid, 0, 0x4552, 512, len(hybrid) // 512)
    for index, start, blocks, kind in [
        (1, 1, 3, b"Apple_partition_map"),
        (2, 0, len(hybrid) // 512, b"ISO 9660"),
        (3, 256, len(raw) // 512, b"Apple_HFS"),
    ]:
        offset = index * 512
        struct.pack_into(">HHIII", hybrid, offset, 0x504D, 0, 3, start, blocks)
        hybrid[offset + 48 : offset + 48 + len(kind)] = kind

    def iso_record(name, sector, length, directory=False):
        record = bytearray(33 + len(name) + (not (len(name) & 1)))
        record[0] = len(record)
        struct.pack_into("<I", record, 2, sector)
        struct.pack_into(">I", record, 6, sector)
        struct.pack_into("<I", record, 10, length)
        struct.pack_into(">I", record, 14, length)
        record[18:25] = bytes([126, 1, 1, 0, 0, 0, 0])
        record[25] = 2 if directory else 0
        record[28:32] = bytes([1, 0, 0, 1])
        record[32] = len(name)
        record[33 : 33 + len(name)] = name
        return record

    primary = memoryview(hybrid)[32768:34816]
    primary[:7] = b"\x01CD001\x01"
    primary[40:72] = b"HYBRID ISO".ljust(32, b" ")
    struct.pack_into("<I", primary, 80, len(hybrid) // 2048)
    struct.pack_into(">I", primary, 84, len(hybrid) // 2048)
    primary[120:132] = bytes([1, 0, 0, 1, 1, 0, 0, 1, 0, 8, 8, 0])
    primary[156:190] = iso_record(b"\x00", 20, 2048, True)
    primary[881] = 1
    hybrid[34816:34823] = b"\xffCD001\x01"
    directory = (
        iso_record(b"\x00", 20, 2048, True) + iso_record(b"\x01", 20, 2048, True) + iso_record(b"HELLO.TXT;1", 21, 3)
    )
    hybrid[40960 : 40960 + len(directory)] = directory
    hybrid[43008:43011] = b"ISO"
    hybrid[131072:] = raw
    image = fixture / "hybrid-two-volumes.iso"
    image.write_bytes(hybrid)
    output = subprocess.check_output([str(program), host(image), host(cache)], text=True, timeout=20)
    volumes = [line.split("|", 2) for line in output.strip().splitlines()]
    assert len(volumes) == 2 and all(v[0] == "2" for v in volumes), output
    for _, name, path in volumes:
        if windows:
            path = subprocess.check_output(["wslpath", "-u", path], text=True).strip()
        if name == "Mount Test":
            assert (Path(path) / "Hello").read_bytes() == b"Image mount test"
        else:
            assert (Path(path) / "HELLO.TXT").read_bytes() == b"ISO"
    literal_adc = b"".join(
        bytes([0x80 | (len(raw[i : i + 128]) - 1)]) + raw[i : i + 128] for i in range(0, len(raw), 128)
    )
    for name, method, packed in [
        ("copy", 1, raw),
        ("zlib", 0x80000005, zlib.compress(raw)),
        ("bzip2", 0x80000006, bz2.compress(raw)),
        ("xz", 0x80000008, lzma.compress(raw)),
        ("adc", 0x80000004, literal_adc),
    ]:
        run(dmg(raw, method, packed), name + ".dmg")
    run(b"bad", "truncated", False)
    run(dmg(raw, 0x80000005, b"bad stream"), "bad-zlib.dmg", False)
    run(hfsplus(True), "traversal.hfsplus", False)
    print("HFS+/HFSX/APM; copy/zlib/bzip2/XZ/ADC DMG; both forks, FinderInfo, failures and unchanged source passed")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--write-classic-fixtures":
        destination = Path(sys.argv[2])
        destination.mkdir(parents=True, exist_ok=True)
        raw = classic_hfs(True)
        (destination / "Mount.hfv").write_bytes(raw)
        (destination / "Mount.dmg").write_bytes(dmg(raw, 0x80000005, zlib.compress(raw)))
    else:
        main()
