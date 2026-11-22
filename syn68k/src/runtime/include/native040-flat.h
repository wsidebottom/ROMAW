/*+===================================================================
  File:      native040-flat.h

  Summary:   Bind live flat guest-memory mappings to closed native scalar and span operations.

  Classes:   None.

  Functions: syn68k_native040_flat_state; build-time native040_flat_read, native040_flat_write and native040_flat_span.

  Origin:    Original ARDI Executor direct flat-memory 68040 lowering.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_NATIVE040_FLAT_H
#define SYN68K_NATIVE040_FLAT_H
#include <stdint.h>

enum { SYN68K_NATIVE040_ALIAS_SHIFT = 17, SYN68K_NATIVE040_ALIAS_COUNT = 64 };

typedef struct Syn68kNative040FlatState {
    const uintptr_t* offset;
    const uint32_t *mask, *direct_size, *alias_base, *alias_size;
    const unsigned* alias_count;
    const uintptr_t* aliases;
    int (*const* bus_read)(uint32_t, unsigned, uint32_t*);
} Syn68kNative040FlatState;

/* error is 2 for a bounds fault and 11 for an invalid host request. Reads
   publish value only on success. Writes validate the complete scalar first;
   the span root accepts lengths 1..100 and accesses no guest bytes. */
typedef struct {
    const Syn68kNative040FlatState* state;
    uint32_t address, value, fault_address;
    unsigned size, fc, error, ok;
} Syn68kNative040Flat;

/* Mapping owners retain all referenced objects. Device callbacks may change
   their values; a later scalar observes the current mapping and bus hook. */
const Syn68kNative040FlatState* syn68k_native040_flat_state(void);
void native040_flat_read(Syn68kNative040Flat* frame);
void native040_flat_write(Syn68kNative040Flat* frame);
void native040_flat_span(Syn68kNative040Flat* frame);
#endif
