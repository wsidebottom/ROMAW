#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_classic_access_probe.py
#
#   Summary:   PPC ABI checks for 185 low-memory and classic record accessors.
#
#   Classes:   None declared.
#
#   Functions: make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""PPC ABI checks for 185 low-memory and classic record accessors.

Live low-memory setters receive the current value, so probes never replace the
emulator's actual zone, heap, callback or application-name state with test data.
Malformed values, signed truncation and aliases use the isolated native fixture.
Record checks use application-owned synthetic records, not live UI objects.
"""
import argparse
import json
from pathlib import Path
from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_probe(destination, real=False, mode="valid", *, reference_only=False):
    destination.mkdir(parents=True, exist_ok=True)
    rows = json.loads((Path(__file__).resolve().parents[2] / "carbonlib/tests/classic_access_cases.json").read_text())
    if not real:
        write_carbon_fallback(destination)
    imports = [("CarbonLib", row["name"], 2) for row in rows]
    if real:
        # Import a saved export so CarbonLib initializes before the app runs.
        # Resolving this import does not invoke the text or clipboard API.
        imports.append(("CarbonLib", "TXNPaste", 2))
    imports += (
        [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        if real
        else [("CarbonLib", "CarbonFallbackToken", 2)]
    )
    p = Probe(imports)
    p.data.extend(bytes(32768 - len(p.data)))
    p.data[0x1800:0x1840] = b"\xa5" * 64

    def immediate(register, value):
        value &= 0xFFFFFFFF
        p.emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 65535)

    def store(offset, width, value):
        immediate(3, value)
        p.emit({1: 0x987E0000, 2: 0xB07E0000, 4: 0x907E0000}[width] | offset)

    def load(offset, width, absolute=False, signed=False, register=3):
        opcode = {1: 0x88000000, 2: 0xA8000000 if signed else 0xA0000000, 4: 0x80000000}[width]
        p.emit(opcode | register << 21 | (0 if absolute else 30 << 16) | offset)

    if mode != "valid":
        if real:
            raise ValueError("Fault fixtures use original synthetic fallback only")
        if mode == "bad-handle":
            immediate(3, 0xFFFFFFFF)
            p.call("GetListPort")
        elif mode == "bad-master":
            store(0x1000, 4, 0xFFFFFFF0)
            p.emit(0x387E1000)
            p.call("GetListPort")
        elif mode == "bad-output":
            p.emit(0x387E1100, 0x907E1000, 0x387E1000)
            immediate(4, 0xFFFFFFFF)
            p.call("GetListViewBounds")
        elif mode == "bad-copy":
            immediate(3, 0xFFFFFFF0)
            p.call("LMSetCurApName")
        elif mode == "bad-index":
            p.emit(0x38608000)
            p.call("LMGetLvl2DT")
        else:
            raise ValueError(mode)
        p.emit(0)
        p.finish(destination, "PPC Carbon Classic185 Probe")
        return

    def unchanged(address, width, name, arguments):
        load(address, width, True)
        p.emit(0x907E1900)
        p.emit(*arguments)
        call(name)
        load(address, width, True)
        p.emit(0x809E1900)
        p.equal("live low-memory state preserved: " + name)

    if real:
        p.data[0x2200:0x220A] = b"\x09CarbonLib"
        p.emit(0x387E2200, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE2100, 0x38FE2104, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "open original CarbonLib oracle")
        for index, entry in enumerate(rows):
            offset = 0x2800 + index * 64
            name = entry["name"].encode()
            assert len(name) < 64
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x807E2100, 0x389E0000 | offset, 0x38BE0000 | (0x2400 + index * 4), 0x38DE2108)
            p.call("FindSymbol")
            p.expect(0, "resolve original " + entry["name"])
            p.emit(0x887E2108)
            p.expect(2, "original export is a PPC descriptor")
            p.emit(0x807E0000 | (0x2400 + index * 4), 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("oracle entry is original PPC code, not a native service")

    setup_checks = len(p.checks)

    def call_saved(index):
        p.emit(
            0x819E0000 | (0x2400 + index * 4),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
            0x7FC2F378,
        )

    def call(name):
        if not real:
            p.call(name)
            return
        assert name == row["name"]
        index = next(i for i, entry in enumerate(rows) if entry["name"] == name)
        if reference_only:
            call_saved(index)
            return
        # Preserve arguments before either execution. The native result is
        # recorded before the original runs, so the oracle cannot conceal a
        # wrong native write by replacing it with the expected bytes.
        p.emit(0x907E1D10, 0x909E1D14)
        p.call(name)
        p.emit(0x907E1D00)
        kind = row["kind"]
        address, size, absolute = 0, 0, False
        if row["family"] == "lowmem":
            absolute = True
            if name.startswith("LMSet"):
                address = row["address"]
                size = row.get("size", {"stb": 1, "sth": 2, "stw": 4}.get(kind, 4))
                if kind == "index_set":
                    size = 32
            elif kind == "rgb_out":
                address, size, absolute = 0x1800, 14, False
        elif kind in ("set", "copy_in", "popup_set"):
            address = (0x1500 if kind == "popup_set" else 0x1100) + row["offset"]
            size = row["width"]
        elif kind in ("copy_out", "title"):
            address, size = 0x17FC, (8 if kind == "title" else row["width"]) + 8
        for byte in range(size):
            load(address + byte, 1, absolute)
            p.emit(0x987E1E00 | byte)
        p.emit(0x807E1D10, 0x809E1D14)
        call_saved(index)
        if not name.startswith(("LMSet", "Set")):
            p.emit(0x809E1D00)
            p.equal(name + " exact original/native register result")
        for byte in range(size):
            load(address + byte, 1, absolute)
            load(0x1E00 + byte, 1, register=4)
            p.equal(name + " original/native byte " + str(byte))
        p.emit(0x807E1D00)

    value = 0x8123F4A5
    for row in rows:
        name, kind = row["name"], row["kind"]
        label = name + " ABI"
        if row["family"] == "lowmem":
            address = row["address"]
            width = {"lha": 2, "lbz": 1, "lwz": 4, "stb": 1, "sth": 2, "stw": 4}.get(kind)
            if kind in ("lha", "lbz", "lwz"):
                load(address, width, True, kind == "lha")
                p.emit(0x907E1900)
                call(name)
                p.emit(0x809E1900)
                p.equal(label + " live value matches direct PPC load")
            elif kind in ("stb", "sth", "stw"):
                load(address, width, True)
                p.emit(0x907E1900)
                call(name)
                load(address, width, True)
                p.emit(0x809E1900)
                p.equal(label + " same-value write")
            elif kind == "address":
                call(name)
                p.expect(address, label + " raw guest address")
            elif kind == "copy_in":
                for index in range(0, row["size"], 4):
                    load(address + index, 4, True)
                    p.emit(0x907E1C00 | index)
                p.emit(0x38600000 | address)
                call(name)
                for index in range(0, row["size"], 4):
                    load(address + index, 4, True)
                    p.emit(0x809E1C00 | index)
                    p.equal(label + " fixed-size self-copy")
            elif kind == "rgb_out":
                load(address, 4, True)
                p.emit(0x907E1900)
                load(address + 4, 2, True)
                p.emit(0x907E1904, 0x387E1804)
                call(name)
                p.pointer(0x1804, label + " preserved result pointer")
                for offset, width, expected in ((0x1804, 4, 0x1900), (0x1808, 2, 0x1904)):
                    load(offset, width)
                    p.emit(0x809E0000 | expected)
                    p.equal(label + " RGB bytes")
                load(0x1800, 4)
                p.expect(0xA5A5A5A5, label + " before guard")
                load(0x180A, 1)
                p.expect(0xA5, label + " after guard")
            elif kind == "rgb_in":
                for offset, width in ((0, 4), (4, 2)):
                    unchanged(address + offset, width, name, (0x38600000 | address,))
            elif kind in ("index_get", "index_set"):
                for index in range(8):
                    load(address + index * 4, 4, True)
                    p.emit(0x907E1900)
                    if kind == "index_get":
                        p.emit(0x38600000 | index)
                    else:
                        p.emit(0x38800000 | index)
                    call(name)
                    if kind == "index_set":
                        load(address + index * 4, 4, True)
                    p.emit(0x809E1900)
                    p.equal(label + " indexed table entry")
            else:
                raise ValueError(row)
            continue

        # Fresh handle -> record -> data handle -> data record links on each
        # call, independent of earlier setters and deliberately opaque values.
        p.emit(0x387E1100, 0x907E1000, 0x387E1400, 0x907E111C, 0x387E1500, 0x907E1400)
        offset, width = row["offset"], row["width"]
        address = (0x1500 if kind.startswith("popup_") else 0x1100) + offset
        if kind in ("get", "nonzero", "popup_get", "direct_get", "focus"):
            store(address, width, value)
            p.emit(0x387E1100 if kind in ("direct_get", "focus") else 0x387E1000)
            call(name)
            expected = value & ((1 << (width * 8)) - 1)
            if row["signed"] and expected & (1 << (width * 8 - 1)):
                expected -= 1 << (width * 8)
            if kind == "nonzero":
                expected = int(expected != 0)
            elif kind == "focus":
                expected = -1
            p.expect(expected, label + " raw pointer/signed value")
        elif kind in ("set", "popup_set"):
            immediate(4, value)
            p.emit(0x387E1000)
            call(name)
            load(address, width)
            p.expect(value & ((1 << (width * 8)) - 1), label + " field truncation")
        elif kind in ("copy_in", "copy_out"):
            for delta in range(0, width, 4):
                store((0x1800 if kind == "copy_in" else address) + delta, 4, value ^ delta)
            p.emit(0x387E1000, 0x389E1800)
            call(name)
            if kind == "copy_out":
                p.pointer(0x1800, label + " output pointer")
            for delta in range(0, width, 4):
                load((address if kind == "copy_in" else 0x1800) + delta, 4)
                p.expect(value ^ delta, label + " packed record copy")
        elif kind == "identity":
            p.emit(0x387E1100)
            call(name)
            p.pointer(0x1100, label)
        elif kind == "title":
            for index, byte in enumerate(b"\x05Title"):
                store(address + index, 1, byte)
            store(0x1800, 4, 0xA5A5A5A5)
            store(0x1804, 4, 0xA5A5A5A5)
            p.emit(0x387E1000, 0x389E1800)
            call(name)
            p.pointer(0x1800, label + " title pointer")
            load(0x1800, 4)
            p.expect(0x05546974, label + " title prefix")
            load(0x1804, 4)
            p.expect(0x6C65A5A5, label + " title suffix and guard")
        else:
            raise ValueError(row)
        if row["guard"]:
            p.emit(0x38600000, 0x38800000)
            call(name)
            if kind in ("get", "popup_get", "focus"):
                p.expect(-1 if kind == "focus" else 0, label + " NULL handle")
    accessor_checks = len(p.checks) - setup_checks
    if real:
        p.emit(0x387E2100)
        p.call("CloseConnection")
        p.expect(0, "close original CarbonLib oracle")
    else:
        p.call("CarbonFallbackToken")
        p.expect(73, "original PEF fallback remains initialized")
    application = "PPC Carbon Classic Real Probe" if real else "PPC Carbon Classic185 Probe"
    p.finish(destination, application, carbon=real)
    if real:
        coverage = {
            "application": application,
            "assertions": len(p.checks),
            "sections": [
                {"section": "Original oracle setup", "checks": setup_checks},
                {"section": "Classic low-memory and record access", "checks": accessor_checks},
                {"section": "Oracle cleanup", "checks": 1},
            ],
            "exercised_entries": [] if reference_only else sorted(row["name"] for row in rows),
        }
        (destination / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\n", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        choices=("valid", "bad-handle", "bad-master", "bad-output", "bad-copy", "bad-index"),
        default="valid",
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib",), default="CarbonLib")
    args = parser.parse_args()
    make_probe(args.destination, args.real, args.mode)
