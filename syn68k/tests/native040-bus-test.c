/*+===================================================================
  File:      native040-bus-test.c

  Summary:   Compare typed native mapped-bus dispatch with the C model and forbid host walker execution.

  Classes:   None.

  Functions: main, __wrap_syn68k_mmu040_translate; imports the unwrapped model walker.

  Origin:    Original ARDI Executor direct 68040 mapped-bus regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char bytes[0x10000];
    unsigned descriptors, operands, fail;
} Bus;

static Bus model_bus, native_bus;
static unsigned forbid_host_walker, host_walks;

Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t address,
                                                  unsigned fc, int writing)
{
    assert(!forbid_host_walker);
    ++host_walks;
    return __real_syn68k_mmu040_translate(mmu, bus, address, fc, writing);
}

static void put(Bus* bus, unsigned address, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bus->bytes[address + i] = value >> (24 - 8 * i);
}

static int descriptor_read(void* opaque, uint32_t address, uint32_t* value)
{
    Bus* bus = opaque;
    ++bus->descriptors;
    if (address > sizeof bus->bytes - 4 || bus->fail == 1)
        return 0;
    *value = 0;
    for (unsigned i = 0; i < 4; ++i)
        *value = *value << 8 | bus->bytes[address + i];
    return 1;
}

static int descriptor_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    Bus* bus = opaque;
    ++bus->descriptors;
    if (address > sizeof bus->bytes - 4 || bus->fail == 2)
        return 0;
    if (locked) {
        uint32_t previous = 0;
        for (unsigned i = 0; i < 4; ++i)
            previous = previous << 8 | bus->bytes[address + i];
        value |= previous;
    }
    put(bus, address, value);
    return 1;
}

static int operand_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    Bus* bus = opaque;
    (void)attributes;
    (void)fc;
    ++bus->operands;
    if (address >= sizeof bus->bytes || size > sizeof bus->bytes - address || bus->fail == 3)
        return 0;
    memcpy(bytes, bus->bytes + address, size);
    return 1;
}

static int operand_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    Bus* bus = opaque;
    (void)attributes;
    (void)fc;
    ++bus->operands;
    if (address >= sizeof bus->bytes || size > sizeof bus->bytes - address || bus->fail == 3)
        return 0;
    memcpy(bus->bytes + address, bytes, size);
    return 1;
}

static Syn68kMMU040Memory memory(Syn68kMMU040* mmu, Bus* bus)
{
    return (Syn68kMMU040Memory){.mmu = mmu,
                                .tables = {bus, descriptor_read, descriptor_write},
                                .opaque = bus,
                                .read = operand_read,
                                .write = operand_write};
}

static void compare(Syn68kMMU040Access a, Syn68kMMU040Access b)
{
#define SAME(field) assert(a.field == b.field)
    SAME(translation.physical);
    SAME(translation.status);
    SAME(translation.descriptor_address);
    SAME(translation.attributes);
    SAME(translation.fault);
    SAME(translation.transparent);
    SAME(translation.cached);
    SAME(logical);
    SAME(completed);
    SAME(bus_error);
    SAME(request);
    SAME(value);
    SAME(size);
    SAME(fc);
    SAME(writing);
    SAME(rmw);
#undef SAME
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned aligned = 0; aligned < 2; ++aligned)
        for (unsigned ram = 0; ram < 2; ++ram)
            for (unsigned mode = 0; mode < 3; ++mode)
                for (unsigned fc = 0; fc < 9; ++fc)
                    for (unsigned size = 0; size <= 4; ++size)
                        for (unsigned split = 0; split < 4; ++split)
                            for (unsigned fail = 0; fail < 4; ++fail) {
                                memset(&model_bus, 0, sizeof model_bus);
                                model_bus.fail = ram ? 0 : fail;
                                for (unsigned i = 0; i < sizeof model_bus.bytes; ++i)
                                    model_bus.bytes[i] = (i * 37 + 19) >> 3;
                                put(&model_bus, 0x1000, 0x2002);
                                put(&model_bus, 0x2000, 0x3002);
                                put(&model_bus, 0x3010, 0x8001);
                                put(&model_bus, 0x3014, fail == 3 && ram ? 0 : 0xc001);
                                native_bus = model_bus;
                                Syn68kMMU040 model_mmu = {.tc = mode == 0   ? 0
                                                                : mode == 1 ? 0x8000
                                                                            : 0xc000,
                                                          .urp = 0x1000,
                                                          .srp = 0x1000};
                                Syn68kMMU040 native_mmu = model_mmu;
                                Syn68kMMU040Memory model = memory(&model_mmu, &model_bus);
                                Syn68kMMU040Memory native = memory(&native_mmu, &native_bus);
                                model.aligned_transfers = native.aligned_transfers = aligned;
                                Syn68kMMU040Native entries;
                                Syn68kNative040RAM span = {ram ? native_bus.bytes : NULL, 0, sizeof native_bus.bytes};
                                assert(syn68k_native040_memory(&native, &entries, span));
                                unsigned attempted = 0;
                                entries.write_attempted = &attempted;
                                unsigned page_size = mode == 2 ? 8192 : 4096;
                                uint32_t address = page_size * 5 - split;
                                for (unsigned operation = 0; operation < 4; ++operation) {
                                    uint32_t a = 0xdeadbeef, b = a;
                                    Syn68kMMU040Access expected, actual;
                                    if (operation < 2)
                                        expected = syn68k_mmu040_read(&model, address, fc, size, operation, &a);
                                    else if (operation == 2)
                                        expected = syn68k_mmu040_write(&model, address, fc, size, a);
                                    else
                                        expected = syn68k_mmu040_write_rmw(&model, address, fc, size, a);
                                    forbid_host_walker = 1;
                                    if (operation < 2)
                                        actual = syn68k_mmu040_read(&native, address, fc, size, operation, &b);
                                    else if (operation == 2)
                                        actual = syn68k_mmu040_write(&native, address, fc, size, b);
                                    else
                                        actual = syn68k_mmu040_write_rmw(&native, address, fc, size, b);
                                    forbid_host_walker = 0;
                                    compare(expected, actual);
                                    assert(a == b && attempted == (operation >= 2));
                                    assert(!memcmp(&model_mmu, &native_mmu, sizeof model_mmu));
                                    assert(!memcmp(model_bus.bytes, native_bus.bytes, sizeof model_bus.bytes));
                                    if (!ram || fc == 0 || fc == 3 || fc == 4 || fc == 7 || fc == 8) {
                                        assert(model_bus.operands == native_bus.operands);
                                        if (!ram)
                                            assert(model_bus.descriptors == native_bus.descriptors);
                                    }
                                    else
                                        assert(!native_bus.operands && !native_bus.descriptors);
                                    ++cases;
                                }
                            }
    assert(host_walks);
    Syn68kMMU040 mmu = {0};
    Syn68kMMU040Memory bus = {.mmu = &mmu};
    Syn68kMMU040Native entries = {0}, unchanged = entries;
    Syn68kMMU040Memory before = bus;
    assert(!syn68k_native040_memory(NULL, &entries, (Syn68kNative040RAM){0}));
    assert(!syn68k_native040_memory(&bus, NULL, (Syn68kNative040RAM){0}));
    assert(!memcmp(&before, &bus, sizeof bus) && !memcmp(&entries, &unchanged, sizeof entries));
    bus.mmu = NULL;
    assert(!syn68k_native040_memory(&bus, &entries, (Syn68kNative040RAM){0}));
    printf("Native typed mapped bus: %u differential requests; no host walker calls from native dispatch\n", cases);
    return 0;
}
