/*+===================================================================
  File:      native040-ea-test.c

  Summary:   Compare emitted effective addresses with the CPU model, bus effects and nested callbacks.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor direct 68040 regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-ea.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

CPUState cpu_state;
static Syn68kNative040Entry entry;
static unsigned char code[16];
static unsigned cases, fail_call, nested;
static unsigned char ram[65536];

typedef struct {
    uint32_t address[8];
    unsigned size[8], fc[8], calls;
} Traffic;

static int read_operand(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    Traffic* traffic = opaque;
    unsigned at = traffic->calls++;
    assert(at < 8 && !attributes);
    traffic->address[at] = address;
    traffic->size[at] = size;
    traffic->fc[at] = fc;
    if (nested) {
        nested = 0;
        CPUState other = {0};
        other.regs[15].ul.n = 0x1234;
        Syn68kMMU040 mmu = {0};
        Syn68kMMU040Memory memory = {.mmu = &mmu};
        Syn68kNative040EA inner = {.cpu = &other, .context.memory = &memory, .code = 0x1f, .size = 1};
        entry(&inner);
        assert(!inner.context.error && inner.result.address == 0x1234 && other.regs[15].ul.n == 0x1236);
        ++cases;
    }
    if (fail_call == at + 1)
        return 0;
    for (unsigned i = 0; i < size; ++i) {
        uint32_t byte = address + i;
        bytes[i] = byte >= 0x2000 && byte < 0x2010 ? code[byte - 0x2000] : (byte * 37 + 0x51) >> 7;
    }
    return 1;
}

static void compare(unsigned ea, unsigned size, int writing, int registers, unsigned extension, unsigned supervisor)
{
    CPUState initial = {0};
    for (unsigned reg = 0; reg < 16; ++reg)
        initial.regs[reg].ul.n = 0x23450000 + reg * 0x12341;
    initial.sr = supervisor ? 0x2000 : 0;
    code[0] = extension >> 8;
    code[1] = extension;
    for (unsigned i = 2; i < sizeof code; ++i)
        code[i] = 0x91 + i * 13;
    Syn68kMMU040 model_mmu = {0}, native_mmu = {0};
    Traffic model_bus = {0}, native_bus = {0};
    Syn68kMMU040Memory model_memory = {.mmu = &model_mmu, .opaque = &model_bus, .read = read_operand};
    Syn68kMMU040Memory native_memory = {.mmu = &native_mmu, .opaque = &native_bus, .read = read_operand};
    Syn68kEAContext context = {.pc = 0x2000, .memory = &model_memory};
    unsigned reenter = nested;
    nested = 0;
    cpu_state = initial;
    Syn68kEA expected = syn68k_effective_address(&context, ea, size, writing, registers);
    CPUState actual = initial;
    Syn68kNative040EA frame = {.cpu = &actual,
                               .context = {.pc = 0x2000, .memory = &native_memory},
                               .code = ea,
                               .size = size,
                               .writing = writing,
                               .registers = registers};
    nested = reenter;
    entry(&frame);
    assert(frame.context.memory == &native_memory);
    assert(frame.result.address == expected.address && frame.result.reg == expected.reg &&
           frame.result.instruction == expected.instruction);
    assert(!memcmp(&actual, &cpu_state, sizeof actual));
    frame.context.memory = context.memory;
    assert(!memcmp(&frame.context, &context, sizeof context));
    assert(!memcmp(&native_bus, &model_bus, sizeof model_bus));
    assert(!memcmp(&native_mmu, &model_mmu, sizeof model_mmu));
    ++cases;
}

static void put(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4);
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - 8 * i);
}

static void mapped_ram(void)
{
    for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
        memset(ram, 0, sizeof ram);
        Syn68kMMU040 mmu = {.tc = 0x8000, .urp = 0x1000, .srp = 0x4000};
        put(0x1000, 0x2002);
        put(0x2000, 0x3002);
        put(0x4000, 0x5002);
        put(0x5000, 0x6002);
        unsigned pte = supervisor ? 0x6010 : 0x3010;
        unsigned physical = supervisor ? 0xc000 : 0x8000;
        put(pte, physical | 1);
        put(physical, 0x01610010); // Full extension, suppressed index, indirect.
        put(physical + 16, 0x00005020);
        CPUState cpu = {0};
        cpu.sr = supervisor ? 0x2000 : 0;
        cpu.regs[8].ul.n = 0x4000;
        Syn68kMMU040Memory memory = {.mmu = &mmu}; // Any callback would fail.
        Syn68kNative040EA frame = {.cpu = &cpu,
                                   .context = {.pc = 0x4000, .memory = &memory},
                                   .code = 0x30,
                                   .size = 12,
                                   .ram = {ram, 0, sizeof ram}};
        entry(&frame);
        assert(!frame.context.error && frame.context.pc == 0x4004 && frame.result.address == 0x5020);
        assert(frame.context.memory == &memory && frame.result.reg == -1);
        ++cases;
        // Preserve calculated addresses without repeating an indirect operand.
        uint32_t saved = 0x76543210;
        put(physical + 16, 0);
        frame.context.pc = 0x4000;
        frame.context.resume_ea = &saved;
        entry(&frame);
        assert(!frame.context.error && frame.result.address == saved && frame.context.pc == 0x4004);
        ++cases;
        // Split extension fetch: retain the original PC and completed prefix.
        frame.context = (Syn68kEAContext){.pc = 0x4ffe, .memory = &memory};
        frame.code = 0x39;
        entry(&frame);
        assert(frame.context.error == 2 && frame.context.pc == 0x4ffe && frame.context.fault_address == 0x5000);
        assert(frame.context.access.completed == 2 && frame.context.memory == &memory);
        ++cases;
    }
}

int main(void)
{
    entry = syn68k_native040_entry(NATIVE040_EA);
    assert(entry);
    for (unsigned extension = 0; extension <= 0xffff; ++extension) {
        compare(0x30, 12, 0, 1, extension, 0);
        compare(0x3b, 4, 0, 1, extension, 1);
    }
    for (unsigned ea = 0; ea < 64; ++ea)
        for (unsigned size = 1; size <= 12; ++size)
            for (int writing = 0; writing < 2; ++writing)
                for (int registers = 0; registers < 3; ++registers)
                    compare(ea, size, writing, registers, 0x0162, ea & 1);
    for (fail_call = 1; fail_call <= 5; ++fail_call) {
        compare(0x30, 12, 0, 1, 0x0133, 0);
        compare(0x3b, 12, 0, 1, 0x0137, 1);
    }
    fail_call = 0;
    nested = 1;
    compare(0x30, 12, 0, 1, 0x0161, 0);
    assert(!nested);
    Syn68kNative040EA invalid = {0};
    entry(&invalid);
    assert(invalid.context.error == 11 && invalid.result.reg == -1);
    ++cases;
    mapped_ram();
    printf("%u emitted EA model/bus/extension/fault/reentry cases passed\n", cases);
    return 0;
}
