#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_system_probe.py
#
#   Summary:   Generate original CFM initializers exercising 750 exceptions, MMU, caches and reservations.
#
#   Classes:   None.
#
#   Functions: make_probe, make_fp_probe, make_break_probe, make_watch_probe, make_program_probe,
#              make_cache_probe, make_reserve_probe, make_thermal_probe, make_performance_probe, spr, rotate
#
#   Origin:    Original PowerPC encodings and PEF fixture; no Apple runtime binaries.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def spr(number, register, write=False):
    return (0x7C0003A6 if write else 0x7C0002A6) | register << 21 | (number & 31) << 16 | (number & 0x3E0) << 6


def rotate(destination, source, begin, end):
    return 21 << 26 | source << 21 | destination << 16 | begin << 6 | end << 1


def make_probe(destination):
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def fail_unless_equal():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038)  # frame, LR, stmw r14
    emit(0x7F6000A6)  # mfmsr r27
    for register, number in zip(range(20, 24), range(536, 540)):
        emit(spr(number, register))
    for register, address in zip(range(16, 20), range(0x300, 0x310, 4)):
        emit(0x80000000 | register << 21 | address)
    handler = [spr(537, 10, True), spr(536, 11, True), 0x39EF0001, 0x4C000064]
    for address, word in zip(range(0x300, 0x310, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    # Identity-map low 256 MB through DBAT1; DBAT0 initially has no valid entry.
    emit(0x38600000, spr(536, 3, True), 0x38600002, spr(539, 3, True))
    emit(0x38601FFE, spr(538, 3, True))
    # Derive a physical BAT base and a distinct logical address for our own data.
    emit(0x39220040, rotate(10, 9, 0, 14), 0x614A0002)
    emit(rotate(12, 9, 15, 31), 0x658C9000)
    constant(11, 0x90000002)
    constant(14, 0xA55A1234)
    emit(0x91C20040, 0x39E00000, 0x63600010, 0x7C000124, 0x4C00012C)
    emit(0x806C0000, 0x7F600124, 0x4C00012C)  # fault/retry load; restore MSR
    # Restore the shared low-memory vectors and original BATs before reporting.
    for register, address in zip(range(16, 20), range(0x300, 0x310, 4)):
        emit(0x90000000 | register << 21 | address)
    for register, number in zip(range(20, 24), range(536, 540)):
        emit(spr(number, register, True))
    emit(0x7C037000)  # cmpw r3,r14
    fail_unless_equal()
    emit(0x2C0F0001)
    fail_unless_equal()
    emit(spr(19, 4), 0x7C046000)  # DAR == logical operand address
    fail_unless_equal()
    emit(spr(18, 4))
    constant(5, 0x40000000)
    emit(0x7C042800)
    fail_unless_equal()
    # Install a private 64 KiB hashed table and map two backing pages.
    emit(spr(25, 28), 0x7FA904A6, spr(540, 16), spr(542, 17))  # SDR1, SR9, DBAT2U/3U
    emit(0x3F020001, 0x3B181000, rotate(24, 24, 0, 15))  # align down(r2 + 0x11000, 64 KiB)
    emit(0x3F380001, 0x3B591000)  # r25/r26 point to pages after the table
    constant(6, 0x11223344)
    constant(7, 0x55667788)
    emit(0x90D90000, 0x90FA0000)
    constant(3, 0x80001180)  # valid PTE for VSID 0x23, page 1
    emit(0x63240102, 0x63480102, 0x90780880, 0x90980884)  # HPT offset (0x23 ^ 1) * 64
    emit(spr(25, 24, True), 0x38A00023, 0x7CA901A4)
    emit(0x38600000, spr(536, 3, True), spr(540, 3, True), spr(542, 3, True))
    emit(0x38600002, spr(539, 3, True), 0x38601FFE, spr(538, 3, True))
    constant(12, 0x90001000)
    invalidate = (0x7C006264, 0x7C00046C, 0x4C00012C)  # tlbie r12; tlbsync; isync
    emit(*invalidate, 0x63600010, 0x7C000124, 0x4C00012C)
    # Updating the PTE leaves its old TLB entry live until TLBIE.
    emit(0x812C0000, 0x91180884, 0x814C0000, *invalidate, 0x816C0000)
    emit(*invalidate, 0x7F600124, 0x4C00012C)  # evict the private mapping and restore MSR
    emit(0x7FA901A4, spr(25, 28, True), spr(540, 16, True), spr(542, 17, True))
    for register, number in zip(range(20, 24), range(536, 540)):
        emit(spr(number, register, True))
    emit(0x4C00012C)
    for compare in (0x7C093000, 0x7C0A3000, 0x7C0B3800):
        emit(compare)
        fail_unless_equal()
    # Nap with EE set. Either DEC or the board's external interrupt wakes the
    # CPU; both handlers record delivery and return to the suspended next PC.
    emit(spr(1008, 20), spr(22, 21))
    vectors = (0x500, 0x504, 0x900, 0x904)
    for i, address in enumerate(vectors):
        emit(0x80000000 | (16 + i) << 21 | address)
        constant(3, (0x39EF0001, 0x4C000064)[i % 2])
        emit(0x90600000 | address)
    emit(0x63638000, 0x7C600124, 0x4C00012C)  # Drain a previously masked DEC request.
    constant(3, 0x00400000)
    emit(spr(1008, 3, True), 0x38600010, spr(22, 3, True), 0x39E00000)
    emit(0x67630004, 0x60638000, 0x7C0004AC, 0x7C600124, 0x4C00012C, 0x7F600124)
    emit(spr(1008, 20, True), spr(22, 21, True))
    for i, address in enumerate(vectors):
        emit(0x90000000 | (16 + i) << 21 | address)
    emit(0x2C0F0000)
    no_wake = len(code)
    emit(0x41820000)
    emit(0x38600000, 0x48000008)
    failed = len(code)
    emit(0x38600001)
    emit(0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    code[no_wake] |= (failed - no_wake) * 4
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(0x23000 - 16)
    image = pef(code, data, main=0, init=8, reloc=[0x4601])
    write_fragment(destination, "PPC 750 System Probe", image, 1)
    make_fp_probe(destination)
    make_break_probe(destination)
    make_watch_probe(destination)
    make_program_probe(destination)
    make_cache_probe(destination)
    make_reserve_probe(destination)
    make_thermal_probe(destination)
    make_performance_probe(destination)


def make_fp_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    # Jump past enough valid but unreachable instructions to fill the AOT cache.
    padding = 4096
    emit(0x48000000 | (padding + 1) * 4)
    code.extend([0x60000000] * padding)
    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, 0x7F6000A6, 0xFCA0048E)
    for register, address in zip(range(16, 21), range(0x700, 0x714, 4)):
        emit(0x80000000 | register << 21 | address)
    # Enable FP inside the program handler, clear FPSCR through f0, retry.
    handler = [0x38802000, 0x7C800124, 0xFDFE058E, 0x39EF0001, 0x4C000064]
    for address, word in zip(range(0x700, 0x714, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    emit(0xC8020020, 0xC8220028, 0xC8420030, 0xC8620038, 0xC8820040)
    emit(0xFDFE258E, 0x39E00000, 0x63600800, 0x7C000124)  # ZE, FE0
    emit(0xFC611024, 0x7F600124, 0xFDFE2D8E)  # fdiv/retry, restore MSR and FPSCR
    for register, address in zip(range(16, 21), range(0x700, 0x714, 4)):
        emit(0x90000000 | register << 21 | address)
    emit(0x2C0F0001)
    check()
    emit(0xD8620050, 0x80620050)
    constant(14, 0x7FF00000)
    emit(0x7C037000)
    check()
    emit(0x80620054, 0x2C030000)
    check()
    # Convert 1.5 using nearest-even and truncation, then inspect the integer
    # words and FPSCR. Restore the caller's FPSCR before checking the results.
    emit(0xFDFE058E, 0xC8220048, 0xFC60081D, 0xFC80081E)
    emit(0xD8620050, 0x81020054, 0xD8820050, 0x81220054)
    emit(0xFC80048E, 0xD8820050, 0x81420054, 0xFDFE2D8E)
    emit(0x2C080002)
    check()
    emit(0x2C090001)
    check()
    constant(14, 0x82020000)  # FX, XX and FI, with FR clear after FCTIWZ.
    emit(0x7C0A7000)
    check()
    # LFS/STFS preserve the signaling NaN. STFIWX exposes the widened low word.
    constant(3, 0x7F800001)
    emit(0x90620050, 0xC0620050, 0xD0620054, 0x80A20054, 0x7C032800)
    check()
    emit(0x39000050, 0x7C6247AE, 0x80A20050)
    constant(14, 0x20000000)
    emit(0x7C057000)
    check()
    emit(0x38600000, 0x48000008)
    failed = len(code)
    emit(0x38600001, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(16)
    data += struct.pack(">6Q", 0, 0x3FF0000000000000, 0, 0x4000000000000000, 0x10, 0x3FF8000000000000) + bytes(16)
    write_fragment(destination, "PPC 750 FP Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_break_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, spr(1010, 24), spr(268, 3), spr(269, 4))
    for register, address in zip(range(16, 21), range(0x1300, 0x1314, 4)):
        emit(0x80000000 | register << 21 | address)
    handler = [0x38800000, spr(1010, 4, True), 0x4C00012C, 0x39EF0001, 0x4C000064]
    for address, word in zip(range(0x1300, 0x1314, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    emit(0x39E00000, 0x83E20008)  # counter, relocated initializer code base
    target_address = len(code)
    emit(0x387F0000, 0x60630002, spr(1010, 3, True), 0x4C00012C)
    code[target_address] |= len(code) * 4
    emit(0x39C01234)  # Break before this LI; handler disables IABR, RFI retries.
    for register, address in zip(range(16, 21), range(0x1300, 0x1314, 4)):
        emit(0x90000000 | register << 21 | address)
    emit(spr(1010, 24, True), 0x4C00012C, 0x2C0F0001)
    check()
    emit(0x2C0E1234)
    check()
    emit(0x38600000, 0x48000008)
    failed = len(code)
    emit(0x38600001, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0)
    write_fragment(destination, "PPC 750 Break Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_watch_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, spr(1013, 24))
    for register, address in zip(range(16, 20), range(0x300, 0x310, 4)):
        emit(0x80000000 | register << 21 | address)
    handler = [0x38800000, spr(1013, 4, True), 0x39EF0001, 0x4C000064]
    for address, word in zip(range(0x300, 0x310, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    constant(14, 0xA55A1234)
    emit(0x39820020, 0x91CC0000, 0x39E00000)  # operand, original data, counter
    emit(rotate(3, 12, 0, 28), 0x60630001, spr(1013, 3, True))
    emit(0x806C0000)  # DABR stops the load; handler disables it, RFI retries.
    for register, address in zip(range(16, 20), range(0x300, 0x310, 4)):
        emit(0x90000000 | register << 21 | address)
    emit(spr(1013, 24, True), 0x7C037000)
    check()
    emit(0x2C0F0001)
    check()
    emit(spr(19, 4), 0x7C046000)
    check()
    emit(spr(18, 4))
    constant(5, 0x00400000)
    emit(0x7C042800)
    check()
    emit(0x38600000, 0x48000008)
    failed = len(code)
    emit(0x38600001, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(48)
    write_fragment(destination, "PPC 750 Watch Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_program_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038)
    for register, address in zip(range(16, 19), range(0x700, 0x70C, 4)):
        emit(0x80000000 | register << 21 | address)
    handler = [0x914B0000, 0x39EF0001, 0x4C000064]  # Replace invalid form, count, retry.
    for address, word in zip(range(0x700, 0x70C, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    constant(14, 0xA55A1234)
    constant(10, 0x806C0000)  # lwz r3,0(r12)
    emit(0x39820020, 0x91CC0000, 0x39E00000, 0x83E20008)
    target_address = len(code)
    emit(0x397F0000)
    code[target_address] |= len(code) * 4
    emit(0x84600000)  # Invalid lwzu r3,0(r0), stopped by the kernel guard.
    for register, address in zip(range(16, 19), range(0x700, 0x70C, 4)):
        emit(0x90000000 | register << 21 | address)
    constant(10, 0x84600000)
    emit(0x914B0000, 0x7C037000)
    check()
    emit(0x2C0F0001)
    check()
    emit(spr(26, 4), 0x7C045800)
    check()
    emit(spr(27, 4))
    constant(5, 0x82000)
    emit(0x7C042800)
    check()

    # A program trap saves the current instruction and needs a handler that
    # skips it. SC saves the following instruction and can return directly.
    vectors = (*range(0x700, 0x714, 4), 0xC00, 0xC04)
    handlers = (spr(26, 4), 0x38840004, spr(26, 4, True), 0x39EF0001, 0x4C000064, 0x39CE0001, 0x4C000064)
    for register, address, word in zip(range(16, 23), vectors, handlers):
        emit(0x80000000 | register << 21 | address)
        constant(3, word)
        emit(0x90600000 | address)
    for address in (0x700, 0xC00):
        emit(0x38800000 | address, 0x7C0020AC, 0x7C0004AC, 0x7C0027AC, 0x4C00012C)
    emit(0x39E00000, 0x39C00000, 0x38600007)
    emit(0x0C830008, 0x0C830007, 0x7C831808)  # Untaken TWI, taken TWI, taken TW.
    emit(spr(27, 5), 0x44000002, spr(27, 6))  # Capture both saved exception states.
    for register, address in zip(range(16, 23), vectors):
        emit(0x90000000 | register << 21 | address)
    for address in (0x700, 0xC00):
        emit(0x38800000 | address, 0x7C0020AC, 0x7C0004AC, 0x7C0027AC, 0x4C00012C)
    emit(0x2C0F0002)
    check()
    emit(0x2C0E0001)
    check()
    constant(4, 0x22000)
    emit(0x7C052000)
    check()
    emit(0x2C062000)
    check()
    emit(0x38600000, 0x48000008)
    failed = len(code)
    emit(0x38600001, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(48)
    write_fragment(destination, "PPC 750 Program Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_cache_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    def call_target():
        emit(spr(9, 11, True), 0x4E800421)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, spr(1008, 24), 0x83E20008, 0x39820020)
    target_pointer = len(code)
    emit(0x397F0000, rotate(11, 11, 0, 26))  # Round reserved code space up to a physical cache line.
    constant(10, 0x38600001)
    emit(0x914B0000)
    constant(10, 0x4E800020)
    emit(0x914B0004)
    constant(14, 0xA55A1234)
    emit(0x38600000, 0x906C0000)
    constant(3, 0xCC00)
    emit(0x7C0004AC, 0x4C00012C, spr(1008, 3, True), 0x4C00012C)
    emit(0x91CC0000, 0x806C0000, 0x7C037000)  # Dirty data is visible through D-cache.
    check()
    constant(3, 0xC400)
    emit(0x7C0004AC, spr(1008, 3, True), 0x806C0000, 0x2C030000)  # Discard without copyback.
    check()
    emit(0x91CC0000, 0x7C00606C)
    constant(3, 0xC400)
    emit(0x7C0004AC, spr(1008, 3, True), 0x806C0000, 0x7C037000)
    check()  # DC BST published the data before the second flash invalidate.
    call_target()
    emit(0x2C030001)
    check()
    constant(10, 0x38600002)
    emit(0x914B0000, 0x7C0058AC, 0x7C0004AC)
    call_target()
    emit(0x2C030001)  # Data copyback alone leaves the old instruction in I-cache.
    check()
    emit(0x7C005FAC, 0x4C00012C)
    call_target()
    emit(0x2C030002)
    check()
    emit(0x3B800000, 0x48000008)
    failed = len(code)
    emit(0x3B800001)
    # Common cleanup restores the generated code, writes dirty data and disables L1.
    constant(10, 0x38600001)
    emit(0x914B0000, 0x7C00586C, 0x7C00606C, 0x7C0004AC, 0x7C005FAC, 0x4C00012C)
    emit(spr(1008, 24, True), 0x4C00012C, 0x7F83E378)
    emit(0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    padding = len(code) * 4
    code[target_pointer] |= padding + 31
    code.extend([0] * 16)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(48)
    write_fragment(destination, "PPC 750 Cache Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_reserve_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check(equal=True):
        failures.append(len(code))
        emit(0x40820000 if equal else 0x41820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, spr(1008, 24))
    constant(14, 0xA55A1234)
    constant(15, 0xDEADBEEF)
    emit(0x39820020, 0x39620060, 0x91CC0000, 0x38600000, 0x906B0000)
    emit(0x38604400, 0x7C0004AC, spr(1008, 3, True))
    emit(0x7C606028, 0x7C037000)  # LWARX reads the first physical granule.
    check()
    emit(0x7DC0592D)  # STWCX to a different granule still succeeds on the 750.
    check()
    emit(0x806B0000, 0x7C037000)
    check()
    emit(0x7DE0592D)  # Consumed reservation: a second conditional store fails.
    check(False)
    emit(0x806B0000, 0x7C037000)
    check()
    emit(0x3B800000, 0x48000008)
    failed = len(code)
    emit(0x3B800001)
    emit(0x7C00606C, 0x7C00586C, 0x7C0004AC, spr(1008, 24, True), 0x4C00012C, 0x7F83E378)
    emit(0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(112)
    write_fragment(destination, "PPC 750 Reserve Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_thermal_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, 0x7F6000A6)
    for register, number in zip(range(20, 23), range(1020, 1023)):
        emit(spr(number, register))
    for register, address in zip(range(14, 20), range(0x1700, 0x1718, 4)):
        emit(0x80000000 | register << 21 | address)
    emit(0x82E00900, spr(22, 31))  # Preserve DEC vector/value before unmasking asynchronous sources.
    constant(3, 0x4C000064)
    emit(0x90600900)
    constant(3, 0x7FFFFFFF)
    emit(spr(22, 3, True), 0x38600000, spr(1022, 3, True))
    emit(spr(1019, 6), 0x3860FFFF, spr(1019, 3, True), spr(1019, 4), 0x2C0401FF)
    check()
    emit(spr(1019, 6, True))
    for number in (287, 1009):
        emit(spr(number, 3), 0x3880FFFF, spr(number, 4, True), spr(number, 5), 0x7C032800)
        check()  # Supervisor writes to both read-only SPRs must be no-ops.
    handler = [spr(1020, 24), spr(1021, 25), 0x38600000, spr(1022, 3, True), 0x3B5A0001, 0x4C000064]
    for address, word in zip(range(0x1700, 0x1718, 4), handler):
        constant(3, word)
        emit(0x90600000 | address)
    # Both upper-bound comparisons cross at the embedding's nominal 25 C.
    # The fixture's deterministic zero-C sensor also satisfies this comparison.
    constant(3, 0x3F800007)
    emit(spr(1020, 3, True), spr(1021, 3, True), 0x3B400000, 0x38600003, spr(1022, 3, True))
    emit(0x63608000, 0x7C000124, 0x4C00012C, 0x3B807FFF)
    waiting = len(code)
    emit(0x2C1A0001)
    finished = len(code)
    emit(0x41820000)
    emit(0x379CFFFF, 0x40820000 | (((waiting - len(code) - 1) * 4) & 0xFFFF))
    failures.append(len(code))
    emit(0x48000000)  # Timed-out IRQ delivery.
    code[finished] |= (len(code) - finished) * 4
    constant(4, 0xC0000000)
    emit(0x7703C000, 0x7C032000)
    check()
    emit(0x7723C000, 0x7C032000)
    check()
    emit(0x3BA00000, 0x48000008)
    failed = len(code)
    emit(0x3BA00001)
    # Force non-crossing polled samples to clear read-only TIN before restoring
    # the caller's controls and low-memory thermal exception vector.
    emit(0x7F600124, 0x4C00012C, 0x38600000, spr(1022, 3, True))
    constant(3, 0x3F800001)
    emit(spr(1020, 3, True), 0x38600005, spr(1021, 3, True), 0x38600003, spr(1022, 3, True), 0x3B807FFF)
    clearing = len(code)
    emit(spr(1020, 3), spr(1021, 4), 0x7C632038, 0x74634000)
    cleared = len(code)
    emit(0x40820000)
    emit(0x379CFFFF, 0x40820000 | (((clearing - len(code) - 1) * 4) & 0xFFFF), 0x3BA00001)
    code[cleared] |= (len(code) - cleared) * 4
    for register, number in zip(range(20, 23), range(1020, 1023)):
        emit(spr(number, register, True))
    for register, address in zip(range(14, 20), range(0x1700, 0x1718, 4)):
        emit(0x90000000 | register << 21 | address)
    emit(0x92E00900, spr(22, 31, True))
    emit(0x7F600124, 0x4C00012C, 0x7FA3EB78, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(48)
    write_fragment(destination, "PPC 750 Thermal Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


def make_performance_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 0xFFFF)

    def check():
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0xBDC10038, spr(952, 14), spr(1008, 21), 0x39620020)
    constant(4, 0x80000000)
    emit(spr(952, 4, True))
    for register, number in zip(range(15, 21), range(953, 959)):
        emit(spr(number, register))
    emit(0x38600000)
    for number in (953, 954, 957, 958):
        emit(spr(number, 3, True))
    constant(3, (8 << 27) | (13 << 22))  # Branch taken / integer completed.
    emit(spr(956, 3, True), 0x3860008B, spr(952, 3, True))  # Complete / load-store complete.
    # Exactly six completed instructions, two integer and two LSU operations.
    # The taken branch targets fallthrough. The final MMCR0 write freezes all
    # counters before its own completion or any of the following comparisons.
    emit(0x38C00001, 0x38E00002, 0x80C20020, 0x90C20024, 0x48000004, spr(952, 4, True))
    for number, expected in ((953, 6), (954, 2), (957, 1), (958, 2)):
        emit(spr(number, 3), 0x2C030000 | expected)
        check()
        emit(spr(number - 16, 5), 0x7C032800)
        check()  # User read aliases expose the same counters.
    emit(spr(936, 3), 0x7C032000)
    check()
    # An enabled, unlocked D-cache must count the first real tag miss once;
    # repeated loads and a write to the resident line must remain hits.
    emit(0x7C0058AC)
    constant(4, 0xFFFFEFFF)
    emit(0x7EA32038, 0x60634000, spr(1008, 3, True), 0x38600000)
    for number in (953, 954, 957, 958):
        emit(spr(number, 3, True))
    constant(3, 5 << 27)
    emit(spr(956, 3, True))
    constant(4, 0x80000000)
    emit(0x38600000, spr(952, 3, True), 0x80CB0000, 0x80EB0000, 0x90CB0004, spr(952, 4, True))
    emit(spr(957, 3), 0x2C030001)
    check()
    emit(0x3BA00000, 0x48000008)
    failed = len(code)
    emit(0x3BA00001)
    emit(0x7C0058AC, spr(1008, 21, True))
    for register, number in zip(range(15, 21), range(953, 959)):
        emit(spr(number, register, True))
    emit(spr(952, 14, True), 0x7FA3EB78, 0xB9C10038, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)
    for index in failures:
        code[index] |= (failed - index) * 4
    main = len(code) * 4
    emit(0x38600000, 0x4E800020)
    data = struct.pack(">4I", main, 0, 0, 0) + bytes(48)
    write_fragment(destination, "PPC 750 Performance Probe", pef(code, data, main=0, init=8, reloc=[0x4601]), 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
