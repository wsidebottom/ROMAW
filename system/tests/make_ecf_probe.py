#!/usr/bin/env python3
# +===================================================================
#   File:      make_ecf_probe.py
#
#   Summary:   Generate a PPC application exercising real window creation and disposal for the ECF suite.
#
#   Classes:   None.
#
#   Functions: make_probe and the command-line entry point.
#
#   Origin:    Original ROMAW ECF fixture using the repository PEF and Macintosh resource generators.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Generate the small window application used by the Windows ECF launch suite."""

import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378]

    def call(index):
        code.extend([0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421])

    call(0)  # InitWindows through the real InterfaceLib import.
    code.extend([0x38600000, 0x389E0040, 0x38BE0050, 0x38C00001, 0x38E00000, 0x3900FFFF, 0x39200001, 0x39400000])
    call(1)  # NewWindow(NULL, &bounds, title, true, documentProc, -1, true, 0).
    code.append(0x2C030000)
    failed = len(code)
    code.append(0x41820000)
    call(2)  # DisposeWindow; the suite saves preferences at this real Toolbox entry.
    code.extend([0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020])
    code[failed] |= (len(code) - failed) * 4
    code.append(0)  # A failed window allocation is a guest fault, never a fake success.
    data = bytearray(128)
    struct.pack_into(">4h", data, 64, 80, 80, 200, 320)
    data[80:90] = b"\x09ECF Probe"
    fragment = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in ("InitWindows", "NewWindow", "DisposeWindow")],
        main=0,
        reloc=[0x4600, 0x4A02],
    )
    write_fragment(destination, "ECF Launch Probe", fragment, 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    make_probe(parser.parse_args().destination)
