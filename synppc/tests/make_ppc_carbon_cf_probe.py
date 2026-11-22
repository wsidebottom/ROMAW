#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_probe.py
#
#   Summary:   Original PPC CF accessor ABI checks and optional real-library interoperability.
#
#   Classes:   Probe
#
#   Functions: check_array_apply, write_cf_sentinel, make_layout_probe, make_real_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC CF accessor ABI checks and optional real-library interoperability.

The default fixture checks only documented-in-source guest record layouts and
uses original sentinel PEFs for fallback initialization. --real instead requires
the user's installed CarbonLib/CoreFoundation and constructs, mutates and releases
actual objects through that fallback. No Apple code is embedded in this file.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback

ACCESSORS = [
    "CFArrayApplyFunction",
    "CFArrayGetCount",
    "CFArrayGetValueAtIndex",
    "CFArrayGetValues",
    "CFGetTypeID",
    "CFDataGetLength",
    "CFDataGetBytePtr",
    "CFDataGetMutableBytePtr",
    "CFDataGetBytes",
    "CFStringGetLength",
    "CFStringGetCharactersPtr",
]

ARRAY_APPLIER = [0x80A40000, 0x38A50001, 0x90A40000, 0x80A40004, 0x7CA51A78, 0x90A40004, 0x4E800020]


def check_array_apply(p, slot, start, values, indirect=False):
    p.emit(0x38600000, 0x907E0370, 0x907E0374)
    p.emit(
        (0x807E0000 if indirect else 0x387E0000) | slot,
        0x38800000 | start,
        0x38A00000 | len(values),
        0x38DE02D0,
        0x38FE0370,
    )
    p.call("CFArrayApplyFunction")
    p.emit(0x807E0370)
    p.expect(len(values), "native apply borrows every element")
    checksum = 0
    for value in values:
        checksum ^= value
    p.emit(0x807E0374)
    p.expect(checksum, "native apply preserves opaque value/context arguments")


