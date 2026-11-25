/*+===================================================================
  File:      mmu040.c

  Summary:   Implements mmu040 support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_mmu040_control, syn68k_mmu040_flush, syn68k_mmu040_init, syn68k_mmu040_ptest, syn68k_mmu040_reset,
             syn68k_mmu040_translate

  Origin:    Original MC68040 translation engine, based on MC68040UM chapter 3.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Original MC68040 translation engine, based on MC68040UM chapter 3.
   This unit never dereferences a guest address; all descriptor traffic goes
   through the supplied physical bus. It does not deliver CPU exceptions. */
#include "mmu040.h"
#include <string.h>

enum {
    ENABLE = 0x8000,
    PAGE8K = 0x4000,
    RESIDENT = 1,
    TRANSPARENT = 2,
    WRITE_PROTECT = 4,
    USED = 8,
    MODIFIED = 16,
    SUPERVISOR = 128,
    GLOBAL = 1024,
    BUS_ERROR = 2048
};

void syn68k_mmu040_init(Syn68kMMU040* mmu)
{
    memset(mmu, 0, sizeof *mmu);
}

void syn68k_mmu040_reset(Syn68kMMU040* mmu)
{
    mmu->tc &= ~ENABLE;
    for (unsigned i = 0; i < 2; ++i) {
        mmu->itt[i] &= ~ENABLE;
        mmu->dtt[i] &= ~ENABLE;
    }
}

int syn68k_mmu040_control(Syn68kMMU040* mmu, enum Syn68kMMU040Control control, int writing, uint32_t* value)
{
    uint32_t *target, mask;
    if (!value)
        return 0;
    switch (control) {
    case MMU040_TC:
        target = &mmu->tc;
        mask = 0xc000;
        break;
    case MMU040_URP:
    case MMU040_SRP:
        target = control == MMU040_URP ? &mmu->urp : &mmu->srp;
        mask = 0xfffffe00u;
        break;
    case MMU040_ITT0:
    case MMU040_ITT1:
        target = &mmu->itt[control - MMU040_ITT0];
        mask = 0xffffe364u;
        break;
    case MMU040_DTT0:
    case MMU040_DTT1:
        target = &mmu->dtt[control - MMU040_DTT0];
        mask = 0xffffe364u;
        break;
    case MMU040_MMUSR:
        target = &mmu->mmusr;
        mask = 0xfffffff7u;
        break;
    default:
        return 0;
    }
    if (writing)
        *target = *value & mask;
    else
        *value = *target & mask;
    return 1;
}

static int ordinary_fc(unsigned fc)
{
    return fc == 1 || fc == 2 || fc == 5 || fc == 6;
}

static unsigned page_shift(const Syn68kMMU040* mmu)
{
    return mmu->tc & PAGE8K ? 13 : 12;
}

static enum Syn68kMMU040Fault permission(uint32_t status, int supervisor, int writing)
{
    if (!supervisor && (status & SUPERVISOR))
        return MMU040_SUPERVISOR_ONLY;
    if (writing && (status & WRITE_PROTECT))
        return MMU040_WRITE_PROTECTED;
    return MMU040_OK;
}

static int transparent(uint32_t ttr, uint32_t logical, int supervisor)
{
    if (!(ttr & ENABLE))
        return 0;
    if (!(ttr & 0x4000) && !!(ttr & 0x2000) != supervisor)
        return 0;
    return !(((logical ^ ttr) >> 24) & ~((ttr >> 16) & 255));
}

/* Figure 3-10 pipelines a table descriptor's U write after reading its
   successor. Attempt that pending write even if the successor read failed. */
static int fetch(const Syn68kMMU040Bus* bus, uint32_t address, uint32_t* descriptor, uint32_t pending_address,
                 uint32_t pending_value, int pending, Syn68kMMU040Result* result)
{
    int read_ok = bus && bus->read32 ? bus->read32(bus->opaque, address, descriptor) : 0;
    int write_ok = read_ok < 0            ? -1
                   : !pending             ? 1
                   : bus && bus->update32 ? bus->update32(bus->opaque, pending_address, pending_value | USED, 0)
                                          : 0;
    if (read_ok <= 0 || write_ok <= 0) {
        int physical = read_ok < 0 || write_ok < 0;
        result->fault = physical ? MMU040_PHYSICAL_ABORT : MMU040_TABLE_BUS_ERROR;
        result->status = physical ? 0 : BUS_ERROR;
        result->descriptor_address = read_ok < 0    ? address
                                     : write_ok < 0 ? pending_address
                                     : read_ok      ? pending_address
                                                    : address;
        return 0;
    }
    result->descriptor_address = address;
    return 1;
}

