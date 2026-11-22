#!/usr/bin/env python3
# +===================================================================
#   File:      make_cfm68k_text_probe.py
#
#   Summary:   Original client for an optional, locally installed m68k TextCommon library.
#
#   Classes:   None declared.
#
#   Functions: make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original client for an optional, locally installed m68k TextCommon library.

No Apple library bytes are included or copied by this generator.
"""

import struct
import sys
from pathlib import Path

from make_cfm68k_shared_probe import fragment
from make_ppc_probe import write_fragment


def make_probe(destination):
    code = bytearray()
    branches = []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def push(value):
        words(0x2F3C, value >> 16, value & 0xFFFF)

    def call(offset):
        words(0x226C, offset, 0x2059, 0x4E90)

    def check(value):
        words(0x0C80, value >> 16, value & 0xFFFF)
        branches.append(len(code))
        words(0x6600, 0)

    words(0x2A51, 0x284D)
    # kTextEncodingUnicodeDefault, default variant and format.
    for value in (0, 0, 0x100):
        push(value)
    call(8)
    check(0x100)
    for offset, encoding, expected in (
        (12, 0x100, 0x100),
        (16, 0x10000, 1),
        (20, 0, 0),
    ):
        push(encoding)
        call(offset)
        check(expected)
    words(0x2A4C, 0x7000, 0x4E75)
    failure = len(code)
    words(0x4AFC)
    for branch in branches:
        struct.pack_into(">h", code, branch + 2, failure - (branch + 2))
    names = (
        "CreateTextEncoding",
        "GetTextEncodingBase",
        "GetTextEncodingVariant",
        "GetTextEncodingFormat",
    )
    application = fragment(
        code,
        bytes(32),
        main=0,
        imports=tuple(("TextCommon", name, 2) for name in names),
        reloc=(0x4600, 0x4A03),
    )
    write_fragment(destination, "CFM-68K TextCommon Probe", application, 1, arch=b"m68k")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
