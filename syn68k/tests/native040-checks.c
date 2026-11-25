/*+===================================================================
  File:      native040-checks.c

  Summary:   Adapt the maintained MMU regression cases to published native packet entries.

  Classes:   None.

  Functions: syn68k_test_native040_translate, syn68k_test_native040_ptest, syn68k_test_native040_flush,
             syn68k_test_native040_control, syn68k_test_native040_reset, syn68k_test_native040_read,
             syn68k_test_native040_write, syn68k_test_native040_write_rmw.

  Origin:    Original ARDI Executor emitted 68040 regression adapter.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "native040.h"
#include <assert.h>
#include <stdlib.h>

/* Emitted cache groups must remain inside the published code image. */
Syn68kCache040Access __wrap_syn68k_cache040_access(Syn68kCache040* cache, const Syn68kCache040Bus* bus,
                                                   uint32_t address, unsigned fc, unsigned size, int writing,
                                                   uint32_t value, uint32_t attributes, uint32_t cacr, int no_allocate)
{
    (void)cache;
    (void)bus;
    (void)address;
    (void)fc;
    (void)size;
    (void)writing;
    (void)value;
    (void)attributes;
    (void)cacr;
    (void)no_allocate;
    abort();
}

Syn68kCache040Access __wrap_syn68k_cache040_maintain(Syn68kCache040* cache, const Syn68kCache040Bus* bus,
                                                     unsigned caches, unsigned scope, uint32_t address,
                                                     unsigned page_shift, int push)
{
    (void)cache;
    (void)bus;
    (void)caches;
    (void)scope;
    (void)address;
    (void)page_shift;
    (void)push;
    abort();
}

static void invoke(enum Syn68kNative040Operation op, Syn68kNative040MMU* frame)
{
    Syn68kNative040Entry entry = syn68k_native040_entry(op);
    assert(entry);
    entry(frame);
}

Syn68kMMU040Result syn68k_test_native040_translate(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t address,
                                                   unsigned fc, int writing)
{
    Syn68kMMU040Memory memory = {.mmu = mmu};
    if (bus)
        memory.tables = *bus;
    Syn68kNative040MMU frame = {.memory = &memory, .address = address, .fc = fc, .writing = writing};
    invoke(NATIVE040_TRANSLATE, &frame);
    return frame.translation;
}

Syn68kMMU040Result syn68k_test_native040_ptest(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t address,
                                               unsigned fc, int writing)
{
    Syn68kMMU040Memory memory = {.mmu = mmu};
    if (bus)
        memory.tables = *bus;
    Syn68kNative040MMU frame = {.memory = &memory, .address = address, .fc = fc, .writing = writing};
    invoke(NATIVE040_PTEST, &frame);
    return frame.translation;
}

void syn68k_test_native040_flush(Syn68kMMU040* mmu, int all, uint32_t address, int supervisor, int global)
{
    Syn68kMMU040Memory memory = {.mmu = mmu};
    Syn68kNative040MMU frame = {
        .memory = &memory, .address = address, .all = all, .supervisor = supervisor, .global = global};
    invoke(NATIVE040_FLUSH, &frame);
}

int syn68k_test_native040_control(Syn68kMMU040* mmu, enum Syn68kMMU040Control control, int writing, uint32_t* value)
{
    if (!value)
        return 0; // Invalid host requests have no operand to bind.
    Syn68kMMU040Memory memory = {.mmu = mmu};
    Syn68kNative040MMU frame = {.memory = &memory, .control = control, .writing = writing, .value = *value};
    invoke(NATIVE040_CONTROL, &frame);
    *value = frame.value;
    return frame.ok;
}

void syn68k_test_native040_reset(Syn68kMMU040* mmu)
{
    Syn68kMMU040Memory memory = {.mmu = mmu};
    Syn68kNative040MMU frame = {.memory = &memory};
    invoke(NATIVE040_RESET, &frame);
}

Syn68kMMU040Access syn68k_test_native040_read(Syn68kMMU040Memory* memory, uint32_t address, unsigned fc, unsigned size,
                                              int rmw, uint32_t* value)
{
    if (!value)
        return syn68k_mmu040_read(memory, address, fc, size, rmw, value); // Invalid host request.
    Syn68kNative040MMU frame = {
        .memory = memory, .address = address, .fc = fc, .size = size, .rmw = rmw, .value = *value};
    invoke(NATIVE040_READ, &frame);
    *value = frame.value;
    return frame.access;
}

Syn68kMMU040Access syn68k_test_native040_write(Syn68kMMU040Memory* memory, uint32_t address, unsigned fc, unsigned size,
                                               uint32_t value)
{
    Syn68kNative040MMU frame = {.memory = memory, .address = address, .fc = fc, .size = size, .value = value};
    invoke(NATIVE040_WRITE, &frame);
    return frame.access;
}

Syn68kMMU040Access syn68k_test_native040_write_rmw(Syn68kMMU040Memory* memory, uint32_t address, unsigned fc,
                                                   unsigned size, uint32_t value)
{
    Syn68kNative040MMU frame = {.memory = memory, .address = address, .fc = fc, .size = size, .value = value};
    invoke(NATIVE040_WRITE_RMW, &frame);
    return frame.access;
}
