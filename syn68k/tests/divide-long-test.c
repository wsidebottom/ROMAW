/*+===================================================================
  File:      divide-long-test.c

  Summary:   Implements divide long test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Original long integer division, exception and operand-fetch fixtures. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases, trapped, expected_vector;
static uint32 expected_pc, expected_address, expected_a0;
static uint16 expected_sr;
static unsigned stack_bank;
static uint32 expected_source_sp;

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
    cpu_state.usp = EM_A7 = 0x8000;
    cpu_state.isp = 0x9000;
    EM_A0 = expected_a0 = 0x3000;
    trapped = expected_vector = 0;
    stack_bank = 3;
    expected_sr = status();
}

static syn68k_addr_t handler(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    if (trapped != expected_vector || pc != expected_pc)
        fprintf(stderr, "case %u: vector %u/%u PC %08x/%08x\n", cases, trapped, expected_vector, pc, expected_pc);
    assert(trapped == expected_vector && pc == expected_pc);
    assert(EM_A0 == expected_a0);
    assert(READUW(EM_A7) == expected_sr && READUL(EM_A7 + 2) == pc);
    assert(READUW(EM_A7 + 6) == (trapped == 2 ? 0x7008 : trapped == 5 ? 0x2014 : 0x0010));
    if (stack_bank < 3) {
        if (stack_bank == 0)
            assert(cpu_state.usp == expected_source_sp && EM_A7 == 0x9000 - 12);
        else
            assert(EM_A7 == expected_source_sp - 12);
    }
    if (trapped == 2)
        assert(READUL(EM_A7 + 20) == expected_address);
    if (trapped == 5)
        assert(READUL(EM_A7 + 8) == expected_address);
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void instruction(unsigned ea, unsigned extension)
{
    WRITEUW(0x1000, 0x4c40 | ea);
    WRITEUW(0x1002, extension);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    destroy_blocks(0x1000, 16);
}

static void run(uint32 pc)
{
    interpret_code(hash_lookup_code_and_create_if_needed(pc));
    ++cases;
}

/* Independent signed reference: guard the one host int64 overflow, then use
   signed quotient/remainder and explicit destination range checks. */
static void check_arithmetic(unsigned extension, uint32 divisor)
{
    unsigned q = (extension >> 12) & 7, r = extension & 7;
    int sign = !!(extension & 0x0800), wide = !!(extension & 0x0400);
    uint32 saved[8];
    for (unsigned i = 0; i < 8; ++i)
        saved[i] = cpu_state.regs[i].ul.n;
    uint64 bits = saved[q];
    if (wide)
        bits |= (uint64)saved[r] << 32;
    else if (sign)
        bits = (int64)(int32)saved[q];
    uint32 quotient = 0, remainder = 0;
    int overflow = 0;
    expected_sr &= ~1u;
    if (!divisor) {
        expected_vector = 5;
        expected_pc = 0x1004;
        expected_address = 0x1000;
    }
    else {
        if (sign) {
            if (bits == ((uint64)1 << 63) && divisor == 0xffffffffu)
                overflow = 1;
            else {
                int64 result = (int64)bits / (int32)divisor;
                overflow = result < -2147483647 - 1 || result > 2147483647;
                quotient = result;
                remainder = (int64)bits % (int32)divisor;
            }
        }
        else {
            uint64 result = bits / divisor;
            overflow = result > 0xffffffffu;
            quotient = result;
            remainder = bits % divisor;
        }
        if (overflow)
            expected_sr |= 2;
        else {
            expected_sr &= ~14u;
            expected_sr |= ((quotient >> 31) << 3) | (!quotient << 2);
            if (r != q)
                saved[r] = remainder;
            saved[q] = quotient;
        }
    }
    run(0x1000);
    assert(trapped == expected_vector);
    assert((status() & 31) == (expected_sr & 31));
    for (unsigned i = 0; i < 8; ++i)
        assert(cpu_state.regs[i].ul.n == saved[i]);
}

