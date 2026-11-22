#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_allocation_probe.py
#
#   Summary:   Original allocator callback routing and optional real-library differential tests.
#
#   Classes:   None declared.
#
#   Functions: make_allocation_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original allocator callback routing and optional real-library differential tests."""
import argparse
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_carbon_allocator_probe import write_allocator_reference


OPERATIONS = ("CFAllocatorAllocate", "CFAllocatorReallocate", "CFAllocatorDeallocate")


def make_allocation_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if mode not in (
        "valid",
        "allocate-invalid",
        "reallocate-invalid",
        "deallocate-invalid",
        "allocate-failure",
        "reallocate-failure",
        "deallocate-failure",
    ):
        raise ValueError(mode)
    if not real:
        write_carbon_fallback(destination)
        write_allocator_reference(destination)
    imports = [(library, name, 2) for name in OPERATIONS]
    if real:
        imports += [
            (library, name, 2)
            for name in ("CFAllocatorCreate", "CFAllocatorGetDefault", "CFAllocatorSetDefault", "CFRelease")
        ]
        imports += [
            ("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "GetGWorld")
        ]
    else:
        imports += [("CoreFoundationLib", "CFAllocatorProbeToken", 2), ("CarbonLib", "CarbonFallbackToken", 2)]
    if mode.endswith("failure"):
        imports += [("InterfaceLib", "ROMAWCarbonAllocationFailure", 2)]
    p = Probe(imports)

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 0xFFFF),
        )

    def original(operation):
        p.emit(0x819E0098 + operation * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def reset():
        p.emit(0x38600000)
        for offset in range(832, 856, 4):
            p.emit(0x907E0000 | offset)

    def invoke(operation, present, size, hints, implicit=False, oracle=False):
        p.emit(0x38600000 if implicit else 0x807E0084 if real else 0x387E0300)
        if operation == 0:
            integer(4, size)
            integer(5, hints)
        else:
            p.emit(0x389E0380 if present else 0x38800000)
            if operation == 1:
                integer(5, size)
                integer(6, hints)
        if oracle:
            original(operation)
        else:
            p.call(OPERATIONS[operation])

    if mode != "valid":
        operation = {"allocate": 0, "reallocate": 1, "deallocate": 2}[mode.split("-")[0]]
        if mode.endswith("failure"):
            callback = Probe(imports)
            callback.call("ROMAWCarbonAllocationFailure")
            callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
            p.routines.append((640, callback.code))
        p.emit(0x387E0280, 0x907E0000 | (800 + operation * 4))
        invoke(operation, True, 16, 0)
        p.emit(0)
    else:
        # Original callbacks record every raw PPC argument and return a caller-
        # selected guest buffer. They never allocate or release real host memory.
        for operation, offset in enumerate((640, 656, 672)):
            words = [0x38E00001 + operation, 0x90E20340, 0x90620344, 0x90820348]
            if operation == 2:
                words += [0x38E00000, 0x90E2034C, 0x90E20350]
            elif operation == 0:
                words += [0x90A2034C, 0x38E00000, 0x90E20350]
            else:
                words += [0x90A2034C, 0x90C20350]
            words += [0x80E20354, 0x38E70001, 0x90E20354, 0x80620358, 0x4E800020]
            p.routines.append((offset, words))
        integer(3, 0x80000001)
        p.emit(0x907E0310, 0x907E0204)
        for offset, field in ((640, 800), (656, 804), (672, 808)):
            p.emit(0x387E0000 | offset, 0x907E0000 | field, 0x907E0000 | (532 + (field - 800)))
        p.emit(0x387E03A0, 0x907E0358)

        if real:
            for offset, name in (
                (256, b"CoreFoundationLib"),
                *[(288 + i * 40, name.encode()) for i, name in enumerate(OPERATIONS)],
            ):
                p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
            p.call("GetSharedLibrary")
            p.expect(0, "real allocation oracle CFM connection")
            for operation in range(3):
                p.emit(0x807E0090, 0x389E0000 | (288 + operation * 40), 0x38BE0098 + operation * 4, 0x38DE00A4)
                p.call("FindSymbol")
                p.expect(0, "real allocation oracle export")
                p.emit(0x887E00A4)
                p.expect(2, "allocation oracle is a descriptor")
                p.emit(0x807E0098 + operation * 4, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
                p.nonzero("allocation oracle executes original PPC code")

            p.call("CFAllocatorGetDefault")
            p.nonzero("live allocator before custom default")
            p.emit(0x907E0080)
            # Exercise real storage through the system allocator's original
            # callbacks, including realloc preservation and actual disposal.
            p.emit(0x38600000, 0x38800020, 0x38A00000)
            p.call("CFAllocatorAllocate")
            p.nonzero("native allocation through real system callback")
            p.emit(0x907E00B0)
            integer(4, 0x13579BDF)
            for offset in range(0, 32, 4):
                p.emit(0x90830000 | offset)
            p.emit(0x38600000, 0x809E00B0, 0x38A00040, 0x38C00000)
            p.call("CFAllocatorReallocate")
            p.nonzero("native reallocation through real system callback")
            p.emit(0x907E00B0)
            for offset in range(0, 32, 4):
                p.emit(0x809E00B0, 0x80640000 | offset)
                p.expect(0x13579BDF, "system reallocation preserves the original bytes")
            p.emit(0x38600000, 0x809E00B0)
            p.call("CFAllocatorDeallocate")
            p.emit(0x38600000, 0x389E0200)
            p.call("CFAllocatorCreate")
            p.nonzero("actual allocator with original PPC callbacks")
            p.emit(0x907E0084)
            p.call("CFAllocatorSetDefault")

        cases = [(0, False, size) for size in (-0x80000000, -1, 0, 1, 16, 0x7FFFFFFF)]
        cases += [(1, present, size) for present in (False, True) for size in (-0x80000000, -1, 0, 1, 16, 0x7FFFFFFF)]
        cases += [(2, present, 0) for present in (False, True)]
        for implicit in (False, True) if real else (False,):
            for operation, present, size in cases:
                reset()
                if real:
                    invoke(operation, present, size, 0xFEDCBA98, oracle=True)
                    if operation != 2:
                        p.emit(0x907E00C0)
                    for offset in range(0, 24, 4):
                        p.emit(0x807E0340 + offset, 0x907E035C + offset)
                    reset()
                invoke(operation, present, size, 0xFEDCBA98, implicit=implicit)
                if real and operation != 2:
                    p.emit(0x809E00C0)
                    p.equal("native allocation result matches original")
                elif not real and operation != 2:
                    returns_block = operation == 0 and size != 0 or operation == 1 and size != 0
                    p.emit(0x389E03A0 if returns_block else 0x38800000)
                    p.equal("callback result pointer is translated exactly once")
                if operation == 0:
                    kind = 1 if size else 0
                elif operation == 2:
                    kind = 3 if present else 0
                else:
                    kind = 1 if not present and size > 0 else 3 if present and not size else 2 if present or size else 0
                p.emit(0x807E0340)
                p.expect(kind, "allocator dispatch follows signed size and pointer branches")
                p.emit(0x807E0354)
                p.expect(int(bool(kind)), "exactly one selected callback or none")
                if kind:
                    pointer = "block" if present else 0
                    arguments = (
                        [size, 0xFEDCBA98, 0x80000001]
                        if kind == 1
                        else [pointer, size, 0xFEDCBA98, 0x80000001] if kind == 2 else [pointer, 0x80000001]
                    )
                    for i, value in enumerate(arguments):
                        p.emit(0x807E0344 + i * 4)
                        if value == "block":
                            p.emit(0x389E0380)
                        else:
                            integer(4, value)
                        p.equal("allocator callback preserves raw pointer, size, hints and info words")
                if real:
                    for offset in range(0, 24, 4):
                        p.emit(0x807E0340 + offset, 0x809E035C + offset)
                        p.equal("native and original callback argument records match")
        # Successful callbacks returning null must preserve allocation failure.
        p.emit(0x38600000, 0x907E0358)
        for operation, present in ((0, False), (1, True)):
            invoke(operation, present, 16, 0)
            p.expect(0, "callback allocation failure remains null")
        if real:
            p.emit(0x807E0080)
            p.call("CFAllocatorSetDefault")
            p.emit(0x807E0084)
            p.call("CFRelease")
            p.emit(0x387E0090)
            p.call("CloseConnection")
            p.expect(0, "allocation oracle CFM connection released")
            p.emit(0x387E00D0, 0x389E00D4)
            p.call("GetGWorld")
        else:
            p.call("CFAllocatorProbeToken")
            p.nonzero("reference allocator initializer executed")
            p.call("CarbonFallbackToken")
            p.expect(73, "allocation retains initialized PEF fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Alloc " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_allocation_probe(args.destination, args.library, args.real, args.mode)
