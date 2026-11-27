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

static void stop_checks(void)
{
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_STOP);
    assert(entry);
    const unsigned banks[] = {0, 0, 1, 2}, pending[] = {0, 1, 7};
    unsigned checked = 0;
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned value = 0; value < 65536; ++value)
            for (unsigned p = 0; p < sizeof pending / sizeof *pending; ++p) {
                CPUState before, expected, compiled;
                memset(&before, 0x5a, sizeof before);
                before.sr = mode << 12;
                before.usp = 0x11223344;
                before.isp = 0x55667788;
                before.msp = 0x99aabbcc;
                before.regs[15].ul.n = 0xddeeff00;
                memset((void*)before.interrupt_pending, 0, sizeof before.interrupt_pending);
                if (pending[p])
                    before.interrupt_pending[pending[p]] = 1;
                expected = compiled = before;
                unsigned masked = value & 0xf71f;
                expected.sr = masked & 0xffe0;
                expected.ccc = masked & 1;
                expected.ccv = masked & 2;
                expected.ccnz = masked & 4 ? 0 : 4;
                expected.ccn = masked & 8;
                expected.ccx = masked & 16;
                if (mode != (masked >> 12 & 3)) {
                    uint32 saved[] = {before.usp, before.isp, before.msp};
                    saved[banks[mode]] = before.regs[15].ul.n;
                    expected.usp = saved[0];
                    expected.isp = saved[1];
                    expected.msp = saved[2];
                    expected.regs[15].ul.n = saved[banks[masked >> 12 & 3]];
                }
                if (pending[p])
                    expected.interrupt_status_changed = -1;
                Syn68kNative040Control frame = {.cpu = &compiled, .command = value, .ok = -1};
                entry(&frame);
                assert(frame.ok == 1 && !memcmp(&expected, &compiled, sizeof expected));
                ++checked;
            }
    CPUState cpu = {0}, before = cpu;
    Syn68kNative040Control frame = {.cpu = NULL, .command = 0, .ok = 1};
    entry(&frame);
    assert(!frame.ok);
    frame = (Syn68kNative040Control){.cpu = &cpu, .command = 65536, .ok = 1};
    entry(&frame);
    assert(!frame.ok && !memcmp(&before, &cpu, sizeof cpu));
    printf("%u emitted STOP immediate/mask/stack-bank/interrupt-notification cases passed\n", checked);
}

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

typedef struct {
    unsigned char frame[14];
    uint32 addresses[5];
    unsigned calls, failed_offset;
} RTEMemory;

static int rte_stack_read(void* opaque, uint32 address, unsigned char* bytes, unsigned size, uint32 attributes,
                          unsigned fc)
{
    (void)attributes;
    RTEMemory* stack = opaque;
    unsigned offset = address - 0x1000;
    assert(fc == 5 && stack->calls < 5 && offset + size <= sizeof stack->frame);
    stack->addresses[stack->calls++] = address;
    if (offset == stack->failed_offset)
        return 0;
    memcpy(bytes, stack->frame + offset, size);
    return 1;
}

static void rte_checks(void)
{
    Syn68kNative040Entry entry = syn68k_native040_entry(NATIVE040_RTE);
    assert(entry);
    for (unsigned continuation = 0; continuation < 2; ++continuation)
        for (unsigned fault = 0; fault < 3; ++fault) {
            RTEMemory stack = {.frame = {0x20, 0x15, 0, 0, 0x30, 0, 0x70, 8, 0x12, 0x34, 0x56, 0x78},
                               .failed_offset = fault == 1 ? 8 : fault == 2 ? 12 : 99};
            stack.frame[12] = continuation ? 0x10 : 0;
            Syn68kMMU040 mmu = {0};
            Syn68kMMU040Memory memory = {.mmu = &mmu, .opaque = &stack, .read = rte_stack_read};
            CPUState cpu = {.sr = 0x2000};
            cpu.regs[15].ul.n = 0x1000;
            Syn68kNative040MMU bus = {.memory = &memory};
            Syn68kNative040RTE packet = {.cpu = &cpu, .memory = &bus, .pc = 0x2000};
            entry(&packet);
            assert(stack.calls == (continuation && fault != 2 ? 5u : 4u));
            assert(stack.addresses[0] == 0x1000 && stack.addresses[1] == 0x1002 &&
                   stack.addresses[2] == 0x1006 && stack.addresses[3] == 0x100c);
            if (stack.calls == 5)
                assert(stack.addresses[4] == 0x1008);
            if (fault == 2 || (fault == 1 && continuation)) {
                assert(packet.step.vector == 2 && packet.step.format == 7 && packet.step.next == 0x2000 &&
                       packet.step.fault_address == 0x1000 + stack.failed_offset && packet.step.access.bus_error);
                assert(packet.step.rte_internal_fault == (fault == 1) &&
                       packet.step.restartable_read == (fault == 2) && cpu.regs[15].ul.n == 0x1000);
            }
            else {
                assert(!packet.step.vector && packet.step.next == 0x3000 && cpu.regs[15].ul.n == 0x103c &&
                       cpu.ccc == 1 && !cpu.ccnz && cpu.ccx == 16);
                assert(packet.step.movem_state == continuation * 2);
                if (continuation)
                    assert(packet.step.movem_ea == 0x12345678);
            }
        }
    puts("6 emitted RTE frame-order, continuation and double-fault cases passed");
}
int main(void)
{
    entries[0] = syn68k_native040_entry(NATIVE040_MOVEC_READ);
    entries[1] = syn68k_native040_entry(NATIVE040_MOVEC_WRITE);
    assert(entries[0] && entries[1]);
    stop_checks();
    rte_checks();
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
