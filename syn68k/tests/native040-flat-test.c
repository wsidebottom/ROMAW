/*+===================================================================
  File:      native040-flat-test.c

  Summary:   Compare closed flat-memory roots with maintained EA memory access, wrapping, aliases and callbacks.

  Classes:   None.

  Functions: main; imports maintained flat EA definitions under private reference names.

  Origin:    Original ARDI Executor direct flat-memory 68040 regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040.h"
#include "native040-flat.h"
#include "native040-ea.h"
#include "syn68k_private.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

#define syn68k_effective_address flat_reference_ea
#define syn68k_ea_read_memory_fc flat_reference_read
#define syn68k_ea_write_memory_fc flat_reference_write
#define syn68k_ea_read_memory flat_reference_read_data
#define syn68k_ea_fetch_memory flat_reference_fetch
#define syn68k_ea_read_operand flat_reference_operand
#define syn68k_ea_write_memory flat_reference_write_data
#define syn68k_bus_read flat_reference_bus_read
#define SYN68K_EA_MAPPED(context) 0
#include "../src/runtime/ea.c"
#undef SYN68K_EA_MAPPED
#undef syn68k_bus_read
#undef syn68k_effective_address
#undef syn68k_ea_read_memory_fc
#undef syn68k_ea_write_memory_fc
#undef syn68k_ea_read_memory
#undef syn68k_ea_fetch_memory
#undef syn68k_ea_read_operand
#undef syn68k_ea_write_memory

extern int (*syn68k_bus_read)(uint32, unsigned, uint32*);
extern uint32 syn68k_direct_memory_size;
uint64 ROMlib_offset;
static unsigned char *flat_ram, *alias_bytes[2];
static void* allocations[3];
static Syn68kNative040Entry flat_entries[3];
static unsigned flat_cases, configuration, callback_mode, callback_count, callback_nested, flat_packet_mode;
static uint32_t callback_address, callback_size;

static void invoke_flat(unsigned operation, Syn68kNative040Flat* packet);

static void protected_flat(void)
{
#if defined(_WIN32)
    extern __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
    extern __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, unsigned long, unsigned long*);
    extern __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);
    unsigned char* bytes = VirtualAlloc(NULL, 8192, 0x3000, 4);
    unsigned long previous;
    assert(bytes && VirtualProtect(bytes + 4096, 4096, 1, &previous));
#else
    unsigned char* bytes = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(bytes != MAP_FAILED && !mprotect(bytes + 4096, 4096, PROT_NONE));
#endif
    memset(bytes, 0xa5, 4096);
    ROMlib_offset = (uintptr_t)bytes;
    syn68k_set_memory_window(4096, 0xe00000, 0x40000);
    syn68k_address_mask = UINT32_MAX;
    syn68k_bus_read = flat_reference_bus_read = NULL;
    for (unsigned size = 2; size <= 4; size *= 2)
        for (unsigned available = 1; available < size; ++available) {
            Syn68kEAContext reference = {0};
            assert(!flat_reference_read(&reference, 4096 - available, size, 5, 0));
            assert(reference.error == 2 && reference.fault_address == 4096);
            Syn68kNative040Flat frame = {.state = syn68k_native040_flat_state(),
                                         .address = 4096 - available,
                                         .size = size,
                                         .fc = 5,
                                         .value = 0x12345678};
            invoke_flat(0, &frame);
            assert(!frame.ok && frame.error == 2 && frame.fault_address == 4096 && frame.value == 0x12345678);
            reference = (Syn68kEAContext){0};
            flat_reference_write(&reference, 4096 - available, size, 0x12345678, 5);
            invoke_flat(1, &frame);
            assert(!frame.ok && frame.error == 2 && frame.fault_address == 4096);
            for (unsigned i = 0; i < available; ++i)
                assert(bytes[4096 - available + i] == 0xa5);
            flat_cases += 2;
        }
    for (unsigned available = 1; available < 8; ++available) {
        Syn68kNative040Flat frame = {.state = syn68k_native040_flat_state(), .address = 4096 - available, .size = 8};
        invoke_flat(2, &frame);
        assert(!frame.ok && frame.error == 2 && frame.fault_address == 4096);
        ++flat_cases;
    }
    Syn68kNative040Flat frame = {.state = syn68k_native040_flat_state(), .address = 4092, .size = 4, .fc = 6};
    invoke_flat(0, &frame);
    assert(frame.ok && !frame.error && frame.value == 0xa5a5a5a5);
    ++flat_cases;
#if defined(_WIN32)
    assert(VirtualFree(bytes, 0, 0x8000));
#else
    assert(!munmap(bytes, 8192));
#endif
}

static int flat_device(uint32_t address, unsigned size, uint32_t* value)
{
    ++callback_count;
    callback_address = address;
    callback_size = size;
    if (callback_mode == 2) {
        *value = 0xfedcba98 ^ address ^ size;
        return 1;
    }
    if (callback_mode == 3)
        syn68k_address_mask = 0xffff;
    if (callback_mode == 4)
        syn68k_direct_memory_size = 0x1000000;
    if (callback_mode == 6) {
        syn68k_set_memory_window(0x7000, 0xe00000, 0x40000);
        assert(syn68k_host_to_guest((uintptr_t)alias_bytes[1]) == 0xe00000);
    }
    if (callback_mode == 7) {
        ROMlib_offset = (uintptr_t)flat_ram + 0x1000;
        syn68k_address_mask = 0xffff;
        syn68k_direct_memory_size = 0x6000;
    }
    if (callback_mode == 8)
        syn68k_bus_read = flat_reference_bus_read = NULL;
    if (callback_mode == 5) {
        Syn68kNative040Flat child = {.state = syn68k_native040_flat_state(), .address = 4, .size = 4, .fc = 6};
        flat_entries[0](&child);
        assert(child.ok && !child.error && child.value == 0x04050607);
        ++callback_nested;
    }
    return 0;
}

static void setup_flat(void)
{
    ROMlib_offset = (uintptr_t)flat_ram;
    syn68k_set_memory_window(configuration >= 3 ? 0x1000000 : 0x7000, 0xe00000, 0x40000);
    syn68k_address_mask = configuration == 0 || configuration == 4   ? 0xffff
                          : configuration == 1 || configuration == 5 ? 0xffffff
                                                                     : UINT32_MAX;
    assert(syn68k_host_to_guest((uintptr_t)alias_bytes[0]) == 0xe00000);
    assert(syn68k_host_to_guest((uintptr_t)alias_bytes[1]) == 0xe20000);
    syn68k_bus_read = flat_reference_bus_read = callback_mode ? flat_device : NULL;
    callback_count = callback_nested = callback_address = callback_size = 0;
}

/* Exercise the packet and typed buses with no physical callbacks and an
   invalid MMU root. Flat bindings must bypass every translation/walker. */
