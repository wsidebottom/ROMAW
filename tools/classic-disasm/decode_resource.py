#!/usr/bin/env python3
# +===================================================================
#   File:      decode_resource.py
#
#   Summary:   Decode classic compressed resources using python-rsrcfork.
#
#   Classes:   None declared.
#
#   Functions: decode
#
#   Origin:    Existing repository source in tools/classic-disasm; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Decode classic compressed resources using python-rsrcfork."""

from pathlib import Path
import io
import struct
import sys

from rsrcfork.compress import CompressedHeaderInfo, decompress
from rsrcfork.compress.common import read_exact, read_variable_length_integer
from rsrcfork.compress.dcmp0 import TABLE


def decode(source):
    """Decode dcmp 0 including its newer jump-table and word-delta commands.

    Extension semantics reference (MIT-licensed resource_dasm):
    https://github.com/fuzziqersoftware/resource_dasm/blob/master/src/ResourceDecompressors/System01.cc
    Literal table and integer parsing use python-rsrcfork (MIT).
    """
    if not source.startswith(bytes.fromhex("a89f6572")):
        return source
    header = CompressedHeaderInfo.parse(source)
    if header.dcmp_id != 0:
        return decompress(source)
    stream = io.BytesIO(source[header.header_length :])
    output = bytearray()
    literals = []
    integer = lambda: read_variable_length_integer(stream)

    def word(value):
        output.extend(struct.pack(">H", value & 0xFFFF))

    while True:
        tag = read_exact(stream, 1)[0]
        if tag == 0xFF:
            break
        if tag < 0x20:
            count = (tag & 15) or read_exact(stream, 1)[0]
            chunk = read_exact(stream, count * 2)
            if tag >= 0x10:
                literals.append(chunk)
            output.extend(chunk)
        elif tag < 0x4B:
            if tag <= 0x22:
                count = 2 if tag == 0x22 else 1
                index = int.from_bytes(read_exact(stream, count), "big")
                if tag == 0x21:
                    index += 0x100
                index += 0x28
            else:
                index = tag - 0x23
            output.extend(literals[index])
        elif tag < 0xFE:
            output.extend(TABLE[tag - 0x4B])
        else:
            kind = read_exact(stream, 1)[0]
            value = integer()
            second = integer()
            if kind == 0:
                tail = struct.pack(">HHH", 0x3F3C, value & 0xFFFF, 0xA9F0)
                output.extend(tail)
                current = 6
                for _ in range(second & 0xFFFF):
                    current += integer() - 6
                    word(current)
                    output.extend(tail)
            elif kind == 1:
                count = (integer() & 0xFFFF) + 1
                a5_offset = integer()
                for index in range(count):
                    if index:
                        value -= 8
                        a5_offset = a5_offset + second if second else integer()
                    for item in (0x6100, value, 0x4EED, a5_offset):
                        word(item)
            elif kind in (2, 3):
                width = kind - 1
                output.extend((value & ((1 << (8 * width)) - 1)).to_bytes(width, "big") * ((second & 0xFFFF) + 1))
            elif kind in (4, 5, 6):
                width = 4 if kind == 6 else 2
                for index in range((second & 0xFFFF) + 1):
                    if index:
                        value += int.from_bytes(read_exact(stream, 1), "big", signed=True) if kind == 4 else integer()
                    output.extend((value & ((1 << (width * 8)) - 1)).to_bytes(width, "big"))
            else:
                raise ValueError(f"Unsupported extension {kind}")
        if len(output) > header.decompressed_length + 1:
            raise ValueError("Decompression exceeds declared length")
    if stream.read():
        raise ValueError("Trailing compressed data")
    if len(output) == header.decompressed_length + 1 and header.decompressed_length % 2:
        output.pop()
    if len(output) != header.decompressed_length:
        raise ValueError("Decompressed length mismatch")
    return bytes(output)


if __name__ == "__main__":
    Path(sys.argv[2]).write_bytes(decode(Path(sys.argv[1]).read_bytes()))
