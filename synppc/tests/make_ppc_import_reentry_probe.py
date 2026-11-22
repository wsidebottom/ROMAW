#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_import_reentry_probe.py
#
#   Summary:   Exercise native import registration reentered by a guest grow-zone callback.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original ROMAW integration fixture using a guest Memory Manager grow-zone callback.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    names = [
        "GetSharedLibrary",
        "FindSymbol",
        "GetZone",
        "SetZone",
        "InitZone",
        "MaxBlock",
        "SetGrowZone",
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "NewPtr",
        "DisposePtr",
        "DebugStr",
    ]
    p = Probe([("InterfaceLib", name, 2) for name in names])
    p.identify_failed_checks = True
    p.data.extend(bytes(2048 - len(p.data)))

    def string(offset, text):
        b = text.encode("ascii")
        p.data[offset : offset + len(b) + 1] = bytes([len(b)]) + b

    string(0x240, "InterfaceLib")
    string(0x260, "FreeMem")
    string(0x280, "BlockZero")
    p.emit(0x387E0240, 0x3C807077, 0x60847063, 0x38A00002, 0x38DE0188, 0x38FE018C, 0x391E0600)
    p.call("GetSharedLibrary")
    p.expect(0, "open native InterfaceLib")
    p.emit(0x387E0200, 0x388000F0, 0x38A00001)
    p.call("NewRoutineDescriptor")
    p.nonzero("create Pascal grow-zone callback")
    p.emit(0x907E01A8)
    p.call("GetZone")
    p.emit(0x907E0180, 0x3C600001)
    p.call("NewPtr")
    p.nonzero("allocate isolated fixture heap")
    p.emit(0x907E01B8, 0x7C661B78, 0x3CA30001, 0x38600000, 0x38800010)
    p.call("InitZone")
    p.emit(0x807E01B8)
    p.call("SetZone")
    p.emit(0x38601000)
    p.call("NewPtr")
    p.nonzero("reserve reclaimable heap storage")
    p.emit(0x907E01BC)
    p.call("MaxBlock")
    p.call("NewPtr")
    p.nonzero("fill isolated heap before registering imports")
    p.emit(0x907E01C0, 0x807E01A8)
    p.call("SetGrowZone")
    p.emit(0x807E0188, 0x389E0260, 0x38BE0190, 0x38DE0194)
    p.call("FindSymbol")
    p.expect(0, "outer native import registration completes")
    p.emit(0x807E01A0)
    p.expect(1, "grow-zone callback reenters exactly once")
    p.emit(0x807E01A4)
    p.expect(0, "nested import registration completes")
    p.emit(0x807E0180)
    p.call("SetZone")
    # Force subsequent reallocations and exercise both published vectors.
    extra = [
        "GetSysFont",
        "GetAppFont",
        "RealScript",
        "GetZone",
        "SystemZone",
        "SetZone",
        "GetPtrSize",
        "PtrZone",
        "NewHandle",
        "GetHandleSize",
        "HGetState",
        "HNoPurge",
        "NewHandleClear",
        "MaxMem",
        "TempFreeMem",
        "TempMaxMem",
    ]
    for i, name in enumerate(extra):
        offset = 0x300 + i * 32
        string(offset, name)
        p.emit(0x807E0188, 0x389E0000 | offset, 0x38BE01B0, 0x38DE01B4)
        p.call("FindSymbol")
        p.expect(0, "subsequent import registration remains intact: " + name)
    p.emit(0x819E0190, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    p.nonzero("outer FreeMem vector remains callable")
    p.data[0x2A0:0x2A8] = b"ABCDEFGH"
    p.emit(0x387E02A2, 0x38800004, 0x819E0198, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    p.emit(0x807E02A0)
    p.expect(0x41420000, "nested BlockZero vector clears exact leading bytes")
    p.emit(0x807E02A4)
    p.expect(0x00004748, "nested BlockZero vector preserves trailing guard")
    p.emit(0x807E01A8)
    p.call("DisposeRoutineDescriptor")
    string(0x580, "PPC_IMPORT_REENTRY_OK")
    p.emit(0x387E0580)
    p.call("DebugStr")
    callback = Probe(p.imports)
    callback.code = []
    callback.emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378, 0x38600000)
    callback.call("SetGrowZone")
    callback.emit(0x807E01BC)
    callback.call("DisposePtr")
    callback.emit(0x807E01A0, 0x38630001, 0x907E01A0, 0x807E0188, 0x389E0280, 0x38BE0198, 0x38DE019C)
    callback.call("FindSymbol")
    callback.emit(0x907E01A4, 0x38601000, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020)
    p.routines.append((0x200, callback.code))
    p.finish(destination, "PPC Import Reentry Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