static Syn68kMMU040Result walk(const Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical, int supervisor,
                               int writing)
{
    Syn68kMMU040Result result = {0};
    result.fault = MMU040_INVALID;
    uint32_t descriptor = 0, previous = 0, previous_address = 0, wp = 0;
    uint32_t address = (supervisor ? mmu->srp : mmu->urp) & 0xfffffe00u;
    address |= (logical >> 25) * 4;
    for (unsigned level = 0; level < 3; ++level) {
        if (!fetch(bus, address, &descriptor, previous_address, previous, level != 0 && !(previous & USED), &result))
            return result;
        if (level == 2)
            break;
        if (!(descriptor & 2))
            return result;
        wp |= descriptor & WRITE_PROTECT;
        previous = descriptor;
        previous_address = address;
        if (level == 0)
            address = (descriptor & 0xfffffe00u) | (((logical >> 18) & 127) * 4);
        else if (mmu->tc & PAGE8K)
            address = (descriptor & 0xffffff80u) | (((logical >> 13) & 31) * 4);
        else
            address = (descriptor & 0xffffff00u) | (((logical >> 12) & 63) * 4);
    }
    if ((descriptor & 3) == 2) {
        address = descriptor & 0xfffffffcu;
        if (!fetch(bus, address, &descriptor, 0, 0, 0, &result))
            return result;
    }
    if (!(descriptor & 1))
        return result;
    wp |= descriptor & WRITE_PROTECT;
    enum Syn68kMMU040Fault fault = permission(descriptor | wp, supervisor, writing);
    uint32_t bits = USED;
    if (writing && fault == MMU040_OK)
        bits |= MODIFIED;
    if ((descriptor & bits) != bits) {
        /* A first permitted write with M clear uses a descriptor write; other
         U-only transitions use locked RMW, preserving unrelated bits. */
        int locked = !(bits & MODIFIED) || (descriptor & MODIFIED);
        uint32_t value = locked ? USED : descriptor | bits;
        int updated = bus && bus->update32 ? bus->update32(bus->opaque, address, value, locked) : 0;
        if (updated <= 0) {
            result.fault = updated < 0 ? MMU040_PHYSICAL_ABORT : MMU040_TABLE_BUS_ERROR;
            result.status = updated < 0 ? 0 : BUS_ERROR;
            return result;
        }
        descriptor |= bits;
    }
    uint32_t mask = (mmu->tc & PAGE8K) ? 0xffffe000u : 0xfffff000u;
    result.physical = (descriptor & mask) | (logical & ~mask);
    result.status = (descriptor & mask) | (descriptor & 0x7f4) | wp | RESIDENT;
    result.attributes = result.status & 0x7f4;
    result.fault = fault;
    return result;
}

static void flush_bank(Syn68kMMU040* mmu, unsigned bank, int all, uint32_t logical, int supervisor, int include_global)
{
    for (unsigned set = 0; set < 16; ++set)
        for (unsigned way = 0; way < 4; ++way) {
            Syn68kMMU040Entry* entry = &mmu->atc[bank][set][way];
            if (entry->valid && (include_global || !(entry->status & GLOBAL)) &&
                (all || (entry->supervisor == !!supervisor && entry->logical_page == logical >> entry->shift)))
                entry->valid = 0;
        }
}

void syn68k_mmu040_flush(Syn68kMMU040* mmu, int all, uint32_t logical, int supervisor, int include_global)
{
    flush_bank(mmu, 0, all, logical, supervisor, include_global);
    flush_bank(mmu, 1, all, logical, supervisor, include_global);
}

Syn68kMMU040Result syn68k_mmu040_translate(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical, unsigned fc,
                                           int writing)
{
    Syn68kMMU040Result result = {0};
    if (fc > 7) {
        result.fault = MMU040_BAD_REQUEST;
        return result;
    }
    unsigned supervisor = !!(fc & 4);
    unsigned bank = (fc & 3) == 2;
    if (ordinary_fc(fc)) {
        const uint32_t* tt = bank ? mmu->itt : mmu->dtt;
        for (unsigned i = 0; i < 2; ++i)
            if (transparent(tt[i], logical, supervisor)) {
                result.physical = logical;
                result.status = RESIDENT | TRANSPARENT;
                result.attributes = tt[i] & 0x364;
                result.transparent = 1;
                result.fault = writing && (tt[i] & WRITE_PROTECT) ? MMU040_WRITE_PROTECTED : MMU040_OK;
                return result;
            }
    }
    if (!ordinary_fc(fc) || !(mmu->tc & ENABLE)) {
        result.physical = logical;
        return result;
    }
    unsigned shift = page_shift(mmu), page = logical >> shift, set = page & 15;
    unsigned replacement = mmu->replacement[bank]++ & 3;
    Syn68kMMU040Entry* chosen = NULL;
    for (unsigned way = 0; way < 4; ++way) {
        Syn68kMMU040Entry* entry = &mmu->atc[bank][set][way];
        if (entry->valid && entry->shift == shift && entry->logical_page == page && entry->supervisor == supervisor) {
            chosen = entry;
            if (!(entry->status & RESIDENT))
                result.fault = entry->fault;
            else
                result.fault = permission(entry->status, supervisor, writing);
            if (result.fault != MMU040_OK || !writing || (entry->status & MODIFIED)) {
                result.status = entry->status;
                result.attributes = entry->status & 0x7f4;
                if (entry->status & RESIDENT)
                    result.physical = entry->physical_page | (logical & ((1u << shift) - 1));
                result.descriptor_address = entry->descriptor_address;
                result.cached = 1;
                return result;
            }
            /* M-clear writes must search again, including changed mappings. */
            break;
        }
        if (!entry->valid && !chosen)
            chosen = entry;
    }
    if (!chosen)
        chosen = &mmu->atc[bank][set][replacement];
    result = walk(mmu, bus, logical, supervisor, writing);
    if (result.fault == MMU040_PHYSICAL_ABORT)
        return result;
    *chosen = (Syn68kMMU040Entry){page,          result.physical & ~((1u << shift) - 1),
                                  result.status, result.descriptor_address,
                                  result.fault,  1,
                                  supervisor,    shift};
    return result;
}

Syn68kMMU040Result syn68k_mmu040_ptest(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical, unsigned fc,
                                       int writing)
{
    if (fc <= 7 && ordinary_fc(fc))
        flush_bank(mmu, (fc & 3) == 2, 0, logical, !!(fc & 4), 1);
    Syn68kMMU040Result result = syn68k_mmu040_translate(mmu, bus, logical, fc, writing);
    if (result.fault != MMU040_BAD_REQUEST && result.fault != MMU040_PHYSICAL_ABORT)
        mmu->mmusr = result.status;
    return result;
}
