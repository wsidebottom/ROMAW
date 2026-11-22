/*+===================================================================
  File:      fpsp-test.c

  Summary:   Implements fpsp test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Execute Motorola's guest FPSP through both real CPU dispatch modes. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fpsp-reference.h"

static unsigned trapped, cases;
static uint64 tolerance;
static uint32 original_vectors[64];

static syn68k_addr_t failed(syn68k_addr_t pc, void* argument)
{
    trapped = (unsigned)(uintptr_t)argument;
    fprintf(stderr, "FPSP trap %u at %08x, frame %04x\n", trapped, pc, READUW(EM_A7 + 6));
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void run_pair(unsigned command, unsigned control, unsigned exponent, uint64 significand,
                     unsigned target_exponent, uint64 target_significand, unsigned expected_exponent,
                     uint64 expected_significand)
{
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = control;
    cpu_state.fpu.regs[1].exponent = exponent;
    cpu_state.fpu.regs[1].significand = significand;
    cpu_state.fpu.regs[0].exponent = target_exponent;
    cpu_state.fpu.regs[0].significand = target_significand;
    destroy_blocks(0x1000, 16);
    WRITEUW(0x1000, 0xf200);
    WRITEUW(0x1002, command);
    WRITEUW(0x1004, 0x4e75);
    EM_A7 = 0xff000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    uint64 actual = cpu_state.fpu.regs[0].significand;
    uint64 difference = actual > expected_significand ? actual - expected_significand : expected_significand - actual;
    if (trapped || cpu_state.fpu.regs[0].exponent != expected_exponent || difference > tolerance)
        fprintf(stderr,
                "FPSP command %04x control %04x: %04x %016llx, expected "
                "%04x %016llx\n",
                command, control, cpu_state.fpu.regs[0].exponent, (unsigned long long)cpu_state.fpu.regs[0].significand,
                expected_exponent, (unsigned long long)expected_significand);
    assert(!trapped);
    assert(cpu_state.fpu.regs[0].exponent == expected_exponent);
    assert(difference <= tolerance);
    assert(cpu_state.fpu.fpcr == control && !cpu_state.fpu.frame_size);
    ++cases;
}

static void run(unsigned command, unsigned control, unsigned exponent, uint64 significand, unsigned expected_exponent,
                uint64 expected_significand)
{
    run_pair(command, control, exponent, significand, 0x7fff, UINT64_MAX, expected_exponent, expected_significand);
}

static void user_mode(void)
{
    cpu_state.sr = 0;
    cpu_state.isp = 0xfe000;
    cpu_state.msp = 0xfd000;
    cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccx = 1;
    cpu_state.ccnz = 0;
    run(0x0401, 0, 0x4000, UINT64_C(0xf000000000000000), 0x4001,
        UINT64_C(0x8000000000000000)); /* User FINT(3.75) = 4. */
    assert(!cpu_state.sr && EM_A7 == 0xff000 && cpu_state.isp == 0xfe000);
    assert(cpu_state.ccc && cpu_state.ccv && cpu_state.ccn && cpu_state.ccx && !cpu_state.ccnz);
    syn68k_fpu_reset();
    destroy_blocks(0x1000, 16);
    WRITEUW(0x1000, 0xf21f); /* FINT.L (A7)+,FP0 on the user stack. */
    WRITEUW(0x1002, 0x4001);
    WRITEUW(0x1004, 0x4ef9);
    WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
    EM_A7 = 0xff000;
    WRITEUL(EM_A7, 3);
    trapped = 0;
    interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
    assert(!trapped && !cpu_state.sr && EM_A7 == 0xff004);
    assert(cpu_state.isp == 0xfe000 && !cpu_state.fpu.frame_size);
    assert(cpu_state.fpu.regs[0].exponent == 0x4000 &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xc000000000000000));
    assert(cpu_state.ccc && cpu_state.ccv && cpu_state.ccn && cpu_state.ccx && !cpu_state.ccnz);
    ++cases;
    cpu_state.sr = 0x2000;
    EM_A7 = 0xff000;
}

