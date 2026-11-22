#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_fused_probe.py
#
#   Summary:   Original PPC application checking fused cancellation and single rounding.
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

"""Original PPC application checking fused cancellation and single rounding."""

from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = [
        0xC8220010,  # lfd f1,a(r2)
        0xC8620018,  # lfd f3,b(r2)
        0xC8420020,  # lfd f2,c(r2)
        0xFC8110F8,  # fmsub f4,f1,f3,f2
        0xC8A20028,  # lfd f5,expected(r2)
        0xFC042800,  # fcmpu cr0,f4,f5
        0x40820000,
        0xC8220030,
        0xC8620030,
        0xC8420038,
        0xEC8110FA,  # fmadds f4,f1,f3,f2
        0xC8A20040,
        0xFC042800,
        0x40820000,
        0x38600000,
        0x4E800020,
        0,
    ]
    for branch in [6, 13]:
        code[branch] |= (16 - branch) * 4
    data = bytearray(72)
    struct.pack_into(
        ">7d",
        data,
        16,
        1.0 + 2.0**-33,
        1.0 - 2.0**-33,
        1.0,
        -(2.0**-66),
        1.0 + 2.0**-12,
        2.0**-80,
        1.0 + 2.0**-11 + 2.0**-23,
    )
    write_fragment(destination, "PPC Fused Probe", pef(code, data, main=0, reloc=[0x4600]), 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
