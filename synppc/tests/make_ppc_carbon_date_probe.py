#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_date_probe.py
#
#   Summary:   Original PPC floating-result ABI checks, optionally with real CFDate objects.
#
#   Classes:   None declared.
#
#   Functions: make_date_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC floating-result ABI checks, optionally with real CFDate objects."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe, write_cf_sentinel
from make_ppc_carbon_native_probe import write_carbon_fallback

ACCESSORS = ["CFAbsoluteTimeGetCurrent", "CFDateGetAbsoluteTime", "CFDateGetTimeIntervalSinceDate", "CFDateCompare"]


def make_date_probe(destination, library="CarbonLib", real=False):
    if not real:
        write_carbon_fallback(destination)
        write_cf_sentinel(destination)
    imports = [(library, name, 2) for name in ACCESSORS]
    # Real Carbon startup rejects direct application imports from InterfaceLib.
    # Its public CarbonLib re-export reaches the same classic clock service.
    imports += [("CarbonLib", "GetDateTime", 2)]
    if real:
        imports += [(library, name, 2) for name in ("CFDateCreate", "CFRelease")]
    else:
        imports += [("CarbonLib", "CarbonFallbackToken", 2), ("CoreFoundationLib", "CFFallbackToken", 2)]
    p = Probe(imports)
    values = (12345.75, -0.5)
    for slot, value in zip((128, 160), values):
        if real:
            # Each value needs distinct source bytes when the code executes.
            source = 256 if slot == 128 else 264
            struct.pack_into(">d", p.data, source, value)
            p.emit(0x38600000, 0xC83E0000 | source)
            p.call("CFDateCreate")
            p.nonzero("real CFDate constructed with an f1 argument")
            p.emit(0x907E0000 | slot)
        else:
            struct.pack_into(">d", p.data, slot + 8, value)

    def date_arg(register, slot):
        p.emit((0x807E0000 if real else 0x387E0000) + (register - 3) * 0x200000 | slot)

    def double_result(expected, label):
        p.emit(0xD83E00E0)
        hi, lo = struct.unpack(">II", struct.pack(">d", expected))
        for offset, word in ((224, hi), (228, lo)):
            p.emit(0x807E0000 | offset)
            p.expect(word, label)

    for slot, value in zip((128, 160), values):
        date_arg(3, slot)
        p.call("CFDateGetAbsoluteTime")
        double_result(value, "CFDate double result in f1")
    for first, second, difference in ((128, 160, 12346.25), (160, 128, -12346.25), (128, 128, 0.0)):
        date_arg(3, first)
        date_arg(4, second)
        p.call("CFDateGetTimeIntervalSinceDate")
        double_result(difference, "CFDate interval double result")
        date_arg(3, first)
        date_arg(4, second)
        p.emit(0x38A0FFFF)
        p.call("CFDateCompare")
        p.expect(1 if difference > 0 else -1 if difference < 0 else 0, "CFDate comparison and opaque context")

    # Bracket the native time read with classic GetDateTime. Convert unsigned
    # seconds exactly using the original PPC 2^52 construction, then compare
    # f1 against both bounds to permit a second rollover during the calls.
    struct.pack_into(">dd", p.data, 288, 4503599627370496.0, 3061152000.0)
    p.emit(0x387E00C8)
    p.call("GetDateTime")
    p.call("CFAbsoluteTimeGetCurrent")
    p.emit(0xD83E00D0, 0xFC010800)
    p.checks.append({"offset": len(p.code) * 4, "check": "CF absolute time is ordered"})
    p.emit(0x40830008, 0, 0x387E00D8)
    p.call("GetDateTime")
    for offset, condition in ((200, 0x40800008), (216, 0x40810008)):
        p.emit(0x807E0000 | offset, 0x907E0114, 0x3C604330, 0x907E0110)
        p.emit(0xC85E0110, 0xC81E0120, 0xFC420028, 0xC81E0128, 0xFC420028, 0xC83E00D0, 0xFC011000)
        p.checks.append({"offset": len(p.code) * 4, "check": "CF absolute time lies between classic clock reads"})
        p.emit(condition, 0)
    if real:
        for slot in (128, 160):
            p.emit(0x807E0000 | slot)
            p.call("CFRelease")
    else:
        for name in ("CarbonFallbackToken", "CFFallbackToken"):
            p.call(name)
            p.expect(73, "date accessors preserve fallback initialization")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Date " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_date_probe(args.destination, args.library, args.real)
