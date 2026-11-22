# +===================================================================
#   File:      decode_ndif_fixture.py
#
#   Summary:   Decode local NDIF fixtures independently for the image-reader comparison.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in packages/disk-images/tests; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Decode local NDIF fixtures independently for the image-reader comparison."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from mac_resources import AppleDouble, ResourceFork

source, target = map(Path, sys.argv[1:])
header = ResourceFork(AppleDouble(source.with_name("._" + source.name).read_bytes()).get(2)).get(b"bcem", 128)
data = source.read_bytes()
number = lambda at: int.from_bytes(header[at : at + 4], "big")
chunks = [header[i : i + 12] for i in range(128, len(header), 12)]
result = bytearray()
for i, chunk in enumerate(chunks[:-1]):
    kind = chunk[3]
    start = int.from_bytes(chunk[:3], "big") * 512
    end = int.from_bytes(chunks[i + 1][:3], "big") * 512
    assert len(result) == start
    offset = number(76) + int.from_bytes(chunk[4:8], "big")
    size = int.from_bytes(chunk[8:], "big")
    packed = data[offset : offset + size]
    out = bytearray()
    if kind == 0:
        out = bytes(end - start)
    elif kind == 2:
        out = packed
    elif kind == 131:
        cursor = 0
        while cursor < len(packed):
            command = packed[cursor]
            cursor += 1
            if command >= 128:
                count = command - 127
                out += packed[cursor : cursor + count]
                cursor += count
            else:
                if command >= 64:
                    count = command - 60
                    distance = int.from_bytes(packed[cursor : cursor + 2], "big") + 1
                    cursor += 2
                else:
                    count = (command >> 2) + 3
                    distance = ((command & 3) * 256 + packed[cursor]) + 1
                    cursor += 1
                assert distance <= len(out)
                for _ in range(count):
                    out.append(out[-distance])
    else:
        raise ValueError(f"Unsupported independent test compression {kind}")
    assert len(out) == end - start
    result += out
assert len(result) == number(68) * 512
target.write_bytes(result)
print(f"Independent NDIF decode: {len(result)} bytes")