static void invoke_flat(unsigned operation, Syn68kNative040Flat* packet)
{
    if (!flat_packet_mode) {
        flat_entries[operation](packet);
        return;
    }
    Syn68kMMU040 mmu = {.tc = 0x8000};
    Syn68kMMU040 before = mmu;
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    Syn68kMMU040Native native;
    assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
    unsigned attempted = 0;
    native.flat = packet->state;
    native.write_attempted = &attempted;
    Syn68kNative040MMU frame = {
        .memory = &memory, .address = packet->address, .value = packet->value, .size = packet->size, .fc = packet->fc};
    if (flat_packet_mode == 2 && operation < 2) {
        frame.access = operation ? native.write_rmw(&memory, frame.address, frame.fc, frame.size, frame.value)
                                 : native.read(&memory, frame.address, frame.fc, frame.size, 1, &frame.value);
    }
    else {
        const unsigned roots[] = {NATIVE040_READ, NATIVE040_WRITE, NATIVE040_SPAN};
        syn68k_native040_entry(roots[operation])(&frame);
    }
    assert(!memcmp(&before, &mmu, sizeof mmu));
    assert(attempted == (operation == 1));
    assert(frame.access.request == packet->address && frame.access.size == packet->size &&
           frame.access.fc == packet->fc);
    assert(frame.access.writing == (operation != 0));
    assert(frame.access.rmw == (flat_packet_mode == 2 && operation < 2));
    packet->error = frame.access.translation.fault == MMU040_BAD_REQUEST ? 11 : frame.access.bus_error ? 2 : 0;
    packet->ok = !packet->error;
    packet->fault_address = packet->error == 2 ? frame.access.logical : 0;
    packet->value = frame.value;
    if (packet->ok && operation < 2)
        assert(frame.access.completed == packet->size);
    else
        assert(!frame.access.completed);
}

