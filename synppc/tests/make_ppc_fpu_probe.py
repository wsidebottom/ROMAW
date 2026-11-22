#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_fpu_probe.py
#
#   Summary:   Original mixed PPC/68K fixture using ROMAW's installed Motorola FPSP.
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

"""Original mixed PPC/68K fixture using ROMAW's installed Motorola FPSP."""

from pathlib import Path
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = [
        0x9421FFC0,  # stwu r1,-64(r1)
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,  # Preserve TOC in r30.
        0x387E0020,  # 68K procedure at data+32.
        0x38800031,  # C procedure returning a four-byte result in D0.
        0x819E0008,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,  # CallUniversalProc.
        0x2C03002A,
        0x4082001C,  # Failed result branches to the illegal instruction.
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x38600000,
        0x4E800020,
        0,
    ]
    data = bytearray(64)
    guest = bytes.fromhex(
        "f23c400000000002"  # FMOVE.L #2,FP0.
        "f2000012"  # FTENTOX FP0,FP0: software computes 100.
        "f2000004"  # FSQRT FP0,FP0: native arithmetic computes 10.
        "f2006000"  # FMOVE.L FP0,D0.
        "068000000020"  # ADDI.L #32,D0.
        "4e75"
    )
    data[32 : 32 + len(guest)] = guest
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", "CallUniversalProc", 2)],
        main=0,
        reloc=[0x4600, 0x4A00],
    )
    write_fragment(destination, "PPC FPU Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
