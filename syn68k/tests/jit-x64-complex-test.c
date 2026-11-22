/*+===================================================================
  File:      jit-x64-complex-test.c

  Summary:   Implements jit x64 complex test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_syn68k_x64_complex_run, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Compare ahead-compiled integer kernels with the independent SCM engine. */
#include "jit-x64.h"
#include "trap.h"
#include "destroyblock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* ram;
static unsigned cases;
static void* retire_code;
static unsigned fault_vector, fault_address, fault_count;
uint32 __real_syn68k_x64_complex_run(uint32 pc, unsigned op);

uint32 __wrap_syn68k_x64_complex_run(uint32 pc, unsigned op)
{
    if (retire_code) {
        syn68k_x64_release(retire_code);
        retire_code = NULL;
    }
    return __real_syn68k_x64_complex_run(pc, op);
}

static syn68k_addr_t fault(syn68k_addr_t pc, void* argument)
{
    unsigned vector = (uintptr_t)argument;
    assert(pc == 0x1000 && vector == fault_vector);
    assert((READUW(EM_A7) & 31) == 31);
    assert(READUW(EM_A7 + 6) == (vector == 2 ? 0x7008 : 0x10));
    if (vector == 2)
        assert(READUL(EM_A7 + 20) == fault_address);
    ++fault_count;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

typedef struct {
    uint32 regs[16];
    unsigned flags;
    uint8 data[256];
} snapshot;

static unsigned status(void)
{
    return !!cpu_state.ccc | !!cpu_state.ccv << 1 | !cpu_state.ccnz << 2 | !!cpu_state.ccn << 3 | !!cpu_state.ccx << 4;
}

static void capture(snapshot* out)
{
    for (unsigned r = 0; r < 16; ++r)
        out->regs[r] = EM_DREG(r);
    out->flags = status();
    memcpy(out->data, ram + 0x4000, sizeof out->data);
}

static void compare(unsigned op, unsigned a, unsigned b, unsigned f, unsigned extension)
{
    snapshot expected = {0}, actual = {0};
    for (unsigned mode = 0; mode < 3; ++mode) {
        destroy_blocks(0x1000, 64);
        for (unsigned r = 0; r < 8; ++r)
            EM_DREG(r) = 0x12340000 | (r & 1 ? b : a);
        for (unsigned r = 0; r < 8; ++r)
            EM_AREG(r) = 0x4080 + r * 8;
        for (unsigned i = 0; i < 256; ++i)
            ram[0x4000 + i] = i & 1 ? b : a;
        cpu_state.sr = 0;
        cpu_state.ccc = f & 1 ? 0x80000000 : 0;
        cpu_state.ccv = f & 2 ? 0x12345678 : 0;
        cpu_state.ccnz = f & 4 ? 0 : 0x87654321;
        cpu_state.ccn = f & 8 ? 0x80000000 : 0;
        cpu_state.ccx = f & 16 ? 0x12345678 : 0;
        unsigned pc = 0x1002;
        WRITEUW(0x1000, op);
        int cas = (op & 0xF9C0) == 0x08C0 && (op & 0x600);
        int bf = (op & 0xF8C0) == 0xE8C0;
        if (cas || bf) {
            WRITEUW(pc, extension);
            pc += 2;
            if (op == 0x0CFC || op == 0x0EFC) {
                WRITEUW(pc, extension >> 16);
                pc += 2;
                EM_DREG((extension >> 12) & 15) = 0x4040;
                EM_DREG((extension >> 28) & 15) = 0x4080;
            }
            else if ((op & 63) >= 40) {
                unsigned code = op & 63;
                if ((code >> 3) == 5)
                    WRITEUW(pc, 0xFFF0), pc += 2;
                else if ((code >> 3) == 6 || code == 59) {
                    WRITEUW(pc, 0x01F0); /* full, both suppressed, long base */
                    WRITEUL(pc + 2, 0x4080);
                    pc += 6;
                }
                else if (code == 56)
                    WRITEUW(pc, 0x4080), pc += 2;
                else if (code == 57)
                    WRITEUL(pc, 0x4080), pc += 4;
                else if (code == 58)
                    WRITEUW(pc, 0x4080 - pc), pc += 2;
            }
            if (bf) {
                EM_D6 = (int32)(a % 129) - 64;
                EM_D7 = b % 65;
            }
            if (cas) {
                unsigned size = (op & 0x600) == 0x200 ? 1 : (op & 0x600) == 0x400 ? 2 : 4;
                uint32 address = (op == 0x0CFC || op == 0x0EFC) ? 0x4040
                                 : (op & 63) >= 40              ? ((op & 63) < 48 ? EM_AREG(op & 7) - 16 : 0x4080)
                                                                : EM_AREG(op & 7) - ((op & 0x38) == 0x20 ? size : 0);
                if (f & 1)
                    EM_DREG(extension & 7) = size == 1   ? READUB(address)
                                             : size == 2 ? READUW(address)
                                                         : READUL(address);
                if ((f & 2) && (op == 0x0CFC || op == 0x0EFC))
                    EM_DREG((extension >> 16) & 7) = size == 2 ? READUW(0x4080) : READUL(0x4080);
            }
        }
        else if ((op & 0xF1F0) == 0x8140 || (op & 0xF1F0) == 0x8180) {
            WRITEUW(pc, extension);
            pc += 2;
        }
        else if ((op & 0xFFC0) == 0x4800 && (op & 63) >= 40) {
            WRITEUW(pc, 0xFFF0);
            pc += 2;
        }
        WRITEUW(pc, 0x4EF9);
        WRITEUL(pc + 2, MAGIC_EXIT_EMULATOR_ADDRESS);
        syn68k_x64_enabled = mode != 0;
        uint64 count = syn68k_native_execution_count();
        if (mode < 2)
            interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        else {
            int8 lengths[] = {(pc - 0x1000) / 2, 1};
            unsigned translated;
            int terminal;
            void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 2, &translated, 0x1000, &terminal);
            assert(code && translated == 1 && terminal);
            assert(syn68k_x64_execute(code) == pc);
            syn68k_x64_release(code);
        }
        if (mode)
            assert(syn68k_native_execution_count() > count);
        capture(mode ? &actual : &expected);
        if (mode && memcmp(&expected, &actual, sizeof actual)) {
            fprintf(stderr,
                    "complex mismatch op=%04X a=%X b=%X f=%X ext=%X"
                    " flags=%X/%X mode=%u\n",
                    op, a, b, f, extension, expected.flags, actual.flags, mode);
            for (unsigned r = 0; r < 16; ++r)
                if (expected.regs[r] != actual.regs[r])
                    fprintf(stderr, "r%u=%08X/%08X\n", r, expected.regs[r], actual.regs[r]);
            abort();
        }
    }
    ++cases;
}