static void compare_read(uint32_t address, unsigned size, unsigned fc)
{
    setup_flat();
    Syn68kEAContext expected = {0};
    uint32_t value = flat_reference_read(&expected, address, size, fc, 0);
    unsigned calls = callback_count, nested = callback_nested;
    uint32_t bus_address = callback_address, bus_size = callback_size;
    uint32_t mask = syn68k_address_mask, available = syn68k_direct_memory_size;
    uintptr_t offset = ROMlib_offset;
    const Syn68kNative040FlatState* state = syn68k_native040_flat_state();
    unsigned aliases = *state->alias_count;
    uintptr_t mappings[2] = {state->aliases[0], state->aliases[1]};
    int hook = syn68k_bus_read != NULL;
    setup_flat();
    Syn68kNative040Flat actual = {
        .state = syn68k_native040_flat_state(), .address = address, .size = size, .fc = fc, .value = 0x13579bdf};
    invoke_flat(0, &actual);
    if (actual.error != expected.error || actual.fault_address != expected.fault_address ||
        (actual.ok && actual.value != value))
        fprintf(stderr, "Flat read cfg=%u address=%x size=%u fc=%u callback=%u error=%u/%u value=%x/%x\n",
                configuration, address, size, fc, callback_mode, expected.error, actual.error, value, actual.value);
    assert(actual.error == expected.error && actual.ok == !expected.error);
    assert(actual.fault_address == expected.fault_address);
    assert(actual.value == (expected.error ? 0x13579bdf : value));
    assert(callback_count == calls && callback_nested == nested && callback_address == bus_address &&
           callback_size == bus_size);
    assert(mask == syn68k_address_mask && available == syn68k_direct_memory_size && offset == ROMlib_offset);
    assert(aliases == *state->alias_count && hook == (syn68k_bus_read != NULL));
    for (unsigned i = 0; i < aliases; ++i)
        assert(state->aliases[i] == mappings[i]);
    ++flat_cases;
}

static void compare_write(uint32_t address, unsigned size, unsigned fc)
{
    setup_flat();
    unsigned char *locations[16], before[16], after[16];
    unsigned count = 0;
    for (int i = -4; i < (int)size + 4; ++i) {
        uint32_t at = address + i;
        if (syn68k_memory_span(at, 1)) {
            assert(count < 16);
            locations[count] = (unsigned char*)SYN68K_TO_US(at);
            before[count] = *locations[count];
            ++count;
        }
    }
    Syn68kEAContext expected = {0};
    flat_reference_write(&expected, address, size, 0x89abcdef, fc);
    for (unsigned i = 0; i < count; ++i)
        after[i] = *locations[i];
    for (unsigned i = 0; i < count; ++i)
        *locations[i] = before[i];
    Syn68kNative040Flat actual = {
        .state = syn68k_native040_flat_state(), .address = address, .size = size, .fc = fc, .value = 0x89abcdef};
    invoke_flat(1, &actual);
    assert(actual.error == expected.error && actual.ok == !expected.error);
    assert(actual.fault_address == expected.fault_address && !callback_count);
    for (unsigned i = 0; i < count; ++i)
        assert(*locations[i] == after[i]);
    for (unsigned i = 0; i < count; ++i)
        *locations[i] = before[i];
    ++flat_cases;
}

static void compare_span(uint32_t address, unsigned size)
{
    setup_flat();
    unsigned error = size && size <= 100 ? 0 : 11;
    uint32_t fault = 0;
    if (!error)
        for (unsigned i = 0; i < size; ++i)
            if (!syn68k_memory_span(address + i, 1)) {
                error = 2;
                fault = address + i;
                break;
            }
    Syn68kNative040Flat actual = {
        .state = syn68k_native040_flat_state(), .address = address, .size = size, .value = 0x89abcdef};
    invoke_flat(2, &actual);
    assert(actual.error == error && actual.ok == !error && actual.fault_address == fault);
    assert(actual.value == 0x89abcdef && !callback_count);
    ++flat_cases;
}

