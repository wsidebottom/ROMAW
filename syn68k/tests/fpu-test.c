/*+===================================================================
  File:      fpu-test.c

  Summary:   Implements fpu test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Execute instruction words through both real Syn68k dispatch paths. */
#include "syn68k_public.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned trapped;
static uint16 frame;
static uint32 stacked_pc;
static int return_from_exception;

static syn68k_addr_t exception_handler(syn68k_addr_t address, void* argument)
{
    (void)address;
    trapped = (unsigned)(uintptr_t)argument;
    frame = READUW(EM_A7 + 6);
    stacked_pc = READUL(EM_A7 + 2);
    return return_from_exception ? MAGIC_RTE_ADDRESS : MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void run(const uint16* words, size_t count, unsigned expected_trap)
{
    destroy_blocks(0x1000, 512);
    for (size_t i = 0; i < count; ++i)
        WRITEUW(0x1000 + i * 2, words[i]);
    WRITEUW(0x1000 + count * 2, 0x4e75);
    EM_A7 = 0xff000;
    trapped = 0;
    CALL_EMULATOR(0x1000);
    if (trapped != expected_trap)
        fprintf(stderr, "opcode %04x: exception %u, expected %u\n", words[0], trapped, expected_trap);
    assert(trapped == expected_trap);
}

#define RUN(expected, ...)                                  \
    do {                                                    \
        const uint16 words[] = {__VA_ARGS__};               \
        run(words, sizeof words / sizeof *words, expected); \
    } while (0)

static void fp(unsigned reg, uint16 exponent, uint64 significand)
{
    cpu_state.fpu.regs[reg].exponent = exponent;
    cpu_state.fpu.regs[reg].significand = significand;
}

static void expect_fp(unsigned reg, uint16 exponent, uint64 significand)
{
    assert(cpu_state.fpu.regs[reg].exponent == exponent);
    assert(cpu_state.fpu.regs[reg].significand == significand);
}

static void conditional_tests(void)
{
    /* Truth masks indexed by condition 0..15: false, EQ, OGT, OGE, OLT,
     OLE, OGL, OR, UN, UEQ, UGT, UGE, ULT, ULE, NE, true. */
    static const struct {
        uint32 fpsr;
        uint16 truth;
    } states[] = {{0, 0xcccc}, {0x08000000, 0xf0f0}, {0x04000000, 0xaaaa}, {0x0c000000, 0xaaaa}, {0x01000000, 0xff00}};

    for (unsigned state = 0; state < sizeof states / sizeof *states; ++state)
        for (unsigned code = 0; code < 32; ++code) {
            int yes = !!(states[state].truth & (1u << (code & 15)));
            cpu_state.fpu.fpcr = 0;
            cpu_state.fpu.fpsr = states[state].fpsr;
            EM_D0 = 0xabcdef00;
            RUN(0, 0xf240, code);
            assert(EM_D0 == (0xabcdef00u | (yes ? 255 : 0)));
            RUN(0, 0xf280 | code, 6, 0x7001, 0x6002, 0x7002);
            assert(EM_D0 == (yes ? 2 : 1));
            EM_D1 = 1;
            RUN(0, 0xf249, code, 6, 0x7001, 0x6002, 0x7002);
            assert(EM_D0 == (yes ? 1 : 2) && EM_D1 == (yes ? 1 : 0));
            RUN(yes ? 7 : 0, 0xf27c, code);
        }
}

static void control_addressing_tests(void)
{
    /* PRM FMOVEM control-register EA table: Dn allows one control register;
     An allows FPIAR alone. Both directions retain the instruction's PC on
     an invalid encoding and must leave the source/destination unchanged. */
    syn68k_fpu_reset();
    cpu_state.sr = 0x2000;
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned address_register = 0; address_register < 2; ++address_register)
            for (unsigned mask = 0; mask < 8; ++mask) {
                cpu_state.fpu.fpcr = 0x90;
                cpu_state.fpu.fpsr = 0x0a120098;
                cpu_state.fpu.fpiar = 0x12345678;
                EM_D0 = 0x56789ab0;
                EM_A0 = 0x56789ab0;
                uint16 words[] = {0xf200 | (address_register ? 8 : 0), 0x8000 | (writing << 13) | (mask << 10)};
                int valid = address_register ? mask == 1 : mask == 1 || mask == 2 || mask == 4;
                run(words, 2, valid ? 0 : 11);
                if (!valid) {
                    assert(frame == 0x002c && stacked_pc == 0x1000);
                    assert(EM_A0 == 0x56789ab0 && EM_D0 == 0x56789ab0);
                    assert(cpu_state.fpu.fpcr == 0x90 && cpu_state.fpu.fpsr == 0x0a120098 &&
                           cpu_state.fpu.fpiar == 0x12345678);
                }
                else if (writing) {
                    uint32 expected = mask == 4 ? 0x90 : mask == 2 ? 0x0a120098 : 0x12345678;
                    assert((address_register ? EM_A0 : EM_D0) == expected);
                }
                else
                    assert((mask == 4   ? cpu_state.fpu.fpcr
                            : mask == 2 ? cpu_state.fpu.fpsr
                                        : cpu_state.fpu.fpiar) == (mask == 4   ? 0x9ab0
                                                                   : mask == 2 ? 0x06789ab0
                                                                               : 0x56789ab0));
            }
    /* Zero masks, reserved extension bits and invalid output EAs must not
     evaluate postincrement or fetch an inaccessible extension/operand. */
    EM_A0 = 0x100000;
    RUN(11, 0xf218, 0x8000);
    assert(EM_A0 == 0x100000);
    RUN(11, 0xf218, 0x9001);
    assert(EM_A0 == 0x100000);
    RUN(11, 0xf23c, 0xb000);
    assert(frame == 0x002c && stacked_pc == 0x1000);
    syn68k_fpu_reset();
}

