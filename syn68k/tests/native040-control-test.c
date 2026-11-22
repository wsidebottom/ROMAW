/*+===================================================================
  File:      native040-control-test.c

  Summary:   Compare closed MOVEC roots with maintained register semantics across masks, banks and invalid selectors.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor direct 68040 control-register regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-control.h"
#include "native040.h"
#include "control040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Syn68kNative040Entry entries[2];
static unsigned cases;

static void compare(unsigned selector, unsigned reg, unsigned writing, unsigned sr, unsigned connection, uint32 value)
{
    CPUState model, compiled;
    Syn68kMMU040 model_mmu, compiled_mmu;
    memset(&model, 0x5a, sizeof model);
    memset(&model_mmu, 0xa5, sizeof model_mmu);
    model.sr = sr;
    model.usp = 0x11223344;
    model.isp = 0x55667788;
    model.msp = 0x99aabbcc;
    model.regs[15].ul.n = 0xddeeff00;
    model.regs[reg].ul.n = value;
    compiled = model;
    compiled_mmu = model_mmu;
    Syn68kMMU040Memory model_memory = {.mmu = connection == 1 ? &model_mmu : NULL};
    Syn68kMMU040Memory compiled_memory = {.mmu = connection == 1 ? &compiled_mmu : NULL};
    int expected = syn68k_control040_transfer(&model, connection ? &model_memory : NULL, selector, writing, &value);
    if (expected && !writing)
        model.regs[reg].ul.n = value;
    Syn68kNative040Control frame = {
        .cpu = &compiled, .memory = connection ? &compiled_memory : NULL, .command = reg << 12 | selector, .ok = -1};
    entries[writing](&frame);
    assert(frame.ok == expected && !memcmp(&model, &compiled, sizeof model) &&
           !memcmp(&model_mmu, &compiled_mmu, sizeof model_mmu));
    ++cases;
}

int main(void)
{
    entries[0] = syn68k_native040_entry(NATIVE040_MOVEC_READ);
    entries[1] = syn68k_native040_entry(NATIVE040_MOVEC_WRITE);
    assert(entries[0] && entries[1]);
    for (unsigned selector = 0; selector < 4096; ++selector)
        for (unsigned writing = 0; writing < 2; ++writing)
            compare(selector, 0, writing, 0x2000, 1, 0x12345678);
    const unsigned selectors[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x800, 0x801, 0x803, 0x804, 0x805, 0x806, 0x807};
    const uint32 values[] = {0, 1, UINT32_MAX, 0x80008000, 0x12345678, 0x80000003};
    for (unsigned i = 0; i < sizeof selectors / sizeof *selectors; ++i)
        for (unsigned reg = 0; reg < 16; ++reg)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned mode = 0; mode < 4; ++mode)
                    for (unsigned connection = 0; connection < 3; ++connection)
                        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v)
                            compare(selectors[i], reg, writing, mode << 12, connection, values[v]);
    CPUState cpu = {0}, before = cpu;
    for (unsigned writing = 0; writing < 2; ++writing) {
        Syn68kNative040Control frame = {.cpu = NULL, .command = 0, .ok = 1};
        entries[writing](&frame);
        assert(!frame.ok);
        frame = (Syn68kNative040Control){.cpu = &cpu, .command = 65536, .ok = 1};
        entries[writing](&frame);
        assert(!frame.ok && !memcmp(&cpu, &before, sizeof cpu));
        cases += 2;
    }
    printf("%u emitted MOVEC selector/register/mask/active-bank/flat-gate/invalid-packet cases passed\n", cases);
    return 0;
}