static void compare_flat_ea(unsigned code, unsigned size, int writing, int registers, unsigned extension,
                            unsigned supervisor, int aliases)
{
    setup_flat();
    memset(&cpu_state, 0, sizeof cpu_state);
    cpu_state.sr = supervisor ? 0x2000 : 0;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.regs[i].ul.n = i + 4;
        cpu_state.regs[i + 8].ul.n = aliases ? 0xe1fff0 : 0x6000;
    }
    Syn68kEAContext prepare = {0};
    flat_reference_write(&prepare, 0x1000, 2, extension, 5);
    flat_reference_write(&prepare, 0x1002, 4, 0x00000010, 5);
    flat_reference_write(&prepare, 0x1006, 4, 0x00000020, 5);
    assert(!prepare.error);
    CPUState initial = cpu_state;
    Syn68kEAContext expected = {.pc = 0x1000};
    Syn68kEA result = flat_reference_ea(&expected, code, size, writing, registers);
    CPUState completed = cpu_state;
    unsigned calls = callback_count, nested = callback_nested;
    uint32_t bus_address = callback_address, bus_size = callback_size;
    uintptr_t offset = ROMlib_offset;
    uint32_t mask = syn68k_address_mask, available = syn68k_direct_memory_size;
    setup_flat();
    cpu_state = initial;
    Syn68kMMU040 mmu = {.tc = 0x8000};
    Syn68kMMU040 before = mmu;
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    Syn68kMMU040Native native;
    assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
    native.flat = syn68k_native040_flat_state();
    Syn68kNative040EA packet = {.cpu = &cpu_state,
                                .context = {.pc = 0x1000, .memory = &memory},
                                .code = code,
                                .size = size,
                                .writing = writing,
                                .registers = registers};
    syn68k_native040_entry(NATIVE040_EA)(&packet);
    if (packet.context.error != expected.error || packet.context.pc != expected.pc ||
        memcmp(&completed, &cpu_state, sizeof completed) || memcmp(&result, &packet.result, sizeof result))
        fprintf(
            stderr,
            "Flat EA cfg=%u code=%x size=%u writing=%u regs=%u extension=%x supervisor=%u alias=%u callback=%u errors=%u/%u pc=%x/%x\n",
            configuration, code, size, writing, registers, extension, supervisor, aliases, callback_mode,
            expected.error, packet.context.error, expected.pc, packet.context.pc);
    assert(!memcmp(&completed, &cpu_state, sizeof completed));
    assert(!memcmp(&result, &packet.result, sizeof result));
    assert(packet.context.pc == expected.pc && packet.context.error == expected.error &&
           packet.context.fault_address == expected.fault_address);
    assert(!memcmp(&before, &mmu, sizeof before));
    assert(calls == callback_count && nested == callback_nested && bus_address == callback_address &&
           bus_size == callback_size);
    assert(mask == syn68k_address_mask && available == syn68k_direct_memory_size && offset == ROMlib_offset);
    ++flat_cases;
}

static void mapped_span_nop(void)
{
    Syn68kMMU040 mmu = {.tc = 0x8000};
    Syn68kMMU040 before = mmu;
    Syn68kMMU040Memory memory = {.mmu = &mmu};
    for (unsigned size = 1; size <= 100; ++size) {
        Syn68kNative040MMU packet = {
            .memory = &memory, .address = UINT32_MAX, .size = size, .fc = 5, .value = 0x13579bdf};
        syn68k_native040_entry(NATIVE040_SPAN)(&packet);
        assert(packet.ok && !packet.access.bus_error && packet.access.translation.fault == MMU040_OK);
        assert(packet.value == 0x13579bdf && !packet.access.completed && !memcmp(&before, &mmu, sizeof before));
        ++flat_cases;
    }
}

