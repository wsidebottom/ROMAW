/*+===================================================================
  File:      classic_access_test.c

  Summary:   Exercise all 185 entry bodies against isolated guest memory. Changing real live zone/heap globals to
             malformed values would invalidate the emulator; the PPC probes separately check their live-state ABI
             without changing it.

  Classes:   None.

  Functions: ROMlib_carbon_classic_access_tests, ROMlib_carbon_guest_memory, ROMlib_carbon_try_guest_memory

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise all 185 entry bodies against isolated guest memory. Changing real
   live zone/heap globals to malformed values would invalidate the emulator;
   the PPC probes separately check their live-state ABI without changing it. */
#include "../src/internal.h"
#include "cf_values_test.h"
#include "classic-access-cases.inc"
#include <stddef.h>
#undef NDEBUG
#include <assert.h>

_Static_assert(offsetof(ListRec, rView) == 0, "classic list view");
_Static_assert(offsetof(ListRec, cellSize) == 16, "classic list cell size");
_Static_assert(offsetof(ListRec, clikTime) == 40, "classic list click time");
_Static_assert(offsetof(ListRec, lastClick) == 56, "classic list last click");
_Static_assert(offsetof(ListRec, dataBounds) == 72, "classic list data bounds");
_Static_assert(offsetof(ControlRecord, contrlRect) == 8, "classic control rectangle");
_Static_assert(offsetof(ControlRecord, contrlHilite) == 17, "classic control hilite");
_Static_assert(offsetof(MenuInfo, menuData) == 14, "classic menu title");
_Static_assert(offsetof(DialogRecord, editField) == 164, "classic dialog focus");

static Byte memory[0x4000], expected[0x4000];
static int memory_error;
typedef uint32 (*get_zero)(void);
typedef uint32 (*get_one)(uint32);
typedef uint32 (*get_two)(uint32, uint32);
typedef void (*set_one)(uint32);
typedef void (*set_two)(uint32, uint32);

static void* guest_memory(uint32_t address, size_t length, int write_access, int required)
{
    void* result;
    int values_status = ROMlib_carbon_values_test_memory(address, length, &result);
    if (values_status >= 0)
        return result;
    (void)write_access;
    if (memory_error)
        return NULL;
    if (ROMlib_offset != (uintptr_t)memory || length > sizeof memory || address > sizeof memory - length) {
        memory_error = required;
        return NULL;
    }
    return memory + address;
}

void* ROMlib_carbon_guest_memory(uint32_t address, size_t length, int write_access)
{
    return guest_memory(address, length, write_access, 1);
}

void* ROMlib_carbon_try_guest_memory(uint32_t address, size_t length, int write_access)
{
    return guest_memory(address, length, write_access, 0);
}

static uint32 load(const Byte* bytes, unsigned address, unsigned width, unsigned sign)
{
    uint32 value = 0;
    for (unsigned i = 0; i < width; ++i)
        value = (value << 8) | bytes[address + i];
    if (sign && width < 4 && (bytes[address] & 0x80))
        value |= UINT32_MAX << (width * 8);
    return value;
}

static void store(Byte* bytes, unsigned address, unsigned width, uint32 value)
{
    for (unsigned i = 0; i < width; ++i)
        bytes[address + i] = value >> (8 * (width - 1 - i));
}

static void seed(void)
{
    memory_error = 0;
    for (unsigned i = 0; i < sizeof memory; ++i)
        memory[i] = i * 37 + 11;
}

static void begin(void)
{
    memcpy(expected, memory, sizeof memory);
}

static void check(void)
{
    assert(!memory_error);
    assert(!memcmp(memory, expected, sizeof memory));
}

static void ordered_copy(unsigned source, unsigned destination, unsigned width, unsigned chunk)
{
    for (unsigned i = 0; i < width; i += chunk) {
        Byte buffer[4];
        unsigned count = width - i < chunk ? width - i : chunk;
        memcpy(buffer, expected + source + i, count);
        memcpy(expected + destination + i, buffer, count);
    }
}

