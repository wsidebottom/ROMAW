/*+===================================================================
  File:      native040-registers-test.c

  Summary:   Validate raw-FPR transfers, captured store faults, mask order, wrap, RAM and reentrant packets.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor direct 68040 register-transfer regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-registers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Syn68kNative040Entry entries[2];
static unsigned cases;

typedef struct {
    unsigned char bytes[128];
    uint32_t base;
    unsigned calls, fail, flip, nested, child_calls;
    CPUState* cpu;
    uint32_t addresses[40], values[40];
    unsigned sizes[40], codes[40];
} Bus;

static int read_data(void*, uint32_t, unsigned char*, unsigned, uint32_t, unsigned);

static void callback(Bus* bus)
{
    if (bus->calls != 2)
        return;
    if (bus->flip)
        bus->cpu->sr ^= 0x2000;
    if (bus->nested) {
        CPUState child_cpu = {0};
        Bus child_bus = {.base = 0x2000, .cpu = &child_cpu};
        child_bus.bytes[0] = 0x7f;
        child_bus.bytes[1] = 0xff;
        child_bus.bytes[4] = 0xa0;
        child_bus.bytes[11] = 7;
        Syn68kMMU040 child_mmu = {0};
        Syn68kMMU040Memory memory = {.mmu = &child_mmu, .opaque = &child_bus, .read = read_data};
        Syn68kNative040Registers frame = {.cpu = &child_cpu, .memory = &memory, .address = child_bus.base, .mask = 1};
        entries[0](&frame);
        assert(frame.ok && child_bus.calls == 3 && child_cpu.fpu.regs[7].exponent == 0x7fff &&
               child_cpu.fpu.regs[7].significand == UINT64_C(0xa000000000000007));
        ++bus->child_calls;
    }
}

static unsigned record(Bus* bus, uint32_t address, unsigned size, uint32_t attributes, unsigned fc)
{
    assert(!attributes && bus->calls < 40);
    unsigned at = bus->calls++;
    bus->addresses[at] = address;
    bus->sizes[at] = size;
    bus->codes[at] = fc;
    return at;
}

static int read_data(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                     unsigned fc)
{
    Bus* bus = opaque;
    unsigned at = record(bus, address, size, attributes, fc);
    if (bus->fail == bus->calls)
        return 0;
    uint32_t delta = address - bus->base;
    assert(delta <= sizeof bus->bytes && size <= sizeof bus->bytes - delta);
    memcpy(bytes, bus->bytes + delta, size);
    for (unsigned i = 0; i < size; ++i)
        bus->values[at] = bus->values[at] << 8 | bytes[i];
    callback(bus);
    return 1;
}

static int write_data(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    Bus* bus = opaque;
    unsigned at = record(bus, address, size, attributes, fc);
    for (unsigned i = 0; i < size; ++i)
        bus->values[at] = bus->values[at] << 8 | bytes[i];
    if (bus->fail == bus->calls)
        return 0;
    uint32_t delta = address - bus->base;
    assert(delta <= sizeof bus->bytes && size <= sizeof bus->bytes - delta);
    memcpy(bus->bytes + delta, bytes, size);
    callback(bus);
    return 1;
}

static unsigned reverse_byte(unsigned value)
{
    value = (value & 0x55) << 1 | (value >> 1 & 0x55);
    value = (value & 0x33) << 2 | (value >> 2 & 0x33);
    return (value << 4 | value >> 4) & 255;
}

static void put(unsigned char* bytes, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        bytes[i] = value >> ((size - i - 1) * 8);
}

static uint32_t get(const unsigned char* bytes, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | bytes[i];
    return value;
}

static void check(unsigned mask, unsigned reverse, unsigned writing, unsigned supervisor, unsigned failure,
                  unsigned direct, unsigned wrap, unsigned nested)
{
    CPUState cpu, expected;
    memset(&cpu, 0x5a, sizeof cpu);
    cpu.sr = supervisor ? 0x2000 : 0;
    for (unsigned reg = 0; reg < 8; ++reg) {
        cpu.fpu.regs[reg].exponent = 0x7fff ^ reg * 0x1234;
        cpu.fpu.regs[reg].significand = UINT64_C(0xa23456789abcdef0) ^ (uint64_t)reg * UINT64_C(0x19385746abcdef01);
    }
    expected = cpu;
    Bus bus = {.base = wrap ? 0xfffffff0 : 0x1000, .cpu = &cpu, .fail = failure, .flip = !direct, .nested = nested};
    for (unsigned i = 0; i < sizeof bus.bytes; ++i)
        bus.bytes[i] = i * 37 + 0x93;
    unsigned char wanted[sizeof bus.bytes];
    memcpy(wanted, bus.bytes, sizeof wanted);
    unsigned selected = reverse ? mask : reverse_byte(mask);
    unsigned count = __builtin_popcount(selected), chunks = writing ? 4 : 3;
    unsigned transfers = count * chunks;
    unsigned successful = direct || !failure || failure > transfers ? transfers : failure - 1;
    const unsigned offsets[] = {0, 2, 4, 8}, sizes[] = {2, 2, 4, 4};
    for (unsigned reg = 0; reg < 8; ++reg) {
        if (!(selected & (1u << reg)))
            continue;
        unsigned rank = __builtin_popcount(selected & ((1u << reg) - 1));
        unsigned char* slot = wanted + rank * 12;
        if (writing) {
            unsigned char value[12];
            put(value, cpu.fpu.regs[reg].exponent, 2);
            put(value + 2, 0, 2);
            put(value + 4, cpu.fpu.regs[reg].significand >> 32, 4);
            put(value + 8, cpu.fpu.regs[reg].significand, 4);
            for (unsigned part = 0; part < 4; ++part)
                if (rank * 4 + part < successful)
                    memcpy(slot + offsets[part], value + offsets[part], sizes[part]);
        }
        else if ((rank + 1) * 3 <= successful) {
            expected.fpu.regs[reg].exponent = get(slot, 4) >> 16;
            expected.fpu.regs[reg].significand = (uint64_t)get(slot + 4, 4) << 32 | get(slot + 8, 4);
        }
    }
    if (!direct && successful >= 2)
        expected.sr ^= 0x2000;
    Syn68kMMU040 mmu = {0};
    Syn68kMMU040Memory memory = {.mmu = &mmu, .opaque = &bus, .read = read_data, .write = write_data};
    Syn68kMMU040Native native;
    assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
    unsigned attempted = 0;
    native.write_attempted = &attempted;
    Syn68kNative040Registers frame = {.cpu = &cpu,
                                      .memory = &memory,
                                      .ram = direct ? (Syn68kNative040RAM){bus.bytes, bus.base, sizeof bus.bytes}
                                                    : (Syn68kNative040RAM){0},
                                      .address = bus.base,
                                      .mask = mask,
                                      .reverse = reverse,
                                      .store_tail_valid = 1};
    entries[writing](&frame);
    assert(frame.ok == (successful == transfers));
    assert(!memcmp(&cpu, &expected, sizeof cpu) && !memcmp(bus.bytes, wanted, sizeof wanted));
    assert(attempted == (writing && count != 0));
    unsigned completed = successful / chunks;
    assert(frame.address == bus.base + completed * 12);
    if (direct)
        assert(!bus.calls);
    else {
        assert(bus.calls == successful + (successful != transfers));
        assert(bus.child_calls == (nested && successful >= 2));
        for (unsigned call = 0; call < bus.calls; ++call) {
            unsigned part = call % chunks, rank = call / chunks;
            assert(bus.addresses[call] == bus.base + rank * 12 + (writing ? offsets[part] : part * 4));
            assert(bus.sizes[call] == (writing ? sizes[part] : 4));
            assert(bus.codes[call] == (((supervisor ^ (call >= 2)) ? 4 : 0) | 1));
        }
    }
    if (!frame.ok)
        assert(frame.access.bus_error && frame.access.logical == bus.addresses[bus.calls - 1]);
    assert(frame.store_tail_valid == (writing && !frame.ok && successful % 4 == 2));
    if (frame.store_tail_valid) {
        unsigned rank = successful / 4, seen = 0;
        for (unsigned reg = 0; reg < 8; ++reg)
            if (selected & (1u << reg)) {
                if (seen++ == rank)
                    assert(frame.store_tail == (uint32_t)expected.fpu.regs[reg].significand);
            }
    }
    ++cases;
}

int main(void)
{
    entries[0] = syn68k_native040_entry(NATIVE040_LOAD_FPRS);
    entries[1] = syn68k_native040_entry(NATIVE040_STORE_FPRS);
    assert(entries[0] && entries[1]);
    for (unsigned mask = 0; mask <= 255; ++mask)
        for (unsigned reverse = 0; reverse < 2; ++reverse)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
                    for (unsigned failure = 0; failure <= 33; ++failure)
                        check(mask, reverse, writing, supervisor, failure, 0, 0, 0);
                    check(mask, reverse, writing, supervisor, 0, 1, 0, 0);
                    check(mask, reverse, writing, supervisor, 0, 0, 1, 0);
                }
    check(255, 0, 0, 0, 0, 0, 0, 1);
    check(255, 1, 1, 1, 0, 0, 0, 1);
    CPUState cpu = {0}, before = cpu;
    Syn68kMMU040 mmu = {0};
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned bad = 0; bad < 4; ++bad) {
            Syn68kNative040Registers frame = {.cpu = &cpu, .memory = &memory, .mask = 0, .ok = 1};
            if (bad == 0)
                frame.cpu = NULL;
            if (bad == 1)
                frame.memory = NULL;
            if (bad == 2)
                frame.mask = 256;
            if (bad == 3)
                frame.reverse = 2;
            entries[writing](&frame);
            assert(!frame.ok && !memcmp(&cpu, &before, sizeof cpu));
            ++cases;
        }
    printf("%u emitted raw-register mask/order/partial-fault/FC/RAM/wrap/reentry cases passed\n", cases);
    return 0;
}