static void reserved_encoding_tests(void)
{
    /* Invalid instruction patterns use the F-line vector with a format-zero
     frame, not the ordinary illegal-instruction vector or the FPSP format.
     Use an unmapped A0 to expose operand access before legality checks. */
    static const uint16 instructions[][4] = {{0xf200, 0x2000}, /* Reserved arithmetic class. */
                                             {0xf208, 0x4000}, /* An arithmetic source. */
                                             {0xf210, 0x0400}, /* EA bits in register-to-register arithmetic. */
                                             {0xf218, 0x4007}, /* Reserved operation before postincrement. */
                                             {0xf218, 0x5c00}, /* FMOVECR requires the zero EA field. */
                                             {0xf200, 0x6800}, /* Extended result cannot use Dn. */
                                             {0xf218, 0xf080}, /* FMOVEM output cannot postincrement. */
                                             {0xf220, 0xd080}, /* FMOVEM input cannot predecrement. */
                                             {0xf210, 0xd180}, /* Reserved FMOVEM extension bit. */
                                             {0xf210, 0xd880}, /* Reserved dynamic register-list bit. */
                                             {0xf240, 0x0020}, /* Reserved FScc condition. */
                                             {0xf248, 0x0020}, /* Reserved FDBcc condition. */
                                             {0xf27c, 0x0020}, /* Reserved FTRAPcc condition. */
                                             {0xf27d, 0x0000}, /* Reserved FTRAPcc encoding. */
                                             {0xf2a0, 0x0000}, /* Reserved word FBcc condition. */
                                             {0xf2e0, 0x0000}, /* Reserved long FBcc condition. */
                                             {0xf300},         /* FSAVE Dn. */
                                             {0xf308},         /* FSAVE An. */
                                             {0xf318},         /* FSAVE (An)+. */
                                             {0xf33a},         /* FSAVE PC-relative. */
                                             {0xf360},         /* FRESTORE -(An). */
                                             {0xf37c},         /* FRESTORE immediate. */
                                             {0xf380},         /* Reserved FPU instruction class. */
                                             {0xf3c0}};
    for (unsigned i = 0; i < sizeof instructions / sizeof *instructions; ++i) {
        syn68k_fpu_reset();
        cpu_state.sr = 0x2000;
        EM_A0 = 0x100000;
        EM_D0 = 0x12345678;
        run(instructions[i], 4, 11);
        assert(frame == 0x002c && stacked_pc == 0x1000);
        assert(EM_A0 == 0x100000 && EM_D0 == 0x12345678);
        assert(cpu_state.fpu.null_state && !cpu_state.fpu.frame_size && !cpu_state.fpu.fpcr && !cpu_state.fpu.fpsr &&
               !cpu_state.fpu.fpiar);
        expect_fp(0, 0x7fff, UINT64_MAX);
    }
    /* A reserved first word has no extension: do not fetch across RAM's end. */
    syn68k_fpu_reset();
    WRITEUW(0xffffe, 0xf380);
    EM_A7 = 0xff000;
    trapped = 0;
    syn68k_addr_t next;
    assert(syn68k_fpu_execute(0xffffe, &next) && trapped == 11);
    assert(frame == 0x002c && stacked_pc == 0xffffe);
}

static void signed_frame_exponent_tests(void)
{
    /* FPSP get_op's mk_norm sets ETE15/FPTE15 for ordinary biased
     exponents <= $3fff too. Only the wrapped range $4000..$7fff is
     negative when that bit is set (res_func's signed 15-bit extract). */
    for (unsigned busy = 0; busy < 2; ++busy) {
        syn68k_fpu_reset();
        unsigned size = busy ? 100 : 52;
        memset(SYN68K_TO_US(0x7000), 0, size);
        WRITEUL(0x7000, busy ? 0x41600000 : 0x41300000);
        WRITEUB(0x7000 + size - 40, 0x10);
        WRITEUW(0x7000 + size - 36, 0x0422); /* 1 + 0.5. */
        WRITEUB(0x7000 + size - 32, 0x10);
        WRITEUB(0x7000 + size - 28, 4);
        WRITEUW(0x7000 + size - 24, 0x3fff);
        WRITEUL(0x7000 + size - 20, 0x80000000);
        WRITEUW(0x7000 + size - 12, 0x3ffe);
        WRITEUL(0x7000 + size - 8, 0x80000000);
        EM_A0 = 0x7000;
        RUN(0, 0xf358);
        expect_fp(0, 0x3fff, UINT64_C(0xc000000000000000));
        assert(EM_A0 == 0x7000 + size && !cpu_state.fpu.pending_vector);
    }
    static const unsigned operations[] = {0x20, 0x22, 5, 0x23};
    for (unsigned test = 0; test < sizeof operations / sizeof *operations; ++test) {
        syn68k_fpu_reset();
        memset(SYN68K_TO_US(0x7000), 0, 100);
        WRITEUL(0x7000, 0x41600000);
        WRITEUL(0x7028, 0x12345678);
        WRITEUB(0x703c, 0x10); /* ETEMP exponent bit 15, normalized tag. */
        WRITEUW(0x7040, 0x0400 | operations[test]);
        WRITEUB(0x7044, 0x10); /* FPTEMP exponent bit 15. */
        WRITEUB(0x7048, 4);
        WRITEUW(0x704c, 0x7fff);
        WRITEUL(0x7050, 0x80000000);
        WRITEUW(0x7058, 0x7fff);
        WRITEUL(0x705c, 0x80000000);
        EM_A0 = 0x7000;
        RUN(0, 0xf358);
        assert(EM_A0 == 0x7064 && cpu_state.fpu.fpiar == 0x12345678);
        if (test == 0)
            expect_fp(0, 0x3fff, UINT64_C(0x8000000000000000));
        else if (test == 1)
            expect_fp(0, 0, UINT64_C(0x8000000000000000));
        else if (test == 2)
            expect_fp(0, 0x1fff, UINT64_C(0x8000000000000000));
        else {
            assert(cpu_state.fpu.pending_vector == 51);
            assert(!(cpu_state.fpu.fpsr & 0x07000000));
            RUN(51, 0xf280, 0);
        }
    }
    syn68k_fpu_reset();
}

static void sqrt_unsupported_frame_test(void)
{
    syn68k_fpu_reset();
    fp(1, 0, 1);
    RUN(55, 0xf200, 0x0404);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    /* The 040 recodes FSQRT to operation 5 in its internal command.
     FPSP uses bit zero to distinguish it from conversion-unit work. */
    assert(READUW(0x7040) == 0x0405 && READUB(0x7048) == 4);
    syn68k_fpu_reset();
}

static void operand_frame_tag_tests(void)
{
    static const unsigned exponents[] = {0, 0x3ffe, 0x3fff, 0x4000};
    for (unsigned i = 0; i < sizeof exponents / sizeof *exponents; ++i) {
        syn68k_fpu_reset();
        fp(0, 0, 0);
        fp(1, exponents[i], UINT64_C(0x8000000000000000));
        RUN(11, 0xf200, 0x0401);
        EM_A0 = 0x7000;
        RUN(0, 0xf310);
        assert(READUB(0x700c) == (exponents[i] <= 0x3fff ? 0x10 : 0));
        assert(READUB(0x7014) == 0x30); /* Zero destination: tag + FPTE15. */
    }
    syn68k_fpu_reset();
}

