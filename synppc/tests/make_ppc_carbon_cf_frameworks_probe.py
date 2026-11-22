#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_frameworks_probe.py
#
#   Summary:   Build one combined framework regression application with named checks and coverage reporting.
#
#   Classes:   FrameworksProbe
#
#   Functions: make_probe, main
#
#   Origin:    Original generated PPC checks using local export metadata. No Apple instructions are embedded.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+
"""Build one combined framework regression application, requiring local real libraries.

The same application image runs under both CPU engines. Its checks.json names
individual assertions; coverage.json distinguishes exercised and pending entries.
All 106 native registrations are enabled after the complete cohort validation.
"""
import argparse
import json
import struct
import uuid
import plistlib
import re
from datetime import datetime

from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from pathlib import Path

from make_ppc_carbon_cf_values_probe import ValuesProbe

ROOT = Path(__file__).resolve().parents[2]
ROWS = json.loads((ROOT / "carbonlib/plans/cf-frameworks-batch.json").read_text())["entries"]
TIMEZONES = (
    "CFTimeZoneCopyAbbreviation",
    "CFTimeZoneCopyDefault",
    "CFTimeZoneCopySystem",
    "CFTimeZoneCreate",
    "CFTimeZoneCreateWithTimeIntervalFromGMT",
    "CFTimeZoneGetData",
    "CFTimeZoneGetName",
    "CFTimeZoneGetSecondsFromGMT",
    "CFTimeZoneGetTypeID",
    "CFTimeZoneIsDaylightSavingTime",
    "CFTimeZoneResetSystem",
    "CFTimeZoneSetDefault",
)
CF_SINGLETONS = (
    "kCFAllocatorDefault",
    "kCFAllocatorMalloc",
    "kCFAllocatorNull",
    "kCFAllocatorUseContext",
    "kCFBooleanTrue",
    "kCFBooleanFalse",
    "kCFNumberNaN",
    "kCFNumberNegativeInfinity",
    "kCFNumberPositiveInfinity",
)


def shared_keys():
    source = (ROOT / "carbonlib/src/cf_key_entries.h").read_text()
    values = {}
    links = {}
    for name, slot, text in re.findall(
        r'^CARBON_CF_KEY\("([^"]+)", 0x[0-9a-f]+, (0x[0-9a-f]+), "([^"]+)"\)', source, re.M
    ):
        values[slot] = (name, text)
    for name, source_slot in re.findall(
        r'^CARBON_CF_KEY_ALIAS\("([^"]+)", 0x[0-9a-f]+, 0x[0-9a-f]+, (0x[0-9a-f]+)\)', source, re.M
    ):
        links[name] = values[source_slot]
    if len(values) + len(links) != 60:
        raise ValueError("Incomplete CoreFoundation key table")
    return list(values.values()), links


