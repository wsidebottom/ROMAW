/*+===================================================================
  File:      guest_test_memory.h

  Summary:   Small address-translation fixture for standalone tests, without a CPU runtime. Keep native heap/stack
             pointers full-width and assign guest aliases when they are outside the fixture's linear memory window.
             ifndef ROMAW_GUEST_TEST_MEMORY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Small address-translation fixture for standalone tests, without a CPU runtime.
   Keep native heap/stack pointers full-width and assign guest aliases when they
   are outside the fixture's linear memory window. */
#ifndef ROMAW_GUEST_TEST_MEMORY_H
#define ROMAW_GUEST_TEST_MEMORY_H

#include <assert.h>

_Static_assert(sizeof(uintptr_t) == 8, "These fixtures exercise the Windows x64 pointer layout");

static uintptr_t fixture_aliases[256];
static unsigned fixture_alias_count;

static uint32 fixture_host_to_guest(uintptr_t pointer)
{
    if (!pointer)
        return 0;
    if (pointer >= ROMlib_offset && pointer - ROMlib_offset < 0xd0000000)
        return pointer - ROMlib_offset;
    uintptr_t page = pointer & ~(uintptr_t)0xfffff;
    unsigned index;
    for (index = 0; index < fixture_alias_count; ++index)
        if (fixture_aliases[index] == page)
            break;
    if (index == fixture_alias_count) {
        assert(fixture_alias_count < 256);
        fixture_aliases[fixture_alias_count++] = page;
    }
    return 0xe0000000 + (index << 20) + (pointer & 0xfffff);
}

static void* fixture_guest_to_host(uint32 address)
{
    if (address >= 0xe0000000 && address < 0xf0000000) {
        unsigned index = (address - 0xe0000000) >> 20;
        assert(index < fixture_alias_count);
        return (void*)(fixture_aliases[index] + (address & 0xfffff));
    }
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

#endif