static void software_exception_frame_test(void)
{
    syn68k_fpu_reset();
    memset(SYN68K_TO_US(0x7000), 0, 100);
    WRITEUL(0x7000, 0x41600000);
    WRITEUB(0x703c, 0x60);
    WRITEUW(0x7040, 0x040e); /* FPSP completed FSIN, posting SNAN. */
    WRITEUB(0x7048, 5);      /* E1 + software exception S. */
    WRITEUB(0x7049, 0x80);   /* X. */
    WRITEUW(0x7058, 0x7fff);
    WRITEUL(0x705c, 0xc0000000);
    cpu_state.fpu.fpcr = cpu_state.fpu.fpsr = 0x4000;
    EM_A0 = 0x7000;
    RUN(0, 0xf350);
    assert(cpu_state.fpu.pending_vector == 54 && cpu_state.fpu.frame_size == 100);
    EM_A0 = 0xfffd0; /* Short frame still needs 52 writable bytes. */
    RUN(2, 0xf310);
    assert(cpu_state.fpu.pending_vector == 54 && cpu_state.fpu.frame_size == 100);
    memset(SYN68K_TO_US(0x8000), 0xa5, 100);
    EM_A0 = 0x8034;
    RUN(0, 0xf320); /* FSAVE -(A0), contracting the E1 frame. */
    assert(EM_A0 == 0x8000 && READUL(0x8000) == 0x41300000);
    assert(READUB(0x800c) == 0x60 && READUW(0x8010) == 0x040e);
    assert(READUB(0x8034) == 0xa5);
    RUN(0, 0xf358);
    assert(EM_A0 == 0x8034 && cpu_state.fpu.pending_vector == 54);
    syn68k_fpu_reset();
}

static void single_mantissa_tests(void)
{
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = 0x80; /* FSGL ignores the FPCR precision. */
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    fp(1, 0x4000, UINT64_C(0xc000000000000000));
    RUN(0, 0xf200, 0x0424);
    expect_fp(0, 0x3ffd, UINT64_C(0xaaaaab0000000000));
    assert((cpu_state.fpu.fpsr & 0x208) == 0x208);
    cpu_state.fpu.fpcr = 0x40;
    fp(0, 0x40c7, UINT64_C(0x8000000000000000));
    fp(1, 0x40c7, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0427); /* Extended range, despite single FPCR precision. */
    expect_fp(0, 0x418f, UINT64_C(0x8000000000000000));
    fp(0, 0x3fff, UINT64_C(0x8000000000000001));
    fp(1, 0x4000, UINT64_C(0xc000000000000001));
    RUN(0, 0xf200, 0x0427); /* Discard excess input mantissa before multiplying. */
    expect_fp(0, 0x4000, UINT64_C(0xc000000000000000));
    assert(!(cpu_state.fpu.fpsr & 0xff00));
    cpu_state.fpu.fpcr = 0x200;
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    RUN(49, 0xf200, 0x0424, 0xf280, 0);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUW(0x7034) == 0x30 && READUW(0x7040) == 0x0424);
    RUN(0, 0xf350);
    assert(cpu_state.fpu.pending_vector == 49);
    syn68k_fpu_reset();
    fp(0, 0, UINT64_C(0x8000000000000000));
    fp(1, 0x3ffe, UINT64_C(0x8000000000000000));
    RUN(51, 0xf200, 0x0427, 0xf280, 0);
    syn68k_fpu_reset();
}

static void nan_selection_tests(void)
{
    static const unsigned operations[] = {0x20, 0x22, 0x23, 0x28};
    for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
        for (unsigned src = 0; src < 3; ++src)
            for (unsigned dst = 0; dst < 3; ++dst) {
                syn68k_fpu_reset();
                uint64 a = dst == 2 ? UINT64_C(0x8000000000000011) : UINT64_C(0xc000000000000011);
                uint64 b = src == 2 ? UINT64_C(0x0000000000000022) : UINT64_C(0x4000000000000022);
                fp(0, dst ? 0x7fff : 0x3fff, dst ? a : UINT64_C(0x8000000000000000));
                fp(1, src ? 0xffff : 0x3fff, src ? b : UINT64_C(0x8000000000000000));
                RUN(0, 0xf200, 0x0400 | operations[op]);
                if (src || dst) {
                    expect_fp(0, dst ? 0x7fff : 0xffff, (dst ? a : b) | UINT64_C(0x4000000000000000));
                    assert(cpu_state.fpu.fpsr & 0x01000000);
                    assert(!!(cpu_state.fpu.fpsr & 0x4000) == (src == 2 || dst == 2));
                }
            }
    static const unsigned unary[] = {0, 4, 0x18, 0x1a, 0x40, 0x44};
    for (unsigned op = 0; op < sizeof unary / sizeof *unary; ++op) {
        syn68k_fpu_reset();
        fp(1, 0xffff, UINT64_C(0x4000000000000022));
        RUN(0, 0xf200, 0x0400 | unary[op]);
        expect_fp(0, 0xffff, UINT64_C(0x4000000000000022));
    }
    syn68k_fpu_reset();
    EM_A0 = 0x8000;
    for (unsigned operation = 0; operation < 128; ++operation)
        if ((operation >= 0x6d) || operation == 5 || operation == 7 || operation == 0x13 || operation == 0x17 ||
            operation == 0x1b || (operation >= 0x29 && operation <= 0x2f)) {
            RUN(11, 0xf218, 0x4000 | operation);
            assert(frame == 0x002c && stacked_pc == 0x1000 && EM_A0 == 0x8000);
            assert(!cpu_state.fpu.frame_size);
        }
}