static void packed_decimal(void)
{
    static const struct {
        uint32 packed;
        unsigned exponent;
        uint64 significand;
    } values[] = {
        {0x00000001, 0x3fff, UINT64_C(0xa000000000000000)}, /* 1.25 */
        {0x80010001, 0xc002, UINT64_C(0xc800000000000000)}, /* -12.5 */
        {0x40010001, 0x3ffc, UINT64_C(0x8000000000000000)}  /* 0.125 */
    };

    for (unsigned i = 0; i < sizeof values / sizeof *values; ++i)
        for (unsigned dynamic = 0; dynamic < 2; ++dynamic) {
            syn68k_fpu_reset();
            WRITEUL(0x5000, values[i].packed);
            WRITEUL(0x5004, 0x25000000);
            WRITEUL(0x5008, 0);
            destroy_blocks(0x1000, 32);
            WRITEUW(0x1000, 0xf218); /* fmove.p (a0)+,fp0 */
            WRITEUW(0x1002, 0x4c00);
            WRITEUW(0x1004, 0xf219); /* fmove.p fp0,(a1)+,{17} or {d2} */
            WRITEUW(0x1006, dynamic ? 0x7c20 : 0x6c11);
            WRITEUW(0x1008, 0x4e75);
            EM_A0 = 0x5000;
            EM_A1 = 0x5100;
            EM_D2 = 17;
            EM_A7 = 0xff000;
            trapped = 0;
            CALL_EMULATOR(0x1000);
            assert(!trapped && !cpu_state.fpu.frame_size);
            assert(EM_A0 == 0x500c && EM_A1 == 0x510c);
            assert(cpu_state.fpu.regs[0].exponent == values[i].exponent);
            assert(cpu_state.fpu.regs[0].significand == values[i].significand);
            assert(READUL(0x5100) == values[i].packed);
            assert(READUL(0x5104) == 0x25000000 && !READUL(0x5108));
            cases += 2;
        }
}

static void install(uint32 base)
{
    for (unsigned i = 0; i < 64; ++i)
        WRITEUL(cpu_state.vbr + i * 4, original_vectors[i]);
    assert(syn68k_fpsp_install(base));
    assert(!syn68k_fpsp_install(base));
    unsigned char guard[16];
    memset(SYN68K_TO_US(0xff000), 0xa5, sizeof guard);
    memcpy(guard, SYN68K_TO_US(0xff000), sizeof guard);
    uint32 vector = READUL(cpu_state.vbr + 11 * 4);
    assert(!syn68k_fpsp_install(0xff000));
    assert(!syn68k_fpsp_install(0xfffffff0));
    assert(!syn68k_fpsp_install(base + 1));
    assert(!syn68k_fpsp_install(cpu_state.vbr));
    assert(READUL(cpu_state.vbr + 11 * 4) == vector);
    assert(!memcmp(guard, SYN68K_TO_US(0xff000), sizeof guard));
}

static void forward_exception(unsigned opcode, unsigned command, unsigned control, unsigned exponent,
                              uint64 significand, unsigned expected)
{
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = control;
    cpu_state.fpu.regs[1].exponent = exponent;
    cpu_state.fpu.regs[1].significand = significand;
    destroy_blocks(0x1000, 16);
    WRITEUW(0x1000, opcode);
    WRITEUW(0x1002, command);
    WRITEUW(0x1004, 0xf280); /* Report a restored deferred exception. */
    WRITEUW(0x1006, 0);
    WRITEUW(0x1008, 0x4e75);
    EM_A7 = 0xff000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    if (trapped != expected)
        fprintf(stderr, "FPSP forwarding %04x/%04x: trap %u, expected %u\n", opcode, command, trapped, expected);
    assert(trapped == expected);
    ++cases;
}

