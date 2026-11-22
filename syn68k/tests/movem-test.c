/*+===================================================================
  File:      movem-test.c

  Summary:   Implements movem test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original MOVEM live-mask, ordering and bounded transfer fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector = 2;
static uint32 expected_pc = 0x1000, expected_fault;
static uint16 expected_sr;

static uint16 status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void reset(unsigned ccr)
{
    cpu_state.sr = 0;
    cpu_state.ccc = ccr & 1;
    cpu_state.ccv = ccr & 2;
    cpu_state.ccnz = (~ccr) & 4;
    cpu_state.ccn = ccr & 8;
    cpu_state.ccx = ccr & 16;
    cpu_state.usp = EM_A7 = 0x6000;
    cpu_state.isp = 0x8000;
    cpu_state.msp = 0xa000;
    for (unsigned i = 0; i < 8; ++i)
        EM_DREG(i) = 0xa1b2c3d4;
    expected_sr = status();
    trapped = 0;
    expected_vector = 2;
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    assert(trapped == expected_vector && pc == expected_pc);
    assert(READUW(EM_A7) == expected_sr);
    assert(READUL(EM_A7 + 2) == expected_pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : 0x0010));
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_fault);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned mode, unsigned address, unsigned size, unsigned load, unsigned mask)
{
    WRITEUW(0x1000, 0x4880 | (load << 10) | (size == 4 ? 0x40 : 0) | (mode << 3) | address);
    WRITEUW(0x1002, mask);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 16);
}

static unsigned reverse(unsigned mask)
{
    unsigned result = 0;
    for (unsigned i = 0; i < 16; ++i)
        result |= ((mask >> i) & 1) << (15 - i);
    return result;
}

static void run(uint32 pc)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
    ++cases;
}

static void transfer(unsigned mode, unsigned address, unsigned size, unsigned load, unsigned mask, unsigned ccr,
                     unsigned alignment)
{
    reset(ccr);
    for (unsigned i = 0; i < 15; ++i)
        cpu_state.regs[i].ul.n = 0x87658001u + i * 0x1234123u;
    uint32 base = 0x20080 + alignment;
    EM_AREG(address) = base;
    uint32 original[16], expected[16];
    for (unsigned i = 0; i < 16; ++i)
        original[i] = expected[i] = cpu_state.regs[i].ul.n;
    for (unsigned i = 0; i < 128; ++i)
        WRITEUB(base - 64 + i, (i * 73 + 129) & 255);
    unsigned selected = mode == 4 ? reverse(mask) : mask;
    unsigned count = __builtin_popcount(selected);
    uint32 cursor = mode == 4 ? base - count * size : base;
    for (unsigned reg = 0; reg < 16; ++reg)
        if (selected & (1u << reg)) {
            if (load)
                expected[reg] = size == 4 ? READUL(cursor) : (int32)(int16)READUW(cursor);
            cursor += size;
        }
    if (mode == 3 || mode == 4)
        expected[8 + address] = mode == 4 ? base - count * size : cursor;
    run(0x1000);
    assert(!trapped && status() == expected_sr);
    for (unsigned i = 0; i < 16; ++i)
        assert(cpu_state.regs[i].ul.n == expected[i]);
    cursor = mode == 4 ? base - count * size : base;
    for (unsigned i = 0; i < 128; ++i) {
        uint32 location = base - 64 + i;
        unsigned byte = (i * 73 + 129) & 255;
        if (!load && location >= cursor && location < cursor + count * size) {
            unsigned index = (location - cursor) / size, reg = 0;
            for (; reg < 16; ++reg)
                if (selected & (1u << reg)) {
                    if (!index)
                        break;
                    --index;
                }
            uint32 value = mode == 4 && reg == 8 + address ? base - size : original[reg];
            byte = (value >> ((size - 1 - (location - cursor) % size) * 8)) & 255;
        }
        assert(READUB(location) == byte);
    }
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    for (unsigned i = 0; i < 64; ++i)
        trap_install_handler(i, handler, (void*)(uintptr_t)i);

    // A cached register-list edit must select the new destination register.
    instruction(2, 0, 4, 1, 1);
    WRITEUL(0x3000, 0x12345678);
    for (unsigned edit = 0; edit < 2; ++edit) {
        WRITEUW(0x1002, 1 << edit);
        reset(31);
        EM_A0 = 0x3000;
        run(0x1000);
        assert(EM_DREG(edit) == 0x12345678);
    }

    for (unsigned load = 0; load < 2; ++load)
        for (unsigned automatic = 0; automatic < 2; ++automatic)
            for (unsigned size = 2; size <= 4; size += 2) {
                unsigned mode = automatic ? (load ? 3 : 4) : 2;
                instruction(mode, 0, size, load, 0);
                for (unsigned mask = 0; mask < 65536; ++mask) {
                    WRITEUW(0x1002, mask);
                    transfer(mode, 0, size, load, mask, mask & 31, mask & 1);
                }
                for (unsigned address = 0; address < 8; ++address) {
                    instruction(mode, address, size, load, 0);
                    for (unsigned bit = 0; bit < 17; ++bit)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            unsigned mask = bit == 16 ? 65535 : 1 << bit;
                            WRITEUW(0x1002, mask);
                            transfer(mode, address, size, load, mask, ccr, ccr & 1);
                        }
                }
            }

    // Mask and EA displacements both remain live in a cached instruction.
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned size = 2; size <= 4; size += 2) {
            instruction(5, 0, size, load, 1);
            WRITEUW(0x1004, 0);
            WRITEUW(0x1006, 0x4ef9);
            WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
            destroy_blocks(0x1000, 16);
            for (unsigned edit = 0; edit < 2; ++edit) {
                reset(31);
                EM_A0 = 0x3000;
                WRITEUW(0x1004, edit ? -128 : 128);
                WRITEUL(edit ? 0x2f80 : 0x3080, 0x8001abcd);
                run(0x1000);
                assert(!trapped && status() == expected_sr);
                if (load)
                    assert(EM_D0 == (size == 4 ? 0x8001abcdu : 0xffff8001u));
                else
                    assert((size == 4 ? READUL(edit ? 0x2f80 : 0x3080) : READUW(edit ? 0x2f80 : 0x3080)) ==
                           (size == 4 ? 0xa1b2c3d4u : 0xc3d4u));
            }
        }

    // Every legal EA form fetches extensions after the register mask.
    const unsigned modes[] = {0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x39, 0x3a, 0x3b};
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned size = 2; size <= 4; size += 2)
            for (unsigned index = 0; index < sizeof modes / sizeof modes[0]; ++index) {
                unsigned ea = modes[index], mode = ea >> 3;
                if ((load && mode == 4) || (!load && (mode == 3 || ea >= 0x3a)))
                    continue;
                unsigned length = mode <= 4 ? 4 : ea == 0x39 ? 8 : 6;
                instruction(mode, ea & 7, size, load, mode == 4 ? 0x8000 : 1);
                unsigned extension = ea == 0x28                 ? 0xfff0
                                     : ea == 0x30 || ea == 0x3b ? 0x3a04
                                     : ea == 0x3a               ? 0x1ffc
                                                                : 0x3000;
                if (ea == 0x39)
                    WRITEUL(0x1004, extension);
                else if (length == 6)
                    WRITEUW(0x1004, extension);
                WRITEUW(0x1000 + length, 0x4ef9);
                WRITEUL(0x1002 + length, MAGIC_EXIT_EMULATOR_ADDRESS);
                destroy_blocks(0x1000, 24);
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        reset(ccr);
                        cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
                        expected_sr = status();
                        EM_A7 = bank == 0 ? 0x6000 : bank == 1 ? 0x8000 : 0xa000;
                        EM_A0 = mode == 4 ? 0x3000 + size : mode == 5 ? 0x3010 : mode == 6 ? 0x2ff4 : 0x3000;
                        EM_D3 = 4;
                        uint32 address = ea == 0x3b ? 0x1010 : 0x3000;
                        WRITEUL(address, 0x8001abcd);
                        run(0x1000);
                        assert(!trapped && status() == expected_sr);
                        if (load)
                            assert(EM_D0 == (size == 4 ? 0x8001abcdu : 0xffff8001u));
                        else
                            assert((size == 4 ? READUL(address) : READUW(address)) ==
                                   (size == 4 ? 0xa1b2c3d4u : 0xc3d4u));
                    }
            }

    // Full indexed indirection and reserved extensions, including an empty list.
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned empty = 0; empty < 2; ++empty)
            for (unsigned reserved = 0; reserved < 2; ++reserved) {
                instruction(6, 0, 4, load, empty ? 0 : 1);
                WRITEUW(0x1004, reserved ? 0x0100 : 0x0151);
                WRITEUW(0x1006, 0x4ef9);
                WRITEUL(0x1008, MAGIC_EXIT_EMULATOR_ADDRESS);
                destroy_blocks(0x1000, 24);
                reset(31);
                EM_A0 = 0x3000;
                WRITEUL(0x3000, 0x4000);
                WRITEUL(0x4000, 0x8001abcd);
                expected_vector = reserved ? 4 : 2;
                run(0x1000);
                assert(trapped == (reserved ? 4 : 0));
                if (!reserved && !empty)
                    assert(load ? EM_D0 == 0x8001abcd : READUL(0x4000) == 0xa1b2c3d4);
                else
                    assert(EM_D0 == 0xa1b2c3d4 && READUL(0x4000) == 0x8001abcd);
            }

    // Illegal forms must trap before reading the register-list extension.
    const unsigned illegal[] = {0x4898, 0x48d8, 0x48ba, 0x48fb, 0x4ca0, 0x4ce0, 0x4cbc, 0x4cfc};
    for (unsigned i = 0; i < sizeof illegal / sizeof illegal[0]; ++i) {
        uint32 pc = 0xfeffe;
        WRITEUW(pc, illegal[i]);
        syn68k_set_memory_window(0xff000, 0, 0);
        destroy_blocks(pc, 8);
        reset(31);
        expected_pc = pc;
        expected_vector = 4;
        run(pc);
        assert(trapped == 4);
    }
    expected_pc = 0x1000;
    syn68k_set_memory_window(1024 * 1024, 0, 0);

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    for (unsigned load = 0; load < 2; ++load)
        for (unsigned size = 2; size <= 4; size += 2)
            for (unsigned valid = 0; valid < 4; ++valid)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    instruction(load ? 3 : 2, 0, size, load, 15);
                    reset(ccr);
                    uint32 start = 0xff000 - valid * size;
                    EM_A0 = start;
                    for (unsigned i = 0; i < valid; ++i) {
                        if (size == 4)
                            WRITEUL(start + i * size, 0x80011234u + i);
                        else
                            WRITEUW(start + i * size, 0x8001u + i);
                    }
                    syn68k_set_memory_window(0xff000, 0, 0);
                    expected_fault = 0xff000;
                    run(0x1000);
                    assert(trapped == 2 && EM_A0 == (load ? 0xff000 : start));
                    for (unsigned reg = 0; reg < 4; ++reg) {
                        uint32 value = size == 4 ? 0x80011234u + reg : 0xffff8001u + reg;
                        assert(EM_DREG(reg) == (load && reg < valid ? value : 0xa1b2c3d4));
                        if (!load && reg < valid)
                            assert((size == 4 ? READUL(start + reg * size) : READUW(start + reg * size)) ==
                                   (size == 4 ? 0xa1b2c3d4u : 0xc3d4u));
                    }
                }
    for (unsigned size = 2; size <= 4; size += 2)
        for (unsigned valid = 0; valid < 4; ++valid) {
            instruction(4, 0, size, 0, 0xf000);
            reset(31);
            EM_A0 = valid * size;
            expected_fault = 0u - size;
            run(0x1000);
            assert(trapped == 2 && EM_A0 == 0u - size);
            for (unsigned i = 0; i < valid; ++i)
                assert((size == 4 ? READUL(i * size) : READUW(i * size)) == (size == 4 ? 0xa1b2c3d4u : 0xc3d4u));
        }
    instruction(3, 0, 4, 1, 2);
    WRITEUW(0x0ffc, 0x5888); // ADDQ.L #4,A0
    WRITEUW(0x0ffe, 0x5280); // ADDQ.L #1,D0
    WRITEUW(0x1004, 0x7200); // A successful successor replaces flags.
    destroy_blocks(0x0ffc, 32);
    reset(0);
    EM_D0 = 0xffffffff;
    EM_A0 = 0xff000 - 4;
    expected_sr = 0x15;
    expected_fault = 0xff000;
    run(0x0ffc);
    assert(trapped == 2 && EM_A0 == 0xff000 && EM_D1 == 0xa1b2c3d4);

    // Absolute-long EA prefixes fault after a valid register-list fetch.
    for (unsigned available = 0; available < 4; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            uint32 pc = 0xfef00;
            WRITEUW(pc, 0x4cf9);
            WRITEUW(pc + 2, 1);
            syn68k_set_memory_window(pc + 4 + available, 0, 0);
            destroy_blocks(pc, 16);
            reset(ccr);
            expected_pc = pc;
            expected_fault = pc + 4 + available;
            run(pc);
            assert(trapped == 2 && EM_D0 == 0xa1b2c3d4);
        }
    for (unsigned available = 0; available < 2; ++available)
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            uint32 pc = available ? 0xfef00 : 0xfeffe;
            WRITEUW(pc, 0x48e0);
            syn68k_set_memory_window(pc + 2 + available, 0, 0);
            destroy_blocks(pc, 8);
            reset(ccr);
            EM_A0 = 0x3000;
            expected_pc = pc;
            expected_fault = pc + 2 + available;
            run(pc);
            assert(trapped == 2 && EM_A0 == 0x3000);
        }
    printf("%u MOVEM cases passed (%s)\n", cases, native ? "native" : "interpreter");
    return 0;
}
