/*+===================================================================
  File:      native040-flat.c

  Summary:   Perform closed native flat-memory scalar access and bounded span checks with live guest aliases.

  Classes:   None.

  Functions: Build-time native040_flat_read, native040_flat_write and native040_flat_span roots.

  Origin:    Original ARDI Executor direct 68040 lowering using the maintained flat EA memory rules.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-flat.h"

#define ALIAS_SHIFT SYN68K_NATIVE040_ALIAS_SHIFT
#define ALIAS_BYTES (UINT32_C(1) << ALIAS_SHIFT)

static int valid(Syn68kNative040Flat* frame)
{
    frame->ok = 0;
    frame->error = 11;
    frame->fault_address = 0;
    const Syn68kNative040FlatState* state = frame->state;
    return state && state->offset && state->mask && state->direct_size && state->alias_base && state->alias_size &&
           state->alias_count && state->aliases && *state->alias_count <= SYN68K_NATIVE040_ALIAS_COUNT;
}

static int mapped_span(const Syn68kNative040FlatState* state, uint32_t address, unsigned size)
{
    address &= *state->mask;
    if ((uint64_t)address + size <= *state->direct_size)
        return 1;
    uint32_t relative = address - *state->alias_base;
    uint64_t available = (uint64_t)*state->alias_count * ALIAS_BYTES;
    return relative < available && (uint64_t)relative + size <= available;
}

static int span(Syn68kNative040Flat* frame)
{
    for (unsigned i = 0; i < frame->size; ++i)
        if (!mapped_span(frame->state, frame->address + i, 1)) {
            frame->error = 2;
            frame->fault_address = frame->address + i;
            return 0;
        }
    frame->error = 0;
    return 1;
}

static unsigned char* contiguous(const Syn68kNative040FlatState* state, uint32_t address, unsigned size)
{
    address &= *state->mask;
    if ((uint64_t)address + size > (uint64_t)*state->mask + 1 ||
        (*state->alias_count && address < (uint64_t)*state->alias_base + *state->alias_size &&
         (uint64_t)address + size > *state->alias_base) ||
        !mapped_span(state, address, size))
        return 0;
    return (unsigned char*)(uintptr_t)(*state->offset + address);
}

static unsigned char* byte_address(const Syn68kNative040FlatState* state, uint32_t address)
{
    address &= *state->mask;
    uint32_t relative = address - *state->alias_base;
    if (*state->alias_count && relative < *state->alias_size) {
        unsigned index = relative >> ALIAS_SHIFT;
        if (index < *state->alias_count)
            return (unsigned char*)(state->aliases[index] + (relative & (ALIAS_BYTES - 1)));
    }
    return (unsigned char*)(uintptr_t)(*state->offset + address);
}

__attribute__((used, noinline)) void native040_flat_read(Syn68kNative040Flat* frame)
{
    if (!valid(frame) || (frame->size != 1 && frame->size != 2 && frame->size != 4))
        return;
    const Syn68kNative040FlatState* state = frame->state;
    const unsigned char* bytes = contiguous(state, frame->address, frame->size);
    uint32_t value = 0;
    if (bytes) {
        for (unsigned i = 0; i < frame->size; ++i)
            value = value << 8 | bytes[i];
    }
    else {
        uint32_t address = frame->address & *state->mask;
        if (state->bus_read && *state->bus_read && (frame->fc == 1 || frame->fc == 5) &&
            (uint64_t)address + frame->size <= (uint64_t)*state->mask + 1 &&
            (*state->bus_read)(address, frame->size, &value)) {
            frame->value = value;
            frame->ok = 1;
            frame->error = 0;
            return;
        }
        if (!span(frame))
            return;
        for (unsigned i = 0; i < frame->size; ++i)
            value = value << 8 | *byte_address(state, frame->address + i);
    }
    frame->value = value;
    frame->ok = 1;
    frame->error = 0;
}

__attribute__((used, noinline)) void native040_flat_write(Syn68kNative040Flat* frame)
{
    if (!valid(frame) || (frame->size != 1 && frame->size != 2 && frame->size != 4))
        return;
    unsigned char* bytes = contiguous(frame->state, frame->address, frame->size);
    if (bytes) {
        for (unsigned i = 0; i < frame->size; ++i)
            bytes[i] = frame->value >> ((frame->size - i - 1) * 8);
    }
    else {
        if (!span(frame))
            return;
        for (unsigned i = 0; i < frame->size; ++i)
            *byte_address(frame->state, frame->address + i) = frame->value >> ((frame->size - i - 1) * 8);
    }
    frame->ok = 1;
    frame->error = 0;
}

__attribute__((used, noinline)) void native040_flat_span(Syn68kNative040Flat* frame)
{
    if (!valid(frame) || !frame->size || frame->size > 100)
        return;
    frame->ok = span(frame);
}
