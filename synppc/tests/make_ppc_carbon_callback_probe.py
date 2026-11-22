#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_callback_probe.py
#
#   Summary:   Original native CFArray callback, reentry and failure-propagation fixtures.
#
#   Classes:   None declared.
#
#   Functions: make_callback_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original native CFArray callback, reentry and failure-propagation fixtures."""
import struct
from pathlib import Path
import sys

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_callback_probe(destination, mode="valid"):
    if mode not in ("valid", "invalid", "failure"):
        raise ValueError(mode)
    write_carbon_fallback(destination)
    imports = [("CarbonLib", name, 2) for name in ("CFArrayApplyFunction", "CFArrayGetCount", "CarbonFallbackToken")]
    imports += [("InterfaceLib", "ROMAWCarbonCallbackFailure", 2)]
    p = Probe(imports)
    struct.pack_into(">5I", p.data, 132, 0, 3, 0, 0xFFFFFFFF, 0x80000000)
    struct.pack_into(">3I", p.data, 260, 0, 1, 0xDEADBEEF)
    p.emit(0x387E0100, 0x907E0348)
    callback = Probe(imports)
    callback.emit(0x7C9F2378, 0x80BF0000, 0x54A6103A, 0x38C60004, 0x7C7F312E, 0x38A50001, 0x90BF0000)
    if mode == "failure":
        callback.call("ROMAWCarbonCallbackFailure")
    else:
        # The outer callback invokes native ApplyFunction recursively once;
        # the inner callback then invokes another ordinary native CF accessor.
        callback.emit(0x807F0028, 0x2C030000)
        skip_empty = len(callback.code)
        callback.emit(0)
        callback.emit(0x80BF0000, 0x2C050001)
        skip_later = len(callback.code)
        callback.emit(0)
        callback.emit(0x38800000, 0x38A00001, 0x38DE0280, 0x38FE0384)
        callback.call("CFArrayApplyFunction")
        end = len(callback.code)
        callback.code[skip_empty] = 0x41820000 | ((end - skip_empty) * 4)
        callback.code[skip_later] = 0x40820000 | ((end - skip_later) * 4)
        callback.emit(0x387E0080)
        callback.call("CFArrayGetCount")
        callback.emit(0x907F0024)
    callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    if mode != "invalid":
        p.routines.append((640, callback.code))
    p.emit(0x387E0080, 0x38800000, 0x38A00003, 0x38DE0280, 0x38FE0320)
    p.call("CFArrayApplyFunction")
    for offset, expected in (
        (800, 3),
        (804, 0),
        (808, 0xFFFFFFFF),
        (812, 0x80000000),
        (836, 3),
        (900, 1),
        (904, 0xDEADBEEF),
        (936, 3),
    ):
        p.emit(0x807E0000 | offset)
        p.expect(expected, "opaque callback arguments and nested native reentry")
    # No callback is required for an empty range.
    p.emit(0x387E0080, 0x38800003, 0x38A00000, 0x38C00000, 0x38E0FFFF)
    p.call("CFArrayApplyFunction")
    p.call("CarbonFallbackToken")
    p.expect(73, "native callback APIs preserve initialized fallback")
    p.finish(destination, "PPC Carbon Callback Probe")


if __name__ == "__main__":
    make_callback_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
