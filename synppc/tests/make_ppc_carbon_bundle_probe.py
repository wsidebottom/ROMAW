#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_bundle_probe.py
#
#   Summary:   Original CFM bundle-symbol ABI probes and optional original-PPC comparison.
#
#   Classes:   None declared.
#
#   Functions: write_bundle_reference, write_symbols, make_bundle_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CFM bundle-symbol ABI probes and optional original-PPC comparison."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


SYMBOLS = ("CFBundleGetFunctionPointerForName", "CFBundleGetDataPointerForName")


def write_bundle_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [
        (SYMBOLS[0], 0xF5B0, [0x38601234, 0x4E800020]),
        (SYMBOLS[1], 0xF7BC, [0x38601234, 0x4E800020]),
        # This small loader fixture lacks real URL/CFM discovery; keep its profile foreign.
        ("CFBundleLoadExecutable", 0xF190, [0x38000001, 0x98030024, 0x38600001, 0x4E800020]),
        # Copy a fixture Pascal string; check the real caller's size/encoding.
        (
            "CFStringGetPascalString",
            # Deliberately foreign: the fixture accepts Pascal bytes rather
            # than actual CFString records, so keep its converter on fallback.
            0x39278,
            [
                0x2C0500FF,
                0x41820008,
                0,
                0x2C060600,
                0x41820008,
                0,
                0x88A30000,
                0x38A50001,
                0x7CA903A6,
                0x88A30000,
                0x98A40000,
                0x38630001,
                0x38840001,
                0x4200FFF0,
                0x38600001,
                0x4E800020,
            ],
        ),
        ("__CFStringMakeConstantString", 0x38110, [0x4E800020]),
        ("CFLog", 0x403FC, [0x90A20300, 0x80620304, 0x38630001, 0x90620304, 0x4E800020]),
        ("BundleLogCount", 0x100, [0x80620304, 0x4E800020]),
        ("BundleLogArgument", 0x108, [0x80620300, 0x4E800020]),
        ("BundleFallbackToken", 0x110, [0x80620308, 0x4E800020]),
    ]
    imports = []
    if mode in ("loader-failure", "converter-failure"):
        imports = [("InterfaceLib", "ROMAWBundleFailure", 2)]
        bad = 2 if mode == "loader-failure" else 3
        name, offset, _ = routines[bad]
        routines[bad] = (name, offset, [0x81820040, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420])
    exports = []
    for index, (name, offset, words) in enumerate(routines):
        descriptor = index * 8
        struct.pack_into(">II", data, descriptor, offset, 0)
        code[offset // 4 : offset // 4 + len(words)] = words
        exports.append((name, 2, descriptor, 1))
    # Avoid the descriptor at 0x40 when installing the import vector.
    import_offset = 0x80
    if imports:
        offset = routines[bad][1] // 4
        code[offset] = 0x81820000 | import_offset
    init = 0x60
    struct.pack_into(">II", data, init, 0x120, 0)
    # The initializer publishes the live message-table pointer after relocation.
    code[0x120 // 4 : 0x120 // 4 + 7] = [
        0x38600049,
        0x90620308,
        0x38621000,
        0x90620408,
        0x38600000,
        0x4E800020,
        0x60000000,
    ]
    # The init descriptor is separated from the contiguous public descriptors.
    reloc = [0x4600 | (len(routines) - 1), 0xA000, init, 0x4600]
    if imports:
        reloc += [0xA000, import_offset, 0x4A00]
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=init, reloc=reloc), 0
    )


def write_symbols(destination):
    data = bytearray(32)
    struct.pack_into(">II", data, 0, 0, 0)
    struct.pack_into(">II", data, 8, 8, 0)
    image = pef(
        [0x3860002A, 0x4E800020, 0x38600049, 0x90620018, 0x38600000, 0x4E800020],
        data,
        exports=[("testCode", 2, 0, 1), ("testData", 1, 24, 1), ("testRaw", 0, 0, 0)],
        init=8,
        reloc=[0x4601],
    )
    write_fragment(destination, "BundleSymbols", image, 0)