static void moveout_frame_tests(void)
{
    static const struct {
        unsigned format, exponent;
    } integers[] = {{0, 0x401e}, {4, 0x400e}, {6, 0x4006}};

    for (unsigned i = 0; i < sizeof integers / sizeof *integers; ++i) {
        syn68k_fpu_reset();
        fp(0, 0x8000 | integers[i].exponent, UINT64_C(0x8000000000000000));
        unsigned command = 0x6000 | integers[i].format << 10;
        EM_D0 = 0x12345678;
        RUN(52, 0xf200, command); /* Exact integer minimum: hardware erratum. */
        assert(frame == 0x30d0 && stacked_pc == 0x1004);
        assert((cpu_state.fpu.fpsr & 0x2080) == 0x2080);
        EM_A0 = 0x7000;
        RUN(0, 0xf310);
        assert(READUL(0x7000) == 0x41600000 && READUW(0x7040) == command);
        assert(READUB(0x7048) == 4 && READUB(0x7049) == 0x10);
        assert(READUW(0x7018) == (0x8000 | integers[i].exponent));
        assert(READUL(0x701c) == 0x80000000);
        RUN(0, 0xf350);
        assert(cpu_state.fpu.pending_vector == 52);
    }
    syn68k_fpu_reset();
    fp(0, 0x7fff, UINT64_C(0x8000000000001234));
    EM_D0 = 0x76543210;
    RUN(54, 0xf200, 0x6000); /* Integer SNAN is nonmaskable. */
    assert(EM_D0 == 0x76543210 && frame == 0x30d8);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUB(0x703c) == 0x60 && READUL(0x7060) == 0x1234);
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = 0x200;
    fp(0, 0x4000, UINT64_C(0xa000000000000000));
    EM_D0 = 0xffffffff;
    RUN(49, 0xf200, 0x6000); /* INEX commits the rounded integer first. */
    assert(EM_D0 == 2 && frame == 0x30c4);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUW(0x704c) == 0x4000 && READUL(0x7050) == 0xa0000000);
    for (unsigned test = 0; test < 3; ++test) {
        syn68k_fpu_reset();
        unsigned exponent = test == 0 ? 0x407f : test == 1 ? 0x3f80 : 0x3c00;
        fp(0, exponent, UINT64_C(0x8000000000000000));
        cpu_state.fpu.fpsr = 0x08000000;
        EM_A0 = 0x8000;
        unsigned command = test == 2 ? 0x7400 : 0x6400;
        RUN(test == 0 ? 53 : 51, 0xf218, command);
        assert(stacked_pc == 0x1004 && READUL(EM_A7 + 8) == 0x8000);
        assert(EM_A0 == (test == 2 ? 0x8008 : 0x8004));
        assert((cpu_state.fpu.fpsr & 0x0f000000) == 0x08000000);
        if (test == 1)
            assert(READUL(0x8000) == 0x00400000); /* Exact tiny single. */
        if (test == 2)
            assert(READUL(0x8000) == 0x00080000 && !READUL(0x8004));
        EM_A0 = 0x7000;
        RUN(0, 0xf310);
        assert(READUB(0x7048) == 4 && READUB(0x7049) == 0x10);
        assert(READUW(0x704c) == exponent);
    }
    syn68k_fpu_reset();
}

static void unsupported_frame_tests(void)
{
    syn68k_fpu_reset();
    fp(0, 0x4000, UINT64_C(0xc000000000000000)); /* 3 */
    EM_A0 = 0x8000;
    WRITEUL(0x8000, 0x80000001); /* Negative minimum single denormal. */
    RUN(55, 0xf218, 0x4422);
    assert(stacked_pc == 0x1004 && frame == 0x00dc && EM_A0 == 0x8004);
    expect_fp(0, 0x4000, UINT64_C(0xc000000000000000));
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUL(0x7000) == 0x41600000 && READUW(0x7040) == 0x4422);
    assert(READUB(0x703c) == 0xa0 && READUB(0x7048) == 4);
    assert(READUW(0x7058) == 0x8000 && READUL(0x705c) == 0x100 && !READUL(0x7060));
    RUN(0, 0xf350);
    assert(cpu_state.fpu.pending_vector == 55);
    RUN(55, 0xf280, 0);
    RUN(0, 0xf310);
    /* A guest handler can replace the unsupported source with a normalized
     operand. FRESTORE resumes the saved operation, without rereading memory. */
    WRITEUB(0x703c, 0);
    WRITEUW(0x7058, 0x4000);
    WRITEUL(0x705c, 0x80000000);
    cpu_state.fpu.fpsr = 0;
    RUN(0, 0xf358);
    assert(EM_A0 == 0x7064 && !cpu_state.fpu.frame_size);
    expect_fp(0, 0x4001, UINT64_C(0xa000000000000000)); /* 3 + 2 */
    assert(READUL(0x8000) == 0x80000001 && cpu_state.fpu.fpiar == 0x1000);
    EM_A0 = 0x8000;
    WRITEUL(0x8000, 0x12345678);
    WRITEUL(0x8004, 0x89abcdef);
    WRITEUL(0x8008, 0x01234567);
    RUN(55, 0xf218, 0x4c00); /* Packed source fetched in full. */
    assert(EM_A0 == 0x800c);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUL(0x7054) == 0x12345678 && READUL(0x705c) == 0x89abcdef && READUL(0x7060) == 0x01234567);
    EM_A0 = 0xffff8;
    RUN(2, 0xf210, 0x4c00); /* An access fault takes priority. */
    assert(!cpu_state.fpu.frame_size);
    fp(0, 0x3fff, 1); /* Extended unnormal, preserved without conversion. */
    EM_A0 = 0x8000;
    RUN(55, 0xf210, 0x6800);
    assert(frame == 0x30dc && READUL(EM_A7 + 8) == 0x8000);
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUB(0x703c) == 0x80 && READUB(0x7049) == 0x10);
    assert(READUW(0x7058) == 0x3fff && READUL(0x7060) == 1);
    /* Disabling an E1 arithmetic exception resumes the saved operands. */
    syn68k_fpu_reset();
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    fp(1, 0, 0);
    cpu_state.fpu.fpcr = 0x400;
    RUN(50, 0xf200, 0x0420, 0xf280, 0);
    RUN(0, 0xf310);
    cpu_state.fpu.fpcr = 0;
    RUN(0, 0xf350);
    expect_fp(0, 0x7fff, UINT64_C(0x8000000000000000));
    assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
    /* Motorola ignores the explicit integer bit in infinity encodings. */
    fp(0, 0x7fff, 0);
    fp(1, 0x7fff, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0438);
    assert((cpu_state.fpu.fpsr & 0x0f000000) == 0x04000000);
    fp(1, 0, 0);
    RUN(0, 0xf200, 0x0438);
    assert(!(cpu_state.fpu.fpsr & 0x0f000000));
    syn68k_fpu_reset();
}

