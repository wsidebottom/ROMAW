#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_string_pointer_probe.py
#
#   Summary:   Original borrowed CF string pointer fixtures and original-library comparisons.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_pointer_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original borrowed CF string pointer fixtures and original-library comparisons."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_cf_pascal_probe import (
    POINTER_NAMES,
    pointer_reference_bodies,
    write_reference as write_string_reference,
)
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAMES = POINTER_NAMES


def write_reference(destination, mode):
    if mode != "init-failure":
        write_string_reference(destination, mode)
        return
    code = [0] * (0x5DEE0 // 4)
    data = bytearray(0x72A8)
    bodies = pointer_reference_bodies()
    bodies.append(
        (
            "__CFStringComputeEightBitStringEncoding",
            0x357C4,
            [0x81820080, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420],
        )
    )
    imports = [("InterfaceLib", "ROMAWCFBorrowFailure", 2)]
    struct.pack_into(">I", data, 0x3A70, 0xFFFFFFFF)
    exports = []
    for i, (name, offset, words) in enumerate(bodies):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, i * 8, offset, 0)
        exports.append((name, 2, i * 8, 1))
    code[2:6] = [0x38600049, 0x90620300, 0x38600000, 0x4E800020]
    struct.pack_into(">II", data, 40, 8, 0)
    reloc = [0x4605, 0xA000, 0x80, 0x4A00]
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=40, reloc=reloc), 0
    )