int main(int argc, char** argv)
{
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

    /* The handler must observe the source EA update, including on divide zero. */
    for (unsigned pre = 0; pre < 2; ++pre)
        for (unsigned sign = 0; sign < 2; ++sign) {
            instruction(pre ? 0x20 : 0x18, sign ? 0x0801 : 1);
            reset(0);
            EM_D0 = 123;
            EM_D1 = 456;
            WRITEUL(0x3000, 0);
            if (pre)
                EM_A0 += 4;
            else
                expected_a0 += 4;
            expected_vector = 5;
            expected_pc = 0x1004;
            expected_address = 0x1000;
            run(0x1000);
            assert(trapped == 5 && EM_D0 == 123 && EM_D1 == 456);
        }

    const uint32 values[][3] = {{0, 0, 1},
                                {1, 0, 2},
                                {0xffffffff, 0, 3},
                                {0x80000000, 0, 0xffffffff},
                                {0, 0x80000000, 0xffffffff},
                                {0xffffffff, 0x7fffffff, 1},
                                {0x7fffffff, 0xffffffff, 0x80000000},
                                {0x80000001, 0xffffffff, 16},
                                {0x55555555, 0xaaaaaaaa, 0xfffffff0},
                                {0, 1, 2},
                                {0, 0xffffffff, 0x80000000},
                                {0x12345678, 0x87654321, 0}};
    /* All three register fields can alias. Change extension words in a cached
     block; all CCR patterns must retain X and obey the defined result flags. */
    for (unsigned source = 0; source < 8; ++source) {
        instruction(source, 0);
        for (unsigned q = 0; q < 8; ++q)
            for (unsigned r = 0; r < 8; ++r)
                for (unsigned kind = 0; kind < 4; ++kind) {
                    unsigned extension = (q << 12) | (kind << 10) | r;
                    WRITEUW(0x1002, extension);
                    for (unsigned v = 0; v < sizeof values / sizeof values[0]; ++v)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            reset(ccr);
                            for (unsigned i = 0; i < 8; ++i)
                                cpu_state.regs[i].ul.n = 0x11223344u * i;
                            cpu_state.regs[q].ul.n = values[v][0];
                            if (r != q)
                                cpu_state.regs[r].ul.n = values[v][1];
                            cpu_state.regs[source].ul.n = values[v][2];
                            check_arithmetic(extension, cpu_state.regs[source].ul.n);
                        }
                }
    }

    /* Every source EA, including the PC base following the register extension. */
    for (unsigned ea = 0; ea < 64; ++ea) {
        unsigned mode = ea >> 3, reg = ea & 7;
        int legal = mode != 1 && (mode != 7 || reg <= 4);
        instruction(ea, 0x1002);
        reset(31);
        EM_D1 = 100;
        EM_D2 = 0x87654321;
        EM_D3 = 4;
        for (unsigned i = 0; i < 7; ++i)
            EM_AREG(i) = 0x3000;
        uint32 base = EM_AREG(reg), address = base, next = 0x1004;
        uint32 divisor = 3;
        switch (mode) {
        case 0:
            cpu_state.regs[reg].ul.n = divisor;
            break;
        case 3:
            if (reg == 0)
                expected_a0 += 4;
            break;
        case 4:
            address -= 4;
            if (reg == 0)
                expected_a0 -= 4;
            break;
        case 5:
            WRITEUW(next, 16);
            next += 2;
            address += 16;
            break;
        case 6:
            WRITEUW(next, 0x3a04); /* d3.l * 2 + 4 */
            next += 2;
            address += 12;
            break;
        case 7:
            switch (reg) {
            case 0:
                WRITEUW(next, 0x3000);
                next += 2;
                address = 0x3000;
                break;
            case 1:
                WRITEUL(next, 0x3000);
                next += 4;
                address = 0x3000;
                break;
            case 2:
                WRITEUW(next, 0x2000);
                address = next + 0x2000;
                next += 2;
                break;
            case 3:
                WRITEUW(next, 0x3a04);
                address = next + 12;
                next += 2;
                break;
            case 4:
                address = next;
                next += 4;
                break;
            }
            break;
        }
        if (mode && legal)
            WRITEUL(address, divisor);
        WRITEUW(next, 0x4ef9);
        WRITEUL(next + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        destroy_blocks(0x1000, 32);
        if (!legal) {
            expected_vector = 4;
            expected_pc = 0x1000;
            run(0x1000);
            assert(trapped == 4);
        }
        else {
            check_arithmetic(0x1002, divisor);
            assert(EM_A0 == expected_a0);
            uint32 expected = base + (mode == 3 ? 4 : mode == 4 ? -4 : 0);
            assert(EM_AREG(reg) == expected);
        }
    }

    /* Native arithmetic must commit live flags before the decoder/trap. */
    for (unsigned zero = 0; zero < 2; ++zero) {
        instruction(4, 0x1002);
        WRITEUW(0x0ffe, 0x5283); /* addq.l #1,d3 */
        destroy_blocks(0x0ffe, 32);
        reset(0);
        EM_D1 = 9;
        EM_D3 = 0xffffffff;
        EM_D4 = zero ? 0 : 3;
        expected_sr = 0x14;
        expected_vector = zero ? 5 : 0;
        expected_pc = 0x1004;
        expected_address = 0x1000;
        run(0x0ffe);
        assert(trapped == expected_vector);
        assert((status() & 31) == (zero ? 0x14 : 0x10));
        assert(EM_D1 == (zero ? 9 : 3));
    }

    /* Exhaust all reserved combinations, rewriting the same cached opcode.
     Rejection must precede evaluation of the postincrement source. */
    instruction(0x18, 0);
    for (unsigned extension = 0; extension < 65536; ++extension)
        if (extension & 0x83f8) {
            WRITEUW(0x1002, extension);
            reset(31);
            expected_vector = 4;
            expected_pc = 0x1000;
            run(0x1000);
            assert(trapped == 4);
        }

    /* Source A7 updates belong to the interrupted stack bank, before the
     format-2 frame is pushed. Test all banks and incoming condition codes. */
    for (unsigned bank = 0; bank < 3; ++bank)
        for (unsigned pre = 0; pre < 2; ++pre)
            for (unsigned sign = 0; sign < 2; ++sign)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    instruction(pre ? 0x27 : 0x1f, sign ? 0x0801 : 1);
                    reset(ccr);
                    stack_bank = bank;
                    cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
                    EM_A7 = bank == 0 ? 0x8000 : bank == 1 ? 0x9000 : 0xa000;
                    WRITEUL(EM_A7 - (pre ? 4 : 0), 0);
                    expected_source_sp = EM_A7 + (pre ? -4 : 4);
                    expected_sr = status() & ~1u;
                    expected_vector = 5;
                    expected_pc = 0x1004;
                    expected_address = 0x1000;
                    EM_D0 = 123;
                    EM_D1 = 456;
                    run(0x1000);
                    assert(trapped == 5 && EM_D0 == 123 && EM_D1 == 456);
                }

    DWORD old;
    assert(VirtualProtect((char*)memory + 0xff000, 0x1000, PAGE_NOACCESS, &old));
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned available = 0; available < 2; ++available) {
        /* The even PC case abuts an actually protected host page. */
        uint32 limit = available ? 0xfef03 : 0xff000;
        uint32 pc = available ? 0xfef00 : 0xfeffe;
        WRITEUW(pc, 0x4c40);
        if (available)
            WRITEUB(pc + 2, 0);
        syn68k_set_memory_window(limit, 0, 0);
        destroy_blocks(pc, 4);
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr);
            expected_vector = 2;
            expected_pc = pc;
            expected_address = limit;
            run(pc);
            assert(trapped == 2);
        }
    }
    syn68k_set_memory_window(0xff000, 0, 0);
    for (unsigned available = 0; available < 4; ++available) {
        instruction(0x10, 1);
        for (unsigned i = 0; i < available; ++i)
            WRITEUB(0xff000 - available + i, 0);
        for (unsigned ccr = 0; ccr < 32; ++ccr) {
            reset(ccr);
            EM_A0 = expected_a0 = 0xff000 - available;
            expected_vector = 2;
            expected_pc = 0x1000;
            expected_address = 0xff000;
            run(0x1000);
            assert(trapped == 2);
        }
    }
    printf("%u long divide arithmetic/alias/CCR/exception/fetch cases passed (%s)\n", cases,
           native ? "native" : "interpreter");
    return 0;
}