static void arithmetic_frame_tests(void)
{
    static const unsigned operations[] = {0x22, 0x28, 0x23, 4};
    for (unsigned i = 0; i < sizeof operations / sizeof *operations; ++i) {
        syn68k_fpu_reset();
        cpu_state.fpu.fpcr = 0x200;
        fp(0, 0x3fff, i == 2 ? UINT64_MAX : UINT64_C(0x8000000000000000));
        fp(1,
           i == 0   ? 0x3fbf
           : i == 1 ? 0x3fbe
           : i == 2 ? 0x3fff
                    : 0x4000,
           i == 2 ? UINT64_MAX : UINT64_C(0x8000000000000000));
        RUN(49, 0xf200, 0x0400 | operations[i], 0x7007, 0xf280, 0);
        assert(stacked_pc == 0x1006 && EM_D0 == 7);
        assert(cpu_state.fpu.frame_size == 100);
        EM_A0 = 0x7000;
        RUN(0, 0xf310);
        unsigned command = 0x0400 | (operations[i] == 4 ? 5 : operations[i]);
        assert(READUW(0x7040) == command && READUB(0x7048) == 2);
        if (i == 0) {
            expect_fp(0, 0x3fff, UINT64_C(0x8000000000000000));
            assert(READUL(0x701c) == 0x80000000 && READUB(0x703c) == 0x12);
        }
        if (i == 3) {
            expect_fp(0, 0x3fff, UINT64_C(0xb504f333f9de6484));
            assert(READUW(0x7034) == 0x21);
        }
        RUN(0, 0xf358);
        assert(EM_A0 == 0x7064 && cpu_state.fpu.pending_vector == 49);
        RUN(49, 0xf280, 0);
    }
    syn68k_fpu_reset();
    /* FABS's conversion-unit exception carries an unrounded FPTEMP. */
    cpu_state.fpu.fpcr = 0x240;
    fp(1, 0xbfff, UINT64_C(0x8000000000000001));
    RUN(49, 0xf200, 0x0418, 0xf280, 0);
    expect_fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    EM_A0 = 0x7000;
    RUN(0, 0xf310);
    assert(READUL(0x7000) == 0x41300000 && READUB(0x7018) == 4);
    assert(READUW(0x701c) == 0x3fff && READUL(0x7024) == 1);
    RUN(0, 0xf350);
    assert(cpu_state.fpu.pending_vector == 49);
    syn68k_fpu_reset();
    fp(0, 0, UINT64_C(0x8000000000000000));
    fp(1, 0, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0422);
    expect_fp(0, 1, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0438);
    assert(!(cpu_state.fpu.fpsr & 0x0f000000)); /* 2*min > min. */
    cpu_state.fpu.fpcr = 0x2000;
    fp(0, 0x7fff, UINT64_C(0x8000000000000000));
    fp(1, 0xffff, UINT64_C(0x8000000000000000));
    RUN(52, 0xf200, 0x0422, 0x7009, 0xf280, 0);
    expect_fp(0, 0x7fff, UINT64_C(0x8000000000000000));
    assert(EM_D0 == 9 && cpu_state.fpu.frame_size == 52);
    syn68k_fpu_reset();
}

static void busy_frame_tests(void)
{
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = 0x200;
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    fp(1, 0x4000, UINT64_C(0xc000000000000000));
    RUN(49, 0xf200, 0x0420, 0xf280, 0); /* 1/3, rounded result commits. */
    expect_fp(0, 0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaab));
    assert(stacked_pc == 0x1004 && cpu_state.fpu.frame_size == 100);
    EM_A0 = 0x6000;
    RUN(0, 0xf310);
    assert(READUL(0x6000) == 0x41600000 && READUL(0x6028) == 0x1000);
    assert(READUW(0x6034) == 0x10 && READUW(0x6040) == 0x0420);
    assert(READUB(0x6048) == 2 && READUB(0x603c) == 2 && READUB(0x603d) == 0x80);
    assert(READUW(0x6018) == 0x3ffd && READUL(0x601c) == 0xaaaaaaaa && READUL(0x6020) == 0xaaaaaaaa);
    RUN(0, 0xf358);
    assert(EM_A0 == 0x6064 && cpu_state.fpu.pending_vector == 49);
    EM_A0 = 0xfffc0;
    WRITEUL(EM_A0, 0x41600000);
    RUN(2, 0xf358);
    assert(EM_A0 == 0xfffc0 && cpu_state.fpu.pending_vector == 49 && cpu_state.fpu.frame_size == 100);
    RUN(49, 0xf280, 0);
    EM_A0 = 0x6100;
    RUN(0, 0xf310);
    assert(!memcmp(SYN68K_TO_US(0x6000), SYN68K_TO_US(0x6100), 100));
    WRITEUB(0x6148, 0); /* Handler serviced the writeback stage. */
    RUN(0, 0xf358);
    assert(EM_A0 == 0x6164 && !cpu_state.fpu.pending_vector);
    WRITEUB(0x6148, 6); /* E3 precedes a higher-priority E1 signaling NaN. */
    cpu_state.fpu.fpcr = 0x4200;
    cpu_state.fpu.fpsr = 0x4200;
    EM_A0 = 0x6100;
    RUN(0, 0xf358);
    assert(EM_A0 == 0x6164 && cpu_state.fpu.pending_vector == 49);
    EM_A0 = 0x6100;
    RUN(0, 0xf310);
    WRITEUB(0x6148, 4); /* Handler completed E3; E1 is now visible. */
    RUN(0, 0xf350);
    assert(cpu_state.fpu.pending_vector == 54);
    RUN(54, 0xf280, 0);
    RUN(0, 0xf310);
    WRITEUB(0x6148, 2);
    cpu_state.fpu.fpcr = cpu_state.fpu.fpsr = 0;
    fp(0, 0, 0);
    RUN(0, 0xf358); /* Masked INEX resumes from WBTEMP, not ETEMP/FPTEMP. */
    expect_fp(0, 0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaab));
    assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
    for (unsigned test = 0; test < 3; ++test) {
        syn68k_fpu_reset();
        cpu_state.fpu.fpcr = test == 0 ? 0x40 : 0;
        uint16 exponent = test == 0 ? 0x3f80 : test == 1 ? 0x7ffe : 0;
        uint64 significand = test == 1 ? UINT64_MAX : UINT64_C(0x8000000000000000);
        fp(0, exponent, significand);
        fp(1, test == 1 ? 0x3ffe : 0x4000, UINT64_C(0x8000000000000000));
        unsigned vector = test == 1 ? 53 : 51;
        RUN(vector, 0xf200, 0x0420, 0xf280, 0);
        expect_fp(0, exponent, significand);        /* Nonmaskable range trap. */
        assert(!(cpu_state.fpu.fpsr & 0x07000000)); /* Finite intermediate. */
        EM_A0 = 0x6200;
        RUN(0, 0xf310);
        assert(READUL(0x6200) == 0x41600000 && READUB(0x6248) == 2);
        assert(READUW(0x6218) == (test == 0 ? 0x3f7f : 0x7fff));
        assert(READUB(0x6245) == (test == 2 ? 0x10 : 0));
        RUN(0, 0xf358);
        assert(EM_A0 == 0x6264 && cpu_state.fpu.pending_vector == vector);
        EM_A0 = 0x6200;
        RUN(0, 0xf310);
    }
    syn68k_fpu_reset();
}

