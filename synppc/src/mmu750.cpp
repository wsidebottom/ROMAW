/*+===================================================================
  File:      mmu750.cpp

  Summary:   Translate 750 effective addresses through real mode, BATs and hashed page tables.

  Classes:   ROMlib_ppc_mmu750, ROMlib_ppc_mmu_bus, ROMlib_ppc_mmu_result.

  Functions: ROMlib_ppc_mmu_reset, ROMlib_ppc_mmu_flush, ROMlib_ppc_mmu_tlbie, ROMlib_ppc_mmu_translate

  Origin:    Original implementation from MPC750UM chapter 5 and MPCFPE chapter 7;
             see the architectural references in include/synppc/mmu750.h.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/mmu750.h"
#include <cstring>

namespace {
constexpr uint32_t msr_pr = 0x4000, msr_ir = 0x20, msr_dr = 0x10;
constexpr uint32_t referenced = 0x100, changed = 0x80;

int failure(ROMlib_ppc_mmu_result& r, unsigned access, unsigned fault)
{
    r.fault = fault;
    if (access & ROMLIB_PPC_MMU_PROBE)
        return 0;
    if (fault == ROMLIB_PPC_MMU_CONFIGURATION)
        return 0;
    if (fault == ROMLIB_PPC_MMU_BUS) {
        r.vector = 0x200;
        return 0;
    }
    if (fault == ROMLIB_PPC_MMU_ALIGNMENT) {
        r.vector = 0x600;
        return 0;
    }
    bool instruction = access & ROMLIB_PPC_MMU_INSTRUCTION;
    r.vector = instruction ? 0x400 : 0x300;
    r.cause = fault == ROMLIB_PPC_MMU_MISSING      ? 0x40000000
              : fault == ROMLIB_PPC_MMU_PROTECTION ? 0x08000000
              : instruction                        ? 0x10000000
                                                   : 0x04000000;
    if (!instruction && (access & ROMLIB_PPC_MMU_WRITE))
        r.cause |= 0x02000000;
    return 0;
}

bool permitted(unsigned pp, bool key, bool writing)
{
    return !(key && !pp) && (!writing || (pp != 3 && !(key && pp == 1)));
}

int qualify(ROMlib_ppc_mmu_result& result, unsigned access, unsigned pp, bool key)
{
    // MPC750UM table 4-3: DCBZ W/I alignment precedes BAT/TLB protection.
    if ((access & ROMLIB_PPC_MMU_DCBZ) && (result.wimg & 12))
        return failure(result, access, ROMLIB_PPC_MMU_ALIGNMENT);
    if (!permitted(pp, key, access & ROMLIB_PPC_MMU_WRITE))
        return failure(result, access, ROMLIB_PPC_MMU_PROTECTION);
    if ((access & ROMLIB_PPC_MMU_INSTRUCTION) && (result.wimg & 1))
        return failure(result, access, ROMLIB_PPC_MMU_NO_EXECUTE);
    return 1;
}
}

extern "C" void ROMlib_ppc_mmu_reset(ROMlib_ppc_mmu750* state)
{
    if (state)
        memset(state, 0, sizeof *state);
}

extern "C" void ROMlib_ppc_mmu_flush(ROMlib_ppc_mmu750* state)
{
    if (state) {
        memset(state->tlb, 0, sizeof state->tlb);
        memset(state->replace, 0, sizeof state->replace);
    }
}

extern "C" void ROMlib_ppc_mmu_tlbie(ROMlib_ppc_mmu750* state, uint32_t address)
{
    if (!state)
        return;
    unsigned set = (address >> 12) & 63;
    for (unsigned kind = 0; kind < 2; ++kind) {
        memset(state->tlb[kind][set], 0, sizeof state->tlb[kind][set]);
        state->replace[kind][set] = 0;
    }
}

extern "C" int ROMlib_ppc_mmu_translate(ROMlib_ppc_mmu750* state, const ROMlib_ppc_mmu_bus* bus, uint32_t address,
                                        unsigned access, ROMlib_ppc_mmu_result* output)
{
    if (!output)
        return 0;
    *output = {};
    auto& result = *output;
    result.address = address;
    result.bat = -1;
    bool instruction = access & ROMLIB_PPC_MMU_INSTRUCTION;
    bool writing = access & ROMLIB_PPC_MMU_WRITE;
    bool preflight = access & ROMLIB_PPC_MMU_PREFLIGHT;
    if (!state || (access & ~(15u | ROMLIB_PPC_MMU_PREFLIGHT)) ||
        (preflight && access != (ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_PREFLIGHT)) ||
        (instruction && (writing || (access & ROMLIB_PPC_MMU_DCBZ))) || ((access & ROMLIB_PPC_MMU_DCBZ) && !writing))
        return failure(result, access, ROMLIB_PPC_MMU_CONFIGURATION);
    if (!(state->msr & (instruction ? msr_ir : msr_dr))) {
        result.physical = address;
        result.wimg = 3; // Real-mode accesses: WIMG=0011 (M/G).
        return 1;
    }
    bool user = state->msr & msr_pr;
    const auto& bats = instruction ? state->ibat : state->dbat;
    for (unsigned index = 0; index < 4; ++index) {
        uint32_t upper = bats[index][0], lower = bats[index][1];
        uint32_t mask = ((upper & 0x1FFC) << 15) | 0x1FFFF;
        if (!(upper & (user ? 1 : 2)) || ((address ^ upper) & ~mask))
            continue;
        result.bat = index;
        result.physical = (lower & ~mask) | (address & mask);
        result.wimg = (lower >> 3) & 15;
        return qualify(result, access, lower & 3, true);
    }
    uint32_t segment = state->sr[address >> 28];
    if (segment & 0x80000000)
        return failure(result, access, ROMLIB_PPC_MMU_DIRECT_STORE);
    if (instruction && (segment & 0x10000000))
        return failure(result, access, ROMLIB_PPC_MMU_NO_EXECUTE);
    uint32_t vsid = segment & 0xFFFFFF, page = (address >> 12) & 0xFFFF;
    uint64_t tag = uint64_t(vsid) << 16 | page;
    unsigned kind = instruction ? 0 : 1, set = page & 63;
    bool key = segment & (user ? 0x20000000 : 0x40000000);
    unsigned reload_way = 2;
    for (unsigned way = 0; way < 2; ++way) {
        auto& entry = state->tlb[kind][set][way];
        if (!entry.valid || entry.tag != tag)
            continue;
        result.physical = (entry.pte & 0xFFFFF000) | (address & 0xFFF);
        result.wimg = (entry.pte >> 3) & 15;
        result.tlb_hit = 1;
        if (!qualify(result, access, entry.pte & 3, key))
            return 0;
        if (!writing || (entry.pte & changed) || (access & ROMLIB_PPC_MMU_PROBE)) {
            if (!preflight)
                state->replace[kind][set] = way ^ 1;
            return 1;
        }
        // A 750 store with TLB[C]=0 performs a full table search, then reloads
        // the TLB with the updated C bit; never patch a cached PTE address.
        reload_way = way;
        break;
    }
    if (access & ROMLIB_PPC_MMU_PROBE)
        return failure(result, access, ROMLIB_PPC_MMU_MISSING);
    uint32_t table_mask = state->sdr1 & 0x1FF;
    if ((state->sdr1 & 0xFE00) || (table_mask & (table_mask + 1)) || (state->sdr1 & (table_mask << 16)))
        return failure(result, access, ROMLIB_PPC_MMU_CONFIGURATION);
    uint32_t hash = vsid ^ page, hash_mask = (table_mask << 10) | 0x3FF;
    for (unsigned secondary = 0; secondary < 2; ++secondary) {
        uint32_t group = (state->sdr1 & 0xFFFF0000) | (((secondary ? ~hash : hash) & hash_mask) << 6);
        uint32_t expected = 0x80000000 | vsid << 7 | secondary << 6 | (page >> 10);
        for (unsigned index = 0; index < 8; ++index) {
            uint32_t pte[2], location = group + index * 8;
            if (!bus || !bus->read || !bus->read(bus->opaque, location, pte))
                return failure(result, access, ROMLIB_PPC_MMU_BUS);
            if (pte[0] != expected)
                continue;
            result.physical = (pte[1] & 0xFFFFF000) | (address & 0xFFF);
            result.wimg = (pte[1] >> 3) & 15;
            if (preflight)
                return qualify(result, access, pte[1] & 3, key);
            bool allowed = permitted(pte[1] & 3, key, writing);
            bool guarded = instruction && (result.wimg & 1);
            bool bad_zero = (access & ROMLIB_PPC_MMU_DCBZ) && (result.wimg & 12);
            // R may be set for a protected access. C requires an allowed store;
            // cache-zero alignment failures must not set C (MPC750UM 5.4.5).
            uint32_t history = referenced | (writing && allowed && !bad_zero ? changed : 0);
            uint32_t missing = history & ~pte[1];
            if (missing && (!bus->history || !bus->history(bus->opaque, location, missing)))
                return failure(result, access, ROMLIB_PPC_MMU_BUS);
            pte[1] |= history;
            unsigned way = reload_way < 2                    ? reload_way
                           : !state->tlb[kind][set][0].valid ? 0
                           : !state->tlb[kind][set][1].valid ? 1
                                                             : state->replace[kind][set];
            state->tlb[kind][set][way] = {tag, pte[1], 1};
            state->replace[kind][set] = way ^ 1;
            if (bad_zero)
                return failure(result, access, ROMLIB_PPC_MMU_ALIGNMENT);
            if (!allowed)
                return failure(result, access, ROMLIB_PPC_MMU_PROTECTION);
            if (guarded)
                return failure(result, access, ROMLIB_PPC_MMU_NO_EXECUTE);
            return 1;
        }
    }
    return failure(result, access, ROMLIB_PPC_MMU_MISSING);
}
