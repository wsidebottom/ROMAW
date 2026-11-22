/*+===================================================================
  File:      jit-x64-helper-test.c

  Summary:   Implements jit x64 helper test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Execute emitted code directly, so synthetic fallback cannot pass the test. */
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32 execute(unsigned opcode)
{
    WRITEUW(0x1000, opcode);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    assert(code && translated == 1 && terminal);
    uint32 next = syn68k_x64_execute(code);
    syn68k_x64_release(code);
    return next;
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    const uint32 values[] = {0, 1, 0xFFFF, 0x8000, 0x7FFF, 0xFFFF8000, 0x12345678, 0xFEDCBA98};
    unsigned cases = 0;
    for (unsigned sign = 0; sign < 2; ++sign)
        for (unsigned src = 0; src < 8; ++src)
            for (unsigned dst = 0; dst < 8; ++dst)
                for (unsigned a = 0; a < 8; ++a)
                    for (unsigned b = 0; b < 8; ++b) {
                        EM_DREG(dst) = values[a];
                        EM_DREG(src) = values[b];
                        uint32 left = EM_DREG(dst), right = EM_DREG(src);
                        uint32 expected = sign ? (int32)(int16)left * (int32)(int16)right
                                               : (uint32)(uint16)left * (uint32)(uint16)right;
                        cpu_state.ccx = 0x80000000;
                        cpu_state.ccc = cpu_state.ccv = cpu_state.ccn = cpu_state.ccnz = 7;
                        assert(execute(0xC0C0 | sign << 8 | dst << 9 | src) == 0x1002);
                        assert(EM_DREG(dst) == expected);
                        assert(!cpu_state.ccc && !cpu_state.ccv);
                        assert(!!cpu_state.ccn == !!(expected >> 31));
                        assert(!!cpu_state.ccnz == !!expected && cpu_state.ccx == 0x80000000);
                        ++cases;
                    }
    EM_D0 = 1000;
    EM_D1 = 9;
    assert(execute(0x80C1) == 0x1002 && EM_D0 == 0x1006F);
    EM_D0 = 12345;
    EM_D1 = 67;
    WRITEUW(0x1002, 0);
    assert(execute(0x4C01) == 0x1004 && EM_D0 == 827115);
    EM_A0 = 0x4000;
    WRITEUW(0x1002, 16);
    assert(execute(0x41E8) == 0x1004 && EM_A0 == 0x4010);
    EM_A0 = 0x4000;
    EM_D0 = 0xAABBCCDD;
    WRITEUW(0x1002, 16);
    assert(execute(0x0188) == 0x1004);
    assert(READUB(0x4010) == 0xCC && READUB(0x4012) == 0xDD);
    syn68k_fpu_reset();
    EM_D0 = 1;
    WRITEUW(0x1002, 0x4000); /* FMOVE.L D0,FP0 */
    assert(execute(0xF200) == 0x1004);
    assert(cpu_state.fpu.regs[0].exponent == 0x3FFF &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
    EM_D0 = 0;
    WRITEUW(0x1002, 0x6000); /* FMOVE.L FP0,D0 */
    assert(execute(0xF200) == 0x1004);
    assert(EM_D0 == 1);
    printf("%u direct native multiplies and checked arithmetic/EA/FPU tails passed.\n", cases);
    return 0;
}