static void lowmem_case(const classic_case* c, void* function, uint32 value)
{
    unsigned address = c->address;
    seed();
    if (c->kind <= lowmem_lwz) {
        store(memory, address, c->width, value);
        begin();
        assert(((get_zero)function)() == load(expected, address, c->width, c->sign));
        check();
    }
    else if (c->kind <= lowmem_stw) {
        begin();
        store(expected, address, c->width, value);
        ((set_one)function)(value);
        check();
    }
    else if (c->kind == lowmem_address) {
        begin();
        assert(((get_zero)function)() == address);
        check();
    }
    else if (c->kind == lowmem_copy_in || c->kind == lowmem_rgb_in || c->kind == lowmem_rgb_out) {
        unsigned positions[] = {0, 0x2000, address - 3, address, address + 1, address + 3};
        for (unsigned i = 0; i < NELEM(positions); ++i) {
            seed();
            unsigned source = c->kind == lowmem_rgb_out ? address : positions[i];
            unsigned destination = c->kind == lowmem_rgb_out ? positions[i] : address;
            begin();
            if (c->kind == lowmem_copy_in)
                memmove(expected + destination, expected + source, c->width);
            else
                ordered_copy(source, destination, 6, 4);
            if (c->kind == lowmem_rgb_out)
                assert(((get_one)function)(destination) == destination);
            else
                ((set_one)function)(source);
            check();
        }
    }
    else {
        static const uint32 indices[] = {0, 1, 7, UINT32_MAX, 0xfffffffe, 0x10001};
        for (unsigned i = 0; i < NELEM(indices); ++i) {
            seed();
            unsigned slot = address + 4 * (uint32)(int32)(int16)indices[i];
            store(memory, slot, 4, value);
            begin();
            if (c->kind == lowmem_index_get)
                assert(((get_one)function)(indices[i]) == value);
            else {
                store(expected, slot, 4, ~value);
                ((set_two)function)(~value, indices[i]);
            }
            check();
        }
    }
}

static void record_case(const classic_case* c, void* function, uint32 value)
{
    /* Different master pointer targets prove that an accessor follows the live
       handle, including a non-NULL handle whose target is guest address zero. */
    unsigned records[] = {0, 0x300, 0x700};
    for (unsigned j = 0; j < NELEM(records); ++j) {
        unsigned handle = 0x100, record = records[j], address = record + c->address;
        seed();
        store(memory, handle, 4, record);
        if (c->kind == record_popup_get || c->kind == record_popup_set) {
            store(memory, record + 28, 4, 0x1000);
            store(memory, 0x1000, 4, 0x1200);
            address = 0x1200 + c->address;
        }
        if (c->kind == record_direct_get || c->kind == record_focus)
            handle = record;
        if (c->kind == record_get || c->kind == record_direct_get || c->kind == record_popup_get ||
            c->kind == record_nonzero || c->kind == record_focus) {
            store(memory, address, c->width, value);
            begin();
            uint32 answer = load(expected, address, c->width, c->sign);
            if (c->kind == record_nonzero)
                answer = answer != 0;
            if (c->kind == record_focus)
                answer = !handle || (int16)value < 0 ? UINT32_MAX : (uint32)(int32)(int16)(value + 1);
            assert(((get_one)function)(handle) == answer);
            check();
        }
        else if (c->kind == record_set || c->kind == record_popup_set) {
            begin();
            store(expected, address, c->width, value);
            ((set_two)function)(handle, value);
            check();
        }
        else if (c->kind == record_copy_in || c->kind == record_copy_out) {
            unsigned positions[] = {0, 0x2000, address + 1, address - (address != 0), handle};
            for (unsigned k = 0; k < NELEM(positions); ++k) {
                seed();
                store(memory, handle, 4, record);
                unsigned source = c->kind == record_copy_in ? positions[k] : address;
                unsigned destination = c->kind == record_copy_in ? address : positions[k];
                begin();
                ordered_copy(source, destination, c->width, 4);
                if (c->kind == record_copy_out)
                    assert(((get_two)function)(handle, destination) == destination);
                else
                    ((set_two)function)(handle, source);
                check();
            }
        }
        else if (c->kind == record_identity) {
            begin();
            assert(((get_one)function)(record) == record);
            check();
        }
        else if (c->kind == record_title) {
            unsigned lengths[] = {0, 1, 5, 255};
            for (unsigned k = 0; k < NELEM(lengths); ++k) {
                seed();
                store(memory, handle, 4, record);
                memory[address] = lengths[k];
                memory[address + 1] = 1;
                begin();
                memmove(expected + 0x2000, expected + address, lengths[k] + 1);
                if (load(expected, 0x2000, 2, 0) == 0x0501)
                    expected[0x2000] = 0;
                assert(((get_two)function)(handle, 0x2000) == 0x2000);
                check();
            }
            begin();
            assert(!((get_two)function)(handle, 0));
            check();
            assert(((get_two)function)(0, 0x2000) == 0x2000);
            check();
        }
        if (c->guard) {
            seed();
            begin();
            if (c->kind == record_set || c->kind == record_copy_in || c->kind == record_popup_set)
                ((set_two)function)(0, value);
            else
                assert(((get_one)function)(0) == (c->kind == record_focus ? UINT32_MAX : 0));
            check();
        }
        if (c->kind == record_popup_get || c->kind == record_popup_set) {
            seed();
            store(memory, handle, 4, record);
            store(memory, record + 28, 4, 0);
            begin();
            if (c->kind == record_popup_get)
                assert(!((get_one)function)(handle));
            else
                ((set_two)function)(handle, value);
            check();
        }
    }
}

