/*+===================================================================
  File:      mmu040-memory.c

  Summary:   Implements mmu040 memory support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_mmu040_access_ssw, syn68k_mmu040_read, syn68k_mmu040_write, syn68k_mmu040_write_rmw

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Logical operand access over the separate physical operand/descriptor buses. */
#include "mmu040-memory.h"
#if defined(SYN68K_NATIVE040_TEMPLATE)
#include "native040-flat.h"
#endif

static Syn68kMMU040Access transfer(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                   int writing, int rmw, uint32_t* value)
{
    Syn68kMMU040Access result = {0};
    result.logical = logical;
    result.request = logical;
    result.size = size;
    result.fc = fc;
    result.writing = !!writing;
    result.rmw = !!rmw;
    result.translation.fault = MMU040_BAD_REQUEST;
    if (!memory || !memory->mmu || !value || memory->aligned_transfers > 1 || fc > 7 ||
        (size != 1 && size != 2 && size != 4))
        return result;
#if defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory->native && memory->native->flat) {
        Syn68kNative040Flat flat = {
            .state = memory->native->flat, .address = logical, .size = size, .fc = fc, .value = writing ? *value : 0};
        if (writing)
            native040_flat_write(&flat);
        else
            native040_flat_read(&flat);
        if (flat.error == 11)
            return result;
        result.translation.fault = MMU040_OK;
        if (writing)
            result.value = *value & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
        if (!flat.ok) {
            result.logical = flat.fault_address;
            result.bus_error = 1;
            return result;
        }
        result.completed = size;
        if (!writing)
            *value = result.value = flat.value;
        else if (memory->native->flat_code_pages && memory->native->flat_write &&
                 memory->native->flat_page_shift < 32) {
            uint32_t mask = *memory->native->flat->mask;
            for (unsigned i = 0; i < size; ++i)
                if (memory->native->flat_code_pages[((logical + i) & mask) >> memory->native->flat_page_shift]) {
                    memory->native->flat_write(logical, size);
                    break;
                }
        }
        return result;
    }
#endif
    if (writing ? !memory->write : !memory->read)
        return result;

    unsigned char bytes[4];
    if (writing) {
        result.value = *value & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = result.value >> ((size - i - 1) * 8);
    }
    while (result.completed < size) {
        result.logical = logical + result.completed;
        result.translation = syn68k_mmu040_translate(memory->mmu, &memory->tables, result.logical, fc, writing || rmw);
        if (result.translation.fault != MMU040_OK)
            return result;
        /* TTR boundaries are coarser than either page size. This also splits
         the 32-bit address wrap when translation is disabled or bypassed. */
        unsigned mask = memory->mmu->tc & 0x4000 ? 8191 : 4095;
        unsigned chunk = mask + 1 - (result.logical & mask);
        if (chunk > size - result.completed)
            chunk = size - result.completed;
        /* The uncached data memory unit issues only naturally aligned bus
           transfers (MC68040UM 7.3), including byte/word/byte for odd longs.
           Cache/device operand bindings may instead own this decomposition. */
        if (memory->aligned_transfers) {
            unsigned aligned = (result.translation.physical & 1) || chunk < 2   ? 1
                               : (result.translation.physical & 2) || chunk < 4 ? 2
                                                                                : 4;
            if (chunk > aligned)
                chunk = aligned;
        }
        int ok = writing ? memory->write(memory->opaque, result.translation.physical, bytes + result.completed, chunk,
                                         result.translation.attributes, fc)
                         : memory->read(memory->opaque, result.translation.physical, bytes + result.completed, chunk,
                                        result.translation.attributes, fc);
        if (!ok) {
            result.bus_error = 1;
            return result;
        }
        if (!writing)
            for (unsigned i = result.completed; i < result.completed + chunk; ++i)
                result.value = (result.value << 8) | bytes[i];
        result.completed += chunk;
    }
    if (!writing)
        *value = result.value;
    return result;
}

Syn68kMMU040Access syn68k_mmu040_read(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size, int rmw,
                                      uint32_t* value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->read)
        return memory->native->read(memory, logical, fc, size, rmw, value);
#endif
    return transfer(memory, logical, fc, size, 0, rmw, value);
}

Syn68kMMU040Access syn68k_mmu040_write(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                       uint32_t value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->write)
        return memory->native->write(memory, logical, fc, size, value);
#endif
    return transfer(memory, logical, fc, size, 1, 0, &value);
}

Syn68kMMU040Access syn68k_mmu040_write_rmw(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                           uint32_t value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->write_rmw)
        return memory->native->write_rmw(memory, logical, fc, size, value);
#endif
    return transfer(memory, logical, fc, size, 1, 1, &value);
}

int syn68k_mmu040_access_ssw(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    if (!access || !ssw || access->fc > 7 || (access->size != 1 && access->size != 2 && access->size != 4) ||
        access->completed >= access->size)
        return 0;
    int atc;
    switch (access->translation.fault) {
    case MMU040_OK:
        if (!access->bus_error)
            return 0;
        atc = 0;
        break;
    case MMU040_INVALID:
    case MMU040_TABLE_BUS_ERROR:
    case MMU040_WRITE_PROTECTED:
    case MMU040_SUPERVISOR_ONLY:
        atc = 1;
        break;
    default:
        return 0;
    }
    /* MC68040UM table 3-2: alternate spaces bypass translation and use
       TT=2. The integer MOVES decoder already converts FC2/6 to FC1/5. */
    unsigned alternate = (access->fc & 3) == 0 || (access->fc & 3) == 3;
    if (alternate && atc)
        return 0;
    *ssw = access->fc | (alternate ? 0x10 : 0) |
           (access->size == 1   ? 0x20
            : access->size == 2 ? 0x40
                                : 0) |
           (!access->writing ? 0x100 : 0) | (access->rmw ? 0x200 : 0) | (atc ? 0x400 : 0) |
           (atc && access->completed ? 0x800 : 0);
    return 1;
}