int main(void)
{
    ram = calloc(1, 0x10000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    for (unsigned r = 0; r < 8; ++r)
        assert(!syn68k_x64_complex_opcode(0x4808 | r)); /* LINK.L, not NBCD */
    const unsigned values[] = {0, 1, 9, 10, 15, 0x10, 0x7F, 0x80, 0x99, 0xA0, 0xFF, 0x1234, 0xFFFF};
    for (unsigned kind = 0; kind < 4; ++kind)
        for (unsigned src = 0; src < 8; ++src)
            for (unsigned dst = 0; dst < 8; ++dst)
                for (unsigned mem = 0; mem < 2; ++mem)
                    for (unsigned sample = 0; sample < 32; ++sample) {
                        unsigned op = (kind == 0   ? 0xC100
                                       : kind == 1 ? 0x8100
                                       : kind == 2 ? 0x8140
                                                   : 0x8180) |
                                      dst << 9 | src | mem << 3;
                        compare(op, values[sample % 13], values[(sample * 7) % 13], sample, sample * 12345);
                    }
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b)
            for (unsigned flags = 0; flags < 4; ++flags)
                for (unsigned subtract = 0; subtract < 2; ++subtract)
                    compare(subtract ? 0x8300 : 0xC300, a, b, (flags & 1 ? 16 : 0) | (flags & 2 ? 4 : 0), 0);
    const unsigned eas[] = {0, 7, 16, 23, 24, 31, 32, 39, 40, 47};
    for (unsigned i = 0; i < sizeof eas / sizeof *eas; ++i)
        for (unsigned value = 0; value < 256; ++value)
            for (unsigned f = 0; f < 32; ++f)
                compare(0x4800 | eas[i], value, value, f, 0);
    const unsigned cas_eas[] = {16, 23, 24, 31, 32, 39, 40, 47, 48, 55, 56, 57};
    for (unsigned size = 0; size < 3; ++size)
        for (unsigned i = 0; i < sizeof cas_eas / sizeof *cas_eas; ++i)
            for (unsigned cmp = 0; cmp < 8; ++cmp)
                for (unsigned update = 0; update < 8; ++update)
                    for (unsigned sample = 0; sample < 32; ++sample)
                        compare((0x0AC0 + size * 0x200) | cas_eas[i], values[sample % 13], values[(sample * 7) % 13],
                                sample, cmp | update << 6 | (sample * 811 & 0xFE38));
    for (unsigned size = 0; size < 2; ++size)
        for (unsigned cmp = 0; cmp < 8; ++cmp)
            for (unsigned update = 0; update < 8; ++update)
                for (unsigned sample = 0; sample < 32; ++sample)
                    compare(size ? 0x0EFC : 0x0CFC, values[sample % 13], values[(sample * 7) % 13], sample,
                            0x8000 | cmp | update << 6 | (0x9000U | ((cmp + 1) & 7) | ((update + 1) & 7) << 6) << 16);
    const unsigned bf_eas[] = {0, 7, 16, 23, 40, 47, 48, 55, 56, 57, 58, 59};
    for (unsigned kind = 0; kind < 8; ++kind)
        for (unsigned i = 0; i < sizeof bf_eas / sizeof *bf_eas; ++i)
            for (unsigned width = 0; width < 33; ++width)
                for (unsigned offset = 0; offset < 32; ++offset)
                    for (unsigned dynamic = 0; dynamic < 4; ++dynamic) {
                        unsigned destination =
                            (kind == 1 || kind == 3 || kind == 5 || kind == 7) ? (offset & 7) << 12 : 0;
                        unsigned ext =
                            destination | (dynamic & 1 ? 0x980 : offset << 6) | (dynamic & 2 ? 0x27 : width & 31);
                        if ((offset + width) & 1)
                            ext |= 0x8000;
                        if (dynamic & 1)
                            ext |= 0x600;
                        if (dynamic & 2)
                            ext |= 0x18;
                        compare(0xE8C0 | kind << 8 | bf_eas[i], (offset * 1234567U) ^ 0xA5808001U,
                                (width * 876543U) ^ 0x87654321U, (offset + width) & 31, ext);
                    }
    /* A helper may fault or invalidate its originating executable page. No
     return address can remain in that page across the tail call. */
    destroy_blocks(0x1000, 64);
    trap_install_handler(2, fault, (void*)(uintptr_t)2);
    trap_install_handler(4, fault, (void*)(uintptr_t)4);
    for (unsigned kind = 0; kind < 5; ++kind) {
        unsigned op = kind == 0 ? 0x0ED0 : kind == 1 ? 0xE9D0 : kind == 2 ? 0x8148 : kind == 3 ? 0x0AC8 : 0xEAD0;
        WRITEUW(0x1000, op);
        WRITEUW(0x1002, kind == 4 ? 1 : 0);
        int8 length = 2;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        cpu_state.sr = 0x2000;
        EM_A7 = cpu_state.isp = 0xE000;
        EM_A0 = kind == 2 ? 0x10001 : kind == 4 ? 0x1000 : 0xFFFF;
        cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 1;
        cpu_state.ccnz = 0;
        fault_vector = kind == 3 ? 4 : 2;
        fault_address = 0x10000;
        retire_code = code;
        uint32 next = syn68k_x64_execute(code);
        assert(!retire_code);
        if (kind == 4)
            assert(next == 0x1004 && READUW(0x1000) == (op ^ 0x8000));
        else
            assert(next == MAGIC_EXIT_EMULATOR_ADDRESS && fault_count == kind + 1);
    }
    /* An actual registered block is destroyed by its own bitfield store. */
    WRITEUW(0x1000, 0xEAD0);
    WRITEUW(0x1002, 1);
    WRITEUW(0x1004, 0x4EF9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A0 = 0x1000;
    syn68k_x64_enabled = 1;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(READUW(0x1000) == 0x6AD0);
    destroy_blocks(0x1000, 64);
    /* A helper in the middle of a larger block must end the native prefix. */
    WRITEUW(0x1000, 0x4E71);
    WRITEUW(0x1002, 0xC300);
    WRITEUW(0x1004, 0x4E71);
    int8 lengths[] = {1, 1, 1};
    unsigned translated;
    int terminal;
    void* prefix = syn68k_x64_compile(SYN68K_TO_US(0x1000), lengths, 3, &translated, 0x1000, &terminal);
    assert(prefix && translated == 2 && terminal);
    assert(syn68k_x64_execute(prefix) == 0x1004);
    syn68k_x64_release(prefix);
    printf("%u complex-integer differential cases and invalidation/fault tails passed.\n", cases);
    free(ram);
    return 0;
}
