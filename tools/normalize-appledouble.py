#!/usr/bin/env python3
# +===================================================================
#   File:      normalize-appledouble.py
#
#   Summary:   Put classic FinderInfo before resource data for OS X 10.4 interoperability.
#
#   Classes:   None declared.
#
#   Functions: entries, normalize, write_normalized
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Put classic FinderInfo before resource data for OS X 10.4 interoperability.

Retro68 emits valid AppleDouble with FinderInfo after the resource fork. Tiger's
GetAppleDoubleBulk can crash on that layout. Preserve each payload, but put
FinderInfo immediately after a two-entry table and the resource fork second.
Legacy metadata remains in the unchanged source; the output contains only native
OS X FinderInfo/resource entries. Extended FinderInfo/ATTR layouts are refused.
"""

import argparse
from pathlib import Path
import struct


def entries(data):
    if len(data) < 26 or data[:8] != bytes.fromhex("0005160700020000"):
        raise ValueError("Expected AppleDouble version 2")
    count = struct.unpack_from(">H", data, 24)[0]
    end = 26 + count * 12
    if end > len(data):
        raise ValueError("Truncated AppleDouble descriptor table")
    result = []
    ids, spans = set(), []
    for index in range(count):
        kind, offset, size = struct.unpack_from(">III", data, 26 + index * 12)
        if kind in ids or offset < end or (size and offset + size > len(data)):
            raise ValueError("Invalid AppleDouble descriptor")
        ids.add(kind)
        if size:
            spans.append((offset, offset + size))
        result.append((kind, data[offset : offset + size]))
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError("Overlapping AppleDouble entries")
    return result


def normalize(data):
    records = entries(data)
    finder = next((value for kind, value in records if kind == 9), None)
    if finder is None or len(finder) not in (16, 32):
        raise ValueError("Expected classic 16- or 32-byte FinderInfo; ATTR unsupported")
    resource = next((value for kind, value in records if kind == 2), b"")
    # Tiger's Resource Manager writer uses this two-entry OS X layout. Merely
    # moving FinderInfo earlier in a legacy multi-entry file is not sufficient:
    # a subsequent resource save can overwrite unrelated descriptors/truncate it.
    finder = finder.ljust(32, b"\0")
    if len(resource) > 0xFFFFFFFF - 82:
        raise ValueError("AppleDouble exceeds 32-bit offsets")
    return (
        struct.pack(">II16sH", 0x51607, 0x20000, b"Mac OS X        ", 2)
        + struct.pack(">IIIIII", 9, 50, 32, 2, 82, len(resource))
        + finder
        + resource
    )


def write_normalized(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or destination.exists():
        raise ValueError("Output must be a new file, distinct from the source")
    original = source.read_bytes()
    result = normalize(original)
    # Check the newly encoded descriptors independently of payload placement.
    records = dict(entries(original))
    expected = {9: records[9].ljust(32, b"\0"), 2: records.get(2, b"")}
    if dict(entries(result)) != expected:
        raise ValueError("AppleDouble payload verification failed")
    with destination.open("xb") as output:
        output.write(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    write_normalized(args.source, args.destination)