class Probe:
    identify_failed_checks = False

    def __init__(self, imports):
        self.imports = imports
        self.import_base = 8 if 8 + 4 * len(imports) <= 0x1000 else 0x6000
        if self.import_base + 4 * len(imports) + 16 > 0x8000:
            self.import_base = 0x8000
        self.code = []
        self.data = bytearray(1024)
        self.checks = []
        self.routines = []
        self.stack_size = 64
        self.emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x93E10034, 0x7C5E1378)

    def reserve_parameter_words(self, count):
        # Keep saved nonvolatile registers above the outgoing parameter area.
        # The default 64-byte frame only has room for eight argument words.
        size = max(64, (24 + count * 4 + 12 + 15) & ~15)
        if size > 0x7FF0:
            raise ValueError("probe parameter area exceeds signed instruction offsets")
        self.stack_size = size
        self.code[0] = 0x94210000 | ((-size) & 65535)
        self.code[2] = 0x90010000 | (size + 4)
        self.code[3] = 0x93C10000 | (size - 8)
        self.code[4] = 0x93E10000 | (size - 12)

    def emit(self, *words):
        self.code.extend(words)

    def call(self, name, *, library=None):
        index = next(
            i
            for i, (entry_library, symbol, _) in enumerate(self.imports)
            if symbol == name and (library is None or entry_library == library)
        )
        offset = self.import_base + index * 4
        if offset < 0x8000:
            self.emit(0x819E0000 | offset)
        else:
            self.emit(0x3D9E0000 | ((offset + 0x8000) >> 16), 0x818C0000 | (offset & 0xFFFF))
        self.emit(0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def _check(self, compare, branch, label):
        self.checks.append({"offset": len(self.code) * 4, "check": label})
        if self.identify_failed_checks:
            index = len(self.checks)
            if index > 0xFFFF:
                raise ValueError("Too many assertions in one regression group")
            self.emit(compare, branch | 16, 0x3C004341, 0x60000000 | index, 0)
        else:
            self.emit(compare, branch | 8, 0)

    def equal(self, label):
        self._check(0x7C032000, 0x41820000, label)

    def expect(self, value, label):
        value &= 0xFFFFFFFF
        self.emit(0x3C800000 | (value >> 16), 0x60840000 | (value & 65535))
        self.equal(label)

    def pointer(self, offset, label):
        self.emit(0x389E0000 | offset)
        self.equal(label)

    def nonzero(self, label):
        self._check(0x2C030000, 0x40820000, label)

    def finish(self, destination, name, carbon=False, extra_resources=(), *, trailing_words=()):
        self.emit(
            0x83E10000 | (self.stack_size - 12),
            0x83C10000 | (self.stack_size - 8),
            0x80010000 | (self.stack_size + 4),
            0x7C0803A6,
            0x38210000 | self.stack_size,
            0x38600000,
            0x4E800020,
        )
        import_end = self.import_base + 4 * len(self.imports)
        if len(self.data) < import_end:
            self.data.extend(bytes(import_end - len(self.data)))
        reloc = [0x4600]
        if self.import_base != 8:
            reloc += [0xA000, self.import_base]
        for first in range(0, len(self.imports), 512):
            count = min(512, len(self.imports) - first)
            reloc.append(0x4A00 | (count - 1))
        for offset, words in self.routines:
            struct.pack_into(">II", self.data, offset, len(self.code) * 4, 0)
            self.emit(*words)
            reloc += [0xA000 | (offset >> 16), offset & 0xFFFF, 0x4600]
        self.code.extend(trailing_words)
        image = pef(self.code, self.data, imports=self.imports, main=0, reloc=reloc)
        # CarbonLib's real initializer checks SIZE 1/0/-1 and requires these
        # application capability bits (reference code 0x60790-0x60854).
        resources = [(b"SIZE", 0, struct.pack(">HII", 0x58C0, 16 * 1024 * 1024, 16 * 1024 * 1024))] if carbon else []
        write_fragment(destination, name, image, 1, extra_resources=[*resources, *extra_resources])
        (destination / "checks.json").write_text(json.dumps(self.checks, indent=2) + "\n")


def write_cf_sentinel(destination):
    image = pef(
        [0x80620010, 0x4E800020, 0x38600049, 0x90620010, 0x38600000, 0x4E800020, 0xC8230008, 0x4E800020],
        struct.pack(">4I", 0, 0, 8, 0) + bytes(8) + struct.pack(">II", 24, 0),
        exports=[("CFFallbackToken", 2, 0, 1), ("CFDateGetAbsoluteTime", 2, 24, 1)],
        init=8,
        reloc=[0x4601, 0xA000, 24, 0x4600],
    )
    write_fragment(destination, "CoreFoundationLib", image, 0)


def make_layout_probe(destination, native_library="CarbonLib"):
    write_carbon_fallback(destination)
    write_cf_sentinel(destination)
    imports = [(native_library, name, 2) for name in ACCESSORS]
    imports += [("CarbonLib", "CarbonFallbackToken", 2), ("CoreFoundationLib", "CFFallbackToken", 2)]
    p = Probe(imports)
    p.routines.append((720, ARRAY_APPLIER))
    struct.pack_into(">I", p.data, 136, 0x81234567)
    struct.pack_into(">I", p.data, 132, 0xAABBCCDD)
    struct.pack_into(">3I", p.data, 140, 0x11111111, 0xDEADBEEF, 0xFFFFFFFF)
    struct.pack_into(">I", p.data, 168, 16)
    p.data[224:240] = bytes([0, 255, 128, 127]) + bytes(range(1, 13))
    p.data[272:292] = b"\x5a" * 20
    p.data[316:336] = b"\x5a" * 20
    p.emit(0x387E00E0, 0x907E00B4)
    p.emit(0x387E0080)
    p.call("CFArrayGetCount")
    p.expect(0x81234567, "array count guest byte order")
    p.emit(0x387E0080)
    p.call("CFGetTypeID")
    p.expect(0xCC, "type ID from guest runtime header")
    p.emit(0x38600000, 0x6063CC00, 0x907E0084, 0x38600003, 0x907E0088)
    for index, expected in ((1, 0xDEADBEEF), (2, 0xFFFFFFFF)):
        p.emit(0x387E0080, 0x38800000 | index)
        p.call("CFArrayGetValueAtIndex")
        p.expect(expected, "opaque array value bits")
    p.emit(0x387E0080, 0x38800000, 0x38A00003, 0x38DE0140)
    p.call("CFArrayGetValues")
    for i, expected in enumerate((0x11111111, 0xDEADBEEF, 0xFFFFFFFF)):
        p.emit(0x807E0140 + i * 4)
        p.expect(expected, "array range guest pointer byte order")
    for offset in (316, 332):
        p.emit(0x807E0000 | offset)
        p.expect(0x5A5A5A5A, "array output guard")
    check_array_apply(p, 128, 0, [0x11111111, 0xDEADBEEF, 0xFFFFFFFF])
    p.emit(0x387E00A0)
    p.call("CFDataGetLength")
    p.expect(16, "data length")
    for name in ("CFDataGetBytePtr", "CFDataGetMutableBytePtr"):
        p.emit(0x387E00A0)
        p.call(name)
        p.pointer(224, name)
    p.emit(0x38A0009A, 0x98A30001)
    p.emit(0x387E00A0, 0x38800001, 0x38A00004, 0x38DE0111)
    p.call("CFDataGetBytes")
    p.emit(0x807E0110)
    p.expect(0x5A9A807F, "CFRange start and destination guard")
    p.emit(0x807E0114)
    p.expect(0x015A5A5A, "CFRange length and trailing guard")
    p.emit(0x387E00A0, 0x38800010, 0x38A00000, 0x38C00000)
    p.call("CFDataGetBytes")
    for name in ("CarbonFallbackToken", "CFFallbackToken"):
        p.call(name)
        p.expect(73, name + " initialized")
    struct.pack_into(">III", p.data, 388, 0x10, 3, 0x0041D800)
    p.emit(0x387E0180)
    p.call("CFStringGetLength")
    p.expect(3, "inline UTF-16 length")
    p.emit(0x387E0180)
    p.call("CFStringGetCharactersPtr")
    p.pointer(396, "inline UTF-16 borrowed buffer")
    label = "Carbon" if native_library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " CF Layout Probe")