static void division_frame_tests(void)
{
    const unsigned vectors[] = {50, 52, 54};
    const unsigned enables[] = {0x0400, 0x2000, 0x4000};
    const unsigned operations[] = {0x20, 0x60, 0x64};
    for (unsigned operation = 0; operation < 3; ++operation)
        for (unsigned test = 0; test < 3; ++test) {
            unsigned command = 0x0400 | operations[operation];
            syn68k_fpu_reset();
            cpu_state.fpu.fpcr = enables[test];
            fp(0, test == 1 ? 0 : 0x3fff, test == 1 ? 0 : UINT64_C(0x8000000000000000));
            fp(1, test == 2 ? 0x7fff : 0, test == 2 ? UINT64_C(0x8000000000000001) : 0);
            RUN(vectors[test], 0xf200, command, 0x7042, 0xf280, 0);
            assert(EM_D0 == 66 && cpu_state.fpu.fpiar == 0x1000);
            assert(stacked_pc == 0x1006 && frame == vectors[test] * 4);
            assert(cpu_state.fpu.pending_vector == vectors[test]);
            expect_fp(0, test == 1 ? 0 : 0x3fff, test == 1 ? 0 : UINT64_C(0x8000000000000000));
            RUN(vectors[test], 0xf280, 0); /* Report it again until FSAVE. */
            EM_A0 = 0x5000;
            RUN(0, 0xf310);
            assert(READUL(0x5000) == 0x41300000 && READUW(0x5010) == command);
            assert(!cpu_state.fpu.pending_vector && !cpu_state.fpu.frame_size);
            RUN(0, 0xf358);
            assert(EM_A0 == 0x5034 && cpu_state.fpu.pending_vector == vectors[test]);
            RUN(vectors[test], 0xf280, 0);
            EM_A0 = 0x5000;
            RUN(0, 0xf310);
            /* An old status bit or an explicit FPSR write must never post a new
         exception after the pending frame has been suspended. */
            EM_D0 = enables[test];
            RUN(0, 0xf200, 0x8800, 0xf280, 0);
            assert(!cpu_state.fpu.pending_vector);
        }
    syn68k_fpu_reset();
}

