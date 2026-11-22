#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_pascal_probe.py
#
#   Summary:   PPC Pascal string ABI checks, optionally compared with a local StdCLib oracle.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_pascal_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""PPC Pascal string ABI checks, optionally compared with a local StdCLib oracle.

The default fixture contains only original code. --reference accepts the ignored
System 9 data-StdCLib disassembly directory and builds an ignored local oracle;
reference bytes are never embedded in this generator or committed fixtures.
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

NAMES = [
    "PLstrlen",
    "PLstrcmp",
    "PLstrncmp",
    "PLstrcpy",
    "PLstrncpy",
    "PLstrcat",
    "PLstrncat",
    "PLstrchr",
    "PLstrrchr",
    "PLstrpbrk",
    "PLstrspn",
    "PLstrstr",
    "PLpos",
]


def write_reference(destination, reference):
    metadata = json.loads((reference / "pef.json").read_text())
    offsets = {item["name"]: item["code_offset"] for item in metadata["exports"] if item["name"] in NAMES}
    if len(offsets) != len(NAMES) or min(offsets.values()) != 0x10EA8 or max(offsets.values()) != 0x112A8:
        raise ValueError("Expected the local System 9 StdCLib Pascal-string code range")
    start, end = 0x10EA8, 0x11334
    payload = (reference / "section-0.bin").read_bytes()[start:end]
    if len(payload) != end - start:
        raise ValueError("Truncated reference code")
    words = list(struct.unpack(f">{len(payload) // 4}I", payload))
    data = b"".join(struct.pack(">II", offsets[name] - start, 0) for name in NAMES)
    exports = [(name, 2, i * 8, 1) for i, name in enumerate(NAMES)]
    image = pef(words, data, exports=exports, reloc=[0x4600 | (len(NAMES) - 1)])
    write_fragment(destination, "PascalReference", image, 0)


