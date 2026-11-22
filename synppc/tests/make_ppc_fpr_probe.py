#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_fpr_probe.py
#
#   Summary:   Original PPC application checking non-arithmetic signaling-NaN transfers.
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

"""Original PPC application checking non-arithmetic signaling-NaN transfers."""

from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = []
    branches = []

    def compare(offset):
        for part in [0, 4]:
            code.extend([0x80620030 + part, 0x80820000 | (offset + part), 0x7C032000])
            branches.append(len(code))
            code.append(0x40820000)

    code.extend([0xC8220010, 0xFC400890, 0xD8420030])  # lfd, fmr, stfd
    compare(16)
    code.extend([0xFC400850, 0xD8420030])  # fneg f2,f1; stfd
    compare(24)
    code.extend([0xC0220020, 0xD8220030])  # lfs f1; stfd
    compare(40)
    code.extend([0xC8620010, 0xFC8118AE, 0xD8820030])  # fsel with SNaN guard
    compare(16)
    code.extend([0x38600000, 0x4E800020])
    for branch in branches:
        code[branch] |= (len(code) - branch) * 4
    code.append(0)
    data = bytearray(64)
    struct.pack_into(">QQ", data, 16, 0x7FF0000012345678, 0xFFF0000012345678)
    struct.pack_into(">I", data, 32, 0x7F801234)
    struct.pack_into(">Q", data, 40, 0x7FF0024680000000)
    write_fragment(destination, "PPC FPR Probe", pef(code, data, main=0, reloc=[0x4600]), 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
