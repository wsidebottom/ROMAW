#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_collections_probe.py
#
#   Summary:   Exercise native collection entries against un-intercepted original descriptors.
#
#   Classes:   CollectionsProbe
#
#   Functions: make_probe, make_guard_probe, main
#
#   Origin:    Original PPC fixture generator using local reference metadata and public SDK declarations. Contains no
#              Apple instructions.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Exercise native collection entries against un-intercepted original descriptors."""
import argparse
import json
import struct
from pathlib import Path

from make_ppc_carbon_cf_values_probe import ValuesProbe, make_foreign_probe
from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

ROOT = Path(__file__).resolve().parents[2]
ROWS = json.loads((ROOT / "carbonlib/plans/cf-collections-batch.json").read_text())["entries"]
TREE_MUTATORS = ("CFTreeSetContext", "CFTreeSortChildren")

CALLBACK_RECORDS = {
    "kCFTypeArrayCallBacks": 4,
    "kCFTypeDictionaryValueCallBacks": 4,
    "kCFTypeDictionaryKeyCallBacks": 5,
    "kCFTypeSetCallBacks": 5,
    "kCFTypeBagCallBacks": 5,
    "kCFCopyStringDictionaryKeyCallBacks": 5,
    "kCFCopyStringSetCallBacks": 5,
    "kCFCopyStringBagCallBacks": 5,
}