def make_pascal_probe(destination, reference=None, native_library="CarbonLib"):
    write_carbon_fallback(destination)
    if reference:
        write_reference(destination, reference)
    imports = [(native_library, name, 2) for name in NAMES] + [("CarbonLib", "CarbonFallbackToken", 2)]
    if reference:
        imports += [("PascalReference", name, 2) for name in NAMES]
    code, data, checks = [], bytearray(4096), []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        value &= 0xFFFFFFFF
        emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def equal(label):
        checks.append({"code_offset": len(code) * 4, "check": label})
        emit(0x7C032000, 0x41820008, 0)

    def check(value, label):
        constant(4, value)
        equal(label)

    def call(library, name):
        index = imports.index((library, name, 2))
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def query(name, first, second=None, number=None, expected=0, pointer=False):
        for library in [native_library, "PascalReference"] if reference else [native_library]:
            emit(0x387E0000 | first)
            if second is not None:
                emit(0x389E0000 | second)
                if number is not None:
                    constant(5, number)
            elif number is not None:
                constant(4, number)
            call(library, name)
            if pointer and expected:
                emit(0x389E0000 | expected)
                equal(f"{library}:{name} pointer")
            else:
                check(expected, f"{library}:{name} result")

    def initialize(offset, payload):
        guarded = b"\xa5" * 4 + payload + b"\xa5" * 4
        for i in range(0, len(guarded), 4):
            constant(3, int.from_bytes(guarded[i : i + 4], "big"))
            emit(0x907E0000 | (offset - 4 + i))

    def modify(name, initial, source, limit=None, self_source=False):
        initialize(512, initial)
        initialize(1536, source)
        libraries = [native_library, "PascalReference"] if reference else [native_library]
        for index, library in enumerate(libraries):
            output = 512 + index * 512
            if index:
                initialize(output, initial)
            emit(0x387E0000 | output, 0x389E0000 | (output if self_source else 1536))
            if limit is not None:
                constant(5, limit)
            call(library, name)
            emit(0x389E0000 | output)
            equal(f"{library}:{name} destination result")
        if reference:
            for delta in range(-4, 260, 4):
                emit(0x807E0000 | (512 + delta), 0x809E0000 | (1024 + delta))
                equal(f"{name} oracle bytes {delta} len={initial[0]}/{source[0]} cap={limit}")
        else:
            expected = bytearray(initial)
            cap = 255 if limit is None else max(0, min(255, limit))
            amount = min(source[0], cap)
            if name in ("PLstrcat", "PLstrncat"):
                amount = min(amount, 255 - initial[0])
                expected[initial[0] + 1 : initial[0] + 1 + amount] = source[1 : 1 + amount]
                expected[0] += amount
            elif cap:
                expected[1 : 1 + amount] = source[1 : 1 + amount]
                if name == "PLstrncpy":
                    expected[1 + amount : 1 + cap] = bytes(cap - amount)
                    if initial[0] < cap or source[0] < cap:
                        expected[0] = amount
                else:
                    expected[0] = amount
            guarded = b"\xa5" * 4 + expected + b"\xa5" * 4
            for i in range(0, len(guarded), 4):
                emit(0x807E0000 | (508 + i))
                check(int.from_bytes(guarded[i : i + 4], "big"), f"{name} expected bytes {i}")

    def string(length, multiplier=13):
        return bytes([length]) + bytes((i * multiplier) & 255 for i in range(1, length + 1)) + b"\xa5" * (255 - length)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    data[2048:2055] = bytes([6, 0, 255, 128, 0, 255, 128])
    data[2080:2084] = bytes([3, 255, 128, 0])
    data[2112:2115] = bytes([2, 0, 255])
    data[2144:2148] = bytes([3, 0, 255, 128])
    query("PLstrlen", 2048, expected=6)
    query("PLstrcmp", 2048, 2144, expected=3)
    query("PLstrcmp", 2144, 2048, expected=-3)
    query("PLstrncmp", 2048, 2144, number=3, expected=0)
    query("PLstrncmp", 2048, 2144, number=4, expected=3)
    query("PLstrncmp", 2144, 2048, number=-32768, expected=0)
    query("PLstrchr", 2048, number=0, expected=2049, pointer=True)
    query("PLstrchr", 2048, number=0xFF80, expected=2051, pointer=True)
    query("PLstrrchr", 2048, number=0, expected=2052, pointer=True)
    query("PLstrchr", 2048, number=0x77)
    query("PLstrrchr", 2048, number=0x77)
    query("PLstrpbrk", 2048, 2080, expected=2049, pointer=True)
    query("PLstrspn", 2048, 2112, expected=2)
    query("PLstrstr", 2048, 2080, expected=2050, pointer=True)
    query("PLpos", 2080, 2048, expected=2)
    for name in ("PLstrpbrk", "PLstrspn", "PLstrstr"):
        query(name, 2048, 2176)
    query("PLpos", 2176, 2048)
    query("PLpos", 2048, 2080)
    modify("PLstrcpy", string(7), string(3))
    modify("PLstrcat", string(254), string(255))
    modify("PLstrcat", string(6), string(6), self_source=True)
    for name in ("PLstrncpy", "PLstrncat"):
        lengths = (0, 1, 3, 7, 254, 255) if reference else (0, 3, 255)
        destinations = (0, 1, 3, 7, 127, 255) if reference else (0, 7, 255)
        limits = (-32768, -1, 0, 1, 2, 3, 7, 254, 255, 256, 32767) if reference else (-1, 2, 7, 32767)
        for source_length in lengths:
            for destination_length in destinations:
                for limit in limits:
                    modify(name, string(destination_length, 7), string(source_length), limit)
    call("CarbonLib", "CarbonFallbackToken")
    check(73, "initialized fallback")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    image = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    app = "PPC Carbon Pascal Probe" if native_library == "CarbonLib" else "PPC StdCLib Pascal Probe"
    write_fragment(destination, app, image, 1)
    (destination / "checks.json").write_text(json.dumps(checks, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--library", choices=("CarbonLib", "StdCLib"), default="CarbonLib")
    args = parser.parse_args()
    make_pascal_probe(args.destination, args.reference, args.library)