static void unimplemented_frame_tests(void)
{
    syn68k_fpu_reset();
    fp(0, 0x4000, UINT64_C(0xc000000000000000)); /* 3 */
    fp(1, 0x4000, UINT64_C(0x8000000000000000)); /* 2 */
    RUN(11, 0xf200, 0x0426);                     /* FSCALE FP1,FP0. */
    assert(cpu_state.fpu.frame_size == 52);
    EM_D0 = 0x12345678;
    RUN(11, 0xf240, 15); /* Pending state cannot disappear at the next FPU op. */
    assert(EM_D0 == 0x12345678 && frame == 0x002c && stacked_pc == 0x1000);
    EM_A0 = 0x4000;
    WRITEUL(0x4000, 0xabcdef01);
    RUN(0, 0xf320); /* FSAVE -(A0), all 52 bytes, then idle. */
    assert(EM_A0 == 0x3fcc && READUL(0x3fcc) == 0x41300000);
    assert(READUL(0x4000) == 0xabcdef01 && !cpu_state.fpu.frame_size);
    assert(READUW(0x3fdc) == 0x0426 && READUB(0x3fe4) == 0);
    assert(READUW(0x3fe8) == 0x4000 && READUL(0x3fec) == 0xc0000000);
    assert(READUW(0x3ff4) == 0x4000 && READUL(0x3ff8) == 0x80000000);
    RUN(0, 0xf358); /* FRESTORE (A0)+, preserving architectural registers. */
    assert(EM_A0 == 0x4000 && cpu_state.fpu.frame_size == 52);
    expect_fp(0, 0x4000, UINT64_C(0xc000000000000000));
    EM_A0 = 0x4500;
    RUN(0, 0xf310);
    assert(!memcmp(SYN68K_TO_US(0x3fcc), SYN68K_TO_US(0x4500), 52));
    WRITEUL(0x4500, 0x41000000); /* Handler completed it and restores idle. */
    RUN(0, 0xf358);
    assert(EM_A0 == 0x4504 && !cpu_state.fpu.frame_size);
    EM_A0 = 0xfffe0;
    WRITEUL(EM_A0, 0x41300000);
    RUN(2, 0xf358); /* Incomplete restores do not commit a partial state. */
    assert(EM_A0 == 0xfffe0 && !cpu_state.fpu.frame_size);
    expect_fp(0, 0x4000, UINT64_C(0xc000000000000000));
    RUN(11, 0xf200, 0x000e);
    EM_A0 = 0x100030;
    WRITEUL(0xffffc, 0xaabbccdd);
    RUN(2, 0xf320); /* Incomplete saves retain the pending image. */
    assert(READUL(0xffffc) == 0xaabbccdd && cpu_state.fpu.frame_size == 52);
    EM_A0 = 0x4500;
    RUN(0, 0xf310);
    /* Execute an original guest FPSP-style handler through FSAVE and RTE,
     rather than having the C test callback unwind the exception. */
    uint32 vector = READUL(cpu_state.vbr + 44);
    const uint16 handler[] = {0xf327,     /* FSAVE -(SP) */
                              0x224f,     /* MOVEA.L SP,A1 */
                              0x4fef, 52, /* LEA 52(SP),SP */
                              0x7042, 0x4e73};
    destroy_blocks(0x1800, sizeof handler);
    for (unsigned i = 0; i < sizeof handler / sizeof *handler; ++i)
        WRITEUW(0x1800 + 2 * i, handler[i]);
    WRITEUL(cpu_state.vbr + 44, 0x1800);
    EM_A0 = 0x4600;
    WRITEUL(0x4600, 17);
    RUN(0, 0xf218, 0x400e, 0x7207); /* FSIN.L (A0)+,FP0; MOVEQ #7,D1. */
    WRITEUL(cpu_state.vbr + 44, vector);
    assert(EM_A0 == 0x4604 && EM_D0 == 66 && EM_D1 == 7);
    assert(READUL(EM_A1) == 0x41300000 && READUW(EM_A1 + 16) == 0x400e);
    assert(!cpu_state.fpu.frame_size);
    EM_A0 = 0x4700;
    cpu_state.fpu.fpsr = 0x08123498;
    RUN(11, 0xf23c, 0x400e, 0xffff, 0xfff9); /* FSIN.L #-7,FP0. */
    assert(stacked_pc == 0x1008 && !READUL(EM_A7 + 8));
    assert(cpu_state.fpu.fpsr == 0x08123498 && cpu_state.fpu.fpiar == 0x1000);
    RUN(0, 0xf310);
    assert(READUW(0x4728) == 0xc001 && READUL(0x472c) == 0xe0000000);
    RUN(11, 0xf23c, 0x4c0e, 0x1234, 0x5678, 0x0123, 0x4567, 0x89ab, 0xcdef);
    assert(stacked_pc == 0x1010);
    RUN(0, 0xf310);
    assert(READUL(0x4724) == 0x12345678 && READUL(0x472c) == 0x01234567 && READUL(0x4730) == 0x89abcdef);
    RUN(11, 0xf23c, 0x480e, 0, 0, 0, 0, 0, 1);
    RUN(0, 0xf310);
    assert(READUB(0x470c) == 0x80 && !READUW(0x4728) && READUL(0x4730) == 1);
    EM_A0 = 0x4800;
    RUN(11, 0xf218, 0x4817); /* Reserved opcode must not fetch/increment. */
    assert(EM_A0 == 0x4800);
    RUN(2, 0xf239, 0x400e, 0x0010, 0); /* Bad FSIN operand fetch. */
    assert(!cpu_state.fpu.frame_size);
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    void* memory = VirtualAlloc(NULL, 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(1024 * 1024, 0, 0);
    uint32 vectors[64] = {0};
    int native = argc > 1 && atoi(argv[1]);
    initialize_68k_emulator(NULL, native, vectors, 0);
    native = syn68k_native_code_enabled();
    for (unsigned i = 2; i < 64; ++i)
        trap_install_handler(i, exception_handler, (void*)(uintptr_t)i);
    assert(cpu_state.fpu.null_state);
    RUN(0, 0xf23c, 0x4000, 0, 7, /* FMOVE.L #7,FP0 */
        0xf23c, 0x4080, 0, 3,    /* FMOVE.L #3,FP1 */
        0xf200, 0x0422,          /* FADD.X FP1,FP0 */
        0xf200, 0x6000);         /* FMOVE.L FP0,D0 */
    assert(EM_D0 == 10 && !cpu_state.fpu.null_state);
    expect_fp(0, 0x4002, UINT64_C(0xa000000000000000));
    RUN(0, 0xf200, 0x0423, 0xf200, 0x0428, 0xf200, 0x0420, 0xf200, 0x6000);
    assert(EM_D0 == 9);
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    fp(1, 0x3fc0, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0422);
    expect_fp(0, 0x3fff, UINT64_C(0x8000000000000001));
    assert(!(cpu_state.fpu.fpsr & 0xff00));
    for (unsigned mode = 0; mode < 4; ++mode) {
        cpu_state.fpu.fpcr = 0x40 | mode * 16;
        fp(0, 0x3fff, UINT64_C(0x8000000000000000));
        fp(1, 0x3fe7, UINT64_C(0x8000000000000000));
        RUN(0, 0xf200, 0x0422);
        expect_fp(0, 0x3fff, mode == 3 ? UINT64_C(0x8000010000000000) : UINT64_C(0x8000000000000000));
        assert((cpu_state.fpu.fpsr & 0x208) == 0x208);
    }
    cpu_state.fpu.fpcr = 0;
    fp(0, 0x4001, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x0004);
    expect_fp(0, 0x4000, UINT64_C(0x8000000000000000));
    RUN(0, 0xf200, 0x001a);
    expect_fp(0, 0xc000, UINT64_C(0x8000000000000000));
    assert(cpu_state.fpu.fpsr & 0x08000000);
    RUN(0, 0xf200, 0x0018);
    expect_fp(0, 0x4000, UINT64_C(0x8000000000000000));
    fp(0, 0x8000, 0);
    RUN(0, 0xf200, 0x003a);
    assert((cpu_state.fpu.fpsr & 0x0f000000) == 0x0c000000);
    fp(0, 0x7fff, UINT64_C(0xc000000000000001));
    RUN(0, 0xf200, 0x003a);
    assert(cpu_state.fpu.fpsr & 0x01000000);
    cpu_state.fpu.fpcr = 0x8000;
    RUN(48, 0xf240, 0x0011); /* FSEQ signaling/unordered. */
    cpu_state.fpu.fpcr = 0;
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    fp(1, 0, 0);
    RUN(0, 0xf200, 0x0420);
    expect_fp(0, 0x7fff, UINT64_C(0x8000000000000000));
    assert((cpu_state.fpu.fpsr & 0x410) == 0x410);
    fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    cpu_state.fpu.fpcr = 0x400;
    RUN(0, 0xf200, 0x0420);
    expect_fp(0, 0x3fff, UINT64_C(0x8000000000000000));
    RUN(50, 0xf280, 0); /* Deferred until the next F-line instruction. */
    EM_A0 = 0x3000;
    RUN(0, 0xf310);
    cpu_state.fpu.fpcr = 0;
    EM_A0 = 0x2000;
    EM_D0 = 0x12345600;
    fp(0, 0x4000, UINT64_C(0xa000000000000000)); /* 2.5 */
    RUN(0, 0xf200, 0x7800);                      /* FMOVE.B FP0,D0, nearest-even. */
    assert(EM_D0 == 0x12345602);
    fp(0, 0x4007, UINT64_C(0x8000000000000000)); /* 256 */
    RUN(52, 0xf200, 0x7800);
    assert(EM_D0 == 0x1234567f);
    EM_A1 = 0x3000;
    RUN(0, 0xf311);
    fp(0, 0xc007, UINT64_C(0x8000000000000000));
    RUN(52, 0xf200, 0x7800);
    assert(EM_D0 == 0x12345680);
    RUN(0, 0xf311);
    cpu_state.fpu.fpcr = 0x2000;
    RUN(52, 0xf200, 0x7800);
    assert(frame == 0x30d0 && stacked_pc == 0x1004);
    RUN(0, 0xf311);
    return_from_exception = 1;
    RUN(52, 0xf200, 0x7800);
    return_from_exception = 0;
    RUN(0, 0xf311);
    cpu_state.fpu.fpcr = 0;
    fp(0, 0x4000, UINT64_C(0xa000000000000000));
    RUN(0, 0xf210, 0x6800); /* FMOVE.X FP0,(A0). */
    assert(READUW(0x2000) == 0x4000 && READUL(0x2004) == 0xa0000000);
    RUN(0, 0xf218, 0x4880); /* FMOVE.X (A0)+,FP1. */
    assert(EM_A0 == 0x200c);
    expect_fp(1, 0x4000, UINT64_C(0xa000000000000000));
    EM_A0 = 0x2100;
    fp(1, 0x4001, UINT64_C(0x9000000000000000)); /* 4.5 */
    RUN(0, 0xf220, 0xe003);                      /* FMOVEM.X FP0-FP1,-(A0). */
    assert(EM_A0 == 0x20e8);
    fp(0, 0, 0);
    fp(1, 0, 0);
    RUN(0, 0xf218, 0xd0c0); /* FMOVEM.X (A0)+,FP0-FP1. */
    assert(EM_A0 == 0x2100);
    expect_fp(0, 0x4000, UINT64_C(0xa000000000000000));
    expect_fp(1, 0x4001, UINT64_C(0x9000000000000000));
    /* Dynamic lists use Dn's low byte and the same architectural memory order. */
    EM_D2 = 0xffff0003;
    RUN(0, 0xf220, 0xe820);
    EM_D2 = 0xffffffc0;
    fp(0, 0, 0);
    fp(1, 0, 0);
    RUN(0, 0xf218, 0xd820);
    expect_fp(0, 0x4000, UINT64_C(0xa000000000000000));
    expect_fp(1, 0x4001, UINT64_C(0x9000000000000000));
    /* Single/double conversion must retain SNAN classification before quieting. */
    cpu_state.fpu.fpcr = 0x4000;
    RUN(54, 0xf23c, 0x4400, 0x7f80, 1, 0xf280, 0);
    EM_A0 = 0x2200;
    RUN(0, 0xf310);
    assert(READUW(0x2228) == 0x7fff && READUL(0x222c) == 0x80000100);
    RUN(54, 0xf23c, 0x5400, 0x7ff0, 0, 0, 1, 0xf280, 0);
    RUN(0, 0xf310);
    assert(READUW(0x2228) == 0x7fff && READUL(0x222c) == 0x80000000 && READUL(0x2230) == 0x800);
    RUN(11, 0xf23c, 0x4401, 0x7f80, 1); /* FPSP sees the original SNAN too. */
    RUN(0, 0xf310);
    assert(READUL(0x222c) == 0x80000100 && READUB(0x220c) == 0x60);
    fp(0, 0x7fff, UINT64_C(0x8000000000000001));
    EM_A0 = 0x2200;
    RUN(54, 0xf210, 0x6800);
    assert(frame == 0x30d8 && stacked_pc == 0x1004);
    RUN(0, 0xf311);
    cpu_state.fpu.fpcr = 0;
    RUN(0, 0xf210, 0x6800);
    assert(READUL(0x2204) == 0xc0000000 && READUL(0x2208) == 1);
    EM_A0 = 0x2200;
    RUN(11, 0xf218, 0x480e); /* Operand is fetched before the FPSP trap. */
    assert(EM_A0 == 0x220c);
    assert(frame == 0x202c && stacked_pc == 0x1004);
    assert(READUL(EM_A7 + 8) == 0x2200);
    EM_A0 = 0x3600;
    RUN(0, 0xf310);
    assert(READUL(0x3600) == 0x41300000);
    assert(READUW(0x3610) == 0x480e);
    assert(READUL(0x362c) == 0xc0000000 && READUL(0x3630) == 1);
    RUN(0, 0xf310); /* A second FSAVE is idle. */
    assert(READUL(0x3600) == 0x41000000);
    RUN(11, 0xf200, 0x0001); /* FINT requires the 68040 software package. */
    RUN(0, 0xf310);
    RUN(11, 0xf200, 0x0003); /* FINTRZ likewise. */
    RUN(0, 0xf310);
    RUN(11, 0xf200, 0x5c00); /* FMOVECR. */
    RUN(0, 0xf310);
    RUN(55, 0xf23c, 0x4400, 0, 1); /* Single denormal. */
    assert(frame == 0x00dc && stacked_pc == 0x1008);
    EM_A0 = 0x3000;
    RUN(0, 0xf310);
    RUN(55, 0xf23c, 0x5400, 0, 0, 0, 1); /* Double denormal. */
    RUN(0, 0xf310);
    RUN(55, 0xf23c, 0x4800, 0, 0, 0, 0, 0, 1); /* Extended denormal. */
    RUN(0, 0xf310);
    EM_A0 = 0x100000;
    RUN(2, 0xf210, 0x4000);
    assert(frame == 0x7008 && READUL(EM_A7 + 20) == 0x100000);
    EM_A0 = 0xffffe;
    WRITEUW(EM_A0, 0x4000);
    RUN(2, 0xf210, 0x4800); /* The unused extended-format word is fetched too. */
    assert(frame == 0x7008 && READUL(EM_A7 + 20) == 0x100000);
    EM_A0 = 0xffff8;
    WRITEUL(0xffff8, 0x12345678);
    WRITEUL(0xffffc, 0xabcdef01);
    RUN(2, 0xf210, 0x6800); /* No partial extended store across a boundary. */
    assert(READUL(0xffff8) == 0x12345678 && READUL(0xffffc) == 0xabcdef01);
    EM_A0 = 0x2400;
    cpu_state.fpu.fpcr = 0x90;
    cpu_state.fpu.fpsr = 0x0a120098;
    cpu_state.fpu.fpiar = 0x12345678;
    RUN(0, 0xf218, 0xbc00); /* Control register order with a 12-byte increment. */
    assert(EM_A0 == 0x240c && READUL(0x2400) == 0x90 && READUL(0x2404) == 0x0a120098 && READUL(0x2408) == 0x12345678);
    cpu_state.fpu.fpcr = cpu_state.fpu.fpsr = cpu_state.fpu.fpiar = 0;
    EM_A0 = 0x2400;
    RUN(0, 0xf218, 0x9c00);
    assert(EM_A0 == 0x240c && cpu_state.fpu.fpcr == 0x90 && cpu_state.fpu.fpsr == 0x0a120098 &&
           cpu_state.fpu.fpiar == 0x12345678);
    EM_D0 = 0xabcdef00;
    cpu_state.fpu.fpsr = 0x04000000;
    RUN(0, 0xf240, 0x0001);
    assert(EM_D0 == 0xabcdefff);
    RUN(0, 0xf240, 0x000e);
    assert(EM_D0 == 0xabcdef00);
    RUN(7, 0xf27c, 0x000f);
    EM_A0 = 0x3000;
    RUN(0, 0xf310); /* FSAVE (A0). */
    assert(READUL(0x3000) == 0x41000000);
    WRITEUL(0x3000, 0);
    RUN(0, 0xf350, 0xf310); /* FRESTORE null; FSAVE null. */
    assert(cpu_state.fpu.null_state && !READUL(0x3000));
    WRITEUL(0x3000, 0xdeadbeef);
    RUN(14, 0xf350);
    cpu_state.isp = 0xfe000;
    cpu_state.sr = 0;
    RUN(8, 0xf310);
    cpu_state.sr = 0;
    RUN(8, 0xf350);
    RUN(11, 0xf200, 0x007f); /* Reserved encoding is F-line illegal, not FPSP. */
    assert(frame == 0x002c && stacked_pc == 0x1000);
    RUN(11, 0xf000); /* Other coprocessor remains an F-line trap. */
    conditional_tests();
    control_addressing_tests();
    reserved_encoding_tests();
    unimplemented_frame_tests();
    division_frame_tests();
    busy_frame_tests();
    arithmetic_frame_tests();
    unsupported_frame_tests();
    moveout_frame_tests();
    nan_selection_tests();
    single_mantissa_tests();
    signed_frame_exponent_tests();
    sqrt_unsupported_frame_test();
    operand_frame_tag_tests();
    software_exception_frame_test();
    puts(native ? "FPU native-dispatch checks passed." : "FPU interpreter checks passed.");
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
}
