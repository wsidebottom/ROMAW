#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_aot_probe.py
#
#   Summary:   Original PEF checking eager native transfers and cache space shared with a library.
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

"""Check that integer and floating-point transfers are prepared before launch."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    library = pef(
        [0x3860002A, 0x90620010, 0x38600000, 0x4E800020, 0x80620010, 0x38630001, 0x4E800020],
        bytes(8) + bytes.fromhex("00000010 00000000 00000000"),
        exports=[("ReadValue", 2, 8, 1)],
        init=0,
        reloc=[0x4601],
        code_kind=6,  # PEF executable data.
    )
    write_fragment(destination, "ProbeAOTLibrary", library, 0)

    # The main vector enters the middle of a linear straight-line run.
    code = [0x38630063] * 4 + [0x38600000] + [0x38630001] * 31
    failures = []

    def branch_on_difference():
        failures.append(len(code))
        code.append(0x40820000)

    def compare_words(old_offset, new_offset):
        code.extend([0x80820000 | new_offset, 0x80A20000 | old_offset, 0x7C042800])
        branch_on_difference()

    code.append(0x2C03001F)  # cmpwi r3,31
    branch_on_difference()
    # Call through the library's export and verify the value set by its initializer.
    code += [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x90410038]
    code += [0x818200A8, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421]
    code += [0x80410038, 0x80010044, 0x7C0803A6, 0x38210040, 0x2C03002B]
    branch_on_difference()
    code += [0xC8220008, 0xD8220010]  # lfd f1,8(r2); stfd f1,16(r2)
    compare_words(8, 16)
    compare_words(12, 20)
    code += [0xC0420018, 0xD042001C]  # lfs f2,24(r2); stfs f2,28(r2)
    compare_words(24, 28)
    code += [0x9421FFE0, 0xBFC10018]  # Save the nonvolatile registers.
    code += [0xBBC20020, 0xBFC20028]  # lmw r30,32(r2); stmw r30,40(r2)
    code += [0xBBC10018, 0x38210020]  # Restore the nonvolatile registers and frame.
    compare_words(32, 40)
    compare_words(36, 44)
    code += [0x39020031, 0x39220039, 0x7D483CAA]  # Unaligned source/destination; lswi r10,r8,7.
    code += [0x38E00007, 0x7CE103A6, 0x38C00000, 0x7D49352A]  # XER count = 7; stswx r10,r9,r6.
    compare_words(48, 56)
    compare_words(52, 60)
    code += [0x39020020, 0x39220040, 0x7D404028, 0x7D40492D]  # lwarx r10,0,r8; stwcx. r10,0,r9.
    branch_on_difference()
    compare_words(32, 64)
    code += [0x3902007F, 0x55080034, 0x38C0FFFF]  # Align r8 to a cache line in the padding.
    code += [0x90C80000, 0x90C8001C, 0x7C0047EC]  # Store sentinels and zero the line.
    code += [0x7C00406C, 0x7C0040AC, 0x7C0004AC, 0x7C0047AC, 0x4C00012C, 0x7C0006AC]
    code += [0x80880000, 0x80A8001C, 0x7C842B79]  # Both words must be zero.
    branch_on_difference()
    # The direct branch also enters the middle of a linear run.
    code += [0x4800000C, 0x38600001, 0x38600002]
    # Tail-call an unexported vector whose target has no direct branch to it.
    code += [0x818200A0, 0x804200A4, 0x7D8903A6, 0x4E800420]  # lwz/lwz/mtctr/bctr
    code += [0x38600001, 0x38600002]
    local_entry = len(code) * 4
    code += [0x38600000, 0x4E800020]
    for branch in failures:
        code[branch] |= (len(code) - branch) * 4
    code.append(0)
    # The application alone could fill the cache before library preparation.
    code += [0x48000004] * 4097
    # Double SNaN payload and the smallest single subnormal.
    data = bytes.fromhex("00000010 00000000 7ff00000 deadbeef") + bytes(8) + bytes.fromhex("00000001 00000000")
    data += bytes.fromhex("12345678 90abcdef") + bytes(8)
    data += b"!Encoder!" + bytes(11)
    data += bytes(92)
    data += local_entry.to_bytes(4, "big") + bytes(8)
    image = pef(
        code, data, imports=[("ProbeAOTLibrary", "ReadValue", 2)], main=0, reloc=[0x4600, 0xA000, 160, 0x4600, 0x4A00]
    )
    write_fragment(destination, "PPC AOT Probe", image, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
