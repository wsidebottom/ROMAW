#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_handle_copy_probe.py
#
#   Summary:   Original PPC probes for native/guest Handle* conversion and slot guards.
#
#   Classes:   None declared.
#
#   Functions: make_handle_copy_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC probes for native/guest Handle* conversion and slot guards."""
import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_handle_copy_probe(destination):
    names = ["PtrToHand", "HandToHand", "GetHandleSize", "HLock", "HGetState", "HUnlock", "DisposeHandle", "PtrToXHand"]
    p = Probe([("InterfaceLib", name, 2) for name in names])
    struct.pack_into(">II", p.data, 384, 0x12345678, 0xABCDEF01)

    def guards(slot):
        for offset, value in ((slot - 4, 0x31415926), (slot + 4, 0x27182818)):
            p.emit(0x807E0000 | offset)
            p.expect(value, "Handle output preserves adjacent guest bytes")

    def payload(offset):
        p.emit(0x807E0000 | offset)
        p.call("GetHandleSize")
        p.expect(8, "copied Handle has the requested byte size")
        p.emit(0x807E0000 | offset, 0x80630000, 0x80630000)
        p.expect(0x12345678, "copy contains the first source word")
        p.emit(0x807E0000 | offset, 0x80630000, 0x80630004)
        p.expect(0xABCDEF01, "copy contains the second source word")

    for alignment in range(4):
        slot = 256 + alignment
        for offset, value in ((slot - 4, 0x31415926), (slot, 0xDEADBEEF), (slot + 4, 0x27182818)):
            p.emit(0x3C600000 | (value >> 16), 0x60630000 | (value & 65535), 0x907E0000 | offset)
        p.emit(0x387E0180, 0x389E0000 | slot, 0x38A00008)
        p.call("PtrToHand")
        p.expect(0, "PtrToHand succeeds")
        guards(slot)
        p.emit(0x807E0000 | slot, 0x907E0140)
        payload(320)
        p.emit(0x807E0140)
        p.call("HLock")
        p.emit(0x807E0140)
        p.call("HGetState")
        p.expect(-128, "returned guest Handle can be locked")
        p.emit(0x807E0140)
        p.call("HUnlock")
        p.emit(0x387E0000 | slot)
        p.call("HandToHand")
        p.expect(0, "HandToHand succeeds")
        guards(slot)
        p.emit(0x807E0000 | slot, 0x907E0144, 0x809E0140, 0x7C032000, 0x40820008, 0)
        payload(324)
        p.emit(0x807E0144, 0x80630000, 0x38800000, 0x90830000)
        payload(320)
        p.emit(0x807E0140)
        p.call("HLock")
        for size in (4, 8):
            p.emit(0x387E0180, 0x809E0140, 0x38A00000 | size)
            p.call("PtrToXHand")
            p.expect(0, "PtrToXHand resizes and copies into an existing locked Handle")
            p.emit(0x807E0140)
            p.call("GetHandleSize")
            p.expect(size, "PtrToXHand updates the existing Handle's size")
            p.emit(0x807E0140)
            p.call("HGetState")
            p.expect(-128, "PtrToXHand preserves the Handle lock state")
        payload(320)
        p.emit(0x807E0140)
        p.call("HUnlock")
        for offset in (320, 324):
            p.emit(0x807E0000 | offset)
            p.call("DisposeHandle")
        # Failure must preserve the caller's output word and both guards.
        p.emit(0x38600000, 0x907E0000 | slot, 0x387E0000 | slot)
        p.call("HandToHand")
        p.expect(-109, "nil source Handle reports nilHandleErr")
        guards(slot)
        p.emit(0x807E0000 | slot)
        p.expect(0, "failed HandToHand preserves the output")
        p.emit(0x387E0180, 0x389E0000 | slot, 0x3CA07FFF, 0x60A5FFFF)
        p.call("PtrToHand")
        p.nonzero("impossible allocation fails")
        guards(slot)
        p.emit(0x807E0000 | slot)
        p.expect(0, "failed PtrToHand preserves the output")

    p.emit(0x38600000)
    p.call("HandToHand")
    p.expect(-50, "null Handle slot reports paramErr")
    p.emit(0x387E0180, 0x38800000, 0x38A00008)
    p.call("PtrToHand")
    p.expect(-50, "null output slot reports paramErr")
    p.emit(0x3860FFFE)
    p.call("HandToHand")
    p.expect(-50, "out-of-range Handle slot reports paramErr")
    p.finish(destination, "PPC Handle Copy Probe")


if __name__ == "__main__":
    make_handle_copy_probe(Path(sys.argv[1]))