def make_pointer_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
    imports = [(library, name, 2) for name in NAMES]
    if real:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFStringCreateWithBytes",
                "CFStringCreateWithCharacters",
                "CFStringCreateWithCString",
                "CFStringCreateMutable",
                "CFStringAppendCharacters",
                "CFRelease",
            )
        ]
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [("CoreFoundationLib", name, 2) for name in ("CFBorrowToken", "CFBorrowSetEncoding")]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    p.data += bytes(4096 - len(p.data))

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    if real:
        for offset, name in ((256, b"CoreFoundationLib"), (288, NAMES[0].encode()), (336, NAMES[1].encode())):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "original borrowed-pointer connection")
        for offset, slot in ((288, 160), (336, 164)):
            p.emit(0x807E0090, 0x389E0000 | offset, 0x38BE0000 | slot, 0x38DE00B0)
            p.call("FindSymbol")
            p.expect(0, "original borrowed-pointer export")
            p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("pointer oracle executes original PPC code")
        p.data[512:768] = bytes(range(256))
        p.data[768:772] = b"ABC\0"
        units = (0, 0x41, 0x100, 0xE9, 0xD800, 0xDC00, 0xFFFF)
        for i in range(64):
            struct.pack_into(">H", p.data, 800 + i * 2, units[i % len(units)])
        struct.pack_into(">3H", p.data, 928, 0x41, 0x42, 0x43)
        # Count actual borrowed results so the differential test cannot pass
        # solely by returning NULL for every input.
        objects = [("bytes", n) for n in (0, 1, 3, 31, 255, 256)]
        objects += [("unicode", n) for n in (0, 1, 3, 7, 33)]
        objects += [("cstring", 3), ("mutable-ascii", 3), ("mutable-unicode", 7)]
        for kind, length in objects:
            if kind.startswith("mutable"):
                p.emit(0x38600000, 0x38800000)
                p.call("CFStringCreateMutable")
                p.nonzero("real mutable source")
                p.emit(0x907E00C8, 0x389E03A0 if kind == "mutable-ascii" else 0x389E0320, 0x38A00000 | length)
                p.call("CFStringAppendCharacters")
                p.emit(0x807E00C8)
            elif kind == "cstring":
                p.emit(0x38600000, 0x389E0300, 0x38A00600)
                p.call("CFStringCreateWithCString")
            elif kind == "unicode":
                p.emit(0x38600000, 0x389E0320, 0x38A00000 | length)
                p.call("CFStringCreateWithCharacters")
            else:
                p.emit(0x38600000, 0x389E0200, 0x38A00000 | length, 0x38C00000, 0x38E00000)
                p.call("CFStringCreateWithBytes")
            p.nonzero("real pointer source constructor")
            p.emit(0x907E00C8)
            for encoding in (0, 0x600, 0x100, 0x8000100, 0x201, 0xBFF, 0xFFFFFFFF):
                for which, name in enumerate(NAMES):
                    p.emit(0x807E00C8)
                    integer(4, encoding)
                    p.call(name)
                    p.emit(0x907E00B4, 0x807E00C8)
                    integer(4, encoding)
                    p.emit(
                        0x819E0000 | (160 + which * 4),
                        0x800C0000,
                        0x804C0004,
                        0x7C0903A6,
                        0x4E800421,
                        0x7FC2F378,
                        0x809E00B4,
                    )
                    p.equal(f"{kind} {length} {encoding:x}: original/native pointer identity")
                    if encoding == 0xFFFFFFFF:
                        p.expect(0, "an incompatible encoding cannot borrow storage")
                    p.emit(
                        0x2C030000,
                        0x41820010,
                        0x80BE0000 | (188 + which * 4),
                        0x38A50001,
                        0x90BE0000 | (188 + which * 4),
                    )
            p.emit(0x807E00C8)
            p.call("CFRelease")
        for slot in (188, 192):
            p.emit(0x807E0000 | slot)
            p.nonzero("both pointer APIs borrow actual storage in the real matrix")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "original pointer connection closes")
    else:
        p.call("CFBorrowToken")
        p.expect(0x49, "the companion initializer actually ran")
        if mode == "init-failure":
            p.emit(0x387E0200, 0x38800000)
            p.call(NAMES[0])
            p.emit(0)
            label = "Carbon" if library == "CarbonLib" else "Core"
            p.finish(destination, f"PPC {label} String Pointer Probe")
            return
        for slot, flags, length, source in (
            (512, 0xC, 3, None),
            (544, 8, 3, None),
            (576, 4, 3, None),
            (608, 0, 3, None),
            (640, 0x10, 3, None),
            (672, 0x2C, 3, 800),
            (704, 0x2D, 4, 816),
            (736, 0x2D, 3, 832),
            (768, 0x2D, 0xFFFFFF, 848),
        ):
            struct.pack_into(">III", p.data, slot, 0, flags, length)
            if source is not None:
                p.data[source : source + 5] = b"\3ABC\0"
                struct.pack_into(">I", p.data, slot + 12, length)
                p.emit(0x387E0000 | source, 0x907E0000 | (slot + 8))
                contents = source
            else:
                contents = slot + (8 if flags & 5 == 4 else 12)
                p.data[contents : contents + (5 if flags & 4 else 4)] = b"\3ABC\0" if flags & 4 else b"ABC\0"
            for which, name in enumerate(NAMES):
                p.emit(0x387E0000 | slot, 0x38800000)
                p.call(name)
                if mode == "foreign":
                    p.expect(0x1234, "foreign profiles retain their original pointer entry")
                    continue
                valid = (
                    bool(flags & 8) if which == 0 else bool(flags & 4) and not flags & 0x10 and slot not in (704, 768)
                )
                if valid:
                    p.pointer(
                        contents + (bool(flags & 4) if which == 0 else 0), "native pointer borrows expected storage"
                    )
                else:
                    p.expect(0, "unavailable/inconsistent formats return NULL")
        if mode != "foreign":
            p.emit(0x807E030C)
            p.expect(4, "Pascal pointer checks update the same lazy length cache")
        p.call("CarbonFallbackToken")
        p.expect(0x49, "the main fallback initializer remains active")
    label = "Carbon" if library == "CarbonLib" else "Core"
    # Classic application names have a 31-byte limit.
    kind = "Str Ptr Real" if real else "String Pointer"
    p.finish(destination, f"PPC {label} {kind} Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid", choices=("valid", "foreign", "init-failure"))
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real pointer validation requires valid mode")
    make_pointer_probe(args.destination, args.library, args.real, args.mode)