static unsigned notification_calls, notification_nested;
static uint32_t notification_address;
static unsigned notification_size;
static Syn68kMMU040Memory* notification_memory;
static uint32_t notification_pages[1u << 20];

static void flat_notification(uint32_t address, unsigned size)
{
    ++notification_calls;
    notification_address = address;
    notification_size = size;
    uint32_t actual = 0;
    for (unsigned i = 0; i < size; ++i)
        actual = actual << 8 | *(unsigned char*)SYN68K_TO_US(address + i);
    uint32_t expected = 0x89abcdef & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
    assert(actual == expected); // Notification follows the complete scalar commit.
    if (notification_nested) {
        uint32_t value = 0;
        Syn68kMMU040Access access = notification_memory->native->read(notification_memory, address, 6, size, 0, &value);
        assert(!access.bus_error && access.translation.fault == MMU040_OK && value == expected);
    }
}

static void flat_notification_checks(void)
{
    callback_mode = 0;
    for (unsigned test = 0; test < 10; ++test) {
        configuration = test == 4 ? 4 : test == 5 ? 5 : test == 6 ? 2 : 3;
        setup_flat();
        Syn68kMMU040 mmu = {.tc = 0x8000};
        Syn68kMMU040Memory memory = {.mmu = &mmu};
        Syn68kMMU040Native native;
        assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
        native.flat = syn68k_native040_flat_state();
        native.flat_code_pages = notification_pages;
        native.flat_page_shift = test == 3 ? 16 : 12;
        native.flat_write = flat_notification;
        memset(notification_pages, 0, sizeof notification_pages);
        unsigned shift = native.flat_page_shift;
        uint32_t address = test == 2 || test == 3 ? (1u << shift) - 1
                           : test == 4            ? 0xffff
                           : test == 5            ? 0xffffff
                           : test == 6            ? 0x6ffe
                                                  : 0x6000;
        if (test)
            notification_pages[test == 2 || test == 3 ? 1 : test == 4 || test == 5 ? 0 : address >> shift] = 1;
        if (test == 7)
            native.flat_code_pages = NULL;
        if (test == 8)
            native.flat_write = NULL;
        if (test == 9)
            native.flat_page_shift = 32;
        notification_calls = 0;
        notification_nested = 1;
        notification_memory = &memory;
        Syn68kNative040MMU packet = {.memory = &memory, .address = address, .size = 4, .fc = 5, .value = 0x89abcdef};
        syn68k_native040_entry(NATIVE040_WRITE)(&packet);
        assert(notification_calls == (test >= 1 && test <= 5));
        if (notification_calls)
            assert(notification_address == address && notification_size == 4);
        assert(packet.access.bus_error == (test == 6));
        if (test == 6)
            assert(packet.access.logical == 0x7000 && !packet.access.completed);
        unsigned before = notification_calls;
        packet.size = 1;
        syn68k_native040_entry(NATIVE040_READ)(&packet);
        syn68k_native040_entry(NATIVE040_SPAN)(&packet);
        assert(notification_calls == before);
        ++flat_cases;
    }
    notification_memory = NULL;
}