class CollectionsProbe(ValuesProbe):
    def __init__(self, library, original_only=False):
        rows = [row for row in ROWS if library != "CarbonLib" or not row["companion_only"]]
        self.cohort = {row["name"] for row in rows}
        # Optional real checks already require the local runtime. Metadata
        # identifies support descriptors; no reference executable bytes enter
        # the generated application or a tracked fixture.
        reference = ROOT / "references/disasm/extensions/Extensions/CarbonLib/pef/data-CoreFoundationLib/pef.json"
        exports = {row["name"]: row for row in json.loads(reference.read_text())["exports"]}
        self.tree_mutators = not original_only
        if self.tree_mutators:
            rows += [exports[name] for name in TREE_MUTATORS]
            self.cohort.update(TREE_MUTATORS)
        support = [
            "CFEqual",
            "CFHash",
            "CFStringCreateWithCString",
            "CFStringCreateMutable",
            "CFStringAppendCString",
            "CFStringGetLength",
            "CFStringGetCharacters",
            "CFArrayCreate",
            "CFArrayGetCount",
            "CFArrayGetValueAtIndex",
            "CFDataCreate",
            "CFDataGetLength",
            "CFDataGetBytePtr",
            "CFNumberCreate",
            "CFGetTypeID",
            "CFGetRetainCount",
            "CFSetGetTypeID",
            "CFSetGetCount",
            "CFSetGetCountOfValue",
            "CFSetContainsValue",
            "CFSetGetValue",
            "CFSetGetValues",
            "CFDateCreate",
            "CFDateGetAbsoluteTime",
        ]
        rows += [exports[name] for name in support if name not in self.cohort]
        data = [(alias, name, 1) for name in CALLBACK_RECORDS for alias in ("CarbonLib", "CoreFoundationLib")]
        super().__init__(library, rows=rows, data_imports=data)
        self.callback_pairs = {}
        # Summing callbacks accept only application-owned context words. The
        # runtime initializer writes a known word into the new object's payload.
        self.p.routines = [
            (0x1F00, [0x80A40000, 0x7CA51A14, 0x90A40000, 0x4E800020]),
            (0x1F08, [0x80C50000, 0x7CC62214, 0x90C50000, 0x4E800020]),
            (0x1F10, [0x80630000, 0x80A40000, 0x7CA51A14, 0x90A40000, 0x4E800020]),
            (0x1F18, [0x3C801234, 0x60845678, 0x90830008, 0x4E800020]),
            (0x1F20, [0x38630001, 0x4E800020]),
            (0x1F28, [0x4E800020]),
            (0x1F30, [0x80A40000, 0x38A50001, 0x90A40000, 0x4E800020]),
            (0x1F38, [0x80821EE4, 0x38840001, 0x90821EE4, 0x4E800020]),
            (0x1F40, [0x80C50000, 0x38C60001, 0x90C50000, 0x80630014, 0x80840014, 0x7C641850, 0x4E800020]),
        ]

    def reset_outputs(self):
        self.p.emit(0x38600000, 0x907E1C00, 0x907E1D00)

    def same_pointer(self, name, arguments, expected):
        for side in (0, 1):
            self.call(name, arguments, side)
            self.p.emit(0x809E0000 | (expected[1] + side * 4))
            self.p.equal(name + " returns the corresponding live object")

    def invoke_collection_callback(self, pair, offset, args, side):
        self.arguments(args, side)
        self.p.emit(
            0x819E0000 | (pair[1] + side * 4),
            0x818C0000 | offset,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def callback_data(self):
        p = self.p
        for name, count in CALLBACK_RECORDS.items():
            carbon = p.import_base + 4 * p.imports.index(("CarbonLib", name, 1))
            core = p.import_base + 4 * p.imports.index(("CoreFoundationLib", name, 1))
            p.emit(0x807E0000 | carbon, 0x809E0000 | core)
            p.equal(name + " imports alias one guest record")
            serial = self.next_slot
            self.next_slot += 8
            saved = self.constant(name, False)
            p.emit(0x807E0000 | carbon, 0x907E0000 | serial)
            p.emit(0x809E0000 | saved[1], 0x909E0000 | (serial + 4), 0x7C632278)
            p.nonzero(name + " native and real CFM data use separate storage")
            p.emit(0x807E0000 | carbon, 0x80630000)
            p.expect(0, name + " uses version zero")
            for index in range(1, count + 1):
                p.emit(0x807E0000 | carbon, 0x80630000 | (4 * index))
                p.nonzero(name + " provides callback " + str(index))
            self.callback_pairs[name] = self.pair_slot(serial)

        raw = self.raw(b"collection callback\0")
        string = self.factory("CFStringCreateWithCString", [0, raw, 0])
        array = self.callback_pairs["kCFTypeArrayCallBacks"]
        for side in (0, 1):
            p.emit(0x807E0000 | (string[1] + 4 * side), 0x907E1E00)
            self.call("CFArrayCreate", [0, self.ptr(0x1E00), 1, array], side)
            p.nonzero("the standard retain callback constructs a CFArray")
            p.emit(0x907E1E10)
            self.call("CFArrayGetValueAtIndex", [self.slot(0x1E10), 0], side)
            p.emit(0x809E0000 | (string[1] + 4 * side))
            p.equal("the array retains the original CF object")
            self.call("CFGetRetainCount", [string], side)
            p.expect(2, "the data record supplies the retain callback")
            self.call("CFRelease", [self.slot(0x1E10)])
            self.call("CFGetRetainCount", [string], side)
            p.expect(1, "the data record supplies the release callback")
            self.invoke_collection_callback(array, 4, [0, string], side)
            p.emit(0x809E0000 | (string[1] + 4 * side))
            p.equal("the retain descriptor returns its input object")
            self.call("CFGetRetainCount", [string], side)
            p.expect(2, "the retain descriptor acquires an ownership reference")
            self.invoke_collection_callback(array, 8, [0, string], side)
            self.call("CFGetRetainCount", [string], side)
            p.expect(1, "the release descriptor balances the retain")
            self.invoke_collection_callback(array, 12, [string], side)
            p.nonzero("the data record supplies an owned CF description")
            p.emit(0x907E1E10)
            self.call("CFRelease", [self.slot(0x1E10)])
            self.invoke_collection_callback(array, 16, [string, string], side)
            p.expect(1, "the data record supplies CF equality")
            dictionary = self.callback_pairs["kCFTypeDictionaryKeyCallBacks"]
            self.invoke_collection_callback(dictionary, 20, [string], side)
            p.emit(0x907E1E10)
            self.call("CFHash", [string], side)
            p.emit(0x809E1E10)
            p.equal("the data record uses the CF object hash")
        p.emit(
            0x807E0000 | string[1],
            0x907E1C00,
            0x807E0000 | (string[1] + 4),
            0x907E1D00,
        )
        both = self.factory("CFArrayCreate", [0, ("output", 0x1C00), 1, array])
        self.release(both)
        self.release(string)

        mutable = self.factory("CFStringCreateMutable", [0, 0])
        self.mutate("CFStringAppendCString", [mutable, raw, 0])
        copy = self.callback_pairs["kCFCopyStringSetCallBacks"]
        for side in (0, 1):
            self.invoke_collection_callback(copy, 4, [0, mutable], side)
            p.nonzero("copy-string callback makes an owned immutable object")
            p.emit(0x907E1E10, 0x809E0000 | (mutable[1] + side * 4), 0x7C632278)
            p.nonzero("copy-string callback does not alias a mutable string")
            self.call("CFStringAppendCString", [mutable, self.raw(b"!\0"), 0], side)
            self.call("CFStringGetLength", [self.slot(0x1E10)], side)
            p.expect(len(b"collection callback"), "later mutation leaves the retained string unchanged")
            self.call("CFRelease", [self.slot(0x1E10)])
        self.release(mutable)

    def hashes(self):
        vector = self.raw(struct.pack(">3I", 1, 2, 1))
        dictionary_values = self.raw(struct.pack(">3I", 100, 200, 300))
        for family in ("CFSet", "CFBag", "CFDictionary"):
            dictionary = family == "CFDictionary"
            self.scalar(family + "GetTypeID")
            immutable = self.factory(
                family + "Create",
                [0, vector] + ([dictionary_values] if dictionary else []) + [3, 0] + ([0] if dictionary else []),
            )
            for capacity in (0, 32):
                collection = self.factory(family + "CreateMutable", [0, capacity, 0] + ([0] if dictionary else []))
                for key in (1, 2, 1):
                    self.mutate(family + "AddValue", [collection, key] + ([key * 100] if dictionary else []))
                self.scalar(family + "GetCount", [collection], 3 if family == "CFBag" else 2)
                if dictionary:
                    self.scalar(family + "GetCountOfKey", [collection, 1], 1)
                    self.scalar(family + "ContainsKey", [collection, 99], 0)
                self.scalar(
                    family + "GetCountOfValue",
                    [collection, 100 if dictionary else 1],
                    1 if dictionary or family == "CFSet" else 2,
                )
                self.scalar(family + "ContainsValue", [collection, 200 if dictionary else 2], 1)
                self.scalar(family + "GetValue", [collection, 2], 200 if dictionary else 2)
                self.output(family + "GetValueIfPresent", [collection, 1, ("output", 0x1C00)], 4, True)
                self.scalar(family + "GetValueIfPresent", [collection, 99, 0], 0)
                if dictionary:
                    self.output(family + "GetKeysAndValues", [collection, ("output", 0x1C20), ("output", 0x1C00)], 8)
                    for i in range(8):
                        self.p.emit(0x887E1C20 | i, 0x889E1D20 | i)
                        self.p.equal("dictionary key output agrees")
                else:
                    self.output(family + "GetValues", [collection, ("output", 0x1C00)], 12 if family == "CFBag" else 8)
                self.reset_outputs()
                self.output(
                    family + "ApplyFunction",
                    [collection, self.ptr(0x1F08 if dictionary else 0x1F00), ("output", 0x1C00)],
                    4,
                )
                self.p.emit(0x807E1C00)
                self.p.expect(300 if dictionary else 4 if family == "CFBag" else 3, "collection callback sum")
                self.mutate(family + "ReplaceValue", [collection, 1] + ([999] if dictionary else []))
                self.mutate(family + "ReplaceValue", [collection, 99] + ([999] if dictionary else []))
                self.mutate(family + "SetValue", [collection, 3] + ([333] if dictionary else []))
                self.mutate(family + "SetValue", [collection, 2] + ([222] if dictionary else []))
                self.mutate(family + "RemoveValue", [collection, 1])
                self.mutate(family + "RemoveValue", [collection, 99])
                if not capacity:
                    for key in range(5, 100):
                        self.mutate(family + "AddValue", [collection, key] + ([key * 10] if dictionary else []))
                copy = self.factory(family + "CreateCopy", [0, collection])
                mutable = self.factory(family + "CreateMutableCopy", [0, capacity, copy])
                self.mutate(family + "RemoveAllValues", [mutable])
                self.scalar(family + "GetCount", [mutable], 0)
                self.release(mutable)
                self.release(copy)
                self.release(collection)
            self.release(immutable)
        # Distinct equal strings exercise ownership, hashing and equality.
        first = self.factory("CFStringCreateWithCString", [0, self.raw(b"key\0"), 0])
        equal = self.factory("CFStringCreateWithCString", [0, self.raw(b"key\0"), 0])
        members = {"CFSet": "Set", "CFBag": "Bag", "CFDictionary": "DictionaryKey"}
        values = self.callback_pairs["kCFTypeDictionaryValueCallBacks"]
        for family, member in members.items():
            dictionary = family == "CFDictionary"
            for prefix in ("kCFType", "kCFCopyString"):
                callback = self.callback_pairs[prefix + member + "CallBacks"]
                args = [0, 0, callback] + ([values] if dictionary else [])
                collection = self.factory(family + "CreateMutable", args)
                self.mutate(family + "AddValue", [collection, first] + ([equal] if dictionary else []))
                contains = "ContainsKey" if dictionary else "ContainsValue"
                self.scalar(family + contains, [collection, equal], 1)
                self.mutate(family + "ReplaceValue", [collection, equal] + ([first] if dictionary else []))
                self.mutate(family + "SetValue", [collection, first] + ([equal] if dictionary else []))
                self.scalar("CFGetRetainCount", [first])
                self.scalar("CFGetRetainCount", [equal])
                self.mutate(family + "RemoveAllValues", [collection])
                self.release(collection)
        self.release(equal)
        self.release(first)

    def trees(self):
        self.scalar("CFTreeGetTypeID")
        context = self.ptr(0x1EA0)
        for offset, value in ((0, 0), (4, 42)):
            struct.pack_into(">I", self.p.data, 0x1EA0 + offset, value)
        # Install the live application descriptor in the context at runtime.
        self.p.emit(0x387E1F20, 0x907E1EA8, 0x387E1F28, 0x907E1EAC, 0x38600000, 0x907E1EB0)
        root = self.factory("CFTreeCreate", [0, context], False)
        first = self.factory("CFTreeCreate", [0, context], False)
        second = self.factory("CFTreeCreate", [0, context], False)
        for side in (0, 1):
            self.call("CFTreeAppendChild", [root, first], side)
            self.call("CFTreePrependChild", [root, second], side)
        self.scalar("CFTreeGetChildCount", [root], 2)
        self.same_pointer("CFTreeGetParent", [first], root)
        self.same_pointer("CFTreeFindRoot", [second], root)
        self.same_pointer("CFTreeGetFirstChild", [root], second)
        self.same_pointer("CFTreeGetNextSibling", [second], first)
        self.same_pointer("CFTreeGetChildAtIndex", [root, 1], first)
        self.scalar("CFTreeGetChildAtIndex", [root, 2], 0)
        for side in (0, 1):
            self.call("CFTreeGetChildren", [root, ("output", 0x1C00)], side)
            for i, pair in enumerate((second, first)):
                self.p.emit(0x807E0000 | (0x1C00 + side * 256 + i * 4), 0x809E0000 | (pair[1] + side * 4))
                self.p.equal("ordered live tree child output")
        self.output("CFTreeGetContext", [root, ("output", 0x1C00)], 20)
        self.p.emit(0x807E1C04)
        self.p.expect(43, "tree context retain callback result")
        self.reset_outputs()
        self.output("CFTreeApplyFunctionToChildren", [root, self.ptr(0x1F30), ("output", 0x1C00)], 4)
        self.p.emit(0x807E1C00)
        self.p.expect(2, "tree child callback count")
        for side in (0, 1):
            self.call("CFTreeRemove", [second], side)
        self.scalar("CFTreeGetChildCount", [root], 1)
        for side in (0, 1):
            self.call("CFTreeRemoveAllChildren", [root], side)
        self.scalar("CFTreeGetChildCount", [root], 0)
        # Preserve the installed 1.4 sibling parent-link quirk, and isolate its
        # inconsistent list from the normal parent/child ownership scenario.
        anchor = self.factory("CFTreeCreate", [0, context], False)
        sibling = self.factory("CFTreeCreate", [0, context], False)
        for side in (0, 1):
            self.call("CFTreeInsertSibling", [anchor, sibling], side)
        self.same_pointer("CFTreeGetParent", [sibling], anchor)
        self.same_pointer("CFTreeGetNextSibling", [anchor], sibling)
        for side in (0, 1):
            self.call("CFTreeRemove", [sibling], side)
            self.p.emit(0x807E0000 | (anchor[1] + side * 4), 0x38800000, 0x9083000C)
        for pair in (sibling, anchor, second, first, root):
            self.release(pair)

    def tree_context_and_sort(self):
        p = self.p
        empty = self.raw(bytes(20))
        _, custom = self.raw(struct.pack(">5I", 0, 10, 0, 0, 0))
        p.emit(0x387E1F20, 0x907E0000 | (custom + 8), 0x387E1F38, 0x907E0000 | (custom + 12))
        high = self.raw(struct.pack(">5I", 0, 50, 0, 0, 0))
        p.emit(0x38600000, 0x907E1EE4, 0x907E1EE8)
        root = self.factory("CFTreeCreate", [0, empty], False)
        first = self.factory("CFTreeCreate", [0, high], False)
        second = self.factory("CFTreeCreate", [0, empty], False)
        equal = self.factory("CFTreeCreate", [0, high], False)
        for side in (0, 1):
            child = self.slot(second[1] + 4 * side)
            self.call("CFTreeSetContext", [child, self.ptr(custom)])
            self.call("CFTreeGetContext", [child, self.ptr(0x1C00)], side)
            p.emit(0x807E1C04)
            p.expect(11, "replacement retains context info in a formerly compact tree")
            p.emit(0x807E1C0C)
            p.pointer(0x1F38, "replacement exposes the matching guest release callback")
            for node in (first, second, equal):
                self.call("CFTreeAppendChild", [root, node], side)
            parent = self.slot(root[1] + 4 * side)
            self.call("CFTreeSortChildren", [parent, self.ptr(0x1F40), self.ptr(0x1EE8)])
            for index, node in enumerate((second, first, equal)):
                self.call("CFTreeGetChildAtIndex", [root, index], side)
                p.emit(0x809E0000 | (node[1] + 4 * side))
                p.equal("sort updates sibling links and preserves order for equal keys")
                self.call("CFTreeGetParent", [node], side)
                p.emit(0x809E0000 | (root[1] + 4 * side))
                p.equal("sort preserves the live parent link")
            self.call("CFTreeSetContext", [child, empty])
            self.call("CFTreeGetContext", [child, self.ptr(0x1C00)], side)
            p.emit(0x807E1C04)
            p.expect(0, "replacing a custom context can clear its info")
        p.emit(0x807E1EE4)
        p.expect(2, "context replacement calls each previous release exactly once")
        p.emit(0x807E1EE8)
        p.nonzero("sort passes the caller context into the guest comparator")
        for item in (equal, second, first, root):
            self.release(item)

    def storage(self):
        if self.library != "CoreFoundationLib":
            return
        self.scalar("CFStorageGetTypeID")
        storage = self.factory("CFStorageCreate", [0, 4], False)
        for side in (0, 1):
            self.call("CFStorageInsertValues", [storage, 0, 50000], side)
        self.scalar("CFStorageGetCount", [storage], 50000)
        data = self.raw(struct.pack(">4I", 1, 2, 3, 4))
        for location in (0, 17000, 49996):
            for side in (0, 1):
                self.call("CFStorageReplaceValues", [storage, location, 4, data], side)
            self.output("CFStorageGetValues", [storage, location, 4, ("output", 0x1C00)], 16)
            for side in (0, 1):
                self.call("CFStorageGetValueAtIndex", [storage, location + 1, ("output", 0x1C00)], side)
                self.p.nonzero("storage live cache-miss/hit element")
            for i in range(8):
                self.p.emit(0x887E1C00 | i, 0x889E1D00 | i)
                self.p.equal("storage cached segment range")
            self.reset_outputs()
            self.output("CFStorageApplyFunction", [storage, location, 4, self.ptr(0x1F10), ("output", 0x1C00)], 4)
            self.p.emit(0x807E1C00)
            self.p.expect(10, "storage callback dereferences valid element addresses")
        for side in (0, 1):
            self.call("CFStorageDeleteValues", [storage, 2, 49998], side)
        self.scalar("CFStorageGetCount", [storage], 2)
        self.output("CFStorageGetValues", [storage, 0, 2, ("output", 0x1C00)], 8)
        for side in (0, 1):
            self.call("CFStorageDeleteValues", [storage, 0, 2], side)
        self.scalar("CFStorageGetCount", [storage], 0)
        self.release(storage)

    def runtime(self):
        if self.library != "CoreFoundationLib":
            return
        klass = self.ptr(0x1E40)
        self.p.emit(0x387E1F18, 0x907E1E48)
        ids = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.call("_CFRuntimeRegisterClass", [klass], side)
            self.p.nonzero("register live runtime class")
            self.p.emit(0x907E0000 | (ids + side * 4))
        type_ids = self.pair_slot(ids)
        for side in (0, 1):
            self.call("_CFRuntimeGetClassWithTypeID", [type_ids], side)
            self.p.pointer(0x1E40, "retrieve the registered class record")
        instance = self.factory("_CFRuntimeCreateInstance", [0, type_ids, 4, 0], False)
        for side in (0, 1):
            self.p.emit(0x807E0000 | (instance[1] + side * 4), 0x80630008)
            self.p.expect(0x12345678, "new runtime instance invokes class initializer")
            self.call("_CFRuntimeSetInstanceTypeID", [instance, type_ids], side)
            self.call("_CFRuntimeSetInstanceTypeID", [instance, 0], side)
            self.call("CFGetTypeID", [instance], side)
            self.p.emit(0x809E0000 | (ids + side * 4))
            self.p.equal("valid type retained after invalid replacement")
        self.release(instance)

    def uuids(self):
        self.scalar("CFUUIDGetTypeID")
        uuid = self.factory("CFUUIDCreateFromUUIDBytes", [0, 0x00112233, 0x44556677, 0x8899AABB, 0xCCDDEEFF])
        self.output("CFUUIDGetUUIDBytes", [("output", 0x1C00), uuid], 16)
        for i, word in enumerate((0x00112233, 0x44556677, 0x8899AABB, 0xCCDDEEFF)):
            self.p.emit(0x807E1C00 | (i * 4))
            self.p.expect(word, "UUID hidden struct result word")
        text = self.factory("CFUUIDCreateString", [0, uuid], False)
        self.output("CFStringGetCharacters", [text, 0, 36, ("output", 0x1C00)], 72)
        self.equal_objects(text[1], "UUID string")
        self.scalar("CFStringGetLength", [text], 36)
        parsed = self.factory("CFUUIDCreateFromString", [0, text])
        self.equal_objects(parsed[1], "UUID parse/string round trip")
        for source in (b"{00112233-4455-6677-8899-aabbccddeeff}", b"00112233!4455!6677!8899!AABBCCDDEEFF trailing"):
            spelling = self.factory("CFStringCreateWithCString", [0, self.raw(source + b"\0"), 0])
            alternate = self.factory("CFUUIDCreateFromString", [0, spelling])
            self.output("CFUUIDGetUUIDBytes", [("output", 0x1C00), alternate], 16)
            self.release(alternate)
            self.release(spelling)
        short = self.factory("CFStringCreateWithCString", [0, self.raw(b"0\0"), 0])
        # The original reads uninitialized missing bytes; only check the native
        # bounded failure, never execute the unsafe original malformed path.
        self.call("CFUUIDCreateFromString", [0, short])
        self.p.expect(0, "short UUID text has no uninitialized result bytes")
        self.release(short)
        random = self.factory("CFUUIDCreate", [0], False)
        for side in (0, 1):
            self.call("CFUUIDGetUUIDBytes", [self.ptr(0x1C00), random], side)
            self.p.emit(0x807E1C00, 0x809E1C04, 0x7C632378, 0x809E1C08, 0x7C632378, 0x809E1C0C, 0x7C632378)
            self.p.nonzero("generated UUID has a nonzero payload")
        for pair in (random, parsed, text, uuid):
            self.release(pair)

    def property_lists(self):
        string = self.factory("CFStringCreateWithCString", [0, self.raw(b"list <value> & text\0"), 0])
        data = self.factory("CFDataCreate", [0, self.raw(b"abc\x00\xff"), 5])
        integer = self.raw(struct.pack(">i", -12345))
        number = self.factory("CFNumberCreate", [0, 9, integer])
        self.p.emit(0x38600000, 0x907E1C00, 0x907E1D00)
        date_value = self.raw(struct.pack(">d", -1234.5))
        date_slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.p.emit(0xC83E0000 | date_value[1])
            self.call("CFDateCreate", [0], side)
            self.p.nonzero("original floating-argument date source")
            self.p.emit(0x907E0000 | (date_slot + side * 4))
            self.call("CFDateGetAbsoluteTime", [self.slot(date_slot + side * 4)], side)
            self.p.emit(0xD83E1C00, 0x807E1C00)
            high, low = struct.unpack(">II", struct.pack(">d", -1234.5))
            self.p.expect(high, "native/original date frame FP result high word")
            self.p.emit(0x807E1C04)
            self.p.expect(low, "native/original date frame FP result low word")
        date = self.pair_slot(date_slot)
        array_slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.p.emit(0x807E0000 | (string[1] + side * 4), 0x907E1E00, 0x807E0000 | (data[1] + side * 4), 0x907E1E04)
            self.call("CFArrayCreate", [0, self.ptr(0x1E00), 2, self.constant("kCFTypeArrayCallBacks", False)], side)
            self.p.nonzero("original array source for deep copy")
            self.p.emit(0x907E0000 | (array_slot + side * 4))
        array = self.pair_slot(array_slot)
        dictionary = self.factory(
            "CFDictionaryCreateMutable",
            [
                0,
                0,
                self.constant("kCFTypeDictionaryKeyCallBacks", False),
                self.constant("kCFTypeDictionaryValueCallBacks", False),
            ],
        )
        inputs = [string, data, number, date, array, self.constant("kCFBooleanTrue")]
        keys = []
        for i, value in enumerate(inputs):
            key = self.factory("CFStringCreateWithCString", [0, self.raw(("key" + str(i)).encode() + b"\0"), 0])
            keys.append(key)
            self.mutate("CFDictionarySetValue", [dictionary, key, value])
        for options in (0, 1, 2):
            for source in inputs + [dictionary]:
                copy = self.factory("CFPropertyListCreateDeepCopy", [0, source, options])
                self.release(copy)
        xml = self.factory("CFPropertyListCreateXMLData", [0, dictionary])
        self.scalar("CFDataGetLength", [xml])
        parsed = self.factory("CFPropertyListCreateFromXMLData", [0, xml, 0, ("output", 0x1C00)])
        self.release(parsed)
        self.release(xml)
        self.release(dictionary)
        for pair in keys + [array, date, number, data, string]:
            self.release(pair)

    def preferences(self):
        # Unique application IDs keep this fixture's persistent preference data
        # separate. Every value written below is removed before the final sync.
        app_name = ("exec.cf." + self.fixture_tag[-8:]).encode()
        app = self.factory("CFStringCreateWithCString", [0, self.raw(app_name + b"\0"), 0])
        suite = self.factory("CFStringCreateWithCString", [0, self.raw(app_name + b".suite\0"), 0])
        key = self.factory("CFStringCreateWithCString", [0, self.raw(b"fixture value\0"), 0])
        user = self.constant("kCFPreferencesCurrentUser")
        host = self.constant("kCFPreferencesAnyHost")
        for text, boolean, integer, valid_boolean, valid_integer in (
            (b"true", 1, 0, 1, 0),
            (b"NO", 0, 0, 1, 0),
            (b"42", 0, 42, 0, 1),
            (b"42x", 0, 0, 0, 0),
        ):
            value = self.factory("CFStringCreateWithCString", [0, self.raw(text + b"\0"), 0])
            # Verify each write with the original reader before the other writer
            # runs, so a no-op native setter cannot be hidden by the oracle.
            for side in (0, 1):
                self.call("CFPreferencesSetAppValue", [key, value, app], side)
                self.call("CFPreferencesCopyAppValue", [key, app], 1)
                self.p.nonzero("original preference reader sees the preceding writer")
                self.p.emit(0x907E1E20)
                self.call("CFEqual", [self.slot(0x1E20), value], side)
                self.p.expect(1, "preference setter publishes the expected value")
                self.call("CFRelease", [self.slot(0x1E20)])
            self.output("CFPreferencesGetAppBooleanValue", [key, app, ("output", 0x1C00)], 1, True)
            self.p.emit(0x807E2000)
            self.p.expect(boolean, "typed app boolean result")
            self.p.emit(0x887E1C00)
            self.p.expect(valid_boolean, "typed app boolean validity")
            if self.library == "CoreFoundationLib":
                self.output("CFPreferencesAppBooleanValue", [key, app, ("output", 0x1C00)], 1, True)
            self.output("CFPreferencesGetAppIntegerValue", [key, app, ("output", 0x1C00)], 1, True)
            self.p.emit(0x807E2000)
            self.p.expect(integer, "typed app integer result")
            self.p.emit(0x887E1C00)
            self.p.expect(valid_integer, "typed app integer validity")
            self.release(value)
        number = self.factory("CFNumberCreate", [0, 9, self.raw(struct.pack(">i", 7))])
        for value in (number, self.constant("kCFBooleanTrue")):
            self.call("CFPreferencesSetAppValue", [key, value, app])
            self.output("CFPreferencesGetAppBooleanValue", [key, app, ("output", 0x1C00)], 1, True)
            self.p.emit(0x807E2000)
            self.p.expect(1, "typed numeric/boolean preference")
            self.output("CFPreferencesGetAppIntegerValue", [key, app, ("output", 0x1C00)], 1, True)
        self.release(number)
        self.scalar("CFPreferencesAppSynchronize", [app])
        for side in (0, 1):
            self.call("CFPreferencesSetValue", [key, self.constant("kCFBooleanTrue"), app, user, host], side)
            self.call("CFPreferencesCopyValue", [key, app, user, host], 1)
            self.p.nonzero("original domain reader sees native/original setter")
            self.p.emit(0x907E1E20)
            self.call("CFRelease", [self.slot(0x1E20)])
        copied = self.factory("CFPreferencesCopyValue", [key, app, user, host])
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
        self.mutate("CFDictionarySetValue", [dictionary, key, self.constant("kCFBooleanTrue")])
        for side in (0, 1):
            self.call("CFPreferencesSetMultiple", [dictionary, 0, app, user, host], side)
        copied = self.factory("CFPreferencesCopyMultiple", [0, app, user, host])
        self.release(copied)
        keys = self.factory("CFPreferencesCopyKeyList", [app, user, host])
        copied = self.factory("CFPreferencesCopyMultiple", [keys, app, user, host])
        self.release(copied)
        for side in (0, 1):
            self.call("CFPreferencesSetMultiple", [0, keys, app, user, host], side)
        self.scalar("CFPreferencesCopyValue", [key, app, user, host], 0)
        self.scalar("CFPreferencesSynchronize", [app, user, host])
        # App-list enumeration can legitimately return NULL for an unavailable
        # original host preference directory. Compare the presence first.
        app_list_slot = self.next_slot
        self.next_slot += 8
        for side in (0, 1):
            self.call("CFPreferencesCopyApplicationList", [user, host], side)
            self.p.emit(
                0x907E0000 | (app_list_slot + side * 4),
                0x2C030000,
                0x38600000,
                0x41820008,
                0x38600001,
                0x907E0000 | (0x1E24 + side * 4),
            )
        self.p.emit(0x807E1E24, 0x809E1E28)
        self.p.equal("original/native app-list availability")
        self.p.emit(0x807E1E24, 0x2C030000)
        branch = len(self.p.code)
        self.p.emit(0)
        self.equal_objects(app_list_slot, "preference application list")
        self.release(self.pair_slot(app_list_slot))
        self.p.code[branch] = 0x41820000 | ((len(self.p.code) - branch) * 4)
        self.call("CFPreferencesSetAppValue", [key, 0, app])
        for side in (0, 1):
            self.call("CFPreferencesSetValue", [key, self.constant("kCFBooleanTrue"), suite, user, host], side)
            self.call("CFPreferencesAddSuitePreferencesToApp", [app, suite], side)
            self.call("CFPreferencesCopyAppValue", [key, app], side)
            self.p.nonzero("suite value found after adding suite")
            self.p.emit(0x907E1E20)
            self.call("CFRelease", [self.slot(0x1E20)])
            self.call("CFPreferencesRemoveSuitePreferencesFromApp", [app, suite], side)
            self.call("CFPreferencesCopyAppValue", [key, app], side)
            self.p.expect(0, "suite value absent after removal")
            self.call("CFPreferencesSetValue", [key, 0, suite, user, host], side)
        self.call("CFPreferencesSetAppValue", [key, 0, app])
        self.scalar("CFPreferencesAppSynchronize", [app])
        self.scalar("CFPreferencesSynchronize", [suite, user, host])
        for pair in (keys, dictionary, key, suite, app):
            self.release(pair)

    def finish(self, destination):
        missing = self.cohort - self.used
        if missing:
            raise ValueError("entries without a native scenario: " + ", ".join(sorted(missing)))
        self.call("CloseConnection", [self.ptr(0x2100)])
        self.p.expect(0, "close original collection oracle")
        label = "Carbon" if self.library == "CarbonLib" else "Core"
        application = "PPC " + label + " Coll Real Probe"
        self.p.finish(destination, application, carbon=True)
        self.write_coverage(destination, application, self.cohort)
        (destination / "native-symbols.json").write_text(json.dumps(sorted(self.used), indent=2) + "\n")


def make_probe(destination, library, original_only=False):
    destination.mkdir(parents=True, exist_ok=True)
    probe = CollectionsProbe(library, original_only)
    probe.fixture_tag = destination.name.replace(".", "-")
    probe.section("Shared collection callback data records", probe.callback_data)
    probe.section("Dictionaries, sets and bags", probe.hashes)
    probe.section("Trees", probe.trees)
    if probe.tree_mutators:
        probe.section("Tree context replacement and sorting", probe.tree_context_and_sort)
    probe.section("Storage", probe.storage)
    probe.section("Runtime classes", probe.runtime)
    probe.section("UUIDs", probe.uuids)
    probe.section("Property lists", probe.property_lists)
    probe.section("Preferences", probe.preferences)
    probe.finish(destination)


def make_guard_probe(destination, mode):
    """Original synthetic exact-profile PEFs stop before unsafe worker calls."""
    destination.mkdir(parents=True, exist_ok=True)
    write_carbon_fallback(destination)
    name = {
        "class-extent": "_CFRuntimeGetClassWithTypeID",
        "hash-capacity": "CFSetAddValue",
        "storage-zero-size": "CFStorageGetCount",
        "uuid-output": "CFUUIDGetUUIDBytes",
        "tree-parent": "CFTreeAppendChild",
        "hash-extent": "CFDictionaryCreate",
    }[mode]
    row = next(row for row in ROWS if row["name"] == name)
    code = [0] * (0x5DEE0 // 4)
    data = bytearray(0x72A8)
    code[row["code_offset"] // 4 : row["code_offset"] // 4 + 2] = [0x3860007B, 0x4E800020]
    code[0x100 // 4 : 0x100 // 4 + 2] = [0x38600049, 0x4E800020]
    struct.pack_into(">4I", data, 0, row["code_offset"], 0, 0x100, 0)
    write_fragment(
        destination,
        "CoreFoundationLib",
        pef(
            code,
            data,
            exports=[(name, 2, 0, 1), ("CollectionsGuardToken", 2, 8, 1)],
            reloc=[0xA000, 0, 0x4600, 0xA000, 8, 0x4600],
        ),
        0,
    )
    p = Probe([("CoreFoundationLib", name, 2), ("CoreFoundationLib", "CollectionsGuardToken", 2)])
    p.call("CollectionsGuardToken")
    p.expect(73, "synthetic exact-profile collection guard")
    if mode == "class-extent":
        p.emit(0x3860FFFF)
    elif mode == "hash-capacity":
        struct.pack_into(">5I", p.data, 0x300, 0, 12, 0, 1, 1)
        p.emit(0x387E0300, 0x38800002)
    elif mode == "storage-zero-size":
        p.emit(0x387E0300)
    elif mode == "uuid-output":
        p.emit(0x3860FFF0, 0x389E0300)
    elif mode == "tree-parent":
        p.emit(0x387E0400, 0x389E0300, 0x907E0308)
    else:
        p.emit(0x38600000, 0x38800000, 0x38A00000, 0x38C0FFFF, 0x38E00000, 0x39000000)
    p.call(name)
    p.emit(0)
    p.finish(destination, "PPC Collections Guard Probe")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument(
        "mode",
        nargs="?",
        choices=(
            "foreign",
            "class-extent",
            "hash-capacity",
            "storage-zero-size",
            "uuid-output",
            "tree-parent",
            "hash-extent",
        ),
        default="foreign",
    )
    arguments = parser.parse_args()
    if arguments.mode != "foreign":
        make_guard_probe(arguments.destination, arguments.mode)
    elif arguments.real:
        make_probe(arguments.destination, arguments.library)
    else:
        rows = [row for row in ROWS if arguments.library != "CarbonLib" or not row["companion_only"]]
        make_foreign_probe(arguments.destination, arguments.library, rows, "Coll")


if __name__ == "__main__":
    main()