void ROMlib_carbon_classic_access_tests(void)
{
    uint64 saved_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)memory;
    static const uint32 values[] = {0,      1,      0x7f,       0x80,       0xff,       0x7fff,
                                    0x8000, 0xffff, 0x7fffffff, 0x80000000, UINT32_MAX, 0x8123f4a5};
    assert(NELEM(classic_cases) == 185);
    for (unsigned i = 0; i < NELEM(classic_cases); ++i) {
        const classic_case* c = &classic_cases[i];
        const ROMlib_carbon_export* entry = ROMlib_carbon_find_export(c->name, 2);
        assert(entry && entry->function && entry->reference_offset == c->code && !entry->reference_library &&
               entry->flags == ROMLIB_CARBON_PPC_ONLY);
        for (unsigned k = 0; k < NELEM(values); ++k) {
            if (c->kind <= lowmem_index_set)
                lowmem_case(c, entry->function, values[k]);
            else
                record_case(c, entry->function, values[k]);
        }
    }
    /* Accessors must share ROMAW's established low-memory storage. */
    ROMlib_carbon_LMSetScrVRes(0x8123);
    assert((int16)CW(ScrVRes) == (int16)0x8123);
    ROMlib_carbon_LMSetMemTop(0x81234567);
    uint32 raw;
    memcpy(&raw, &MemTop_H, sizeof raw);
    assert(CL(raw) == 0x81234567);
    seed();
    begin();
    assert(!ROMlib_carbon_GetListPort(UINT32_MAX));
    assert(memory_error && !memcmp(memory, expected, sizeof memory));
    seed();
    begin();
    assert(!ROMlib_carbon_LMGetLvl2DT(0x8000));
    assert(memory_error && !memcmp(memory, expected, sizeof memory));
    seed();
    begin();
    ROMlib_carbon_LMSetCurApName(sizeof memory - 16);
    assert(memory_error && !memcmp(memory, expected, sizeof memory));
    seed();
    store(memory, 0x100, 4, sizeof memory - 4);
    begin();
    ROMlib_carbon_GetListPort(0x100);
    assert(memory_error && !memcmp(memory, expected, sizeof memory));
    seed();
    store(memory, 0x100, 4, 0x300);
    begin();
    ROMlib_carbon_GetListViewBounds(0x100, sizeof memory - 4);
    ordered_copy(0x300, sizeof memory - 4, 4, 4);
    assert(memory_error && !memcmp(memory, expected, sizeof memory));
    memory_error = 0;
    ROMlib_offset = saved_offset;
}
