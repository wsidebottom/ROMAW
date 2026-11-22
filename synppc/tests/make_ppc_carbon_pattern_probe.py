#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_pattern_probe.py
#
#   Summary:   Original PPC checks for native A5-world pattern copies and PEF fallback.
#
#   Classes:   None declared.
#
#   Functions: make_pattern_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC checks for native A5-world pattern copies and PEF fallback."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_pattern_probe(destination):
    write_carbon_fallback(destination)
    patterns = [
        ("GetQDGlobalsBlack", 0xFFFFFFFF),
        ("GetQDGlobalsWhite", 0),
        ("GetQDGlobalsGray", 0xAA55AA55),
        ("GetQDGlobalsLightGray", 0x88228822),
        ("GetQDGlobalsDarkGray", 0x77DD77DD),
    ]
    names = [name for name, _ in patterns] + [
        "SetQDError",
        "QDError",
        "CarbonFallbackToken",
        "GetQDGlobalsRandomSeed",
        "SetQDGlobalsRandomSeed",
        "GetQDGlobalsScreenBits",
        "GetQDGlobalsArrow",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def equal():
        emit(0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def word(offset, expected):
        emit(0x807E0000 + offset, 0x3C800000 | (expected >> 16), 0x60840000 | (expected & 65535))
        equal()

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    for name, expected in patterns:
        emit(0x3860FF4F)
        call("SetQDError")
        emit(0x38601357, 0x907E005C, 0x907E0068, 0x387E0060)
        call(name)
        emit(0x389E0060)
        equal()
        for offset in (96, 100):
            word(offset, expected)
        for offset in (92, 104):
            word(offset, 0x1357)
        call("QDError")
        emit(0x3880FF4F)
        equal()
        # The caller owns its copy; editing it must not change the A5 globals.
        emit(0x38600001, 0x907E0060, 0x387E0060)
        call(name)
        word(96, expected)
        emit(0x38600000)
        call(name)
        emit(0x38800000)
        equal()
        call("QDError")
        emit(0x3880FFCE)
        equal()
    call("GetQDGlobalsRandomSeed")
    emit(0x907E0070, 0x3C608123, 0x60634567)
    call("SetQDGlobalsRandomSeed")
    call("GetQDGlobalsRandomSeed")
    emit(0x3C808123, 0x60844567)
    equal()
    emit(0x807E0070)
    call("SetQDGlobalsRandomSeed")
    for name in ("GetQDGlobalsScreenBits", "GetQDGlobalsArrow"):
        emit(0x38601357, 0x907E007C, 0x907E00C4, 0x387E0080)
        call(name)
        emit(0x389E0080)
        equal()
        word(124, 0x1357)
        word(196, 0x1357)
    call("CarbonFallbackToken")
    emit(0x38800049)
    equal()
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, bytes(224), imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Pattern Probe", app, 1)


if __name__ == "__main__":
    make_pattern_probe(Path(sys.argv[1]))
