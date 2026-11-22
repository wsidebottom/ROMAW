/*+===================================================================
  File:      flat-memory.c

  Summary:   Maintain flat guest RAM windows and host aliases shared by checked access and closed native roots.

  Classes:   None.

  Functions: syn68k_memory_span, syn68k_set_memory_window, syn68k_guest_to_host, syn68k_lookup_guest_address,
             syn68k_host_to_guest, syn68k_native040_flat_state.

  Origin:    Existing ARDI Executor flat mapping implementation extracted from syn68k/src/runtime/init.c.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "syn68k_public.h"
#include "native040-flat.h"
#include <stdio.h>
#include <stdlib.h>

uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
uint32 syn68k_direct_memory_size;
static ptr_sized_uint host_alias[64];
_Static_assert(sizeof host_alias / sizeof *host_alias == SYN68K_NATIVE040_ALIAS_COUNT, "native flat alias table");

extern int (*syn68k_bus_read)(uint32, unsigned, uint32*);

const Syn68kNative040FlatState* syn68k_native040_flat_state(void)
{
    static const Syn68kNative040FlatState state = {.offset = &ROMlib_offset,
                                                   .mask = &syn68k_address_mask,
                                                   .direct_size = &syn68k_direct_memory_size,
                                                   .alias_base = &syn68k_host_alias_base,
                                                   .alias_size = &syn68k_host_alias_size,
                                                   .alias_count = &syn68k_host_alias_count,
                                                   .aliases = host_alias,
                                                   .bus_read = &syn68k_bus_read};
    return &state;
}

#define HOST_ALIAS_SHIFT 17
#define HOST_ALIAS_BYTES (1UL << HOST_ALIAS_SHIFT)
_Static_assert(HOST_ALIAS_SHIFT == SYN68K_NATIVE040_ALIAS_SHIFT, "native flat alias granularity");

int syn68k_memory_span(syn68k_addr_t address, uint32 length)
{
    address &= syn68k_address_mask;
    if ((uint64)address + length <= syn68k_direct_memory_size)
        return 1;
    uint32 relative = address - syn68k_host_alias_base;
    return relative < syn68k_host_alias_count * (uint64)HOST_ALIAS_BYTES &&
           (uint64)relative + length <= syn68k_host_alias_count * (uint64)HOST_ALIAS_BYTES;
}

void syn68k_set_memory_window(uint32 size, uint32 base, uint32 alias_size)
{
    int preserve_aliases = 0;
#if SIZEOF_CHAR_P == 8
    static uint64 automatic_alias_offset;
    static int automatic_alias_window;
    /* Host callbacks and caller-owned vectors can be outside the guest's
     four-gigabyte address space. Reserve aliases even in 32-bit guest mode. */
    if (!alias_size && size <= 0xFF000000U) {
        base = 0xFF000000U;
        alias_size = 8 * 1024 * 1024;
        preserve_aliases = automatic_alias_window && automatic_alias_offset == ROMlib_offset;
        automatic_alias_window = 1;
        automatic_alias_offset = ROMlib_offset;
    }
    else
        automatic_alias_window = 0;
#endif
    syn68k_direct_memory_size = size;
    syn68k_host_alias_base = base;
    syn68k_host_alias_size = alias_size;
    if (!preserve_aliases)
        syn68k_host_alias_count = 0;
}

void* syn68k_guest_to_host(uint32 address)
{
    uint32 relative = address - syn68k_host_alias_base;
    unsigned index = relative >> HOST_ALIAS_SHIFT;
    if (index < syn68k_host_alias_count)
        return (void*)(host_alias[index] + (relative & (HOST_ALIAS_BYTES - 1)));
    return SYN68K_TO_US_RAW(address);
}

int syn68k_lookup_guest_address(ptr_sized_uint address, uint32* guest)
{
    ptr_sized_uint base = address & ~(ptr_sized_uint)(HOST_ALIAS_BYTES - 1);
    unsigned i;
    if (!address) {
        *guest = 0;
        return 1;
    }
    if (address >= ROMlib_offset && address - ROMlib_offset < syn68k_direct_memory_size) {
        *guest = US_TO_SYN68K_RAW(address);
        return 1;
    }
    for (i = 0; i < syn68k_host_alias_count; ++i)
        if (host_alias[i] == base) {
            *guest = syn68k_host_alias_base + i * HOST_ALIAS_BYTES + address - base;
            return 1;
        }
    if (!syn68k_host_alias_size && syn68k_address_mask == 0xFFFFFFFFUL &&
        (ptr_sized_uint)(uint32)(address - ROMlib_offset) == address - ROMlib_offset) {
        *guest = US_TO_SYN68K_RAW(address);
        return 1;
    }
    return 0;
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    ptr_sized_uint base = address & ~(ptr_sized_uint)(HOST_ALIAS_BYTES - 1);
    uint32 guest;
    unsigned i;

    if (syn68k_lookup_guest_address(address, &guest))
        return guest;
    i = syn68k_host_alias_count;
    if (syn68k_host_alias_count >= sizeof(host_alias) / sizeof(host_alias[0]) ||
        (syn68k_host_alias_count + 1) * HOST_ALIAS_BYTES > syn68k_host_alias_size) {
        fprintf(stderr, "syn68k: exhausted 24-bit host pointer mappings\n");
        abort();
    }
    host_alias[syn68k_host_alias_count++] = base;
    return syn68k_host_alias_base + i * HOST_ALIAS_BYTES + address - base;
}
