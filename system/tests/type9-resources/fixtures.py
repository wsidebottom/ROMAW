# +===================================================================
#   File:      fixtures.py
#
#   Summary:   Original type-9 calling-convention fixtures; optional local System comparison.
#
#   Classes:   None declared.
#
#   Functions: header, main
#
#   Origin:    Existing repository source in executor/tests/type9-resources; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Original type-9 calling-convention fixtures; optional local System comparison."""

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from mac_resources import ResourceFork

spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

# Original 68K code: three entry offsets; init/finish pop one pointer;
# decode pops (source, destination, header), validates the header, then copies
# header.uncompressedSize bytes. This deliberately differs from Apple's dcmp 2
# and proves a file's custom decompressor is called rather than substituted.
DE00060012000c205f588f4ed0205f588f4ed04e5600002f082f092f0a2f00206e0010226e000c246e00080c92a89f657266180caa001209010004660e202a000853806b0612d853806afa201f245f225f205f4e5e205fdefc000c4ed0R = bytes.fromhex(
    "00060012000c205f588f4ed0205f588f4ed04e5600002f082f092f0a2f00206e0010226e000c246e00080c92a89f657266180caa001209010004660e202a000853806b0612d853806afa201f245f225f205f4e5e205fdefc000c4ed0"
)


def header(size, decompressor=1234):
    return struct.pack(">IHHIh4s", 0xA89F6572, 18, 0x0901, size, decompressor, b"\0" * 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--system", type=Path, help="Optional raw local System resource fork")
    args = parser.parse_args()
    records = [
        (
            b"dcmp",
            1234,
            0,
            None,
            DE00060012000c205f588f4ed0205f588f4ed04e5600002f082f092f0a2f00206e0010226e000c246e00080c92a89f657266180caa001209010004660e202a000853806b0612d853806afa201f245f225f205f4e5e205fdefc000c4ed0R,
        ),
        (b"dcmp", 1235, 0, None, b"\0\1" * 4),
    ]
    pairs = []
    for size in (1, 2, 31, 128, 65537):
        payload = bytes(i % 251 for i in range(size))
        pairs.append((header(size) + payload, payload))
    if args.system:
        sys.path.insert(0, str(ROOT / "build/resource-reference"))
        from rsrcfork.compress import decompress

        system = ResourceFork(args.system.read_bytes())
        for (kind, rid), (data, attributes, _) in system.records.items():
            if kind == b"dcmp":
                records.append((kind, rid, attributes, None, data))
            elif attributes & 1 and data[:8] == bytes.fromhex("a89f657200120901"):
                pairs.append((data, decompress(data)))
    for rid, (compressed, expected) in enumerate(pairs):
        records.extend(
            [
                (b"ACT9", rid, 5 if rid == 0 else 1, None, compressed),
                (b"EXP9", rid, 0, None, expected),
            ]
        )
    # A tiny executable resource for the stale-translation reload regression.
    records.append((b"EXE9", 0, 1, None, header(4) + bytes.fromhex("702a4e75")))
    malformed = [
        header(8)[:17],
        header(8, 1236) + b"abcdefgh",
        header(8, 1235) + b"abcdefgh",
        header(0xFFFFFFFF),
    ]
    records.extend((b"BAD9", rid, 1, None, data) for rid, data in enumerate(malformed, 1))
    output = ROOT / "build/type9-resource-check"
    output.mkdir(parents=True, exist_ok=True)
    (output / "Fixtures").write_bytes(b"")
    # Preload ACT9 before the later dcmp type. Old maps can contain saved
    # runtime handles; none may be used while preloading another resource.
    records.sort(key=lambda record: record[0])
    fork = bytearray(builder.build_fork(records))
    map_offset = struct.unpack_from(">I", fork, 4)[0]
    types = map_offset + struct.unpack_from(">H", fork, map_offset + 24)[0]
    count = struct.unpack_from(">H", fork, types)[0] + 1
    for index in range(count):
        kind, count_minus_one, offset = struct.unpack_from(">4sHH", fork, types + 2 + index * 8)
        if kind == b"dcmp":
            for reference in range(count_minus_one + 1):
                struct.pack_into(">I", fork, types + offset + reference * 12 + 8, 0x01CFECAC)

    sidecar = struct.pack(">II16sH", 0x00051607, 0x00020000, b"\0" * 16, 2)
    sidecar += struct.pack(">IIIIII", 9, 50, 32, 2, 82, len(fork))
    (output / "._Fixtures").write_bytes(sidecar + b"\0" * 32 + fork)
    print(f"Generated {len(pairs)} type-9 comparisons and four malformed resources")


if __name__ == "__main__":
    main()