int main(void)
{
    flat_entries[0] = syn68k_native040_entry(NATIVE040_FLAT_READ);
    flat_entries[1] = syn68k_native040_entry(NATIVE040_FLAT_WRITE);
    flat_entries[2] = syn68k_native040_entry(NATIVE040_FLAT_SPAN);
    assert(flat_entries[0] && flat_entries[1] && flat_entries[2]);
    allocations[0] = malloc(0x1000000);
    assert(allocations[0]);
    flat_ram = allocations[0];
    for (unsigned i = 0; i < 0x1000000; ++i)
        flat_ram[i] = i;
    for (unsigned n = 0; n < 2; ++n) {
        allocations[n + 1] = malloc(0x40000);
        assert(allocations[n + 1]);
        alias_bytes[n] = (unsigned char*)(((uintptr_t)allocations[n + 1] + 0x1ffff) & ~(uintptr_t)0x1ffff);
        for (unsigned i = 0; i < 0x20000; ++i)
            alias_bytes[n][i] = i ^ (n ? 0x5a : 0xa5);
    }
    const uint32_t addresses[] = {0,        1,          3,           4,         0x6ffc,   0x6ffe,   0x6fff,
                                  0x7000,   0x7001,     0xfffe,      0xffff,    0x10000,  0xdfffff, 0xe00000,
                                  0xe00001, 0xe1fffd,   0xe1ffff,    0xe20000,  0xe3ffff, 0xe40000, 0xfffffd,
                                  0xffffff, 0x12000100, 0xfffffffdu, UINT32_MAX};
    for (flat_packet_mode = 0; flat_packet_mode < 3; ++flat_packet_mode)
        for (configuration = 0; configuration < 6; ++configuration)
            for (unsigned i = 0; i < sizeof addresses / sizeof *addresses; ++i) {
                for (unsigned size = 1; size <= 4; size *= 2)
                    for (unsigned fc = 0; fc < 8; ++fc) {
                        for (callback_mode = 0; callback_mode <= 8; ++callback_mode)
                            compare_read(addresses[i], size, fc);
                        callback_mode = 0;
                        compare_write(addresses[i], size, fc);
                    }
                for (unsigned size = 0; size <= 101; ++size)
                    compare_span(addresses[i], size);
            }
    flat_packet_mode = 0;
    flat_notification_checks();
    const unsigned widths[] = {1, 2, 4, 8, 12};
    callback_mode = 0;
    for (configuration = 0; configuration < 6; ++configuration)
        for (unsigned code = 0; code < 64; ++code)
            for (unsigned n = 0; n < sizeof widths / sizeof *widths; ++n)
                for (unsigned writing = 0; writing < 2; ++writing)
                    for (unsigned registers = 0; registers < 3; ++registers)
                        for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
                            compare_flat_ea(code, widths[n], writing, registers, 0x0810, supervisor, code & 1);
    const unsigned indexed[] = {0x0121, 0x0133, 0x0108, 0x0110};
    for (configuration = 0; configuration < 6; ++configuration)
        for (callback_mode = 1; callback_mode <= 8; ++callback_mode)
            for (unsigned n = 0; n < sizeof indexed / sizeof *indexed; ++n)
                for (unsigned supervisor = 0; supervisor < 2; ++supervisor) {
                    compare_flat_ea(48, 12, 0, 1, indexed[n], supervisor, 0);
                    compare_flat_ea(48, 12, 1, 1, indexed[n], supervisor, 1);
                    compare_flat_ea(59, 12, 0, 1, indexed[n], supervisor, 1);
                }
    callback_mode = 0;
    mapped_span_nop();
    setup_flat();
    for (unsigned bad = 0; bad < 12; ++bad) {
        Syn68kNative040FlatState state = *syn68k_native040_flat_state();
        unsigned excessive = 65;
        switch (bad) {
        case 0:
            state.offset = NULL;
            break;
        case 1:
            state.mask = NULL;
            break;
        case 2:
            state.direct_size = NULL;
            break;
        case 3:
            state.alias_base = NULL;
            break;
        case 4:
            state.alias_size = NULL;
            break;
        case 5:
            state.alias_count = NULL;
            break;
        case 6:
            state.aliases = NULL;
            break;
        case 7:
            state.alias_count = &excessive;
            break;
        }
        Syn68kNative040Flat frame = {.state = bad == 8 ? NULL : &state,
                                     .address = 4,
                                     .size = bad == 9    ? 0
                                             : bad == 10 ? 3
                                             : bad == 11 ? 8
                                                         : 4,
                                     .value = 0x89abcdef};
        for (unsigned root = 0; root < 2; ++root) {
            flat_entries[root](&frame);
            assert(!frame.ok && frame.error == 11 && frame.value == 0x89abcdef);
            ++flat_cases;
        }
    }
    for (flat_packet_mode = 0; flat_packet_mode < 3; ++flat_packet_mode)
        protected_flat();
    syn68k_bus_read = flat_reference_bus_read = NULL;
    for (unsigned i = 0; i < 3; ++i)
        free(allocations[i]);
    printf("%u closed flat RAM/alias/wrap/span/device/live-mapping/reentry cases passed\n", flat_cases);
    return 0;
}
