#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_closure_probe.py
#
#   Summary:   Original CFM closure ownership, shared dependency lifetime and reload checks.
#
#   Classes:   None declared.
#
#   Functions: library, make_closure_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CFM closure ownership, shared dependency lifetime and reload checks."""
import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def library(destination, name, identity, dependency=False):
    slot = (identity - 1) * 4
    # Preserve both distinct IDs, initialize private data, and count the initializer.
    code = [
        0x80030008,
        0x90020050,
        0x80030004,
        0x90020054,
        0x38000049,
        0x90020058,
        0x80820020,
        0x80040000 | slot,
        0x30000001,
        0x90040000 | slot,
        0x38600000,
        0x4E800020,
    ]
    term = len(code) * 4
    code += [
        0x80820020,
        0x80040000 | (slot + 12),
        0x30000001,
        0x90040000 | (slot + 12),
        0x80040018,
        0x54002036,
        0x60000000 | identity,
        0x90040018,
        0x38600000,
        0x4E800020,
    ]
    data = bytearray(96)
    struct.pack_into(">4I", data, 0, 0, 0, term, 0)
    imports = [("ClosureAudit", "Log", 1)]
    exports = [("OwnConnection", 1, 80, 1), ("OwnClosure", 1, 84, 1), ("State", 1, 88, 1)]
    if dependency:
        imports.append(("ClosureDependency", "State", 1))
        exports.append(("DependencyState", 1, 1, -3))
    write_fragment(
        destination,
        name,
        pef(
            code,
            data,
            imports=imports,
            exports=exports,
            init=0,
            term=8,
            reloc=[0x4601, 0xA000, 32, 0x4A01 if dependency else 0x4A00],
        ),
        0,
    )


def make_closure_probe(destination):
    destination.mkdir(parents=True, exist_ok=True)
    write_fragment(destination, "ClosureAudit", pef([0x4E800020], bytes(32), exports=[("Log", 1, 0, 1)]), 0)
    library(destination, "ClosureDependency", 1)
    library(destination, "ClosureRootA", 2, True)
    library(destination, "ClosureRootB", 3, True)
    imports = [("InterfaceLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    imports += [("PrivateInterfaceLib", name, 2) for name in ("FragGetClosureInfo", "FragGetContextInfo")]
    imports.append(("ClosureAudit", "Log", 1))
    p = Probe(imports)
    for offset, name in (
        (256, "ClosureRootA"),
        (288, "ClosureRootB"),
        (320, "ClosureDependency"),
        (352, "State"),
        (384, "OwnConnection"),
        (416, "OwnClosure"),
        (448, "DependencyState"),
    ):
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw

    def load(name, flags, out, expected=0):
        p.emit(0x387E0000 | name, 0x3C807077, 0x60847063, 0x38A00000 | flags, 0x38DE0000 | out, 0x38FE00C0, 0x391E0200)
        p.call("GetSharedLibrary")
        p.expect(expected, "shared reference or borrowed find")

    def symbol(handle, name, out, expected=0):
        p.emit(0x807E0000 | handle, 0x389E0000 | name, 0x38BE0000 | out, 0x38DE00C4)
        p.call("FindSymbol")
        p.expect(expected, "closure or connection symbol")

    def counter(offset, value):
        p.emit(0x807E001C, 0x80630000 | offset)
        p.expect(value, "initializer/terminator count and reverse dependency order")

    def read_context_count():
        p.emit(0x38600000, 0x38800000, 0x38BE0258)
        p.call("FragGetContextInfo")
        p.expect(0, "inspect actual context closures")
        p.emit(0x807E0260)

    def context(additional):
        read_context_count()
        p.emit(0x809E026C, 0x38840000 | additional)
        p.equal("context counts active closures created by this check")

    def closure(slot, expected=0):
        p.emit(0x38600000, 0x809E0000 | slot, 0x38A00000, 0x38DE0258, 0x38E00000, 0x39000003, 0x393E0280)
        p.call("FragGetClosureInfo")
        p.expect(expected, "inspect only active closure IDs")
        if not expected:
            p.emit(0x807E0258, 0x809E0000 | slot)
            p.equal("closure inspection returns its owning ID")
            p.emit(0x807E0260)
            p.expect(3, "closure contains root, dependency and audit")
            p.emit(0x807E025C, 0x809E00A0)
            p.equal("closure root is its connection ID")

    def close(slot):
        p.emit(0x387E0000 | slot)
        p.call("CloseConnection")
        p.expect(0, "release exactly one owning closure")
        p.emit(0x807E0000 | slot)
        p.expect(0, "closed closure handle is cleared")

    # The batch app may have unrelated closures open before this check.
    read_context_count()
    p.nonzero("the running app has an active closure")
    p.emit(0x907E026C)
    load(256, 1, 128)
    symbol(128, 416, 152)
    p.emit(0x807E0098, 0x80630000, 0x809E0080)
    p.equal("root initializer sees this load's closure")
    load(320, 2, 140)
    symbol(140, 416, 152)
    p.emit(0x807E0098, 0x80630000, 0x809E0080)
    p.equal("dependency initializes in root's closure")
    load(256, 1, 132)
    p.emit(0x807E0080, 0x809E0084, 0x7C032000, 0x40820008, 0)
    symbol(128, 384, 148)
    p.emit(0x807E0094, 0x80630000, 0x907E00A0)
    symbol(132, 384, 148)
    p.emit(0x807E0094, 0x80630000, 0x809E00A0)
    p.equal("two closures share a connection")
    context(2)
    closure(128)
    closure(132)
    closure(160, 0xFFFFF50F)
    p.emit(0x807E0080, 0x907E00A8)
    load(288, 1, 136)
    context(3)
    symbol(136, 416, 152)
    p.emit(0x807E0098, 0x80630000, 0x809E0088)
    p.equal("second root gets its own initialization closure")
    symbol(128, 448, 156)
    p.emit(0x807E009C, 0x907E00A4)
    symbol(136, 448, 156)
    p.emit(0x807E009C, 0x809E00A4)
    p.equal("both roots share dependency data")
    for offset in (0, 4, 8):
        counter(offset, 1)
    close(128)
    context(2)
    closure(168, 0xFFFFF50F)
    for offset in (12, 16, 20):
        counter(offset, 0)
    close(132)
    counter(16, 1)
    counter(12, 0)
    symbol(160, 352, 144, 0xFFFFF50F)
    load(256, 2, 128, 0xFFFFF50C)
    symbol(140, 352, 144)
    p.emit(0x807E0090, 0x80630000)
    p.expect(73, "other root keeps dependency alive")
    close(136)
    context(0)
    counter(12, 1)
    counter(20, 1)
    counter(24, 0x231)
    symbol(140, 352, 144, 0xFFFFF50F)
    load(320, 2, 140, 0xFFFFF50C)
    load(256, 1, 128)
    counter(0, 2)
    counter(4, 2)
    counter(8, 1)
    symbol(128, 384, 148)
    p.emit(0x807E0094, 0x80630000, 0x809E00A0, 0x7C032000, 0x40820008, 0)
    close(128)
    counter(12, 2)
    counter(16, 2)
    counter(20, 1)
    counter(24, 0x23121)
    context(0)
    p.finish(destination, "PPC Closure Probe")


if __name__ == "__main__":
    make_closure_probe(Path(sys.argv[1]))
