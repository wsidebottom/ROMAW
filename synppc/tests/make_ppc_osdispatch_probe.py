#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_osdispatch_probe.py
#
#   Summary:   Original PPC call reproducing an unsupported private service with CarbonLib's nine-argument ABI.
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

"""Original PPC call reproducing an unsupported private service with CarbonLib's nine-argument ABI."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination, mode="valid"):
    # Preserve LR/TOC and provide the PPC parameter area for nine arguments.
    code = [
        0x9421FF80,
        0x7C0802A6,
        0x90010084,
        0x93C10078,
        0x7C5E1378,
        0x8060103C,  # OSDispatch's low-memory procedure pointer
        0x3C8003F7,
        0x60849FBE,
        0x38A07FFE,
        0x38C00011,
        0x38E00022,
        0x39000033,
        0x39200044,
        0x39400055,
    ]
    # Remaining four arguments start at SP+56 (r11's parameter slot).
    for offset, value in [(56, 0x66), (60, 0x77), (64, 0x88), (68, 0x99)]:
        code += [0x38000000 | value, 0x90010000 | offset]
    if mode in ["tsm", "tsm-document"]:
        code[5:10] = [
            0x80601750,
            0x38800398,
            0x60000000,
            0x38A07FFE if mode == "tsm" else 0x38A0001D,
            0x38C00000 if mode == "tsm" else 0x38C00100,
        ]
    elif mode != "valid":
        raise ValueError(mode)
    code += [
        0x819E0008,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x83C10078,
        0x80010084,
        0x7C0803A6,
        0x38210080,
        0x38600000,
        0x4E800020,
    ]
    app = pef(
        code,
        bytearray(16),
        imports=[("InterfaceLib", "CallUniversalProc", 2)],
        main=0,
        reloc=[0x4600, 0x4A00],
    )
    write_fragment(
        destination,
        "PPC TSM Probe" if mode.startswith("tsm") else "PPC OSDispatch Probe",
        app,
        1,
    )


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
