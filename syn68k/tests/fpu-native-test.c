/*+===================================================================
  File:      fpu-native-test.c

  Summary:   Compare native FPU arithmetic, control transfers and predicates with the model and reject semantic tails.

  Classes:   None.

  Functions: main, __wrap_trap_direct

  Origin:    Original ARDI Executor direct 68040 FPU regression encodings.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit-x64.h"
#include "fpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned forbidden, decoder_calls, capture_selector, observed_selector;
static uint32 capture_pc = 0x1000;
uint32 __real_trap_direct(uint32 selector, uint32 pc, uint32 address);

uint32 __wrap_trap_direct(uint32 selector, uint32 pc, uint32 address)
{
    if (capture_selector) {
        assert(pc == capture_pc && !address);
        observed_selector = selector;
        return 0x7654;
    }
    if (forbidden)
        fprintf(stderr, "Unexpected FPU semantic tail: command=%04x selector=%u\n", READUW(0x1002), selector);
    assert(!forbidden);
    ++decoder_calls;
    return __real_trap_direct(selector, pc, address);
}

static void register_encoding_checks(void)
{
    for (unsigned ea = 1; ea < 64; ++ea) {
        WRITEUW(0x1000, 0xf200 | ea);
        WRITEUW(0x1002, 0);
        memset(&cpu_state, 0, sizeof cpu_state);
        cpu_state.fpu.null_state = 1;
        CPUState before = cpu_state;
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        observed_selector = 0;
        capture_selector = 1;
        uint32 next = syn68k_x64_execute(code);
        capture_selector = 0;
        if (next != 0x7654 || observed_selector != 11)
            fprintf(stderr, "Invalid FPU register EA %02x bypassed checked exit: next=%08x selector=%u\n", ea, next,
                    observed_selector);
        assert(next == 0x7654 && observed_selector == 11 && !memcmp(&before, &cpu_state, sizeof before));
        syn68k_x64_release(code);
    }
    puts("63 reserved register-FPU EA encodings reached the checked exit without side effects");
}

static void control_register_checks(void)
{
    unsigned cases = 0, reserved = 0;
    for (unsigned reg = 0; reg < 16; ++reg) {
        unsigned opcode = 0xf200 | reg;
        WRITEUW(0x1000, opcode);
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        for (unsigned control = reg < 8 ? 0 : 2; control < 3; ++control)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned sample = 0; sample < 64; ++sample) {
                    memset(&cpu_state, 0, sizeof cpu_state);
                    for (unsigned i = 0; i < 16; ++i)
                        cpu_state.regs[i].ul.n = 0xfedcba98 ^ (sample * 0x1234567 + i);
                    cpu_state.fpu.fpcr = 0xfedcba98;
                    cpu_state.fpu.fpsr = 0x89abcdef;
                    cpu_state.fpu.fpiar = 0x12345678;
                    cpu_state.fpu.null_state = sample & 1;
                    cpu_state.ccc = sample & 1;
                    cpu_state.ccn = sample & 2;
                    cpu_state.ccv = sample & 4;
                    cpu_state.ccnz = sample & 8;
                    cpu_state.ccx = sample & 16;
                    WRITEUW(0x1002, 0x8000 | writing << 13 | (4 >> control) << 10);
                    CPUState before = cpu_state;
                    Syn68kFPUStep result;
                    assert(syn68k_fpu_step_decoded(0x1000, opcode, NULL, &result) && !result.vector);
                    CPUState expected = cpu_state;
                    cpu_state = before;
                    forbidden = 1;
                    uint32 next = syn68k_x64_execute(code);
                    forbidden = 0;
                    assert(next == result.next && !memcmp(&expected, &cpu_state, sizeof expected));
                    ++cases;
                }
        for (unsigned mask = 0; mask < 8; ++mask)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned bad = 0; bad < 3; ++bad) {
                    if (!bad && (reg < 8 ? mask == 1 || mask == 2 || mask == 4 : mask == 1))
                        continue;
                    memset(&cpu_state, 0, sizeof cpu_state);
                    cpu_state.fpu.null_state = 1;
                    CPUState before = cpu_state;
                    WRITEUW(0x1002, 0x8000 | writing << 13 | mask << 10 | (bad == 1 ? 1 : bad == 2 ? 0x3ff : 0));
                    observed_selector = 0;
                    capture_selector = 1;
                    assert(syn68k_x64_execute(code) == 0x7654);
                    capture_selector = 0;
                    assert(observed_selector == 11 && !memcmp(&before, &cpu_state, sizeof before));
                    ++reserved;
                }
        syn68k_x64_release(code);
    }
    printf("%u direct FPCR/FPSR/FPIAR live-command/register/mask cases passed without semantic helpers\n", cases);
    printf("%u reserved control-register encodings reached the checked exit without side effects\n", reserved);
}

static void scalar_input_checks(void)
{
    const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27, 0x38, 0x3a, 0x40, 0x44,
                                   0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    const unsigned formats[] = {0, 1, 4, 6};
    const uint32 integers[] = {0,      1,          0xff,       0x7f,       0x80,       0xffff,
                               0x7fff, 0x8000,     UINT32_MAX, 0x7fffffff, 0x80000000, 0x12345678,
                               0x8001, 0xffff8000, 0x80808080, 0x13579bdf};
    const uint32 singles[] = {0,          0x80000000, 0x3f800000, 0xbf800000, 0x40000000, 0x3eaaaaab,
                              0x00800000, 0x7f7fffff, 0x3f800001, 0x3f7fffff, 0x40200000, 0xc0200000,
                              0x3e800000, 0x47ffffff, 0xc7ffffff, 0x00800001};
    unsigned cases = 0, reserved = 0;
    for (unsigned reg = 0; reg < 8; ++reg) {
        unsigned opcode = 0xf200 | reg;
        WRITEUW(0x1000, opcode);
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        for (unsigned f = 0; f < 4; ++f)
            for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
                for (unsigned rounding = 0; rounding < 4; ++rounding)
                    for (unsigned precision = 0; precision < 4; ++precision)
                        for (unsigned sample = 0; sample < 16; ++sample) {
                            memset(&cpu_state, 0, sizeof cpu_state);
                            cpu_state.fpu.fpcr = rounding << 4 | precision << 6;
                            cpu_state.fpu.fpsr = 0x09ac0050;
                            cpu_state.fpu.fpiar = 0x98765432;
                            cpu_state.fpu.null_state = sample & 1;
                            cpu_state.ccc = sample & 1;
                            cpu_state.ccn = sample & 2;
                            cpu_state.ccv = sample & 4;
                            cpu_state.ccnz = sample & 8;
                            cpu_state.ccx = sample & 16;
                            for (unsigned i = 0; i < 16; ++i)
                                cpu_state.regs[i].ul.n = 0xfedcba98 ^ (sample * 0x1234567 + i);
                            cpu_state.regs[reg].ul.n =
                                formats[f] == 1 ? singles[(sample + reg) & 15] : integers[(sample + reg) & 15];
                            for (unsigned i = 0; i < 8; ++i)
                                cpu_state.fpu.regs[i] = (Syn68kFP80){0x3ff0 + ((sample + i) % 32),
                                                                     UINT64_C(0x8000000000000000) | i * 0x123456789};
                            WRITEUW(0x1002, 0x4000 | formats[f] << 10 | ((sample >> 1) & 7) << 7 | operations[op]);
                            CPUState before = cpu_state;
                            Syn68kFPUStep result;
                            assert(syn68k_fpu_step_decoded(0x1000, opcode, NULL, &result));
                            // Exceptional/range handling retains the checked path.
                            if (result.vector || cpu_state.fpu.frame_size || (cpu_state.fpu.fpsr & 0xfd00))
                                continue;
                            CPUState expected = cpu_state;
                            cpu_state = before;
                            forbidden = 1;
                            uint32 next = syn68k_x64_execute(code);
                            forbidden = 0;
                            if (next != result.next || memcmp(&expected, &cpu_state, sizeof expected))
                                fprintf(stderr, "Scalar FPU mismatch: D%u format=%u op=%02x sample=%u mode=%u/%u\n",
                                        reg, formats[f], operations[op], sample, rounding, precision);
                            assert(next == result.next && !memcmp(&expected, &cpu_state, sizeof expected));
                            ++cases;
                        }
        const unsigned checked_formats[] = {2, 3, 5, 7};
        for (unsigned i = 0; i < 4; ++i) {
            memset(&cpu_state, 0, sizeof cpu_state);
            cpu_state.fpu.null_state = 1;
            CPUState before = cpu_state;
            WRITEUW(0x1002, 0x4000 | checked_formats[i] << 10);
            observed_selector = 0;
            capture_selector = 1;
            assert(syn68k_x64_execute(code) == 0x7654);
            capture_selector = 0;
            assert(observed_selector == 11 && !memcmp(&before, &cpu_state, sizeof before));
            ++reserved;
        }
        syn68k_x64_release(code);
    }
    assert(!decoder_calls);
    WRITEUW(0x1000, 0xf200);
    printf("%u direct Dn integer/single/word/byte FPU input cases passed without semantic helpers\n", cases);
    printf("%u extended/double/packed/ROM source encodings reached checked exits without side effects\n", reserved);
}

static void scalar_destination_checks(void)
{
    const unsigned formats[] = {0, 1, 4, 6};
    const Syn68kFP80 values[] = {{0, 0},
                                 {0x8000, 0},
                                 {0x3fff, UINT64_C(0x8000000000000000)},
                                 {0xbfff, UINT64_C(0xc000000000000000)},
                                 {0x4000, UINT64_C(0xa000000000000000)},
                                 {0xbffe, UINT64_C(0x8000000000000000)},
                                 {0x4005, UINT64_C(0xfe00000000000000)},
                                 {0x4006, UINT64_C(0x8000000000000000)},
                                 {0xc006, UINT64_C(0x8000000000000000)},
                                 {0x400d, UINT64_C(0xfffe000000000000)},
                                 {0xc00d, UINT64_C(0xffff000000000000)},
                                 {0x401d, UINT64_C(0xfffffffe00000000)},
                                 {0xc01e, UINT64_C(0x8000000000000000)},
                                 {0x407e, UINT64_MAX},
                                 {0x3f81, UINT64_C(0x8000000000000000)},
                                 {0x3f80, UINT64_C(0x8000000000000001)}};
    unsigned cases = 0, checked = 0;
    for (unsigned reg = 0; reg < 8; ++reg) {
        unsigned opcode = 0xf200 | reg;
        WRITEUW(0x1000, opcode);
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        assert(code && translated == 1 && terminal);
        for (unsigned format = 0; format < 4; ++format)
            for (unsigned rounding = 0; rounding < 4; ++rounding)
                for (unsigned precision = 0; precision < 4; ++precision)
                    for (unsigned sample = 0; sample < 128; ++sample) {
                        memset(&cpu_state, 0, sizeof cpu_state);
                        for (unsigned i = 0; i < 16; ++i)
                            cpu_state.regs[i].ul.n = 0x98765432 ^ (i * 0x1234567 + sample);
                        for (unsigned i = 0; i < 8; ++i)
                            cpu_state.fpu.regs[i] = values[(sample + i) & 15];
                        cpu_state.fpu.fpcr = rounding << 4 | precision << 6;
                        cpu_state.fpu.fpsr = ((sample & 15) << 24) | 0xac0050;
                        cpu_state.fpu.fpiar = 0x98765432;
                        cpu_state.fpu.null_state = sample & 1;
                        cpu_state.ccc = sample & 1;
                        cpu_state.ccn = sample & 2;
                        cpu_state.ccv = sample & 4;
                        cpu_state.ccnz = sample & 8;
                        cpu_state.ccx = sample & 16;
                        WRITEUW(0x1002, 0x6000 | formats[format] << 10 | ((sample >> 4) & 7) << 7);
                        CPUState before = cpu_state;
                        Syn68kFPUStep result;
                        assert(syn68k_fpu_step_decoded(0x1000, opcode, NULL, &result));
                        if (result.vector || cpu_state.fpu.frame_size || (cpu_state.fpu.fpsr & 0xfd00))
                            continue;
                        CPUState expected = cpu_state;
                        cpu_state = before;
                        forbidden = 1;
                        uint32 next = syn68k_x64_execute(code);
                        forbidden = 0;
                        if (next != result.next || memcmp(&expected, &cpu_state, sizeof expected))
                            fprintf(stderr, "FPU output mismatch: D%u format=%u sample=%u mode=%u/%u\n", reg,
                                    formats[format], sample, rounding, precision);
                        assert(next == result.next && !memcmp(&expected, &cpu_state, sizeof expected));
                        ++cases;
                    }
        const unsigned checked_formats[] = {2, 3, 5, 7};
        for (unsigned i = 0; i < 8; ++i) {
            memset(&cpu_state, 0, sizeof cpu_state);
            cpu_state.fpu.null_state = 1;
            CPUState before = cpu_state;
            WRITEUW(0x1002, i < 4 ? 0x6000 | checked_formats[i] << 10 : 0x6001 | formats[i - 4] << 10);
            observed_selector = 0;
            capture_selector = 1;
            assert(syn68k_x64_execute(code) == 0x7654);
            capture_selector = 0;
            assert(observed_selector == 11 && !memcmp(&before, &cpu_state, sizeof before));
            ++checked;
        }
        for (unsigned kind = 0; kind < 4; ++kind) {
            memset(&cpu_state, 0, sizeof cpu_state);
            cpu_state.fpu.null_state = 1;
            cpu_state.fpu.fpsr = 0x09ac1250;
            cpu_state.fpu.fpiar = 0x98765432;
            cpu_state.fpu.regs[0] = kind == 0   ? values[8]
                                    : kind == 1 ? (Syn68kFP80){0x401e, UINT64_C(0x8000000000000000)}
                                    : kind == 2 ? (Syn68kFP80){0x407f, UINT64_C(0x8000000000000000)}
                                                : (Syn68kFP80){0x3fff, UINT64_C(0xc000000000000000)};
            cpu_state.fpu.fpcr = kind == 3 ? 0x200 : 0;
            WRITEUW(0x1002, kind == 0 ? 0x7800 : kind == 2 ? 0x6400 : 0x6000);
            CPUState before = cpu_state;
            observed_selector = 0;
            capture_selector = 1;
            assert(syn68k_x64_execute(code) == 0x7654);
            capture_selector = 0;
            assert(observed_selector == 11 && !memcmp(&before, &cpu_state, sizeof before));
            ++checked;
        }
        syn68k_x64_release(code);
    }
    assert(!decoder_calls);
    WRITEUW(0x1000, 0xf200);
    printf("%u direct FPU Dn output/rounding/FPCC/upper-byte cases passed without semantic helpers\n", cases);
    printf("%u reserved/range/enabled-exception FPU outputs kept checked exits before CPU commits\n", checked);
}

static unsigned conditional_checks(void)
{
    static const unsigned counters[] = {0, 1, 2, 0x7fff, 0x8000, 0xffff};
    unsigned cases = 0;
    for (unsigned kind = 0; kind < 7; ++kind)
        for (unsigned encoded = 0; encoded < (kind < 2 ? 32U : 1U); ++encoded) {
            unsigned opcode = kind == 0   ? 0xf280 | encoded
                              : kind == 1 ? 0xf2c0 | encoded
                              : kind == 2 ? 0xf24b
                              : kind == 3 ? 0xf243
                              : kind == 4 ? 0xf27a
                              : kind == 5 ? 0xf27b
                                          : 0xf27c;
            WRITEUW(0x1000, opcode);
            int8 length = 1;
            unsigned translated;
            int terminal;
            void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
            assert(code && translated == 1 && terminal);
            for (unsigned condition = 0; condition < (kind < 2 ? 1U : 32U); ++condition)
                for (unsigned fpcc = 0; fpcc < 16; ++fpcc)
                    for (unsigned counter = 0; counter < sizeof counters / sizeof *counters; ++counter) {
                        memset(&cpu_state, 0, sizeof cpu_state);
                        cpu_state.fpu.fpsr = fpcc << 24 | 0x00aa1234;
                        cpu_state.fpu.fpiar = 0x12345678;
                        cpu_state.fpu.null_state = counter & 1;
                        cpu_state.ccc = counter & 1;
                        cpu_state.ccv = counter & 2;
                        cpu_state.ccn = counter & 4;
                        cpu_state.ccnz = counter & 8;
                        cpu_state.ccx = counter & 16;
                        EM_D3 = 0xcafe0000 | counters[counter];
                        unsigned displacement = counters[counter];
                        if (kind < 2) {
                            if (kind == 0)
                                WRITEUW(0x1002, displacement);
                            else
                                WRITEUL(0x1002, displacement & 0x8000 ? 0xffff0000 | displacement : displacement);
                        }
                        else {
                            WRITEUW(0x1002, condition);
                            WRITEUW(0x1004, displacement);
                            WRITEUW(0x1006, 0x1234);
                        }
                        CPUState before = cpu_state;
                        Syn68kFPUStep result;
                        assert(syn68k_fpu_step_decoded(0x1000, opcode, NULL, &result));
                        if (result.vector) {
                            assert(result.vector == 7); // A taken FTRAPcc remains an exceptional exit.
                            continue;
                        }
                        CPUState expected = cpu_state;
                        cpu_state = before;
                        forbidden = 1;
                        uint32 next = syn68k_x64_execute(code);
                        forbidden = 0;
                        if (next != result.next || memcmp(&expected, &cpu_state, sizeof expected))
                            fprintf(
                                stderr,
                                "Conditional FPU mismatch: opcode=%04x cond=%u fpcc=%u count=%u pc=%08x/%08x fpsr=%08x/%08x\n",
                                opcode, condition, fpcc, counter, next, result.next, cpu_state.fpu.fpsr,
                                expected.fpu.fpsr);
                        assert(next == result.next && !memcmp(&expected, &cpu_state, sizeof expected));
                        ++cases;
                    }
            syn68k_x64_release(code);
        }

    for (unsigned kind = 0; kind < 2; ++kind) {
        uint32 pc = kind ? 0xffa : 0xffc;
        unsigned opcode = kind ? 0xf27b : 0xf2cf; // Untaken long FTRAPF / taken long FBT.
        WRITEUW(pc, opcode);
        if (kind)
            WRITEUW(pc + 2, 0);
        WRITEUL(0xffe, 8); // The live longword crosses the host/JIT page boundary.
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(pc), &length, 1, &translated, pc, &terminal);
        assert(code && translated == 1 && terminal);
        memset(&cpu_state, 0, sizeof cpu_state);
        Syn68kFPUStep result;
        assert(syn68k_fpu_step_decoded(pc, opcode, NULL, &result) && !result.vector);
        forbidden = 1;
        assert(syn68k_x64_execute(code) == result.next);
        forbidden = 0;
        // A missing second half must tail to the original FPU PC.
        syn68k_set_memory_window(0x1000, 0, 0);
        capture_pc = pc;
        capture_selector = 1;
        observed_selector = 0;
        assert(syn68k_x64_execute(code) == 0x7654 && observed_selector == 11);
        capture_selector = 0;
        capture_pc = 0x1000;
        syn68k_set_memory_window(0x10000, 0, 0);
        syn68k_x64_release(code);
        cases += 2;
    }
    WRITEUW(0x1000, 0xf200);
    printf(
        "%u direct FPU branch/counter/byte/untaken-trap/FPCC/boundary cases passed; normal paths reject semantic helpers\n",
        cases);
    return cases;
}

int main(void)
{
    static const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27,
                                          0x38, 0x3a, 0x40, 0x44, 0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e,
                                          0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    void* ram = calloc(1, 0x10000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    register_encoding_checks();
    control_register_checks();
    WRITEUW(0x1000, 0xf200);
    int8 length = 1;
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
    assert(code && translated == 1 && terminal);
    unsigned cases = 0;
    for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
        for (unsigned rounding = 0; rounding < 4; ++rounding)
            for (unsigned precision = 0; precision < 4; ++precision)
                for (unsigned sample = 0; sample < 128; ++sample) {
                    memset(&cpu_state, 0, sizeof cpu_state);
                    cpu_state.fpu.fpcr = rounding << 4 | precision << 6;
                    cpu_state.fpu.fpsr = 0x0afffff8;
                    cpu_state.fpu.null_state = sample & 1;
                    cpu_state.fpu.fpiar = 0x98765432;
                    for (unsigned reg = 0; reg < 8; ++reg) {
                        cpu_state.fpu.regs[reg].exponent = 0x3ff0 + ((reg * 3 + sample) % 30);
                        cpu_state.fpu.regs[reg].significand =
                            UINT64_C(0x8000000000000000) | ((UINT64_C(0x9876543210abcdef) * (reg + sample + 1)) >> 1);
                    }
                    if (sample & 64)
                        for (unsigned reg = 0; reg < 8; ++reg) {
                            if ((sample + reg) % 5 == 0) {
                                cpu_state.fpu.regs[reg].exponent = 0;
                                cpu_state.fpu.regs[reg].significand = 0;
                            }
                            cpu_state.fpu.regs[reg].exponent |= ((sample + reg) & 1) << 15;
                        }
                    unsigned source = sample & 7, destination = (sample >> 3) & 7;
                    unsigned operation = operations[op];
                    if (operation == 4 || operation == 0x41 || operation == 0x45)
                        cpu_state.fpu.regs[source].exponent &= 0x7fff;
                    if ((operation == 0x20 || operation == 0x24 || operation == 0x60 || operation == 0x64) &&
                        !cpu_state.fpu.regs[source].significand) {
                        cpu_state.fpu.regs[source].exponent = 0x3fff;
                        cpu_state.fpu.regs[source].significand = UINT64_C(0x8000000000000000);
                    }
                    unsigned command = source << 10 | destination << 7 | operations[op];
                    WRITEUW(0x1002, command);
                    CPUState before = cpu_state;
                    Syn68kFPUStep result;
                    assert(syn68k_fpu_step_decoded(0x1000, 0xf200, NULL, &result) && !result.vector &&
                           !cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
                    Syn68kFPUState expected = cpu_state.fpu;
                    cpu_state = before;
                    forbidden = 1;
                    uint32 next = syn68k_x64_execute(code);
                    forbidden = 0;
                    if (next != result.next || memcmp(&expected, &cpu_state.fpu, sizeof expected))
                        fprintf(
                            stderr,
                            "FPU mismatch: command=%04x rounding=%u precision=%u sample=%u next=%08x expected=%08x fpsr=%08x/%08x\n",
                            command, rounding, precision, sample, next, result.next, cpu_state.fpu.fpsr, expected.fpsr);
                    assert(next == result.next && !memcmp(&expected, &cpu_state.fpu, sizeof expected));
                    ++cases;
                }
    assert(!decoder_calls);
    conditional_checks();
    scalar_input_checks();
    scalar_destination_checks();
    assert(!decoder_calls);
    for (unsigned kind = 0; kind < 4; ++kind) {
        memset(&cpu_state, 0, sizeof cpu_state);
        cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff, UINT64_C(0x8000000000000000)};
        cpu_state.fpu.regs[1] = (Syn68kFP80){0x4000, UINT64_C(0xc000000000000000)};
        cpu_state.fpu.fpcr = kind == 0 ? 0x200 : kind == 1 ? 0x4000 : kind == 2 ? 0x400 : 0;
        if (kind == 1)
            cpu_state.fpu.regs[1] = (Syn68kFP80){0x7fff, UINT64_C(0x800123456789abcd)};
        else if (kind == 2)
            cpu_state.fpu.regs[1] = (Syn68kFP80){0, 0};
        else if (kind == 3)
            cpu_state.fpu.regs[0] = (Syn68kFP80){0x7ffe, UINT64_MAX};
        WRITEUW(0x1002, kind == 3 ? 0x0423 : 0x0420);
        CPUState before = cpu_state;
        Syn68kFPUStep result;
        assert(syn68k_fpu_step_decoded(0x1000, 0xf200, NULL, &result) && !result.vector && cpu_state.fpu.frame_size &&
               cpu_state.fpu.pending_vector);
        Syn68kFPUState expected = cpu_state.fpu;
        cpu_state = before;
        uint32 next = syn68k_x64_execute(code);
        assert(next == result.next && !memcmp(&expected, &cpu_state.fpu, sizeof expected) && decoder_calls == kind + 1);
    }
    // A missing command word must enter the FPU fault decoder, preserving
    // architectural fault priority instead of raising integer illegal-opcode.
    memset(&cpu_state, 0, sizeof cpu_state);
    cpu_state.fpu.null_state = 1;
    Syn68kFPUState before_fault = cpu_state.fpu;
    syn68k_set_memory_window(0x1002, 0, 0);
    capture_selector = 1;
    assert(syn68k_x64_execute(code) == 0x7654);
    capture_selector = 0;
    if (observed_selector != 11) {
        fprintf(stderr, "FPU command fault incorrectly used decoder selector %u\n", observed_selector);
        return 1;
    }
    assert(!memcmp(&before_fault, &cpu_state.fpu, sizeof before_fault));
    syn68k_set_memory_window(0x10000, 0, 0);
    syn68k_x64_release(code);
    free(ram);
    printf(
        "%u direct register FPU/live-command/alias/rounding/precision cases passed without semantic helpers; four exceptional frames match checked paths\n",
        cases);
    return 0;
}