def make_bundle_probe(destination, real=False, library="CarbonLib", mode="valid"):
    if not real:
        write_carbon_fallback(destination)
        write_bundle_reference(destination, mode)
    write_symbols(destination)
    imports = [(library, name, 2) for name in SYMBOLS]
    imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    if real:
        imports += [("CoreFoundationLib", name, 2) for name in ("CFStringCreateWithCString", "CFRelease")]
    else:
        imports += [
            ("CoreFoundationLib", name, 2) for name in ("BundleFallbackToken", "BundleLogCount", "BundleLogArgument")
        ]
    p = Probe(imports)
    p.data += bytes(2048)
    for offset, text in ((256, b"BundleSymbols"), (288, b"CoreFoundationLib")):
        p.data[offset : offset + len(text) + 1] = bytes([len(text)]) + text

    def shared(offset, output):
        p.emit(0x387E0000 | offset, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0000 | output, 0x38FE00B8, 0x391E00C0)
        p.call("GetSharedLibrary")
        p.expect(0, "genuine CFM connection initialized")

    shared(256, 144)
    p.emit(0x807E0090, 0x907E02A8)
    struct.pack_into(">I", p.data, 672, 1)
    p.data[676] = 0 if mode in ("valid", "loader-failure") and not real else 1
    if real:
        shared(288, 152)
        for which, name in enumerate(SYMBOLS):
            raw = name.encode()
            # Use distinct immutable Pascal names for the two oracle lookups.
            offset = 320 + which * 64
            p.data[offset : offset + 1 + len(raw)] = bytes([len(raw)]) + raw
            p.emit(0x807E0098, 0x389E0000 | offset, 0x38BE0000 | (160 + which * 4), 0x38DE00BC)
            p.call("FindSymbol")
            p.expect(0, "original bundle lookup export")
            p.emit(0x807E0000 | (160 + which * 4), 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("bundle oracle executes original PPC code")
    else:
        p.call("BundleFallbackToken")
        p.expect(0x49, "real fixture initializer executes before native access")
    samples = [b"testCode", b"testData", b"testRaw", b"missing", b"", b"x" * 254]
    if real:
        samples += [b"x" * 255, b"\xe9"]
    for sample in samples:
        offset = len(p.data)
        raw = sample + b"\0" if real else bytes([len(sample)]) + sample
        p.data += raw + bytes((-len(raw)) & 3)
        if real:
            p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000)
            p.call("CFStringCreateWithCString")
            p.nonzero("real CFString symbol name")
            p.emit(0x907E00B0)
        for which, name in enumerate(SYMBOLS):
            p.emit(0x387E0280, 0x809E00B0 if real else 0x389E0000 | offset)
            p.call(name)
            if mode in ("loader-failure", "converter-failure"):
                p.emit(0)
                break
            if mode == "foreign":
                p.expect(0x1234, "unrecognized profiles retain the original descriptor")
                continue
            p.emit(0x907E00B4)
            if real:
                p.emit(
                    0x387E0280,
                    0x809E00B0,
                    0x819E0000 | (160 + which * 4),
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x7FC2F378,
                    0x809E00B4,
                )
                p.equal("native symbol result matches original CFM bundle lookup")
            if (which == 0 and sample == b"testCode") or (which == 1 and sample == b"testData"):
                p.emit(0x807E00B4)
                p.nonzero("matching CFM class returns the actual shared symbol")
                if which == 0:
                    p.emit(0x81830000, 0x80430004, 0x7D8903A6, 0x4E800421, 0x7FC2F378)
                    p.expect(42, "returned function descriptor is callable")
                else:
                    p.emit(0x80630000)
                    p.expect(0x49, "returned data belongs to initialized real CFM image")
            else:
                p.emit(0x807E00B4)
                p.expect(0, "missing and wrong-class symbols return null")
        if real:
            if sample == b"testCode":
                for kind in (0, 2, 3):
                    p.emit(0x38600000 | kind, 0x907E02A0)
                    for which, name in enumerate(SYMBOLS):
                        p.emit(0x387E0280, 0x809E00B0)
                        p.call(name)
                        p.expect(0, "other executable kinds do not attempt CFM symbol lookup")
                        p.emit(
                            0x387E0280,
                            0x809E00B0,
                            0x819E0000 | (160 + which * 4),
                            0x800C0000,
                            0x804C0004,
                            0x7C0903A6,
                            0x4E800421,
                            0x7FC2F378,
                        )
                        p.expect(0, "original also rejects the non-CFM executable kind")
                p.emit(0x38600001, 0x907E02A0, 0x38600000, 0x907E02A8)
                for which, name in enumerate(SYMBOLS):
                    p.emit(0x387E0280, 0x809E00B0)
                    p.call(name)
                    p.expect(0, "invalid connection reports no symbol")
                    p.emit(
                        0x387E0280,
                        0x809E00B0,
                        0x819E0000 | (160 + which * 4),
                        0x800C0000,
                        0x804C0004,
                        0x7C0903A6,
                        0x4E800421,
                        0x7FC2F378,
                    )
                    p.expect(0, "original rejects the same invalid connection")
                p.emit(0x807E0090, 0x907E02A8)
            p.emit(0x807E00B0)
            p.call("CFRelease")
    if not real and mode == "valid":
        p.emit(0x887E02A4)
        p.expect(1, "on-demand load updates the same bundle before lookup")
        p.call("BundleLogCount")
        p.expect(10, "every missing/wrong-class symbol calls the real logging dependency")
        p.call("BundleLogArgument")
        p.expect(-2802, "symbol error logging preserves the signed CFM status")
    if real:
        p.emit(0x387E0098)
        p.call("CloseConnection")
        p.expect(0, "oracle connection closes")
    p.emit(0x387E0090)
    p.call("CloseConnection")
    p.expect(0, "symbol connection closes")
    p.finish(
        destination,
        (
            ("PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " Bundle Real Probe")
            if real
            else "PPC Carbon Bundle Probe"
        ),
        carbon=real,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode", nargs="?", default="valid", choices=("valid", "foreign", "loader-failure", "converter-failure")
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real-library probes require valid mode")
    make_bundle_probe(args.destination, args.real, args.library, args.mode)