def make_real_probe(destination, native_library="CarbonLib"):
    constructors = [
        "CFArrayCreate",
        "CFArrayCreateMutable",
        "CFArrayAppendValue",
        "CFArrayRemoveAllValues",
        "CFArrayRemoveValueAtIndex",
        "CFArraySetValueAtIndex",
        "CFRelease",
        "CFDataCreate",
        "CFDataCreateMutable",
        "CFDataAppendBytes",
        "CFDataSetLength",
        "CFArrayGetTypeID",
        "CFDataGetTypeID",
        "CFStringCreateWithCharacters",
        "CFStringCreateWithCString",
        "CFStringCreateWithPascalString",
        "CFStringCreateMutable",
        "CFStringAppendCharacters",
    ]
    p = Probe([(native_library, name, 2) for name in ACCESSORS + constructors])
    # Original callbacks count ownership operations and preserve opaque values.
    p.routines = [
        (720, ARRAY_APPLIER),
        (640, [0x80A202BC, 0x38A50001, 0x90A202BC, 0x7C832378, 0x4E800020]),
        (648, [0x80A202C0, 0x38A50001, 0x90A202C0, 0x4E800020]),
    ]

    def array_reads(slot, selected, start, values):
        for index, expected in selected:
            p.emit(0x807E0000 | slot, 0x38800000 | index)
            p.call("CFArrayGetValueAtIndex")
            p.expect(expected, "real array element")
        p.emit(0x3C605A5A, 0x60635A5A)
        for offset in range(508, 520 + len(values) * 4, 4):
            p.emit(0x907E0000 | offset)
        p.emit(0x807E0000 | slot, 0x38800000 | start, 0x38A00000 | len(values), 0x38DE0200)
        p.call("CFArrayGetValues")
        for i, expected in enumerate(values):
            p.emit(0x807E0200 + i * 4)
            p.expect(expected, "real array range")
        for offset in (508, 512 + len(values) * 4):
            p.emit(0x807E0000 | offset)
            p.expect(0x5A5A5A5A, "real array output guard")
        check_array_apply(p, slot, start, values, indirect=True)

    struct.pack_into(">3I", p.data, 192, 0x11111111, 0x22222222, 0x33333333)
    p.data[224:240] = bytes([0, 255, 128, 127]) + bytes(range(1, 13))
    p.data[272:340] = b"\x5a" * 68
    p.emit(0x38600000, 0x389E00C0, 0x38A00003, 0x38C00000)
    p.call("CFArrayCreate")
    p.nonzero("immutable array created by real fallback")
    p.emit(0x907E0080)
    p.call("CFArrayGetCount")
    p.expect(3, "immutable array native count")
    array_reads(128, [(1, 0x22222222)], 1, [0x22222222, 0x33333333])
    p.call("CFArrayGetTypeID")
    p.emit(0x907E0090, 0x807E0080)
    p.call("CFGetTypeID")
    p.emit(0x809E0090)
    p.equal("native type matches real runtime array class")
    p.emit(0x38600000, 0x38800000, 0x38A00000)
    p.call("CFArrayCreateMutable")
    p.nonzero("mutable array created by real fallback")
    p.emit(0x907E0084)
    p.call("CFArrayGetCount")
    p.expect(0, "empty mutable array native count")
    for i in range(32):
        p.emit(0x807E0084, 0x38801000 + i)
        p.call("CFArrayAppendValue")
    p.emit(0x807E0084)
    p.call("CFArrayGetCount")
    p.expect(32, "real array growth visible to native count")
    for _ in range(3):
        p.emit(0x807E0084, 0x38800000)
        p.call("CFArrayRemoveValueAtIndex")
    p.emit(0x807E0084, 0x8063000C, 0x88630000)
    p.nonzero("real deque has a nonzero left offset")
    array_reads(132, [(0, 0x1003), (28, 0x101F)], 1, [0x1004, 0x1005, 0x1006])
    p.emit(0x807E0084)
    p.call("CFArrayRemoveAllValues")
    p.emit(0x807E0084)
    p.call("CFArrayGetCount")
    p.expect(0, "real array removal visible to native count")
    p.emit(0x3BE00000)
    loop = len(p.code)
    p.emit(0x807E0084, 0x389F1000)
    p.call("CFArrayAppendValue")
    p.emit(0x3BFF0001, 0x2C1F4E20)
    branch = len(p.code)
    p.emit(0x41800000 | (((loop - branch) * 4) & 65535))
    p.emit(0x807E0084, 0x80630004, 0x70630003)
    p.expect(3, "real large array uses CFStorage")
    p.emit(0x807E0084, 0x8063000C, 0x8863001C)
    p.expect(0, "real CFStorage has branch nodes")
    # Cross an actual root-child boundary rather than assuming a leaf size.
    p.emit(0x807E0084, 0x8063000C, 0x80630020, 0x80630000, 0x5463F0BE, 0x3863FFFE, 0x907E02C4)
    p.emit(0x3C605A5A, 0x60635A5A)
    for offset in range(508, 536, 4):
        p.emit(0x907E0000 | offset)
    p.emit(0x807E0084, 0x809E02C4, 0x38A00004, 0x38DE0200)
    p.call("CFArrayGetValues")
    for i in range(4):
        p.emit(0x807E0200 + i * 4, 0x809E02C4, 0x38841000 + i)
        p.equal("real range across a CFStorage branch boundary")
    for offset in (508, 528):
        p.emit(0x807E0000 | offset)
        p.expect(0x5A5A5A5A, "CFStorage boundary-copy guard")
    array_reads(
        132,
        [(0, 0x1000), (254, 0x10FE), (16384, 0x5000), (19999, 0x5E1F)],
        16380,
        [0x1000 + i for i in range(16380, 16392)],
    )
    p.emit(0x807E0084, 0x38800000, 0x3CA0DEAD, 0x60A5BEEF)
    p.call("CFArraySetValueAtIndex")
    array_reads(132, [(0, 0xDEADBEEF)], 0, [0xDEADBEEF, 0x1001, 0x1002])
    p.emit(0x807E0084, 0x38800000)
    p.call("CFArrayRemoveValueAtIndex")
    array_reads(132, [(19998, 0x5E1F)], 0, [0x1001, 0x1002, 0x1003])
    # Give both immutable and fixed mutable arrays genuine custom callbacks.
    p.emit(0x387E0280, 0x907E0294, 0x387E0288, 0x907E0298)
    p.emit(0x38600000, 0x38800008, 0x38BE0290)
    p.call("CFArrayCreateMutable")
    p.nonzero("fixed custom array created by fallback")
    p.emit(0x907E0098)
    for i in range(3):
        p.emit(0x807E0098, 0x38802000 + i)
        p.call("CFArrayAppendValue")
    p.emit(0x807E0098, 0x80630004, 0x7063000F)
    p.expect(13, "fixed array and custom callback layout")
    array_reads(152, [(2, 0x2002)], 0, [0x2000, 0x2001, 0x2002])
    p.emit(0x38600000, 0x389E00C0, 0x38A00003, 0x38DE0290)
    p.call("CFArrayCreate")
    p.nonzero("immutable custom array created by fallback")
    p.emit(0x907E009C, 0x80630004, 0x7063000F)
    p.expect(12, "immutable array and custom callback layout")
    array_reads(156, [(0, 0x11111111)], 0, [0x11111111, 0x22222222, 0x33333333])
    p.emit(0x807E02BC)
    p.expect(6, "native getters perform no extra retains")
    p.emit(0x807E02C0)
    p.expect(0, "native getters perform no releases")
    p.emit(0x38600000, 0x389E00E0, 0x38A00010)
    p.call("CFDataCreate")
    p.nonzero("immutable data created by real fallback")
    p.emit(0x907E0088)
    p.call("CFDataGetLength")
    p.expect(16, "immutable data native length")
    p.call("CFDataGetTypeID")
    p.emit(0x907E0094, 0x807E0088)
    p.call("CFGetTypeID")
    p.emit(0x809E0094)
    p.equal("native type matches real runtime data class")
    # CFDataCreate owns a copy; changing its source does not alter that copy.
    p.emit(0x3860009A, 0x987E00E0, 0x807E0088)
    p.call("CFDataGetBytePtr")
    p.nonzero("immutable bytes from real data")
    p.emit(0x80630000)
    p.expect(0x00FF807F, "immutable constructor copy remains intact")
    p.emit(0x38600000, 0x987E00E0, 0x38600000, 0x38800000)
    p.call("CFDataCreateMutable")
    p.nonzero("mutable data created by real fallback")
    p.emit(0x907E008C, 0x389E00E0, 0x38A00010)
    p.call("CFDataAppendBytes")
    p.emit(0x807E008C)
    p.call("CFDataGetLength")
    p.expect(16, "real data append visible to native length")
    p.emit(0x807E008C)
    p.call("CFDataGetMutableBytePtr")
    p.nonzero("mutable bytes from real data")
    p.emit(0x38A0009A, 0x98A30001)
    p.emit(0x807E008C, 0x38800001, 0x38A00004, 0x38DE0111)
    p.call("CFDataGetBytes")
    p.emit(0x807E0110)
    p.expect(0x5A9A807F, "real mutable bytes and CFRange start")
    p.emit(0x807E0114)
    p.expect(0x015A5A5A, "real CFRange end and guard")
    p.emit(0x807E008C, 0x38800028)
    p.call("CFDataSetLength")
    p.emit(0x807E008C)
    p.call("CFDataGetLength")
    p.expect(40, "real resize visible to native length")
    p.emit(0x807E008C, 0x38800010, 0x38A00018, 0x38DE0130)
    p.call("CFDataGetBytes")
    for offset in range(304, 328, 4):
        p.emit(0x807E0000 | offset)
        p.expect(0, "new real data bytes zero-filled")
    for offset in (300, 328):
        p.emit(0x807E0000 | offset)
        p.expect(0x5A5A5A5A, "resize-copy output guard")
    p.emit(0x807E008C, 0x38800000)
    p.call("CFDataSetLength")
    p.emit(0x807E008C)
    p.call("CFDataGetLength")
    p.expect(0, "real shrink visible to native length")
    struct.pack_into(">4H", p.data, 768, 0x41, 0xD800, 0xDC00, 0)
    p.data[800:804] = b"ABC\0"
    p.data[820:824] = b"\x03A\x00B"
    p.emit(0x38600000, 0x389E0300, 0x38A00004)
    p.call("CFStringCreateWithCharacters")
    p.nonzero("real UTF-16 string constructor")
    p.emit(0x907E00A0)
    p.call("CFStringGetLength")
    p.expect(4, "UTF-16 length counts code units including embedded NUL")
    p.emit(0x807E00A0)
    p.call("CFStringGetCharactersPtr")
    p.nonzero("real UTF-16 borrowed buffer")
    p.emit(0x80830004, 0x7C832378)
    p.expect(0xDC000000, "UTF-16 buffer preserves surrogates and embedded NUL")
    for offset, constructor in ((800, "CFStringCreateWithCString"), (820, "CFStringCreateWithPascalString")):
        p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000)
        p.call(constructor)
        p.nonzero("real byte string constructor")
        p.emit(0x907E00A4)
        p.call("CFStringGetLength")
        p.expect(3, "real byte string length")
        p.emit(0x807E00A4)
        p.call("CFStringGetCharactersPtr")
        p.expect(0, "byte string does not expose a UTF-16 buffer")
        p.emit(0x807E00A4)
        p.call("CFRelease")
    p.emit(0x38600000, 0x38800000)
    p.call("CFStringCreateMutable")
    p.nonzero("real mutable string constructor")
    p.emit(0x907E00A8, 0x389E0300, 0x38A00004)
    p.call("CFStringAppendCharacters")
    p.emit(0x807E00A8)
    p.call("CFStringGetLength")
    p.expect(4, "real mutable string native length")
    p.emit(0x807E00A8, 0x389E0300, 0x38A00002)
    p.call("CFStringAppendCharacters")
    p.emit(0x807E00A8)
    p.call("CFStringGetLength")
    p.expect(6, "real string mutation visible to native length")
    p.emit(0x807E00A8)
    p.call("CFStringGetCharactersPtr")
    p.nonzero("real external UTF-16 borrowed buffer")
    p.emit(0x80830008, 0x7C832378)
    p.expect(0x0041D800, "real mutable UTF-16 buffer after append")
    for slot in (128, 132, 136, 140, 152, 156, 160, 168):
        p.emit(0x807E0000 | slot)
        p.call("CFRelease")
    p.emit(0x807E02C0)
    p.expect(6, "real release calls the custom callbacks")
    label = "Carbon" if native_library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " CF Real Probe", carbon=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    (make_real_probe if args.real else make_layout_probe)(args.destination, args.library)