class FrameworksProbe(ValuesProbe):
    def __init__(self, original_only=False):
        reference = ROOT / "references/disasm/extensions/Extensions/CarbonLib/pef/data-CoreFoundationLib/pef.json"
        exports = {entry["name"]: entry for entry in json.loads(reference.read_text())["exports"]}
        support = [
            "CFEqual",
            "CFRelease",
            "CFArrayGetCount",
            "CFArrayGetValueAtIndex",
            "CFStringCreateWithCString",
            "CFStringGetLength",
            "CFStringGetCharacters",
            "CFDataCreate",
            "CFDataGetLength",
            "CFDataGetBytePtr",
            "CFUUIDCreateFromString",
            "CFURLCreateWithFileSystemPath",
            "CFGetAllocator",
            "CFBundleCreate",
            "CFBundleGetIdentifier",
            "CFURLCopyFileSystemPath",
            "CFURLCopyAbsoluteURL",
            "CFAllocatorGetDefault",
            "CFAllocatorCreate",
            "CFAllocatorGetContext",
            "CFAllocatorAllocate",
            "CFAllocatorReallocate",
            "CFDictionaryCreateMutable",
            "CFDictionarySetValue",
            "CFGetRetainCount",
            "CFArrayCreateMutable",
            "CFArrayAppendValue",
            "CFStringCreateMutable",
            "CFUUIDGetUUIDBytes",
            "CFDateGetAbsoluteTime",
            "CFStringGetTypeID",
            "CFStringCreateWithCharacters",
            "CFStorageCreate",
            "CFStorageInsertValues",
            "CFAllocatorDeallocate",
            "CFCharacterSetCreateWithBitmapRepresentation",
            "CFCharacterSetCreateBitmapRepresentation",
            "CFCharacterSetIsCharacterMember",
            "CFDataCreateMutable",
            "CFDataSetLength",
            "CFDataGetMutableBytePtr",
        ]
        services = json.loads((ROOT / "carbonlib/plans/cf-services-batch.json").read_text())["entries"]
        self.service_names = [row["name"] for row in services]
        self.timezone_names = [] if original_only else list(TIMEZONES)
        self.keys, self.key_aliases = ([], {}) if original_only else shared_keys()
        self.singletons = () if original_only else CF_SINGLETONS
        data_imports = [(library, "kCFAllocatorSystemDefault", 1) for library in ("CarbonLib", "CoreFoundationLib")]
        data_imports.extend(
            (library, name, 1)
            for name in [name for name, _ in self.keys] + list(self.key_aliases) + list(self.singletons)
            for library in ("CarbonLib", "CoreFoundationLib")
        )
        super().__init__(
            "CoreFoundationLib",
            rows=ROWS + [exports[name] for name in support + self.service_names + self.timezone_names],
            extra_names=("CFGetTypeID", "CFBooleanGetValue", "CFNumberGetValue"),
            data_imports=data_imports,
        )
        self.p.imports.append(("StdCLib", "qsort", 2))
        self.p.imports.extend(("StdCLib", name, 2) for name in ("__setjmp", "longjmp"))
        self.p.reserve_parameter_words(17)
        self.p.routines = [
            (0x1F00, [0x3C601234, 0x60635678, 0x4E800020]),
            (0x1F08, [0x3CC08765, 0x60C64321, 0x90C50000, 0x38600001, 0x4E800020]),
            (0x1F10, [0x4E800020]),
            (0x1F20, [0x80C50000, 0x38C60001, 0x90C50000, 0x38601234, 0x4E800020]),
            (0x1F28, [0x4E800020]),
            (0x1F30, [0x4E800020]),
            # Count sort comparisons through the supplied context and compare signed keys.
            (0x1F40, [0x80C50000, 0x38C60001, 0x90C50000, 0x80630000, 0x80840000, 0x7C641850, 0x4E800020]),
            (0x1F48, [0x80630000, 0x80840000, 0x7C641850, 0x4E800020]),
            # Record both operand addresses via the context cursor, then compare the integers.
            (
                0x1F38,
                [
                    0x80C50000,
                    0x90660000,
                    0x90860004,
                    0x38C60008,
                    0x90C50000,
                    0x80630000,
                    0x80840000,
                    0x7C641850,
                    0x4E800020,
                ],
            ),
        ]

    def text(self, value):
        return self.factory("CFStringCreateWithCString", [0, self.raw(value.encode() + b"\0"), 0x600])

    def hfs_paths(self):
        queries = (
            "_CFIsAbsolutePath",
            "_CFStartOfLastPathComponent",
            "_CFLengthAfterDeletingLastPathComponent",
            "_CFStartOfPathExtension",
            "_CFLengthAfterDeletingPathExtension",
        )
        for path, results in (
            ("", (0, 0, 0, 0, 0)),
            (":", (0, 0, 0, 0, 1)),
            ("Vol:dir:file.ext", (1, 8, 7, 12, 12)),
            (":.name", (0, 1, 1, 0, 6)),
            ("Vol:file:", (1, 4, 3, 0, 9)),
        ):
            pointer = self.raw(path.encode("utf-16-be") + bytes(2))
            for name, result in zip(queries, results):
                self.scalar(name, [pointer, len(path)], result)

        for operation, path, extra, capacity, result, final in (
            ("StripTrailingPathSlashes", "Vol:::", "", 16, 1, "Vol"),
            ("StripTrailingPathSlashes", ":", "", 16, 0, ":"),
            ("TransmutePathSlashes", "A:::B:", "/", 16, 1, "A/B/"),
            ("TransmutePathSlashes", "A:B", "/", 16, 0, "A/B"),
            ("AppendPathComponent", "Vol", "子", 5, 1, "Vol:子"),
            ("AppendPathComponent", ":", "A", 3, 1, ":A"),
            ("AppendPathComponent", "Vol", "AB", 5, 0, "Vol"),
            ("AppendPathExtension", "Vol:A:", "x", 16, 1, "Vol:A.x"),
            ("AppendPathExtension", "~name:", "", 16, 0, "~name"),
            ("AppendPathExtension", "Vol:A:", ":x", 16, 0, "Vol:A:"),
        ):
            name = "_CF" + operation
            for side in (0, 1):
                initial = path.encode("utf-16-be").ljust(32, b"\xa5")
                pointer = self.raw(initial)
                length = self.raw(struct.pack(">I", len(path)))
                args = [pointer, length]
                if operation.startswith("Append"):
                    args += [capacity, self.raw(extra.encode("utf-16-be") + bytes(2)), len(extra)]
                elif operation.startswith("Transmute"):
                    args += [ord(extra)]
                self.call(name, args, side)
                self.p.expect(result, name + " return flag")
                self.p.emit(0x807E0000 | length[1])
                self.p.expect(len(final), name + " updated length")
                for index, character in enumerate(final):
                    self.p.emit(0xA07E0000 | (pointer[1] + 2 * index))
                    self.p.expect(ord(character), name + " output character")
                self.p.emit(0xA07E0000 | (pointer[1] + 30))
                self.p.expect(0xA5A5, name + " preserves buffer guard")

    def binary_search(self):
        base = self.raw(struct.pack(">7I", 2, 4, 4, 8, 10, 12, 14))
        # Negative indices identify the comparison with the key as the first operand.
        for count, target, insertion, comparisons in (
            (0, 4, 0, ()),
            (3, 4, 1, (2, 1, 0)),
            (4, 8, 3, (3, -1, 3, 1, 2)),
            (7, 1, 0, (6, -1)),
            (7, 4, 1, (6, -1, 3, 1, 0)),
            (7, 11, 5, (6, -1, 3, 5, 4)),
            (7, 15, 7, (6,)),
        ):
            key = self.raw(struct.pack(">I", target))
            for side in (0, 1):
                self.p.emit(0x387E1C04, 0x907E1C00)
                self.call("CFBSearch", [key, 4, base, count, self.ptr(0x1F38), self.ptr(0x1C00)], side)
                self.p.expect(insertion, "binary search insertion index")
                self.p.emit(0x807E1C00)
                self.p.pointer(0x1C04 + 8 * len(comparisons), "binary search comparison count")
                for i, index in enumerate(comparisons):
                    operands = (key[1], base[1]) if index < 0 else (base[1] + 4 * index, key[1])
                    for j, operand in enumerate(operands):
                        self.p.emit(0x807E0000 | (0x1C04 + i * 8 + j * 4))
                        self.p.pointer(operand, "binary search comparator operand address")

    def corefoundation_singletons(self):
        p = self.p
        _, scratch = self.raw(bytes(16))
        for name in self.singletons:
            carbon, companion = (
                p.import_base + 4 * p.imports.index((library, name, 1))
                for library in ("CarbonLib", "CoreFoundationLib")
            )
            p.emit(0x807E0000 | carbon, 0x809E0000 | companion)
            p.equal(name + " shares one guest slot")
            p.emit(0x807E0000 | carbon, 0x80630000, 0x907E0000 | scratch)
            if name == "kCFAllocatorDefault":
                p.expect(0, "the default allocator sentinel is null")
            elif name == "kCFAllocatorUseContext":
                p.expect(0x221, "the use-context sentinel preserves its classic value")
            elif name in ("kCFAllocatorNull", "kCFAllocatorMalloc"):
                self.call("CFAllocatorAllocate", [self.slot(scratch), 32, 0])
                if name == "kCFAllocatorNull":
                    p.expect(0, "the null allocator returns a null allocation")
                else:
                    p.nonzero("the malloc allocator provides guest storage")
                    p.emit(0x907E0000 | (scratch + 4))
                    self.call("CFAllocatorDeallocate", [self.slot(scratch), self.slot(scratch + 4)])
            elif name.startswith("kCFBoolean"):
                self.call("CFBooleanGetValue", [self.slot(scratch)])
                p.expect(int(name.endswith("True")), name + " preserves the boolean singleton value")
            else:
                self.call("CFNumberGetValue", [self.slot(scratch), 6, self.ptr(scratch + 8)])
                p.expect(1, "read the nonfinite number as a Float64")
                high = {
                    "kCFNumberNaN": 0x7FF80000,
                    "kCFNumberNegativeInfinity": 0xFFF00000,
                    "kCFNumberPositiveInfinity": 0x7FF00000,
                }[name]
                p.emit(0x807E0000 | (scratch + 8))
                p.expect(high, name + " preserves the high Float64 word")
                p.emit(0x807E0000 | (scratch + 12))
                p.expect(0, name + " preserves the low Float64 word")

    def corefoundation_keys(self):
        p = self.p
        _, scratch = self.raw(bytes(8))

        def import_offset(library, name):
            return p.import_base + 4 * p.imports.index((library, name, 1))

        def load(name, library="CarbonLib"):
            p.emit(0x807E0000 | import_offset(library, name), 0x80630000)

        for name, text in self.keys:
            carbon = import_offset("CarbonLib", name)
            companion = import_offset("CoreFoundationLib", name)
            p.emit(0x807E0000 | carbon, 0x809E0000 | companion)
            p.equal(name + " has one shared guest address")
            load(name)
            p.nonzero(name + " points to an initialized CFString")
            p.emit(0x907E0000 | scratch)
            position = len(p.data)
            p.data.extend(text.encode("ascii") + b"\0")
            self.call("CFStringCreateWithCString", [0, self.ptr(position), 0x600])
            p.emit(0x907E0000 | (scratch + 4))
            self.call("CFEqual", [self.slot(scratch), self.slot(scratch + 4)])
            p.expect(1, name + " has the expected key text")
            self.call("CFRelease", [self.slot(scratch + 4)])
        for name, (original, _) in self.key_aliases.items():
            carbon = import_offset("CarbonLib", name)
            companion = import_offset("CoreFoundationLib", name)
            p.emit(0x807E0000 | carbon, 0x809E0000 | companion)
            p.equal(name + " has one shared guest address")
            load(name)
            p.emit(0x907E0000 | scratch)
            load(original, "CoreFoundationLib")
            p.emit(0x809E0000 | scratch)
            p.equal(name + " preserves the legacy key's object identity")

    def timezones(self):
        p, ptr, slot = self.p, self.ptr, self.slot
        _, base = self.raw(bytes(56))
        self.cursor = (self.cursor + 7) & ~7
        _, number = self.raw(bytes(8))

        def load_double(value):
            self.arguments(struct.unpack(">II", struct.pack(">d", value)), 0)
            p.emit(0x907E0000 | number, 0x909E0000 | (number + 4), 0xC83E0000 | number)

        def expect_double(value, label):
            p.emit(0xD83E0000 | number)
            for index, word in enumerate(struct.unpack(">II", struct.pack(">d", value))):
                p.emit(0x807E0000 | (number + index * 4))
                p.expect(word, label)

        def save(offset, label):
            p.nonzero(label)
            p.emit(0x907E0000 | (base + offset))
            return slot(base + offset)

        load_double(3661)
        self.call("CFTimeZoneCreateWithTimeIntervalFromGMT", [0])
        fixed = save(0, "create a fixed-offset CFTimeZone")
        self.call("CFTimeZoneGetTypeID")
        type_id = save(4, "register the time-zone class in the real CF runtime")
        self.call("CFGetTypeID", [fixed])
        p.emit(0x809E0000 | type_id[1])
        p.equal("fixed time zone carries the registered CF type ID")
        load_double(-0.25)
        self.call("CFTimeZoneGetSecondsFromGMT", [fixed])
        expect_double(3660, "fixed offset rounds to minutes")
        self.call("CFTimeZoneIsDaylightSavingTime", [fixed])
        p.expect(0, "fixed zones never use daylight saving")
        self.call("CFTimeZoneGetName", [fixed])
        name = save(8, "fixed time zone exposes a borrowed name")
        self.call("CFTimeZoneGetData", [fixed])
        data = save(12, "fixed time zone exposes a borrowed TZif payload")
        self.call("CFTimeZoneCreate", [0, name, data])
        equivalent = save(16, "construct a second zone from the copied payload")
        self.call("CFEqual", [fixed, equivalent], 1)
        p.expect(1, "saved CoreFoundation can call the native equality callback")
        self.call("CFRelease", [equivalent], 1)
        self.call("CFTimeZoneSetDefault", [fixed])
        self.call("CFTimeZoneCopyDefault")
        p.emit(0x809E0000 | fixed[1])
        p.equal("copy the explicitly selected default")
        self.call("CFRelease", [fixed])
        self.call("CFTimeZoneSetDefault", [0])
        self.call("CFTimeZoneCopySystem")
        system = save(20, "construct a system zone from machine location")
        self.call("CFTimeZoneCopyDefault")
        p.emit(0x809E0000 | system[1])
        p.equal("default falls back to the cached system zone")
        self.call("CFRelease", [system])
        self.call("CFTimeZoneResetSystem")
        self.call("CFTimeZoneCopySystem")
        refreshed = save(24, "rebuild a system zone after reset")
        p.emit(0x809E0000 | system[1], 0x7C632278)
        p.nonzero("reset releases the cache while existing callers keep their zone")
        self.call("CFRelease", [system])
        self.call("CFRelease", [refreshed])
        self.call("CFTimeZoneResetSystem")

        payload = (
            struct.pack(">4s16x6I", b"TZif", 0, 0, 0, 2, 2, 8)
            + struct.pack(">2IB", 978307200, 978307260, 1)
            + bytes(1)
            + struct.pack(">iBBiBB", -18000, 0, 0, -14400, 1, 4)
            + b"EST\0EDT\0"
        )
        self.call("CFDataCreate", [0, self.raw(payload), len(payload)])
        transitions = save(28, "create an explicit DST transition table")
        self.call("CFTimeZoneCreate", [0, name, transitions])
        zoned = save(32, "construct a transition-based time zone")
        self.call("CFRelease", [transitions])
        for instant, hours, daylight, abbreviation in ((-0.5, -5, 0, b"EST"), (0, -4, 1, b"EDT"), (60, -5, 0, b"EST")):
            load_double(instant)
            self.call("CFTimeZoneGetSecondsFromGMT", [zoned])
            expect_double(hours * 3600, "lookup honors the exact transition instant")
            load_double(instant)
            self.call("CFTimeZoneIsDaylightSavingTime", [zoned])
            p.expect(daylight, "daylight saving comes from the selected time type")
            load_double(instant)
            self.call("CFTimeZoneCopyAbbreviation", [zoned])
            actual = save(36, "copy the selected time type's abbreviation")
            self.call("CFStringCreateWithCString", [0, self.raw(abbreviation + b"\0"), 0x600])
            expected = save(40, "make the expected abbreviation")
            self.call("CFEqual", [actual, expected])
            p.expect(1, "abbreviation matches the transition type")
            self.call("CFRelease", [expected])
            self.call("CFRelease", [actual])
        self.call("CFRelease", [zoned], 1)
        self.call("CFRelease", [fixed], 1)
        self.call("CFDataCreate", [0, self.raw(payload[:-1]), len(payload) - 1])
        truncated = save(44, "create a truncated transition table")
        self.call("CFStringCreateWithCString", [0, self.raw(b"Invalid\0"), 0x600])
        invalid_name = save(48, "make a name for malformed data")
        self.call("CFTimeZoneCreate", [0, invalid_name, truncated])
        p.expect(0, "truncated TZif is rejected")
        self.call("CFRelease", [invalid_name])
        self.call("CFRelease", [truncated])
        load_double(64801)
        self.call("CFTimeZoneCreateWithTimeIntervalFromGMT", [0])
        p.expect(0, "fixed offsets outside 18 hours are rejected")

    def calendar(self):
        def load_double(register, value):
            self.cursor = (self.cursor + 7) & ~7
            offset = self.raw(struct.pack(">d", value))[1]
            self.p.emit(0xC81E0000 | register << 21 | offset)

        def expect_words(offset, words, label):
            for index, word in enumerate(words):
                self.p.emit(0x807E0000 | (offset + index * 4))
                self.p.expect(word, f"{label} word {index}")

        def expect_double(value, label):
            self.p.emit(0xD83E1B80)
            expect_words(0x1B80, struct.unpack(">II", struct.pack(">d", value)), label)

        epoch = datetime(2001, 1, 1)
        for instant, week in (
            (datetime(2000, 2, 29, 12, 45, 1, 250000), 9),
            (datetime(2000, 12, 31, 23, 59, 59, 750000), 52),
            (epoch, 1),
            (datetime(2006, 1, 1), 53),
            (datetime(2007, 12, 31), 1),
        ):
            absolute = (instant - epoch).total_seconds()
            packed = struct.pack(
                ">i4Bd",
                instant.year,
                instant.month,
                instant.day,
                instant.hour,
                instant.minute,
                instant.second + instant.microsecond / 1e6,
            )
            date_words = struct.unpack(">4I", packed)
            for side in (0, 1):
                self.call("CFGregorianDateIsValid", [*date_words, 0], side)
                self.p.expect(1, "validate packed Gregorian date")
                self.call("CFGregorianDateGetAbsoluteTime", [*date_words, 0], side)
                expect_double(absolute, "Gregorian date converted to epoch seconds")
                load_double(1, absolute)
                self.call("CFAbsoluteTimeGetGregorianDate", [self.ptr(0x1B80), 0, 0, 0], side)
                # The reference computes the seconds remainder with fnmsub.
                # Exact cancellation under round-to-nearest produces negative zero.
                returned = (*date_words[:2], 0x80000000, 0) if date_words[2:] == (0, 0) else date_words
                expect_words(0x1B80, returned, "Gregorian date returned through hidden pointer")
                for name, result in (
                    ("DayOfWeek", instant.isoweekday()),
                    ("DayOfYear", instant.timetuple().tm_yday),
                    ("WeekOfYear", week),
                ):
                    load_double(1, absolute)
                    self.call("CFAbsoluteTimeGet" + name, [0, 0, 0], side)
                    self.p.expect(result, name + " " + instant.isoformat())

        for side in (0, 1):
            # One month from January 31 clamps to February 28 before adding a day.
            load_double(1, (datetime(2001, 1, 31) - epoch).total_seconds())
            seconds = struct.unpack(">II", struct.pack(">d", 0.75))
            self.call("CFAbsoluteTimeAddGregorianUnits", [0, 0, 0, 0, 1, 1, 2, 3, *seconds], side)
            expect_double((datetime(2001, 3, 1, 2, 3) - epoch).total_seconds() + 0.75, "month-end addition")
            for direction in (-1, 1):
                load_double(1, direction * 90061.25)
                load_double(2, 0)
                self.call("CFAbsoluteTimeGetDifferenceAsGregorianUnits", [self.ptr(0x1B80), 0, 0, 0, 0, 0, 60], side)
                expected = struct.pack(">5id", 0, 0, direction, direction, direction, direction * 1.25)
                expect_words(0x1B80, struct.unpack(">7I", expected), "signed Gregorian units")

    def string_services(self):
        fmt = self.text("%d/%d/%d/%d/%d/%d/%d")
        expected = self.text("1/-2/3/4/5/6/7")
        arguments = [1, -2, 3, 4, 5, 6, 7]
        vector = self.raw(struct.pack(">7i", *arguments))
        for name in (
            "CFStringCreateWithFormat",
            "CFStringCreateWithFormatAndArguments",
            "_CFStringCreateWithFormatAndArgumentsAux",
            "CFStringAppendFormat",
            "CFStringAppendFormatAndArguments",
        ):
            append = name.startswith("CFStringAppend")
            string = self.factory("CFStringCreateMutable", [0, 0]) if append else None
            for side in (0, 1):
                args = [string if append else 0, 0, fmt]
                if name.endswith("Aux"):
                    args.insert(1, 0)
                if name.endswith("Format"):
                    # Five variadic words fit r6..r10. The final two are on the stack.
                    args += arguments
                else:
                    args.append(vector)
                self.call(name, args, side)
                if append:
                    value = string
                else:
                    self.p.nonzero(name + " returned an owned string")
                    self.p.emit(0x907E1B00)
                    value = self.slot(0x1B00)
                self.call("CFEqual", [value, expected], side)
                self.p.expect(1, name + " formatted all seven signed arguments")
                if not append:
                    self.call("CFRelease", [value])
            if append:
                self.release(string)
        self.release(expected)
        self.release(fmt)

        for source, value in ((" -12.5", -12.5), ("0.25", 0.25), ("xyz", 0.0)):
            text = self.text(source)
            for side in (0, 1):
                self.call("CFStringGetDoubleValue", [text], side)
                self.p.emit(0xD83E1B80)
                for i, word in enumerate(struct.unpack(">II", struct.pack(">d", value))):
                    self.p.emit(0x807E0000 | (0x1B80 + 4 * i))
                    self.p.expect(word, "parse decimal " + source)
            self.release(text)
        for source, valid in (("a%20b", 1), ("a%2", 0), ("a b", 0), ("a ", 1), ("a#b#", 1), ("a#b#c", 0)):
            text = self.text(source)
            self.scalar("_CFStringIsLegalURLString", [text], valid)
            self.release(text)
        storage = self.raw(b"Test")
        allocator = self.constant("kCFAllocatorNull")
        string = self.factory("_CFStringCreateWithBytesNoCopy", [0, storage, 4, 0x600, 0, allocator])
        for side in (0, 1):
            out = self.raw(b"\xa5" * 8)
            self.call("_CFStringGetFileSystemRepresentation", [string, out, 5], side)
            self.p.expect(1, "filesystem encoding fits with terminator")
            self.p.emit(0x807E0000 | out[1])
            self.p.expect(0x54657374, "filesystem representation bytes")
            self.p.emit(0x807E0000 | (out[1] + 4))
            self.p.expect(0x00A5A5A5, "filesystem representation terminator and guard")
        self.release(string)

    def dates_and_uuids(self):
        for bits in (0x8000000000000000, 0x7FF8000000001234, 0xC04F100000000000):
            self.cursor = (self.cursor + 7) & ~7
            value = self.raw(struct.pack(">Q", bits))
            for side in (0, 1):
                self.p.emit(0xC83E0000 | value[1])
                self.call("CFDateCreate", [0], side)
                self.p.nonzero("CFDateCreate returns an owned date")
                self.p.emit(0x907E1B00)
                self.call("CFDateGetAbsoluteTime", [self.slot(0x1B00)], side)
                self.p.emit(0xD83E1B80)
                for i, word in enumerate(struct.unpack(">II", struct.pack(">Q", bits))):
                    self.p.emit(0x807E0000 | (0x1B80 + 4 * i))
                    self.p.expect(word, "CFDate preserves absolute-time bits")
                self.call("CFRelease", [self.slot(0x1B00)])
        for constant in (False, True):
            name = "CFUUIDGetConstantUUIDWithBytes" if constant else "CFUUIDCreateWithBytes"
            octets = bytes((i * 19 + constant) & 255 for i in range(16))
            arguments = [0] + [0xA5FF7E00 | byte for byte in octets]
            pair = self.factory(name, arguments)
            for side in (0, 1):
                self.call("CFUUIDGetUUIDBytes", [self.ptr(0x1B80), pair], side)
                for i, word in enumerate(struct.unpack(">4I", octets)):
                    self.p.emit(0x807E0000 | (0x1B80 + 4 * i))
                    self.p.expect(word, "CFUUID byte order across register and stack arguments")
                self.call(name, arguments, side)
                self.p.emit(0x809E0000 | (pair[1] + 4 * side))
                self.p.equal("UUID interning preserves identity")
                if not constant:
                    self.call("CFRelease", [self.slot(pair[1] + 4 * side)])
            if not constant:
                self.release(pair)

    def service_records(self):
        self.scalar("_CFArgv")
        self.scalar("_CFEnviron")
        self.scalar("_CFGetEnvironmentVariable", [self.raw(b"ROMAW_MISSING_CF_FIXTURE\0")], 0)
        key = self.raw(b"ROMAW_CF_FIXTURE\0")
        entry = self.raw(b"ROMAW_CF_FIXTURE=matched\0")
        self.call("_CFEnviron", [], 1)
        # Temporarily replace the first vector slot and restore it after both lookups.
        self.p.emit(0x907E1B00, 0x80830000, 0x909E1B04, 0x389E0000 | entry[1], 0x90830000)
        for side in (0, 1):
            self.call("_CFGetEnvironmentVariable", [key], side)
            self.p.pointer(entry[1] + len(b"ROMAW_CF_FIXTURE="), "lookup borrows bytes after the equals sign")
        self.p.emit(0x807E1B00, 0x809E1B04, 0x90830000)
        self.scalar("_CFTemporaryMemoryAllocator")
        self.scalar("CFStringFileSystemEncoding")
        self.scalar("__CFStringComputeEightBitStringEncoding")
        for side in (0, 1):
            self.call("__CFRangeMake", [self.ptr(0x1B80), -7, 0x12345678], side)
            for i, word in enumerate((-7, 0x12345678)):
                self.p.emit(0x807E0000 | (0x1B80 + 4 * i))
                self.p.expect(word, "range fields in caller's result buffer")
        name = "_CFStringEncodingSetForceASCIICompatibility"
        desc = 0x2400 + 4 * self.names.index(name)
        # Read and restore the setting through the original function descriptor's TOC.
        self.p.emit(0x807E0000 | desc, 0x80630004, 0x907E1B00, 0x806309A8, 0x907E1B04)
        for side in (0, 1):
            for argument, expected in ((0x100, 0), (0xFF, 1)):
                self.call(name, [argument], side)
                self.p.emit(0x807E1B00, 0x806309A8)
                self.p.expect(expected, "ASCII compatibility reads the low Boolean byte")
        self.call(name, [self.slot(0x1B04)], 1)
        self.call("CFStringGetTypeID")
        self.p.emit(0x907E1B00)
        description = self.factory("CFCopyTypeIDDescription", [self.slot(0x1B00)])
        self.release(description)
        source = self.text("replacement")
        self.scalar("__CFStringIsEightBit", [source], 1)
        wide = self.factory("CFStringCreateWithCharacters", [0, self.raw(struct.pack(">H", 0x5B50)), 1])
        self.scalar("__CFStringIsEightBit", [wide], 0)
        self.release(wide)
        self.scalar("__CFStringIsMutable", [source], 0)
        self.scalar("__CFStringCheckMutableAndReplace", [source, 0, 1, source], 0)
        mutable = self.factory("CFStringCreateMutable", [0, 0])
        self.scalar("__CFStringIsMutable", [mutable], 1)
        self.scalar("__CFStringCheckMutableAndReplace", [mutable, 0, 0, source], 1)
        for side in (0, 1):
            self.call("CFEqual", [source, mutable], side)
            self.p.expect(1, "mutable replacement inserts source characters")
            self.call("_CFCopyExtensionForAbstractType", [source], side)
            self.p.emit(0x809E0000 | (source[1] + 4 * side))
            self.p.equal("abstract type copy returns its input")
            self.call("CFRelease", [self.slot(source[1] + 4 * side)])
        self.release(mutable)
        self.release(source)
        storage = self.factory("CFStorageCreate", [0, 4], check=False)
        self.scalar("__CFStorageGetValueSize", [storage], 4)
        self.scalar("__CFStorageGetCapacity", [storage], 0)
        for side in (0, 1):
            self.call("CFStorageInsertValues", [storage, 0, 50000], side)
        self.scalar("__CFStorageGetCapacity", [storage])
        self.release(storage)

    def xml_nodes(self):
        self.scalar("CFXMLNodeGetTypeID")
        text = self.text("framework node text")
        info = self.raw(bytes(32))
        for kind in range(1, 16):
            node = self.factory("CFXMLNodeCreate", [0, kind, text, info, 1])
            self.scalar("CFXMLNodeGetTypeCode", [node], kind)
            self.scalar("CFXMLNodeGetVersion", [node], 1)
            copy = self.factory("CFXMLNodeCreateCopy", [0, node])
            for side in (0, 1):
                self.call("CFXMLNodeGetInfoPtr", [copy], side)
                if kind in (1, 2, 4, 9, 10, 11, 13, 14, 15):
                    self.p.nonzero("XML additional info is stored inline for type " + str(kind))
                else:
                    self.p.expect(0, "XML type has no additional-info record")
                self.call("CFXMLNodeGetString", [copy], side)
                self.p.emit(0x907E1B00)
                self.call("CFEqual", [self.slot(0x1B00), text], side)
                self.p.expect(1, "XML node owns an equal copy of its text")
            if kind == 6:
                tree = self.factory("CFXMLTreeCreateWithNode", [0, node], check=False)
                for side in (0, 1):
                    self.call("CFXMLTreeGetNode", [tree], side)
                    self.p.emit(0x907E1B00)
                    self.call("CFEqual", [self.slot(0x1B00), node], side)
                    self.p.expect(1, "tree context retains its XML node")
                data = self.factory("CFXMLTreeCreateXMLData", [0, tree])
                self.release(data)
                self.release(tree)
            self.release(copy)
            self.release(node)
        self.release(text)

    def xml_parsers(self):
        self.scalar("CFXMLParserGetTypeID")
        xml = b'<a x="v">text<b/></a>\n'
        data = self.factory("CFDataCreate", [0, self.raw(xml), len(xml)])
        self.p.emit(0x387E1F20, 0x907E1A04, 0x387E1F28, 0x907E1A08, 0x387E1F30, 0x907E1A0C, 0x387E1A40, 0x907E1A24)
        callbacks, context = self.ptr(0x1A00), self.ptr(0x1A20)
        parser = self.factory("CFXMLParserCreate", [0, data, 0, 0, 1, callbacks, context], check=False)
        self.scalar("CFXMLParserGetSourceURL", [parser], 0)
        self.scalar("CFXMLParserGetStatusCode", [parser], -2)
        self.scalar("CFXMLParserGetDocument", [parser], 0)
        for name, size in (("CFXMLParserGetContext", 20), ("CFXMLParserGetCallBacks", 24)):
            self.output(name, [parser, ("output", 0x1C00)], size)
        for side in (0, 1):
            self.p.emit(0x38600000, 0x907E1A40)
            self.call("CFXMLParserParse", [parser], side)
            self.p.expect(1, "XML parser completes through client callbacks")
            self.p.emit(0x807E1A40)
            self.p.expect(5, "XML create callback receives document, elements, text and final whitespace")
        for name in ("CFXMLParserGetLocation", "CFXMLParserGetLineNumber", "CFXMLParserGetStatusCode"):
            self.scalar(name, [parser])
        self.scalar("CFXMLParserGetDocument", [parser], 0x1234)
        description = self.text("client requested parser abort")
        for side in (0, 1):
            self.call("CFXMLParserAbort", [parser, 123, description], side)
        self.scalar("CFXMLParserGetStatusCode", [parser], 123)
        error = self.factory("CFXMLParserCopyErrorDescription", [parser])
        self.scalar("CFXMLParserParse", [parser], 0)
        self.release(error)
        self.release(description)
        self.release(parser)
        path = self.fixture_path.resolve().as_posix()
        if path.startswith("/mnt/") and len(path) > 6 and path[6] == "/":
            path = path[5].upper() + ":" + path[6:]
        if len(path) > 2 and path[1:3] == ":/":
            path = path[0] + ":" + path[3:].replace("/", ":")
        path_text = self.text(path)
        url = self.factory("CFURLCreateWithFileSystemPath", [0, path_text, 1, 0])
        parser = self.factory("CFXMLParserCreateWithDataFromURL", [0, url, 0, 1, callbacks, context], check=False)
        self.scalar("CFXMLParserParse", [parser], 1)
        self.scalar("CFXMLParserGetDocument", [parser], 0x1234)
        self.release(parser)
        for name, arguments in (
            ("CFXMLTreeCreateFromData", [0, data, 0, 0, 1]),
            ("CFXMLTreeCreateWithDataFromURL", [0, url, 0, 1]),
        ):
            tree = self.factory(name, arguments, check=False)
            serialized = self.factory("CFXMLTreeCreateXMLData", [0, tree])
            self.release(serialized)
            self.release(tree)
        for pair in (url, path_text, data):
            self.release(pair)

    def plugins(self):
        self.scalar("CFPlugInGetTypeID")
        self.scalar("CFPlugInInstanceGetTypeID")
        uuid_text = self.text("D7CF8351-8943-446A-A961-A34E26CBE920")
        uuid = self.factory("CFUUIDCreateFromString", [0, uuid_text])
        self.scalar("CFPlugInRegisterFactoryFunction", [uuid, self.ptr(0x1F00)], 1)
        # Both writers touch the same initialized registry. This reference
        # appends one type association per call, including duplicate pairs.
        self.scalar("CFPlugInRegisterPlugInType", [uuid, uuid], 1)
        factories = self.factory("CFPlugInFindFactoriesForPlugInType", [uuid])
        self.scalar("CFArrayGetCount", [factories], 2)
        empty = self.factory("CFPlugInFindFactoriesForPlugInTypeInPlugIn", [uuid, 0])
        self.scalar("CFArrayGetCount", [empty], 2)
        self.scalar("CFPlugInInstanceCreate", [0, uuid, uuid], 0x12345678)
        for side in (0, 1):
            self.call("CFPlugInAddInstanceForFactory", [uuid], side)
            self.call("CFPlugInRemoveInstanceForFactory", [uuid], side)
        instance = self.factory(
            "CFPlugInInstanceCreateWithInstanceDataSize",
            [0, 16, self.ptr(0x1F10), uuid, self.ptr(0x1F08)],
            check=False,
        )
        self.scalar("CFPlugInInstanceGetFactoryName", [instance])
        for side in (0, 1):
            self.call("CFPlugInInstanceGetInstanceData", [instance], side)
            self.p.emit(0x809E0000 | (instance[1] + 4 * side), 0x38840014)
            self.p.equal("instance data follows the 20-byte object header")
            self.call("CFPlugInInstanceGetInterfaceFunctionTable", [instance, uuid, self.ptr(0x1B00)], side)
            self.p.expect(1, "instance callback reports its interface table")
            self.p.emit(0x807E1B00)
            self.p.expect(0x87654321, "instance callback output preserves all pointer bits")
        self.release(instance)
        self.scalar("CFPlugInUnregisterPlugInType", [uuid, uuid], 1)
        self.scalar("CFPlugInUnregisterFactory", [uuid], 1)
        for pair in (empty, factories, uuid, uuid_text):
            self.release(pair)

    def when_nonzero(self, pair, side, action):
        self.p.emit(0x807E0000 | (pair[1] + 4 * side), 0x2C030000)
        branch = len(self.p.code)
        self.p.emit(0)
        action()
        self.p.code[branch] = 0x41820000 | ((len(self.p.code) - branch) * 4)

    def nullable_factory(self, name, arguments):
        slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.call(name, arguments, side)
            self.p.emit(0x907E0000 | (slot + 4 * side), 0x2C030000, 0x38800000, 0x41820008, 0x38800001, 0x7C832378)
            if not side:
                self.p.emit(0x907E1B00)
            else:
                self.p.emit(0x809E1B00)
                self.p.equal(name + " agrees on optional result presence")
        pair = self.pair_slot(slot)
        self.when_nonzero(pair, 0, lambda: self.equal_objects(slot, name))
        return pair

    def release_nullable(self, pair):
        for side in (0, 1):
            self.when_nonzero(pair, side, lambda side=side: self.call("CFRelease", [self.slot(pair[1] + 4 * side)]))

    def field_pair(self, pair, displacement):
        slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.p.emit(0x807E0000 | (pair[1] + side * 4), 0x80630000 | displacement, 0x907E0000 | (slot + side * 4))
        return self.pair_slot(slot)

    def distinct_text(self, first, second):
        slot = self.next_slot
        self.next_slot += 8
        for side, text in enumerate((first, second)):
            self.call("CFStringCreateWithCString", [0, self.raw(text.encode() + b"\0"), 0x600], side)
            self.p.nonzero("allocate distinct preference application ID")
            self.p.emit(0x907E0000 | (slot + side * 4))
        return self.pair_slot(slot)

    def corresponding(self, name, arguments, expected):
        for side in (0, 1):
            self.call(name, arguments, side)
            self.p.emit(0x809E0000 | (expected[1] + side * 4))
            self.p.equal(name + " returns the corresponding live record")

    def preference_domains(self):
        tag = self.fixture_tag
        app = self.distinct_text("exec.fwn." + tag, "exec.fwo." + tag)
        user, host = self.constant("kCFPreferencesCurrentUser"), self.constant("kCFPreferencesAnyHost")
        domain = self.factory("_CFPreferencesStandardDomain", [app, user, host], check=False)
        context, callbacks = self.field_pair(domain, 12), self.field_pair(domain, 8)
        owned = self.factory("_CFPreferencesDomainCreate", [context, callbacks], check=False)
        self.scalar("__CFPreferencesAllocator")
        key, value = self.text("framework-check"), self.text("stored native value")
        for side in (0, 1):
            self.call("_CFPreferencesDomainSet", [owned, key, value], side)
        # The original reader inspects the native-created domain before another
        # writer can cover a missing native state update.
        self.call("_CFPreferencesDomainCreateValueForKey", [self.slot(owned[1]), self.slot(key[1])], 1)
        self.p.emit(0x907E1B00)
        self.call("CFEqual", [self.slot(0x1B00), self.slot(value[1])], 1)
        self.p.expect(1, "original preference reader sees native domain write")
        self.call("CFRelease", [self.slot(0x1B00)])
        copied = self.factory("_CFPreferencesDomainCreateValueForKey", [owned, key])
        self.release(copied)
        dictionary = self.factory(
            "CFDictionaryCreateMutable",
            [
                0,
                0,
                self.constant("kCFTypeDictionaryKeyCallBacks", False),
                self.constant("kCFTypeDictionaryValueCallBacks", False),
            ],
        )
        self.mutate("CFDictionarySetValue", [dictionary, key, value])
        for side in (0, 1):
            self.call("_CFPreferencesDomainSetDictionary", [owned, dictionary], side)
            self.call("_CFPreferencesDomainSetIsWorldReadable", [owned, 0], side)
        copied = self.factory("_CFPreferencesDomainCopyDictionary", [owned])
        self.release(copied)
        self.output("_CFPreferencesDomainGetKeysAndValues", [0, owned, 0, ("output", 0x1C00)], 4)
        self.scalar("_CFPreferencesDomainSynchronize", [owned], 1)
        for side in (0, 1):
            self.call("_CFPreferencesDomainSet", [domain, key, value], side)
        self.scalar("_CFPreferencesDomainExists", [app, user, host], 1)
        self.scalar("_CFSynchronizeDomainCache", [], 1)
        # Both calls list the same initialized directory/cache, regardless of
        # the two distinct per-side application IDs used above.
        applications = self.nullable_factory("_CFPreferencesCreateDomainList", [user, host])
        self.release_nullable(applications)
        return app, user, host, domain, owned, key, value, dictionary

    def application_preferences(self):
        app, user, host, domain, owned, key, value, dictionary = self.preference_domains()
        preferences = self.factory("_CFApplicationPreferencesCreateWithUser", [user, app], check=False)
        search = self.factory("CFArrayCreateMutable", [0, 0, self.constant("kCFTypeArrayCallBacks", False)])
        for side in (0, 1):
            self.call("CFArrayAppendValue", [search, domain], side)
        self.scalar("CFArrayGetCount", [search], 1)
        for side in (0, 1):
            self.call("_CFApplicationPreferencesSetSearchList", [preferences, search], side)
            self.call("_CFApplicationPreferencesAddDomain", [preferences, domain, 1], side)
        self.scalar("_CFApplicationPreferencesContainsDomain", [preferences, domain], 1)
        for side in (0, 1):
            self.call("_CFApplicationPreferencesRemoveDomain", [preferences, domain], side)
        self.scalar("_CFApplicationPreferencesContainsDomain", [preferences, domain], 0)
        for side in (0, 1):
            self.call("_CFApplicationPreferencesSetStandardSearchList", [preferences], side)
            self.call("_CFApplicationPreferencesSet", [preferences, key, value], side)
        copied = self.factory("_CFApplicationPreferencesCreateValueForKey", [preferences, key])
        self.release(copied)
        copied = self.factory("_CFApplicationPreferencesCopyRepresentation", [preferences])
        self.release(copied)
        copied = self.factory("_CFApplicationPreferencesCopyRepresentationWithHint", [preferences, 0])
        self.release(copied)
        cached = self.field_pair(preferences, 4)
        self.corresponding("_CFApplicationPreferencesCopyRepresentationWithHint", [preferences, cached], cached)
        for side in (0, 1):
            self.call("_CFApplicationPreferencesUpdate", [preferences], side)
        self.scalar("_CFApplicationPreferencesSynchronize", [preferences], 1)
        suite = self.text("exec.fw.suite")
        for side in (0, 1):
            self.call("_CFApplicationPreferencesAddSuitePreferences", [preferences, suite], side)
            self.call("_CFApplicationPreferencesRemoveSuitePreferences", [preferences, suite], side)
            self.call("_CFApplicationPreferencesRemove", [preferences, key], side)
        self.scalar("_CFApplicationPreferencesCreateValueForKey", [preferences, key], 0)
        for side in (0, 1):
            self.call("_CFApplicationPreferencesSetCacheForApp", [preferences, app], side)
        self.corresponding("_CFStandardApplicationPreferences", [app], preferences)
        for side in (0, 1):
            self.call("_CFDeallocateApplicationPreferences", [preferences], side)
        recreated = self.factory("_CFStandardApplicationPreferences", [app], check=False)
        for side in (0, 1):
            self.call("_CFDeallocateApplicationPreferences", [recreated], side)
            self.call("_CFPreferencesDomainSetDictionary", [owned, 0], side)
        self.scalar("_CFPreferencesDomainCreateValueForKey", [owned, key], 0)
        self.scalar("_CFPreferencesDomainCopyDictionary", [owned], 0)
        self.scalar("_CFPreferencesDomainSynchronize", [owned], 1)
        for side in (0, 1):
            self.call("_CFPreferencesDomainSet", [domain, key, 0], side)
        self.scalar("_CFPreferencesDomainSynchronize", [domain], 1)
        for pair in (search, suite, owned, dictionary, key, value, app):
            self.release(pair)

    @staticmethod
    def hfs_path(path):
        path = path.resolve().as_posix()
        if path.startswith("/mnt/") and len(path) > 6 and path[6] == "/":
            path = path[5].upper() + ":" + path[6:]
        if len(path) > 2 and path[1:3] == ":/":
            path = path[0] + ":" + path[3:].replace("/", ":")
        return path

    def file_url(self, path, directory):
        text = self.text(self.hfs_path(path))
        url = self.factory("CFURLCreateWithFileSystemPath", [0, text, 1, int(directory)])
        self.release(text)
        return url

    def bundles(self):
        url = self.file_url(self.bundle_path, True)
        bundle = self.factory("CFBundleCreate", [0, url])
        self.scalar("_CFBundleLayoutVersion", [bundle])
        self.scalar("_CFBundleURLLooksLikeBundle", [url], 1)
        self.scalar("_CFBundleGetHasChanged", [bundle])
        self.nullable_factory("_CFBundleGetMainBundleIfLooksLikeBundle", [])
        self.nullable_factory("_CFBundleGetLocalInfoDictionary", [bundle])
        self.factory("_CFBundleGetLanguageSearchList", [bundle])
        for entry, arguments in (
            ("_CFBundleCreateIfLooksLikeBundle", [0, url]),
            ("_CFBundleCopyExecutableURLInDirectory", [url]),
            ("_CFBundleCopyBundleRegionsArray", [bundle]),
            ("_CFBundleCopyPrivateFrameworksURL", [bundle]),
            ("_CFBundleCopySharedFrameworksURL", [bundle]),
            ("_CFBundleCopySharedSupportURL", [bundle]),
            ("_CFBundleCopyBuiltInPlugInsURL", [bundle]),
        ):
            self.release(self.factory(entry, arguments))
        for entry, arguments in (
            ("_CFBundleCopyInfoPlistURL", [bundle]),
            ("_CFBundleCopyResourceForkURL", [bundle]),
            ("_CFBundleCopyInfoDictionaryInResourceFork", [url]),
        ):
            self.release_nullable(self.nullable_factory(entry, arguments))
        self.output("_CFBundleGetLanguageAndRegionCodes", [("output", 0x1C00), ("output", 0x1C04)], 8)
        copied = self.factory("_CFBundleCopyLanguageSearchListInDirectory", [0, url, ("output", 0x1C00)])
        self.p.emit(0x887E1C00, 0x889E1D00)
        self.p.equal("language search list directory layout byte")
        self.release(copied)
        self.output("_CFBundleGetPackageInfoInDirectory", [0, url, ("output", 0x1C00), ("output", 0x1C04)], 8, True)
        identifier = self.text("org.romaw.framework.fixture")
        self.corresponding("CFBundleGetBundleWithIdentifier", [identifier], bundle)
        self.scalar("CFBundleGetAllBundles")
        # GetMainBundle is borrowed and may be absent for a flat PEF app.
        self.nullable_factory("CFBundleGetMainBundle", [])
        copied = self.factory("CFBundleCopyInfoDictionaryInDirectory", [url])
        self.release(copied)
        for name in (
            "CFBundleCopySupportFilesDirectoryURL",
            "CFBundleCopyResourcesDirectoryURL",
            "CFBundleCopyPrivateFrameworksURL",
            "CFBundleCopySharedFrameworksURL",
            "CFBundleCopySharedSupportURL",
            "CFBundleCopyBuiltInPlugInsURL",
        ):
            copied = self.factory(name, [bundle])
            self.release(copied)
        # The reference caches an executable path; a repeated call may return
        # an absolute URL instead of the first relative URL. Compare HFS paths.
        executable = self.factory("CFBundleCopyExecutableURL", [bundle], check=False)
        absolute_executable = self.factory("CFURLCopyAbsoluteURL", [executable], check=False)
        self.release_nullable(self.nullable_factory("_CFBundleCopyBundleURLForExecutableURL", [absolute_executable]))
        self.release_nullable(
            self.nullable_factory("_CFBundleCreateWithExecutableURLIfLooksLikeBundle", [0, absolute_executable])
        )
        executable_path = self.factory("CFURLCopyFileSystemPath", [absolute_executable, 1])
        self.release(executable_path)
        self.release(absolute_executable)
        self.release(executable)
        auxiliary = self.text("AuxExec")
        copied = self.nullable_factory("CFBundleCopyAuxiliaryExecutableURL", [bundle, auxiliary])
        self.release_nullable(copied)
        name, kind, language = self.text("fixture"), self.text("txt"), self.text("en")
        for entry, arguments in (
            ("_CFBundleCopyResourceURLForLanguage", [bundle, name, kind, 0, language]),
            ("_CFBundleCopyResourceURLsOfTypeForLanguage", [bundle, kind, 0, language]),
            ("CFBundleCopyResourceURL", [bundle, name, kind, 0]),
            ("CFBundleCopyResourceURLForLocalization", [bundle, name, kind, 0, language]),
            ("CFBundleCopyResourceURLInDirectory", [url, name, kind, 0]),
            ("CFBundleCopyResourceURLsOfType", [bundle, kind, 0]),
            ("CFBundleCopyResourceURLsOfTypeForLocalization", [bundle, kind, 0, language]),
            ("CFBundleCopyResourceURLsOfTypeInDirectory", [url, kind, 0]),
            ("CFBundleCopyBundleLocalizations", [bundle]),
        ):
            copied = self.factory(entry, arguments)
            self.release(copied)
        key, fallback = self.text("greeting"), self.text("fallback")
        info_key = self.text("CFBundleIdentifier")
        info_value = self.factory("_CFBundleGetValueForInfoKey", [bundle, info_key])
        for side in (0, 1):
            self.call("CFEqual", [info_value, identifier], side)
            self.p.expect(1, "bundle information lookup returns fixture identifier")
        self.release(info_key)
        copied = self.factory("CFBundleCopyLocalizedString", [bundle, key, fallback, 0])
        self.release(copied)
        self.output(
            "CFBundleGetLocalizationInfoForLocalization",
            [language, ("output", 0x1C00), ("output", 0x1C04), ("output", 0x1C08), ("output", 0x1C0C)],
            16,
            True,
        )
        copied = self.factory("CFBundleCopyLocalizationForLocalizationInfo", [0, 0, 0, 0])
        self.release(copied)
        localizations = self.factory("CFArrayCreateMutable", [0, 0, self.constant("kCFTypeArrayCallBacks", False)])
        self.mutate("CFArrayAppendValue", [localizations, language])
        copied = self.factory("CFBundleCopyPreferredLocalizationsFromArray", [localizations])
        self.release(copied)
        self.output("CFBundleGetPackageInfo", [bundle, ("output", 0x1C00), ("output", 0x1C04)], 8)
        self.output("CFBundleGetPackageInfoInDirectory", [url, ("output", 0x1C00), ("output", 0x1C04)], 8, True)
        directory = self.file_url(self.bundle_path.parent, True)
        bundle_kind = self.text("bundle")
        copied = self.factory("CFBundleCreateBundlesFromDirectory", [0, directory, bundle_kind], check=False)
        self.scalar("CFArrayGetCount", [copied], 1)
        for side in (0, 1):
            self.call("CFArrayGetValueAtIndex", [copied, 0], side)
            self.p.emit(0x907E1B00)
            self.call("CFBundleGetIdentifier", [self.slot(0x1B00)], side)
            self.p.emit(0x907E1B00)
            self.call("CFEqual", [self.slot(0x1B00), identifier], side)
            self.p.expect(1, "directory bundle enumeration returns the fixture identifier")
        self.release(copied)
        # Resource numbers are per-open identities. Validate success/close for
        # each side rather than requiring different opens to share a number.
        for side in (0, 1):
            self.call("CFBundleOpenBundleResourceFiles", [bundle, self.ptr(0x1C00), self.ptr(0x1C04)], side)
            self.p.expect(0, "bundle resource files open through the generated cfrg fork")
            for offset in (0x1C00, 0x1C04):
                self.p.emit(0xA87E0000 | offset, 0x907E1B00)
                branch = len(self.p.code)
                self.p.emit(0x2C03FFFF, 0)
                self.call("CFBundleCloseBundleResourceMap", [bundle, self.slot(0x1B00)], side)
                self.p.code[branch + 1] = 0x41820000 | ((len(self.p.code) - branch - 1) * 4)
            self.call("CFBundleOpenBundleResourceMap", [bundle], side)
            self.p.emit(0x907E1B00, 0x2C03FFFF, 0x40820008, 0)
            self.call("CFBundleCloseBundleResourceMap", [bundle, self.slot(0x1B00)], side)
            self.call("_CFBundleOpenBundleResourceFork", [bundle], side)
            self.p.emit(0x907E1B00, 0x2C03FFFF, 0x40820008, 0)
            self.call("CFBundleCloseBundleResourceMap", [bundle, self.slot(0x1B00)], side)
        plugin = self.factory("CFPlugInCreate", [0, url])
        self.corresponding("CFPlugInGetBundle", [plugin], plugin)
        for side in (0, 1):
            self.call("CFPlugInSetLoadOnDemand", [plugin, 1], side)
        self.scalar("CFPlugInIsLoadOnDemand", [plugin], 1)
        uuid_text = self.text("EDCF8351-8943-446A-A961-A34E26CBE920")
        factory_id = self.factory("CFUUIDCreateFromString", [0, uuid_text])
        factory_name = self.text("FixtureFactory")
        self.scalar("_CFBundleGetCFMFunctionPointerForName", [bundle, factory_name])
        names = self.factory("CFArrayCreateMutable", [0, 0, self.constant("kCFTypeArrayCallBacks", False)])
        self.mutate("CFArrayAppendValue", [names, factory_name])
        self.mutate("CFArrayAppendValue", [names, key])
        self.output("_CFBundleGetCFMFunctionPointersForNames", [bundle, names, ("output", 0x1C00)], 8)
        self.release(names)
        self.scalar("CFPlugInRegisterFactoryFunctionByName", [factory_id, plugin, factory_name], 1)
        for side in (0, 1):
            self.call("CFPlugInSetLoadOnDemand", [plugin, 0], side)
        self.scalar("CFPlugInIsLoadOnDemand", [plugin], 0)
        self.scalar("CFPlugInUnregisterFactory", [factory_id], 1)
        for pair in (
            factory_name,
            factory_id,
            uuid_text,
            plugin,
            bundle_kind,
            directory,
            localizations,
            fallback,
            key,
            language,
            kind,
            name,
            auxiliary,
            identifier,
            bundle,
            url,
        ):
            self.release(pair)

    def file_services(self):
        source = self.file_url(self.fixture_path, False)
        parent = self.file_url(self.fixture_path.parent, True)
        self.output("_CFGetFSSpecFromURL", [0, source, ("output", 0x1C00)], 6, True)
        restored = self.factory("_CFCreateURLFromFSSpec", [0, ("output", 0x1C00), 0])
        self.release(restored)
        extension = self.text(".xml")
        listing = self.factory("_CFContentsOfDirectory", [0, 0, 0, parent, extension])
        self.scalar("CFArrayGetCount", [listing], 1)
        self.release(listing)
        self.release(extension)
        self.release_nullable(self.nullable_factory("_CFCopySystemVersionDictionary", []))
        self.scalar("_CFGetFileProperties", [0, source, 0, 0, 0, 0, 0, 0], 0)
        for side in (0, 1):
            self.call(
                "_CFGetFileProperties",
                [0, source, self.ptr(0x1C00), self.ptr(0x1C04), self.ptr(0x1C08), 0, self.ptr(0x1C10), 0],
                side,
            )
            self.p.expect(0, "get existing file properties")
            self.p.emit(0x887E1C00)
            self.p.expect(1, "fixture file exists")
            for offset, value in ((4, 0x81FF), (8, 0), (12, self.fixture_path.stat().st_size), (16, -1)):
                self.p.emit(0x807E1C00 | offset)
                self.p.expect(value, "fixture file mode, size and owner")
        exists = self.constant("kCFURLFileExists")
        prop = self.factory("CFURLCreatePropertyFromResource", [0, source, exists, self.ptr(0x1B80)])
        self.release(prop)
        self.release(parent)

        child = self.fixture_path.parent / "ServiceFiles"
        child_url = self.file_url(child, True)
        for side in (0, 1):
            self.call("_CFGetFSSpecFromURL", [0, source, self.ptr(0x1C00)], side)
            self.p.expect(1, "resolve existing fixture sibling")
            self.call("_CFGetFSSpecFromURL", [0, child_url, self.ptr(0x1D00)], side)
            self.p.expect(1, "resolve parent of missing fixture child")
            for offset in (0, 2, 4):
                self.p.emit(0xA07E1C00 | offset, 0xA09E1D00 | offset)
                self.p.equal("new child FSSpec stays in the fixture directory")
        self.release(child_url)
        self.release(source)
        target = child / "data.bin"
        dir_name = self.raw(self.hfs_path(child).encode() + b"\0")
        file_name = self.raw(self.hfs_path(target).encode() + b"\0")
        url = self.file_url(target, False)
        payload = self.raw(b"ABC\0DEF!")
        data = self.factory("CFDataCreate", [0, payload, 8])
        for side in (0, 1):
            self.call("_CFCreateDirectory", [dir_name], side)
            self.p.expect(1, "create isolated service directory")
            self.call("_CFWriteBytesToFile", [url, payload, 8], side)
            self.p.expect(1, "write eight fixture bytes")
            self.call("_CFReadBytesFromFile", [0, url, self.ptr(0x1B80), self.ptr(0x1B84), 4], side)
            self.p.expect(1, "read bounded binary payload")
            self.p.emit(0x807E1B80, 0x80630000)
            self.p.expect(0x41424300, "binary read preserves embedded zero")
            self.p.emit(0x807E1B84)
            self.p.expect(4, "maximum clamps file read")
            self.call("CFAllocatorDeallocate", [0, self.slot(0x1B80)], side)
            self.call("_CFDeleteFile", [file_name], side)
            self.p.expect(1, "delete fixture file")
            self.call("CFURLWriteDataAndPropertiesToResource", [url, data, 0, self.ptr(0x1B84)], side)
            self.p.expect(1, "write through file URL")
            self.call(
                "CFURLCreateDataAndPropertiesFromResource", [0, url, self.ptr(0x1B80), 0, 0, self.ptr(0x1B84)], side
            )
            self.p.expect(1, "read data through file URL")
            self.call("CFEqual", [self.slot(0x1B80), data], side)
            self.p.expect(1, "file data round trip")
            self.call("CFRelease", [self.slot(0x1B80)])
            self.call("CFURLDestroyResource", [url, self.ptr(0x1B84)], side)
            self.p.expect(1, "destroy file through URL")
            self.call("_CFRemoveDirectory", [dir_name], side)
            self.p.expect(1, "remove empty service directory")
        self.release(data)
        self.release(url)

        plain, empty = self.text("a b"), self.text("")
        encoded = self.factory("CFURLCreateStringByAddingPercentEscapes", [0, plain, 0, 0, 0x08000100])
        decoded = self.factory("CFURLCreateStringByReplacingPercentEscapes", [0, encoded, empty])
        for side in (0, 1):
            self.call("CFEqual", [decoded, plain], side)
            self.p.expect(1, "space survives percent escape round trip")
        for pair in (decoded, encoded, empty, plain):
            self.release(pair)
        for contents in (b"<plist><string>fixture</string></plist>", b'{ name = "fixture"; }'):
            data = self.factory("CFDataCreate", [0, self.raw(contents), len(contents)])
            plist = self.factory("_CFPropertyListCreateFromXMLData", [0, data, 0, 0, 1])
            self.release(plist)
            self.release(data)

    def algorithms_and_sets(self):
        payload = self.raw(b"\x80\xff\0abcdefghijk")
        self.scalar("CFHashBytes", [0, 0], 0)
        self.scalar("CFHashBytes", [payload, 2], 0x8FF)
        self.scalar("CFHashBytes", [payload, 14])
        keys = [9, 0, -1, 2, 8, 6, 5, 3, 1, 7, 4, 0, 2, 5, 0, 2, -1, 4]
        for name in ("CFQSortArray", "CFMergeSortArray"):
            for side in (0, 1):
                records = self.raw(b"".join(struct.pack(">ii", key, tag) for tag, key in enumerate(keys)))
                count = self.raw(bytes(4))
                args = [records, len(keys), 8]
                if name == "CFMergeSortArray":
                    args.append(0)
                self.call(name, args + [self.ptr(0x1F40), count], side)
                self.p.emit(0x807E0000 | count[1])
                self.p.nonzero(name + " used the supplied comparator context")
                for index, (tag, key) in enumerate(sorted(enumerate(keys), key=lambda item: item[1])):
                    self.p.emit(0x807E0000 | (records[1] + index * 8))
                    self.p.expect(key, name + " sorted signed key")
                    if name == "CFMergeSortArray":
                        self.p.emit(0x807E0000 | (records[1] + index * 8 + 4))
                        self.p.expect(tag, "merge sort preserves equal-key order")

        stdlib_records = [(8, 11), (-2, 12), (1, 13), (0, 14), (4, 15)]
        records = self.raw(b"".join(struct.pack(">ii", *record) for record in stdlib_records) + b"GUAR")
        self.call("qsort", [0, 0, 8, 0])
        self.call("qsort", [records, len(stdlib_records), 8, self.ptr(0x1F48)])
        for index, (key, tag) in enumerate(sorted(stdlib_records)):
            self.p.emit(0x807E0000 | (records[1] + index * 8))
            self.p.expect(key, "StdCLib qsort calls a two-argument guest comparator")
            self.p.emit(0x807E0000 | (records[1] + index * 8 + 4))
            self.p.expect(tag, "StdCLib qsort moves complete records")
        self.p.emit(0x807E0000 | (records[1] + len(stdlib_records) * 8))
        self.p.expect(0x47554152, "StdCLib qsort preserves the trailing guard")

        jump = self.raw(bytes([0xA5]) * 0xF8)
        if self.cursor & 7:
            self.raw(bytes(4))
        _, scratch = self.raw(struct.pack(">d", 1.25) + bytes(40))
        p = self.p
        p.emit(0x937E0000 | (scratch + 24), 0xD9DE0000 | (scratch + 16), 0xFC00048E, 0xD81E0000 | (scratch + 8))
        for result in (0, -7):
            p.emit(0x3B604567, 0xC9DE0000 | scratch, 0xFF80210C)
            self.call("__setjmp", [jump])
            p.emit(0x2C030000)
            resumed = len(p.code)
            p.emit(0x40820000, 0x3B600000, 0xC9DE0000 | (scratch + 40), 0xFF80010C)
            self.call("longjmp", [jump, result])
            p.emit(0)  # Returning to the call to longjmp would fail.
            p.code[resumed] |= (len(p.code) - resumed) * 4
            p.expect(result or 1, "longjmp resumes with the expected result")
            p.emit(0x7F63DB78)
            p.expect(0x4567, "longjmp restores nonvolatile integer state")
            p.emit(0xD9DE0000 | (scratch + 32), 0x807E0000 | (scratch + 32))
            p.expect(0x3FF40000, "longjmp restores the saved floating-point register")
            p.emit(0xFC00048E, 0xD81E0000 | (scratch + 32), 0x807E0000 | (scratch + 36), 0x70630003)
            p.expect(2, "longjmp restores FPSCR rounding bits")
        p.emit(0x837E0000 | (scratch + 24), 0xC9DE0000 | (scratch + 16), 0xC81E0000 | (scratch + 8), 0xFDFE058E)
        p.emit(0x807E0000 | (jump[1] + 0x10))
        p.expect(0xA5A5A5A5, "scalar setjmp preserves the reserved vector word")

        bitmap = bytearray(8192)
        bitmap[:6] = bytes([0x23, 0x80, 0xFF, 0x01, 0, 0x40])
        bitmap[4095:4097] = b"\xc0\x07"
        bitmap[-1] = 0x80
        data = self.factory("CFDataCreateMutable", [0, 0])
        for side in (0, 1):
            self.call("CFDataSetLength", [data, len(bitmap)], side)
            self.call("CFDataGetMutableBytePtr", [data], side)
            for index, byte in enumerate(bitmap):
                if byte:
                    self.p.emit(0x38800000 | byte, 0x98830000 | index)
        charset = self.factory("CFCharacterSetCreateWithBitmapRepresentation", [0, data])
        for name, kind in (("CFCharacterSetCompact", 0x40), ("CFCharacterSetFast", 0x30)):
            for side in (0, 1):
                self.call(name, [charset], side)
                self.p.emit(0x807E0000 | (charset[1] + side * 4), 0x80630004, 0x70630070)
                self.p.expect(kind, name + " swaps live character-set representation")
            for member in (
                0,
                1,
                2,
                5,
                15,
                16,
                23,
                24,
                25,
                45,
                46,
                32765,
                32766,
                32767,
                32768,
                32770,
                32771,
                65534,
                65535,
            ):
                expected = member < len(bitmap) * 8 and bool(bitmap[member // 8] & (1 << (member % 8)))
                self.scalar("CFCharacterSetIsCharacterMember", [charset, member], expected)
            exported = self.factory("CFCharacterSetCreateBitmapRepresentation", [0, charset])
            for side in (0, 1):
                self.call("CFEqual", [data, exported], side)
                self.p.expect(1, name + " preserves all 8192 bitmap bytes")
            self.release(exported)
        self.release(charset)
        self.release(data)

    def process_and_diagnostics(self):
        self.scalar("_CFProcessPath")
        self.scalar("_CFProcessName")
        self.scalar("_CFProcessNameString")
        # Library initialization may already have cached a name string. Exercise
        # a cold conversion with known bytes, then restore both borrowed globals.
        name = self.raw(b"CF process fixture\0")
        expected = self.text("CF process fixture")
        desc = 0x2400 + 4 * self.names.index("_CFProcessNameString")
        self.p.emit(0x807E0000 | desc, 0x80630004, 0x907E1B10, 0x808308B4, 0x909E1B14, 0x808308B8, 0x909E1B18)
        for side in (0, 1):
            self.p.emit(0x807E1B10, 0x38800000, 0x908308B4, 0x389E0000 | name[1], 0x908308B8)
            self.call("_CFProcessNameString", [], side)
            self.p.emit(0x907E1B00)
            self.call("CFEqual", [expected, self.slot(0x1B00)], side)
            self.p.expect(1, "cold process name cache decodes its borrowed C string")
            self.call("_CFProcessNameString", [], side)
            self.p.emit(0x809E1B00)
            self.p.equal("process name string cache preserves identity")
            self.p.emit(0x807E1B10, 0x809E1B14, 0x908308B4, 0x809E1B18, 0x908308B8)
            self.call("CFRelease", [self.slot(0x1B00)])
        self.release(expected)
        text = self.text("ROMAW CFShow fixture")
        fmt = self.text("ROMAW CFLog %d/%d/%d/%d/%d/%d/%d")
        self.diagnostic_lines = [
            "ROMAW CFShow fixture",
            "Length 20",
            "IsEightBit 1",
            "HasLengthByte 1",
            "HasNullByte 1",
            "InlineContents 1",
            "Allocator SystemDefault",
            "Mutable 0",
            "ROMAW CFLog 1/2/3/4/5/6/7",
        ]
        for side in (0, 1):
            self.call("CFShow", [text], side)
            self.call("CFShowStr", [text], side)
            self.call("CFLog", [4, fmt, 1, 2, 3, 4, 5, 6, 7], side)
        self.release(fmt)
        self.release(text)

    def default_allocator(self):
        self.call("CFAllocatorGetContext", [0, self.ptr(0x1A00)])
        custom = self.factory("CFAllocatorCreate", [0, self.ptr(0x1A00)], check=False)
        for side in (0, 1):
            self.call("CFAllocatorGetDefault", [], side)
            self.p.emit(0x907E1B00)
            self.call("CFAllocatorSetDefault", [custom], side)
            self.call("CFAllocatorGetDefault", [], side)
            self.p.emit(0x809E0000 | (custom[1] + 4 * side))
            self.p.equal("custom allocator becomes the process default")
            self.call("CFAllocatorSetDefault", [self.slot(0x1B00)], side)
            self.call("CFAllocatorGetDefault", [], side)
            self.p.emit(0x809E1B00)
            self.p.equal("previous default allocator is restored")
        self.scalar("CFGetRetainCount", [custom], 2)
        self.release(custom)

    def system_allocator_storage(self):
        p = self.p
        name = "kCFAllocatorSystemDefault"
        carbon, core = (
            p.import_base + 4 * p.imports.index((library, name, 1)) for library in ("CarbonLib", "CoreFoundationLib")
        )
        saved = self.constant(name, False)
        p.emit(0x807E0000 | carbon, 0x809E0000 | core)
        p.equal("CarbonLib and CoreFoundationLib share the allocator data word")
        p.emit(0x809E0000 | saved[1])
        p.equal("the live CFM symbol exposes the same allocator data word")
        system = ("global", carbon)
        self.call("CFAllocatorGetDefault")
        p.emit(0x809E0000 | carbon, 0x80840000)
        p.equal("the default allocator is the shared singleton")

        _, block = self.raw(bytes(4))
        self.call("CFAllocatorGetContext", [system, self.ptr(0x1A00)])

        def worker(offset, arguments):
            self.arguments(arguments, 0)
            p.emit(0x819E0000 | (0x1A00 + offset), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

        worker(20, [16, 0, 0])
        p.nonzero("system context allocate worker obtains a block")
        p.emit(0x907E0000 | block, 0x8063FFFC)
        p.expect(0xAAAACFCF, "context allocate uses the native pointer tag")
        p.emit(0x807E0000 | block, 0x3C804152, 0x60844449, 0x90830000)
        worker(24, [self.slot(block), 32768, 0, 0])
        p.nonzero("context reallocate grows into a locked temporary handle")
        p.emit(0x907E0000 | block, 0x8063FFFC)
        p.expect(0xBBBBCFCF, "context reallocate uses the native handle tag")
        p.emit(0x807E0000 | block, 0x80630000)
        p.expect(0x41524449, "context reallocate preserves the payload")
        worker(32, [123, 0, 0])
        p.expect(123, "system preferred-size callback preserves its requested size")
        worker(28, [self.slot(block), 0])
        for side in (0, 1):
            self.call("CFAllocatorAllocate", [system, 16, 0], side)
            p.nonzero("system allocator obtains a small pointer block")
            p.emit(0x907E0000 | block, 0x8063FFFC)
            p.expect(0xAAAACFCF, "small system allocation uses the shared pointer tag")
            p.emit(0x807E0000 | block, 0x3C804152, 0x60844449, 0x90830000)
            self.call("CFAllocatorReallocate", [system, self.slot(block), 32768, 0], 1 - side)
            p.nonzero("system allocator grows into a temporary handle")
            p.emit(0x907E0000 | block, 0x8063FFFC)
            p.expect(0xBBBBCFCF, "large system allocation uses the shared handle tag")
            p.emit(0x807E0000 | block, 0x80630000)
            p.expect(0x41524449, "cross-implementation grow preserves the payload")
            self.call("CFAllocatorReallocate", [system, self.slot(block), 8, 0], side)
            p.nonzero("system allocator shrinks into a pointer block")
            p.emit(0x907E0000 | block, 0x8063FFFC)
            p.expect(0xAAAACFCF, "shrinking follows the same pointer tag rule")
            p.emit(0x807E0000 | block, 0x80630000)
            p.expect(0x41524449, "cross-implementation shrink preserves the payload")
            self.call("CFAllocatorDeallocate", [system, self.slot(block)], 1 - side)

    def finish(self, destination):
        self.call("CloseConnection", [self.ptr(0x2100)])
        self.p.expect(0, "close original framework oracle")
        self.p.finish(destination, "PPC Core Framework Real Probe", carbon=True)
        self.write_coverage(
            destination,
            "PPC Core Framework Real Probe",
            [row["name"] for row in ROWS] + self.service_names + self.timezone_names,
        )


def make_probe(destination, original_only=False):
    destination.mkdir(parents=True, exist_ok=True)
    probe = FrameworksProbe(original_only)
    probe.fixture_tag = uuid.uuid4().hex[:8]
    probe.fixture_path = destination / "frameworks.xml"
    probe.fixture_path.write_bytes(b'<a x="v">text<b/></a>\n')
    probe.bundle_path = destination / "Framework.bundle"
    contents = probe.bundle_path / "Contents"
    info = {
        "CFBundleIdentifier": "org.romaw.framework.fixture",
        "CFBundleExecutable": "FrameworkExec",
        "CFBundlePackageType": "BNDL",
        "CFBundleSignature": "TEST",
        "CFBundleDevelopmentRegion": "en",
        "CFBundleLocalizations": ["en", "fr"],
        "CFPlugInDynamicRegistration": False,
        "CFPlugInFactories": {"CDCF8351-8943-446A-A961-A34E26CBE920": "FixtureFactory"},
    }
    contents.mkdir(parents=True, exist_ok=True)
    (contents / "Info.plist").write_bytes(plistlib.dumps(info))
    (contents / "PkgInfo").write_bytes(b"BNDLTEST")
    for child in ("Frameworks", "SharedFrameworks", "SharedSupport", "PlugIns", "Plug-ins"):
        (contents / child).mkdir(exist_ok=True)
    for child in ("Resources", "Resources/en.lproj", "Resources/fr.lproj"):
        resource = contents / child
        resource.mkdir(parents=True, exist_ok=True)
        (resource / "fixture.txt").write_bytes(b"framework resource\n")
        (resource / "Localizable.strings").write_bytes(plistlib.dumps({"greeting": "hello"}))
    image = pef([0x3C601234, 0x60635678, 0x4E800020], bytes(8), exports=[("FixtureFactory", 2, 0, 1)], reloc=[0x4600])
    for child in ("MacOS", "MacOSClassic"):
        executable = contents / child
        executable.mkdir(exist_ok=True)
        for name in ("FrameworkExec", "AuxExec"):
            write_fragment(executable, name, image, 0)
    probe.section("XML nodes and trees", probe.xml_nodes)
    probe.section("XML parsers and documents", probe.xml_parsers)
    probe.section("Plug-in factories and instances", probe.plugins)
    probe.section("Preference domains and application caches", probe.application_preferences)
    probe.section("Bundles, localized resources and dynamic plug-ins", probe.bundles)
    probe.section("Default allocator ownership", probe.default_allocator)
    probe.section("System allocator pointer and handle storage", probe.system_allocator_storage)
    probe.section("HFS path queries and in-place edits", probe.hfs_paths)
    probe.section("Binary search insertion and comparator calls", probe.binary_search)
    probe.section("Gregorian calendar, stack arguments and structure returns", probe.calendar)
    if probe.keys:
        probe.section("Shared CoreFoundation string constants", probe.corefoundation_keys)
        probe.section("Shared CoreFoundation allocator, boolean and number constants", probe.corefoundation_singletons)
    if probe.timezone_names:
        probe.section("CoreFoundation time-zone objects and transitions", probe.timezones)
    probe.section("Formatted strings, encodings and numeric parsing", probe.string_services)
    probe.section("Date ownership and UUID interning", probe.dates_and_uuids)
    probe.section("Runtime globals, storage and string records", probe.service_records)
    probe.section("File services, URL resources and property lists", probe.file_services)
    probe.section("Sorting, hashing and character-set conversions", probe.algorithms_and_sets)
    probe.section("Process identity and diagnostic output", probe.process_and_diagnostics)
    probe.finish(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    make_probe(arguments.destination)


if __name__ == "__main__":
    main()