int main(int argc, char** argv)
{
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, argc > 1 ? atoi(argv[1]) : 0, vectors, 0);
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, failed, (void*)(uintptr_t)i);
    for (unsigned i = 0; i < 64; ++i)
        original_vectors[i] = READUL(cpu_state.vbr + i * 4);
    cpu_state.vbr = 0x4000;
    for (unsigned relocation = 0; relocation < 2; ++relocation) {
        install(relocation ? 0x30000 : 0x10000);
        run(0x0401, 0, 0x4000, UINT64_C(0xf000000000000000), 0x4001, UINT64_C(0x8000000000000000)); /* FINT 3.75 = 4. */
        run(0x0403, 0, 0xc000, UINT64_C(0xf000000000000000), 0xc000,
            UINT64_C(0xc000000000000000)); /* FINTRZ -3.75 = -3. */
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned negative = 0; negative < 2; ++negative) {
                int four = mode == 0 || (mode == 2 && negative) || (mode == 3 && !negative);
                run(0x0401, mode << 4, 0x4000 | negative << 15, UINT64_C(0xf000000000000000),
                    (four ? 0x4001 : 0x4000) | negative << 15,
                    four ? UINT64_C(0x8000000000000000) : UINT64_C(0xc000000000000000));
            }
        run(0x5c00, 0, 0, 0, 0x4000, UINT64_C(0xc90fdaa22168c235));
        static const unsigned zero_to_zero[] = {0x02, 0x06, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0e, 0x0f};
        for (unsigned i = 0; i < sizeof zero_to_zero / sizeof *zero_to_zero; ++i)
            run(0x0400 | zero_to_zero[i], 0, 0, 0, 0, 0);
        static const unsigned zero_to_one[] = {0x10, 0x11, 0x12, 0x19, 0x1d};
        for (unsigned i = 0; i < sizeof zero_to_one / sizeof *zero_to_one; ++i)
            run(0x0400 | zero_to_one[i], 0, 0, 0, 0x3fff, UINT64_C(0x8000000000000000));
        run(0x0414, 0, 0x3fff, UINT64_C(0x8000000000000000), 0, 0);
        run(0x041c, 0, 0x3fff, UINT64_C(0x8000000000000000), 0, 0);
        run(0x0416, 0, 0x4000, UINT64_C(0x8000000000000000), 0x3fff, UINT64_C(0x8000000000000000)); /* log2(2) = 1. */
        run(0x0412, 0, 0x4000, UINT64_C(0x8000000000000000), 0x4005, UINT64_C(0xc800000000000000)); /* 10^2 = 100. */
        run_pair(0x0421, 0, 0x4000, UINT64_C(0xc000000000000000), 0x4002, UINT64_C(0xb000000000000000), 0x4000,
                 UINT64_C(0x8000000000000000)); /* 11 mod 3 = 2 */
        assert((cpu_state.fpu.fpsr & 0x00ff0000) == 0x00030000);
        run_pair(0x0425, 0, 0x4000, UINT64_C(0xc000000000000000), 0x4002, UINT64_C(0xb000000000000000), 0xbfff,
                 UINT64_C(0x8000000000000000)); /* remainder(11,3) = -1 */
        assert((cpu_state.fpu.fpsr & 0x00ff0000) == 0x00040000);
        run_pair(0x0426, 0, 0x4000, UINT64_C(0xc000000000000000), 0x3fff, UINT64_C(0xc000000000000000), 0x4002,
                 UINT64_C(0xc000000000000000)); /* scale(1.5,3) = 12 */
        run(0x041e, 0, 0x4002, UINT64_C(0xc800000000000000), 0x4000,
            UINT64_C(0xc000000000000000)); /* getexp(12.5) = 3 */
        run(0x041f, 0, 0x4002, UINT64_C(0xc800000000000000), 0x3fff,
            UINT64_C(0xc800000000000000)); /* getman(12.5) = 1.5625 */
        run(0x0432, 0, 0, 0, 0, 0);        /* sincos(0): sin in FP0, cos in FP2 */
        assert(cpu_state.fpu.regs[2].exponent == 0x3fff &&
               cpu_state.fpu.regs[2].significand == UINT64_C(0x8000000000000000));
        packed_decimal();
        user_mode();
        run(0x0400, 0, 0, 1, 0, 1);                                 /* Unsupported extended denormal move. */
        run(0x0404, 0, 0, 1, 0x1fe0, UINT64_C(0x8000000000000000)); /* sqrt(2^-16446). */
        /* FPSP transcendental approximations are checked within one extended
         ULP; the exact-value and rounding cases above require exact bits. */
        tolerance = 1;
        for (unsigned i = 0; i < sizeof fpsp_reference / sizeof *fpsp_reference; ++i)
            run(0x0400 | fpsp_reference[i].operation, 0, fpsp_reference[i].input_exponent,
                fpsp_reference[i].input_significand, fpsp_reference[i].exponent, fpsp_reference[i].significand);
        tolerance = 0;
        forward_exception(0xf000, 0, 0, 0, 0, 11);
        forward_exception(0xf200, 0x0401, 0x200, 0x4000, UINT64_C(0xf000000000000000), 49);
        forward_exception(0xf200, 0x040e, 0x4000, 0x7fff, UINT64_C(0x8000000000000001), 54);
        forward_exception(0xf200, 0x0404, 0x2000, 0x8000, 1, 52);
    }
    printf("%u Motorola FPSP cases passed at two load addresses.\n", cases);
    return 0;
}
