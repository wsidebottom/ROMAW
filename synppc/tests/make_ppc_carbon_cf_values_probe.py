#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_values_probe.py
#
#   Summary:   Large-cohort native/original CoreFoundation interoperability checks.
#
#   Classes:   ValuesProbe
#
#   Functions: make_probe, write_foreign_corefoundation, check_foreign_words,
#              check_foreign_frame, make_foreign_probe, make_fallback_probe,
#              make_grouped_fallback_probe, make_guard_probe.
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Large-cohort native/original CoreFoundation interoperability checks.

This fixture uses only generated original PPC code. --real requires the user's
installed Apple libraries, including their ordinary dependency initializers.
"""
import argparse
import json
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

ROOT = Path(__file__).resolve().parents[2]
ROWS = json.loads((ROOT / "carbonlib/plans/cf-values-batch.json").read_text())["entries"]
NAMES = [row["name"] for row in ROWS]


NUMERIC_DATA = (
    ("CoreFoundationLib", "kCFAbsoluteTimeIntervalSince1970", 0x41CD27E4, 0x40000000),
    ("CoreFoundationLib", "kCFAbsoluteTimeIntervalSince1904", 0x41E6CEAF, 0x20000000),
    ("MathLib", "pi", 0x400921FB, 0x54442D18),
    ("MathLib", "_FE_DFL_ENV", 0x00000000, 0x00000000),
)


class ValuesProbe:
    def __init__(
        self,
        library,
        rows=None,
        extra_names=(),
        original_library="CoreFoundationLib",
        raw_start=None,
        data_imports=(),
        far_oracle_tables=False,
    ):
        self.rows = ROWS if rows is None else rows
        self.names = [row["name"] for row in self.rows]
        extra = [
            "AEBuildDesc",
            "GetSharedLibrary",
            "FindSymbol",
            "CloseConnection",
            "CFRelease",
            "CFArrayGetCount",
            "CFArrayGetValueAtIndex",
            "CFDataGetLength",
            "CFDataGetBytePtr",
            "CFStringGetLength",
            "CFStringGetCharacters",
            "CFSetCreate",
        ]
        extra.extend(extra_names)
        self.p = Probe(
            [(library, name, 2) for name in self.names]
            + [("CarbonLib", name, 2) for name in extra if name not in self.names]
            + list(data_imports)
        )
        self.p.data.extend(bytes(32768 - len(self.p.data)))
        self.used = set()
        self.cursor = raw_start if raw_start is not None else (0x6800 if self.p.import_base == 8 else 0x5800)
        self.original_vectors = 0x2400
        if not far_oracle_tables:
            self.cursor = max(self.cursor, self.original_vectors + 4 * len(self.names))
        # Raw records use nonnegative 16-bit TOC displacements. Stop at imports when they start earlier.
        self.raw_limit = 0x6000 if self.p.import_base == 0x6000 else 0x8000
        self.next_slot = 0x1000
        self.library = library
        self.constants = {}
        p = self.p
        oracles = [row.get("original_library", original_library) for row in self.rows]
        self.oracle_connections = {}
        library_offset = 0x2200
        for index, oracle in enumerate(dict.fromkeys(oracles)):
            connection = 0x2100 + index * 16
            self.oracle_connections[oracle] = connection
            name = oracle.encode("ascii")
            p.data[library_offset : library_offset + len(name) + 1] = bytes([len(name)]) + name
            self.call(
                "GetSharedLibrary",
                [self.ptr(library_offset), 0x70777063, 1, self.ptr(connection), self.ptr(connection + 4), 0],
            )
            p.expect(0, "open initialized original " + oracle + " oracle")
            library_offset += len(name) + 1
        vectors_size = 4 * len(self.names)
        offset = max(0x2800, self.original_vectors + vectors_size)
        name_bytes = sum(len(name.encode("ascii")) + 1 for name in self.names)
        if far_oracle_tables or offset + name_bytes > self.raw_limit:
            # Keep large oracle name tables beyond the fixture's signed offset area.
            import_end = p.import_base + 4 * len(p.imports)
            # Leave room for the support imports appended by derived fixtures.
            offset = max(0x8000, (import_end + 63) & ~15)
            if far_oracle_tables:
                self.original_vectors = offset
                offset += vectors_size
            p.data.extend(bytes(offset + name_bytes - len(p.data)))
        else:
            self.cursor = max(self.cursor, (offset + name_bytes + 3) & ~3)
        for index, (name, oracle) in enumerate(zip(self.names, oracles)):
            encoded = name.encode("ascii")
            p.data[offset : offset + len(encoded) + 1] = bytes([len(encoded)]) + encoded
            self.call(
                "FindSymbol",
                [
                    self.slot(self.oracle_connections[oracle]),
                    self.ptr(offset),
                    self.ptr(self.original_vectors + index * 4),
                    self.ptr(0x2108),
                ],
            )
            p.expect(0, "resolve original " + name)
            p.emit(0x887E2108)
            p.expect(2, "original export is a PPC descriptor")
            self.load_oracle_vector(index, 3)
            p.emit(0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("original opcode is not the native service instruction")
            offset += len(encoded) + 1

    def section(self, name, action):
        if not hasattr(self, "sections"):
            self.sections = [{"section": "Original oracle setup", "checks": len(self.p.checks)}]
        start = len(self.p.checks)
        action()
        self.sections.append({"section": name, "checks": len(self.p.checks) - start})

    def write_coverage(self, destination, application, expected, cleanup_checks=1):
        expected = set(expected)
        report = {
            "application": application,
            "sections": self.sections + [{"section": "Oracle cleanup", "checks": cleanup_checks}],
            "assertions": len(self.p.checks),
            "exercised_entries": sorted(expected & self.used),
            "pending_entries": sorted(expected - self.used),
            "status": "generated_not_validated",
        }
        if hasattr(self, "service_names"):
            missing = set(self.service_names) - self.used
            if missing:
                raise ValueError(f"Services lack calls: {sorted(missing)}")
            report["service_entries"] = sorted(self.service_names)
        if hasattr(self, "diagnostic_lines"):
            report["diagnostic_lines"] = self.diagnostic_lines
        (destination / "coverage.json").write_text(json.dumps(report, indent=2) + "\n", newline="\n")

    @staticmethod
    def ptr(offset):
        return ("pointer", offset)

    @staticmethod
    def slot(offset):
        return ("slot", offset)

    @staticmethod
    def pair_slot(offset):
        return ("pair", offset)

    def raw(self, data):
        offset = self.cursor
        self.cursor = (offset + len(data) + 3) & ~3
        if self.cursor > self.raw_limit:
            raise ValueError("fixture raw-data extent exceeded")
        self.p.data[offset : offset + len(data)] = data
        return self.ptr(offset)

    def arguments(self, values, side):
        for index, value in enumerate(values):
            register = index + 3 if index < 8 else 11
            if isinstance(value, tuple):
                kind, offset = value
                if kind == "pair":
                    offset += side * 4
                if kind == "output":
                    offset += side * 256
                if offset >= 0x8000 and kind in ("pointer", "output"):
                    upper = (offset + 0x8000) >> 16
                    self.p.emit(0x3C1E0000 | register << 21 | upper)
                    self.p.emit(0x38000000 | register << 21 | register << 16 | (offset & 0xFFFF))
                else:
                    self.p.emit(
                        (0x38000000 if kind in ("pointer", "output") else 0x80000000)
                        | register << 21
                        | 30 << 16
                        | offset
                    )
                if kind == "global":
                    self.p.emit(0x80000000 | register << 21 | register << 16)
            else:
                value &= 0xFFFFFFFF
                self.p.emit(
                    0x3C000000 | register << 21 | value >> 16,
                    0x60000000 | register << 21 | register << 16 | value & 65535,
                )
            if index >= 8:
                offset = 24 + 4 * index
                if offset + 4 > self.p.stack_size - 12:
                    raise ValueError("Insufficient outgoing PPC parameter space")
                self.p.emit(0x91610000 | offset)

    def load_oracle_vector(self, index, register):
        offset = self.original_vectors + index * 4
        if offset < 0x8000:
            self.p.emit(0x801E0000 | register << 21 | offset)
        else:
            self.p.emit(
                0x3C1E0000 | register << 21 | ((offset + 0x8000) >> 16),
                0x80000000 | register << 21 | register << 16 | (offset & 0xFFFF),
            )

    def call(self, name, arguments=(), side=0):
        self.arguments(arguments, side)
        if side:
            index = self.names.index(name)
            self.load_oracle_vector(index, 12)
            self.p.emit(0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
        else:
            self.p.call(name)
            if name in self.names:
                self.used.add(name)

    def scalar(self, name, arguments=(), expected=None):
        for side in (0, 1):
            self.call(name, arguments, side)
            if expected is not None:
                self.p.expect(expected, name + " expected result")
            if not side:
                self.p.emit(0x907E2000)
            else:
                self.p.emit(0x809E2000)
                self.p.equal(name + " matches original result")

    def factory(self, name, arguments=(), check=True):
        slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.call(name, arguments, side)
            self.p.nonzero(name + " returns an object")
            self.p.emit(0x907E0000 | (slot + side * 4))
        if check:
            self.equal_objects(slot, name)
        return self.pair_slot(slot)

    def constant(self, name, indirect=True):
        if name not in self.constants:
            slot = self.next_slot
            self.next_slot += 8
            encoded = name.encode()
            pointer = self.raw(bytes([len(encoded)]) + encoded)
            self.call("FindSymbol", [self.slot(0x2100), pointer, self.ptr(slot), self.ptr(0x2108)])
            self.p.expect(0, "resolve live " + name)
            self.p.emit(0x887E2108)
            self.p.expect(1, "constant export is real data")
            self.constants[name] = slot
        return ("global" if indirect else "slot", self.constants[name])

    def output(self, name, arguments, size, returns_value=False):
        if returns_value:
            self.scalar(name, arguments)
        else:
            for side in (0, 1):
                self.call(name, arguments, side)
        for index in range(size):
            self.p.emit(0x887E0000 | (0x1C00 + index), 0x889E0000 | (0x1D00 + index))
            self.p.equal(name + " output byte " + str(index))

    def equal_objects(self, slot, label):
        self.call("CFEqual", [self.slot(slot), self.slot(slot + 4)], 1)
        self.p.expect(1, label + " native/original object values agree")

    def mutate(self, name, arguments):
        for side in (0, 1):
            self.call(name, arguments, side)
        self.equal_objects(arguments[0][1], name)

    def release(self, pair):
        self.call("CFRelease", [self.slot(pair[1])])
        self.call("CFRelease", [self.slot(pair[1] + 4)])

    def collections(self):
        for name in ("CFDataGetTypeID", "CFSetGetTypeID"):
            self.scalar(name)
        raw = self.raw(b"abcdefghABCDEFGH")
        empty = self.factory("CFDataCreate", [0, raw, 8])
        immutable = self.factory("CFDataCreateCopy", [0, empty])
        no_copy = self.factory("CFDataCreateWithBytesNoCopy", [0, raw, 8, self.constant("kCFAllocatorNull")])
        self.release(no_copy)
        for capacity in (0, 64):
            data = self.factory("CFDataCreateMutable", [0, capacity])
            self.mutate("CFDataAppendBytes", [data, raw, 8])
            self.mutate("CFDataReplaceBytes", [data, 2, 3, raw, 6])
            self.mutate("CFDataDeleteBytes", [data, 1, 2])
            self.mutate("CFDataSetLength", [data, 20])
            self.mutate("CFDataIncreaseLength", [data, -3])
            duplicate = self.factory("CFDataCreateMutableCopy", [0, capacity, data])
            self.release(duplicate)
            self.release(data)
        self.release(immutable)
        self.release(empty)
        # Distinct capacity paths include deque/CFStorage transitions and fixed
        # inline elements. Values are opaque integers with null callbacks.
        vector = self.raw(struct.pack(">5I", 9, 3, 9, 7, 1))
        for capacity in (0, 16):
            array = self.factory("CFArrayCreateMutable", [0, capacity, 0])
            self.mutate("CFArrayReplaceValues", [array, 0, 0, vector, 5])
            self.scalar("CFArrayGetCountOfValue", [array, 1, 4, 9], 1)
            self.scalar("CFArrayContainsValue", [array, 0, 5, 7], 1)
            self.scalar("CFArrayGetFirstIndexOfValue", [array, 1, 4, 9], 2)
            self.scalar("CFArrayGetLastIndexOfValue", [array, 0, 5, 9], 2)
            self.scalar("CFArrayGetLastIndexOfValue", [array, 1, 3, 99], -1)
            self.mutate("CFArrayAppendValue", [array, 42])
            self.mutate("CFArrayInsertValueAtIndex", [array, 1, 17])
            self.mutate("CFArraySetValueAtIndex", [array, 2, 16])
            self.mutate("CFArrayExchangeValuesAtIndices", [array, 0, 6])
            self.p.routines = [
                (
                    0x1F00,
                    [
                        0x7C032000,
                        0x41800010,
                        0x41810014,
                        0x38600000,
                        0x4E800020,
                        0x3860FFFF,
                        0x4E800020,
                        0x38600001,
                        0x4E800020,
                    ],
                )
            ]
            self.mutate("CFArraySortValues", [array, 0, 7, self.ptr(0x1F00), 0])
            self.mutate("CFArrayRemoveValueAtIndex", [array, 1])
            copy = self.factory("CFArrayCreateCopy", [0, array])
            mutable_copy = self.factory("CFArrayCreateMutableCopy", [0, capacity, copy])
            self.mutate("CFArrayAppendArray", [mutable_copy, copy, 1, 2])
            self.mutate("CFArrayRemoveAllValues", [mutable_copy])
            self.release(mutable_copy)
            self.release(copy)
            if not capacity:
                for index in range(260):
                    self.mutate("CFArrayAppendValue", [array, index])
                for index in range(145):
                    self.mutate("CFArrayRemoveValueAtIndex", [array, 0])
            self.release(array)
        self.call("CFSetCreate", [0, vector, 5, 0])
        self.p.nonzero("original shared set")
        self.p.emit(0x907E1E80)
        shared_set = self.slot(0x1E80)
        self.scalar("CFSetGetCount", [shared_set], 4)
        self.scalar("CFSetGetCountOfValue", [shared_set, 9], 1)
        self.scalar("CFSetContainsValue", [shared_set, 7], 1)
        self.scalar("CFSetContainsValue", [shared_set, 99], 0)
        self.scalar("CFSetGetValue", [shared_set, 7], 7)
        self.output("CFSetGetValues", [shared_set, ("output", 0x1C00)], 16)
        self.call("CFRelease", [shared_set])
        # Type callbacks exercise actual retain/release/equality descriptors,
        # alongside the opaque-value cases above.
        first = self.factory("CFStringCreateWithCString", [0, self.raw(b"array first\0"), 0])
        second = self.factory("CFStringCreateWithCString", [0, self.raw(b"array second\0"), 0])
        self.p.emit(0x807E0000 | first[1], 0x907E1E00, 0x807E0000 | second[1], 0x907E1E04)
        self.call("CFSetCreate", [0, self.ptr(0x1E00), 2, self.constant("kCFTypeSetCallBacks", False)])
        self.p.nonzero("shared set with real type callbacks")
        self.p.emit(0x907E1E80)
        self.scalar("CFSetGetCountOfValue", [shared_set, first], 1)
        self.scalar("CFSetContainsValue", [shared_set, second], 1)
        self.scalar("CFSetGetValue", [shared_set, first])
        self.output("CFSetGetValues", [shared_set, ("output", 0x1C00)], 8)
        self.call("CFRelease", [shared_set])
        equal_first = self.factory("CFStringCreateWithCString", [0, self.raw(b"array first\0"), 0])

        def retain_count(pair, expected):
            for side in (0, 1):
                self.call("CFGetRetainCount", [self.slot(pair[1] + side * 4)])
                self.p.expect(expected, "array callback leaves the expected CoreFoundation retain count")

        for capacity in (0, 8):
            typed = self.factory("CFArrayCreateMutable", [0, capacity, self.constant("kCFTypeArrayCallBacks", False)])
            for value in (first, second, first):
                self.mutate("CFArrayAppendValue", [typed, value])
            self.scalar("CFArrayGetCountOfValue", [typed, 0, 3, equal_first], 2)
            retain_count(first, 3)
            retain_count(second, 2)
            self.mutate("CFArraySetValueAtIndex", [typed, 1, first])
            retain_count(second, 1)
            copy = self.factory("CFArrayCreateCopy", [0, typed])
            retain_count(first, 7)
            self.mutate("CFArrayRemoveAllValues", [typed])
            retain_count(first, 4)
            self.release(copy)
            retain_count(first, 1)
            self.mutate("CFArrayAppendValue", [typed, second])
            for side in (0, 1):
                self.call("CFArrayGetCount", [self.slot(typed[1] + side * 4)])
                self.p.expect(1, "array accepts a new value after all old storage was released")
            self.release(typed)
            retain_count(second, 1)
        self.release(equal_first)
        self.release(second)
        self.release(first)

    def values(self):
        for name in ("CFBooleanGetTypeID", "CFDateGetTypeID", "CFNumberGetTypeID"):
            self.scalar(name)
        for name, expected in (("kCFBooleanTrue", 1), ("kCFBooleanFalse", 0)):
            self.scalar("CFBooleanGetValue", [self.constant(name)], expected)
        samples = [
            (1, b"\x80"),
            (2, struct.pack(">h", -32768)),
            (3, struct.pack(">i", -2147483647)),
            (4, struct.pack(">q", -0x123456789ABCDEF)),
            (4, struct.pack(">q", 0x7FFFFFFFFFFFFFFF)),
            (5, struct.pack(">f", 1.25)),
            (6, struct.pack(">d", -0.0)),
            (6, struct.pack(">Q", 0x7FF8000000000000)),
            (6, struct.pack(">d", float("inf"))),
            (7, b"\x7f"),
            (8, struct.pack(">h", 32767)),
            (9, struct.pack(">i", 1234567)),
            (10, struct.pack(">i", -17)),
            (11, struct.pack(">q", 0x100000001)),
            (12, struct.pack(">f", -1234.5)),
            (13, struct.pack(">d", 12345678.75)),
            (14, struct.pack(">i", 42)),
        ]
        previous = None
        for number_type, value in samples:
            number = self.factory("CFNumberCreate", [0, number_type, self.raw(value)], check=False)
            for name in ("CFNumberGetType", "CFNumberGetByteSize", "CFNumberIsFloatType"):
                self.scalar(name, [number])
            for target in range(1, 15):
                self.output(
                    "CFNumberGetValue",
                    [number, target, ("output", 0x1C00)],
                    (0, 1, 2, 4, 8, 4, 8, 1, 2, 4, 4, 8, 4, 8, 4)[target],
                    True,
                )
            self.scalar("CFNumberCompare", [number, number, 0])
            if previous:
                self.scalar("CFNumberCompare", [previous, number, 0])
                self.scalar("CFNumberCompare", [number, previous, 0])
                self.release(previous)
            previous = number
        if previous:
            self.release(previous)
        string = self.factory("CFStringCreateWithCString", [0, self.raw(b"value description\0"), 0])
        self.scalar("CFEqual", [self.slot(string[1]), self.slot(string[1] + 4)], 1)
        self.scalar("CFHash", [self.slot(string[1])])
        description = self.factory("CFCopyDescription", [self.slot(string[1])])
        self.release(description)
        self.release(string)

    def charsets(self):
        self.scalar("CFCharacterSetGetTypeID")
        for kind in range(1, 13):
            self.scalar("CFCharacterSetGetPredefined", [kind])
        # The original empty-bitmap inversion dereferences its null storage.
        # Validate the native empty -> full operation against a safe original
        # full-range constructor, rather than corrupting original low memory.
        self.call("CFCharacterSetCreateMutable", [0])
        self.p.nonzero("native empty character set")
        self.p.emit(0x907E1E00)
        self.call("CFCharacterSetInvert", [self.slot(0x1E00)])
        self.call("CFCharacterSetCreateWithCharactersInRange", [0, 0, 65536], 1)
        self.p.nonzero("original full-range character set")
        self.p.emit(0x907E1E04)
        self.equal_objects(0x1E00, "empty inversion agrees with a full Unicode BMP range")
        self.release(self.pair_slot(0x1E00))
        alphabet = self.factory("CFCharacterSetCreateWithCharactersInRange", [0, 65, 26])
        for character in (0, 64, 65, 90, 91, 65535):
            self.scalar("CFCharacterSetIsCharacterMember", [alphabet, character])
        chars = self.factory("CFStringCreateWithCharacters", [0, self.raw("AzéΩ".encode("utf-16-be")), 4])
        from_string = self.factory("CFCharacterSetCreateWithCharactersInString", [0, chars])
        bitmap = self.factory("CFCharacterSetCreateBitmapRepresentation", [0, from_string])
        from_bitmap = self.factory("CFCharacterSetCreateWithBitmapRepresentation", [0, bitmap])
        mutable = self.factory("CFCharacterSetCreateMutable", [0])
        self.mutate("CFCharacterSetAddCharactersInRange", [mutable, 0, 65536])
        self.mutate("CFCharacterSetRemoveCharactersInRange", [mutable, 66, 25])
        self.mutate("CFCharacterSetAddCharactersInString", [mutable, chars])
        self.mutate("CFCharacterSetRemoveCharactersInString", [mutable, chars])
        self.mutate("CFCharacterSetUnion", [mutable, alphabet])
        self.mutate("CFCharacterSetIntersect", [mutable, from_string])
        self.mutate("CFCharacterSetInvert", [mutable])
        copy = self.factory("CFCharacterSetCreateMutableCopy", [0, mutable])
        self.mutate("CFCharacterSetInvert", [copy])
        for value in (0, 65, 90, 0xE9, 0x3A9, 65535):
            self.scalar("CFCharacterSetIsCharacterMember", [copy, value])
        for pair in (copy, mutable, from_bitmap, bitmap, from_string, chars, alphabet):
            self.release(pair)

    def strings(self):
        self.scalar("CFStringGetTypeID")
        cstring = self.raw(b"Abc abc\0")
        pascal = self.raw(b"\x07Abc abc")
        utf16 = self.raw("Abc Ωé".encode("utf-16-be"))
        null_allocator = self.constant("kCFAllocatorNull")
        cases = [
            ("CFStringCreateWithCString", [0, cstring, 0]),
            ("CFStringCreateWithCStringNoCopy", [0, cstring, 0, null_allocator]),
            ("CFStringCreateWithPascalString", [0, pascal, 0]),
            ("CFStringCreateWithPascalStringNoCopy", [0, pascal, 0, null_allocator]),
            ("CFStringCreateWithCharacters", [0, utf16, 6]),
            ("CFStringCreateWithCharactersNoCopy", [0, utf16, 6, null_allocator]),
            ("CFStringCreateWithBytes", [0, self.raw(b"\xef\xbb\xbfabc"), 6, 0x08000100, 1]),
        ]
        for name, arguments in cases:
            string = self.factory(name, arguments)
            self.scalar("CFStringGetSmallestEncoding", [string])
            external = self.factory("CFStringCreateExternalRepresentation", [0, string, 0x08000100, 0])
            restored = self.factory("CFStringCreateFromExternalRepresentation", [0, external, 0x08000100])
            self.release(restored)
            self.release(external)
            self.release(string)
        source = self.factory("CFStringCreateWithCString", [0, cstring, 0])
        for location, length in ((0, 7), (0, 0), (1, 3)):
            substring = self.factory("CFStringCreateWithSubstring", [0, source, location, length])
            self.release(substring)
        for capacity in (0, 128):
            mutable = self.factory("CFStringCreateMutable", [0, capacity])
            self.mutate("CFStringAppend", [mutable, source])
            self.mutate("CFStringAppendCString", [mutable, cstring, 0])
            self.mutate("CFStringAppendPascalString", [mutable, pascal, 0])
            self.mutate("CFStringAppendCharacters", [mutable, utf16, 6])
            self.mutate("CFStringInsert", [mutable, 1, source])
            self.mutate("CFStringReplace", [mutable, 2, 3, source])
            self.mutate("CFStringDelete", [mutable, 1, 2])
            self.mutate("CFStringReplaceAll", [mutable, source])
            copy = self.factory("CFStringCreateMutableCopy", [0, capacity, mutable])
            self.mutate("CFStringPad", [copy, source, 23, 2])
            self.mutate("CFStringPad", [copy, source, 3, 0])
            for name in ("CFStringLowercase", "CFStringUppercase", "CFStringCapitalize"):
                self.mutate(name, [copy, 0])
            self.release(copy)
            self.release(mutable)
        unicode = self.factory(
            "CFStringCreateWithCharacters", [0, self.raw("éΩ Iı word's END".encode("utf-16-be")), 15]
        )
        for name in ("CFStringLowercase", "CFStringUppercase", "CFStringCapitalize"):
            converted = self.factory("CFStringCreateMutableCopy", [0, 0, unicode])
            self.mutate(name, [converted, 0])
            self.release(converted)
        self.release(unicode)
        # Separately owned external buffers prevent native and oracle mutations
        # from concealing each other's writes.
        for offset in (0x1A00, 0x1B00):
            self.p.data[offset : offset + 6] = "ext".encode("utf-16-be")
        for offset in (0x1A80, 0x1B80):
            self.p.data[offset : offset + 8] = "next".encode("utf-16-be")
        external = self.factory(
            "CFStringCreateMutableWithExternalCharactersNoCopy", [0, ("output", 0x1A00), 3, 32, null_allocator]
        )
        self.mutate("CFStringSetExternalCharactersNoCopy", [external, ("output", 0x1A80), 4, 32])
        self.release(external)
        pattern = self.factory("CFStringCreateWithCString", [0, self.raw(b"abc\0"), 0])
        for location, length in ((0, 7), (1, 6), (0, 0)):
            for options in (0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 32):
                self.output(
                    "CFStringFindWithOptions", [source, pattern, location, length, options, ("output", 0x1C00)], 8, True
                )
        results = self.factory("CFStringCreateArrayWithFindResults", [0, source, pattern, 0, 7, 1])
        self.release(results)
        parts = self.factory("CFStringCreateArrayBySeparatingStrings", [0, source, pattern])
        combined = self.factory("CFStringCreateByCombiningStrings", [0, parts, source])
        self.release(combined)
        self.release(parts)
        lines = self.factory(
            "CFStringCreateWithCharacters", [0, self.raw("a\r\nb\nc\rd\u2028e\u2029".encode("utf-16-be")), 11]
        )
        for location, length in ((0, 0), (1, 0), (2, 0), (3, 1), (0, 11), (11, 0)):
            self.output(
                "CFStringGetLineBounds",
                [lines, location, length, ("output", 0x1C00), ("output", 0x1C04), ("output", 0x1C08)],
                12,
            )
        self.release(lines)
        trim_source = self.factory("CFStringCreateWithCString", [0, self.raw(b"abcabcXabcabc\0"), 0])
        trimmed = self.factory("CFStringCreateMutableCopy", [0, 0, trim_source])
        self.mutate("CFStringTrim", [trimmed, pattern])
        self.release(trimmed)
        self.release(trim_source)
        whitespace = self.factory(
            "CFStringCreateWithCharacters", [0, self.raw(" \t\r\n\u2001word\u3000\n".encode("utf-16-be")), 11]
        )
        trimmed = self.factory("CFStringCreateMutableCopy", [0, 0, whitespace])
        self.mutate("CFStringTrimWhitespace", [trimmed])
        self.release(trimmed)
        self.release(whitespace)
        self.scalar("CFStringGetSystemEncoding")
        self.scalar("CFStringGetListOfAvailableEncodings")
        for encoding in (0, 0x600, 0x100, 0x08000100, 0xBFF, 0x201):
            self.scalar("CFStringGetNameOfEncoding", [encoding])
            self.scalar("CFStringConvertEncodingToIANACharSetName", [encoding])
        for text in (b"UTF-8", b"iso-8859-1", b"unicode", b"macintosh", b"invalid-encoding"):
            name = self.factory("CFStringCreateWithCString", [0, self.raw(text + b"\0"), 0])
            self.scalar("CFStringConvertIANACharSetNameToEncoding", [name])
            self.release(name)
        self.release(pattern)
        self.release(source)

    def numeric_data(self):
        p = self.p
        for library, name, high, low in NUMERIC_DATA:
            carbon_index = p.imports.index(("CarbonLib", name, 1))
            companion_index = p.imports.index((library, name, 1))
            carbon = p.import_base + 4 * carbon_index
            companion = p.import_base + 4 * companion_index
            p.emit(0x807E0000 | carbon, 0x809E0000 | companion)
            p.equal(name + " aliases one guest address across imports")
            p.emit(0x807E0000 | carbon)
            p.nonzero(name + " has relocated storage")
            for offset, value in ((0, high), (4, low)):
                p.emit(0x807E0000 | carbon, 0x80630000 | offset)
                p.expect(value, name + " big-endian word " + str(offset))
            if library == "CoreFoundationLib":
                _, original = self.constant(name, False)
                p.emit(0x807E0000 | carbon, 0x809E0000 | original, 0x7C632278)
                p.nonzero(name + " has separate native and saved fragment storage")
                for offset in (0, 4):
                    p.emit(0x807E0000 | carbon, 0x809E0000 | original, 0x80630000 | offset, 0x80840000 | offset)
                    p.equal(name + " matches the saved fragment word " + str(offset))

    def finish(self, destination):
        self.call("CloseConnection", [self.ptr(0x2100)])
        self.p.expect(0, "close original oracle")
        label = "Carbon" if self.library == "CarbonLib" else "Core"
        application = "PPC " + label + " Values Real Probe"
        self.p.finish(destination, application, carbon=True)
        self.write_coverage(destination, application, self.names)
        (destination / "native-symbols.json").write_text(json.dumps(sorted(self.used), indent=2) + "\n")


def make_probe(destination, library="CarbonLib"):
    destination.mkdir(parents=True, exist_ok=True)
    data_imports = [
        (alias, name, 1) for library_name, name, *_ in NUMERIC_DATA for alias in ("CarbonLib", library_name)
    ]
    probe = ValuesProbe(library, extra_names=("CFGetRetainCount",), data_imports=data_imports)
    probe.section("Numeric data exports", probe.numeric_data)
    probe.section("Arrays and data", probe.collections)
    probe.section("Numbers, booleans and scalar values", probe.values)
    probe.section("Character sets", probe.charsets)
    probe.section("Strings and encodings", probe.strings)
    missing = set(NAMES) - probe.used
    if missing:
        raise ValueError("cohort entries without a native scenario: " + ", ".join(sorted(missing)))
    probe.finish(destination)


def write_foreign_corefoundation(destination, names):
    # A foreign layout must fall back before interpreting the guest arguments.
    code, exports = [], []
    data = bytearray((len(names) + 2) * 8)
    relocations = []
    for index, name in enumerate(names + ["CohortFallbackToken"]):
        struct.pack_into(">II", data, index * 8, len(code) * 4, 0)
        code += [0x3C601234, 0x606356AB, 0x4E800020]
        exports.append((name, 2, index * 8, 1))
        relocations += [0xA000, index * 8, 0x4600]
    # An existing native date entry must forward this foreign profile's entire
    # opaque frame before interpreting its deliberately invalid-looking object.
    date_index = len(names) + 1
    struct.pack_into(">II", data, date_index * 8, len(code) * 4, 0)
    exports.append(("CFDateGetAbsoluteTime", 2, date_index * 8, 1))
    relocations += [0xA000, date_index * 8, 0x4600]
    code += [0x7C2C0B78, 0x9421FFC0]  # mr r12,r1; independent leaf frame.
    for index in range(17):
        if index < 8:
            register = index + 3
        else:
            register = 11
            code += [0x816C0000 | (24 + index * 4)]
        code += [0x2C000000 | register << 16 | (0x1100 + index), 0x41820008, 0]
    for index in range(13):
        high, low = struct.unpack(">II", struct.pack(">d", index + 0.25))
        code += [
            0xD8010010 | (index + 1) << 21,
            0x81610010,
            0x3C000000 | (high >> 16),
            0x60000000 | (high & 65535),
            0x7C0B0000,
            0x41820008,
            0,
            0x81610014,
            0x2C0B0000 | low,
            0x41820008,
            0,
        ]
    # A real FP return, plus a volatile GPR sentinel, makes a word-only callback
    # or zero-filled FPR bank observably wrong.
    code += [0xFF80210C, 0xFC206890, 0x38605678, 0x38210040, 0x4E800020]  # mtfsfi 7,2; fmr f1,f13.
    write_fragment(destination, "CoreFoundationLib", pef(code, data, exports=exports, reloc=relocations), 0)


def check_foreign_words(p, library, rows):
    for row in rows:
        for index in range(row["argument_count"]):
            register = index + 3
            p.emit(
                0x3C000000 | register << 21 | 0xFEDC, 0x60000000 | register << 21 | register << 16 | (0x1000 + index)
            )
        p.call(row["name"], library=library)
        if row["return_type"] == "uint32":
            p.expect(0x123456AB, row["name"] + " preserves the exact foreign return register")


def check_foreign_frame(p, library):
    input_base = (p.import_base + len(p.imports) * 4 + 7) & ~7
    p.data.extend(bytes(max(0, input_base + 13 * 8 - len(p.data))))
    for index in range(13):
        offset = input_base + index * 8
        struct.pack_into(">d", p.data, offset, index + 0.25)
        p.emit(0xC81E0000 | (index + 1) << 21 | offset)
    for index in range(17):
        if index < 8:
            p.emit(0x38000000 | (index + 3) << 21 | (0x1100 + index))
        else:
            p.emit(0x39600000 | (0x1100 + index), 0x91610000 | (24 + index * 4))
    p.call("CFDateGetAbsoluteTime", library=library)
    p.expect(0x5678, "foreign frame preserves seventeen GPR/stack arguments")
    p.emit(0xD8210010, 0x80610010)
    high, low = struct.unpack(">II", struct.pack(">d", 12.25))
    p.expect(high, "foreign frame preserves thirteen FP inputs and the FP result high word")
    p.emit(0x80610014)
    p.expect(low, "foreign frame preserves the FP result low word")
    p.emit(0xFC00048E, 0xD8010010, 0x80610014, 0x70630003)
    p.expect(2, "foreign frame preserves FPSCR result rounding mode")
    p.emit(0xFF80010C)  # Restore round-to-nearest for application cleanup.


def make_foreign_probe(destination, library="CarbonLib", rows=None, group="Values"):
    label = "Carbon" if library == "CarbonLib" else "Core"
    make_fallback_probe(
        destination, f"PPC {label} {group} Foreign Probe", [(library, group, ROWS if rows is None else rows)]
    )


def make_fallback_probe(destination, application, cases):
    destination.mkdir(parents=True, exist_ok=True)
    names = list(dict.fromkeys(row["name"] for _, _, rows in cases for row in rows))
    write_carbon_fallback(destination)
    write_foreign_corefoundation(destination, names)

    entries_by_library = {}
    for library, _, rows in cases:
        entries = entries_by_library.setdefault(library, {"CFDateGetAbsoluteTime"})
        entries.update(row["name"] for row in rows)
    imports = [("CarbonLib", "CarbonFallbackToken", 2), ("CoreFoundationLib", "CohortFallbackToken", 2)]
    imports += [(library, name, 2) for library, entries in entries_by_library.items() for name in sorted(entries)]
    p = Probe(imports)
    p.reserve_parameter_words(17)
    sections = []

    def section(label, check):
        start = len(p.checks)
        check()
        sections.append({"section": label, "checks": len(p.checks) - start})

    def check_setup():
        # Prove native execution in JIT mode even with tiny foreign return stubs.
        p.emit(0x38600000, 0x388003E8, 0x7C8903A6)
        p.emit(*([0x38630001] * 16))
        p.emit(0x4200FFC0)
        p.expect(16000, "engine warmup integer loop")
        p.call("CarbonFallbackToken")
        p.expect(73, "original Carbon fallback initializer")
        p.call("CohortFallbackToken")
        p.expect(0x123456AB, "foreign CoreFoundation fragment")

    section("Engine and fallback setup", check_setup)
    for library, group, rows in cases:
        section(f"{library} {group} word-argument fallback", lambda: check_foreign_words(p, library, rows))
    for library in entries_by_library:
        section(f"{library} full PPC frame fallback", lambda: check_foreign_frame(p, library))
    p.finish(destination, application)
    coverage = {
        "application": application,
        "synthetic_fallback": True,
        "assertions": len(p.checks),
        "sections": sections,
        "exercised_entries": sorted(set().union(*entries_by_library.values())),
        "entries_by_library": {library: sorted(entries) for library, entries in entries_by_library.items()},
        "pending_entries": [],
        "status": "generated_not_validated",
    }
    (destination / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\n", newline="\n")


def make_grouped_fallback_probe(destination):
    collection_rows = json.loads((ROOT / "carbonlib/plans/cf-collections-batch.json").read_text())["entries"]
    make_fallback_probe(
        destination,
        "PPC Carbon Fallback Checks",
        [
            (library, group, [row for row in rows if library != "CarbonLib" or not row.get("companion_only")])
            for library in ("CarbonLib", "CoreFoundationLib")
            for group, rows in (("Values", ROWS), ("Collections", collection_rows))
        ],
    )


def make_guard_probe(destination, mode):
    """Synthetic exact-layout workers exercise failure before unsafe writes."""
    destination.mkdir(parents=True, exist_ok=True)
    write_carbon_fallback(destination)
    code = [0] * (0x5DEE0 // 4)
    data = bytearray(0x72A8)
    name, offset = {
        "bad-data-span": ("CFDataSetLength", 0x1E68C),
        "bad-array-capacity": ("CFArrayReplaceValues", 0x1D4A0),
        "case-expansion": ("CFStringUppercase", 0x3E908),
    }[mode]
    routines = [(name, offset, [0x3860007B, 0x4E800020]), ("CohortGuardToken", 0x100, [0x38600049, 0x4E800020])]
    if mode == "case-expansion":
        routines += [
            ("CFStringGetLength", 0x38858, [0x8063000C, 0x4E800020]),
            ("GuardResize", 0x35918, [0x4E800020]),
            ("GuardMapping", 0x492E0, [0x38000053, 0xB0040000, 0xB0040002, 0x38600002, 0x4E800020]),
        ]
    exports, relocations = [], []
    for index, (symbol, entry, words) in enumerate(routines):
        code[entry // 4 : entry // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, entry, 0)
        exports.append((symbol, 2, index * 8, 1))
        relocations += [0xA000, index * 8, 0x4600]
    write_fragment(destination, "CoreFoundationLib", pef(code, data, exports=exports, reloc=relocations), 0)
    p = Probe([("CarbonLib", name, 2), ("CoreFoundationLib", "CohortGuardToken", 2)])
    p.call("CohortGuardToken")
    p.expect(73, "synthetic exact-layout guard fragment")
    if mode == "bad-data-span":
        struct.pack_into(">6I", p.data, 0x300, 0, 12, 1, 8, 0, 0xFFFFFFF0)
        p.emit(0x387E0300, 0x38800002)
    elif mode == "bad-array-capacity":
        struct.pack_into(">4I", p.data, 0x300, 0, 1, 1, 1)
        p.emit(0x387E0300, 0x38800000, 0x38A00000, 0x38DE0380, 0x38E00002)
    else:
        struct.pack_into(">I", p.data, 0x304, 0x31)
        struct.pack_into(">I", p.data, 0x30C, 1)
        struct.pack_into(">H", p.data, 0x380, 0xDF)
        p.emit(0x387E0380, 0x907E0308, 0x387E0300, 0x38800000)
    p.call(name)
    p.emit(0)  # A missing native diagnostic must fail the expected-message check.
    p.finish(destination, "PPC Carbon Values Guard Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        choices=("foreign", "bad-data-span", "bad-array-capacity", "case-expansion"),
        default="foreign",
    )
    variants = parser.add_mutually_exclusive_group()
    variants.add_argument("--real", action="store_true")
    variants.add_argument("--grouped-fallback", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    arguments = parser.parse_args()
    if arguments.mode != "foreign":
        if arguments.real or arguments.grouped_fallback:
            parser.error("guard modes require their own synthetic fixture")
        make_guard_probe(arguments.destination, arguments.mode)
    elif arguments.real:
        make_probe(arguments.destination, arguments.library)
    elif arguments.grouped_fallback:
        make_grouped_fallback_probe(arguments.destination)
    else:
        make_foreign_probe(arguments.destination, arguments.library)
