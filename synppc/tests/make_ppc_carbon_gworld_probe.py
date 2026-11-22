#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_gworld_probe.py
#
#   Summary:   Original PPC native GWorld readers with real classic manager transitions.
#
#   Classes:   None declared.
#
#   Functions: make_gworld_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC native GWorld readers with real classic manager transitions."""
import struct
from pathlib import Path
import sys

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_gworld_probe(destination):
    write_carbon_fallback(destination)
    names = [
        "InitWindows",
        "NewGWorld",
        "DisposeGWorld",
        "SetGWorld",
        "LockPixels",
        "UnlockPixels",
        "GetGWorld",
        "GetGWorldDevice",
        "GetGWorldPixMap",
        "GetPixBaseAddr",
        "GetPixelsState",
        "GetPortPixMap",
        "GetMainDevice",
        "CallUniversalProc",
        "CarbonFallbackToken",
    ]
    p = Probe([("CarbonLib", name, 2) for name in names])
    struct.pack_into(">4h", p.data, 200, 0, 0, 12, 13)
    p.emit(0x387E0080, 0x389E0084)
    p.call("GetGWorld")
    p.call("InitWindows")

    def same_slot(slot, label):
        p.emit(0x809E0000 | slot)
        p.equal(label)

    def state(expected):
        p.emit(0x807E008C)
        p.call("GetPixelsState")
        p.expect(expected, "native state follows classic manager changes")

    def base(indirect):
        p.emit(0x807E008C)
        p.call("GetPixBaseAddr")
        p.nonzero("native pixel buffer")
        p.emit(0x809E008C, 0x80840000, 0x80840000)
        if indirect:
            p.emit(0x80840000)
        p.equal("pixel base handle/pointer form")

    def classic_state(value):
        # QDOffscreen selector 14, two four-byte arguments. This is the
        # classic dispatch path used by CarbonLib's SetPixelsState wrapper;
        # the sentinel library contains no implementation of that function.
        p.emit(0x80601A74, 0x38800FC8, 0x3CA00008, 0x60A5000E, 0x80DE008C, 0x38E00000 | value)
        p.call("CallUniversalProc")

    for depth in (8, 16, 32):
        p.emit(0x387E0088, 0x38800000 | depth, 0x38BE00C8, 0x38C00000, 0x38E00000, 0x39000000)
        p.call("NewGWorld")
        p.expect(0, "real GWorld allocation")
        p.emit(0x807E0088)
        p.call("GetGWorldDevice")
        p.nonzero("real GWorld device")
        p.emit(0x907E0090, 0x807E0088)
        p.call("GetGWorldPixMap")
        p.nonzero("real GWorld pixmap")
        p.emit(0x907E008C, 0x807E0088)
        p.call("GetPortPixMap")
        same_slot(140, "GWorld and color port share their actual pixmap")
        p.emit(0x807E0088, 0x38800000)
        p.call("SetGWorld")
        p.emit(0x387E0098, 0x389E009C)
        p.call("GetGWorld")
        p.emit(0x807E0098)
        same_slot(136, "native current world")
        p.emit(0x807E009C)
        same_slot(144, "native current device")
        state(0)
        base(True)
        p.emit(0x3880005A, 0x98830000)
        for _ in range(2):
            p.emit(0x807E008C)
            p.call("LockPixels")
            p.expect(1, "classic pixel lock")
        state(0x80)
        base(False)
        p.emit(0x88630000)
        p.expect(0x5A, "pixel payload survives locking")
        p.emit(0x807E008C)
        p.call("UnlockPixels")
        state(0x80)
        p.emit(0x807E008C)
        p.call("UnlockPixels")
        state(0)
        base(True)
        for flags in (0x48, 0xC8, 0):
            classic_state(flags)
            state(flags)
            base(not (flags & 0x80))
        p.emit(0x807E0080, 0x809E0084)
        p.call("SetGWorld")
        p.emit(0x807E0088)
        p.call("DisposeGWorld")
    p.call("GetMainDevice")
    p.emit(0x80630000, 0x80630016, 0x907E00B0)
    p.call("GetPixBaseAddr")
    p.nonzero("screen pixels remain a direct pointer")
    p.emit(0x809E00B0, 0x80840000, 0x80840000)
    p.equal("screen pixels are never dereferenced as a handle")
    p.call("CarbonFallbackToken")
    p.expect(73, "GWorld accessors preserve initialized PEF fallback")
    p.finish(destination, "PPC Carbon GWorld Probe")


if __name__ == "__main__":
    make_gworld_probe(Path(sys.argv[1]))
