/*+===================================================================
  File:      ppc_mmu750_test.cpp

  Summary:   Verify 750 BAT/page protection, physical history transactions and TLB behavior.

  Classes:   table_bus, ROMlib_ppc_mmu750, ROMlib_ppc_mmu_result.

  Functions: mmu750_checks

  Origin:    Original architectural cases from MPC750UM chapter 5 and MPCFPE chapter 7.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/mmu750.h"
#include <array>
#include <map>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

struct table_bus {
    std::map<uint32_t, std::array<uint32_t, 2>> entries;
    unsigned reads = 0, writes = 0;
    bool fail_read = false, fail_write = false;

    static int read(void* opaque, uint32_t address, uint32_t output[2])
    {
        auto& self = *static_cast<table_bus*>(opaque);
        ++self.reads;
        assert(!(address & 7));
        auto entry = self.entries.find(address);
        output[0] = entry == self.entries.end() ? 0 : entry->second[0];
        output[1] = entry == self.entries.end() ? 0 : entry->second[1];
        return !self.fail_read;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask)
    {
        auto& self = *static_cast<table_bus*>(opaque);
        ++self.writes;
        assert(mask && !(mask & ~0x180u));
        auto entry = self.entries.find(address);
        assert(entry != self.entries.end());
        if (self.fail_write)
            return 0;
        entry->second[1] |= mask;
        return 1;
    }

    ROMlib_ppc_mmu_bus bus()
    {
        return {this, read, history};
    }
};

static unsigned cases;
static constexpr unsigned write = ROMLIB_PPC_MMU_WRITE, fetch = ROMLIB_PPC_MMU_INSTRUCTION;

static uint32_t entry(table_bus& bus, ROMlib_ppc_mmu750& state, uint32_t address, unsigned slot, bool secondary,
                      uint32_t low)
{
    uint32_t vsid = state.sr[address >> 28] & 0xFFFFFF;
    uint32_t page = address >> 12 & 0xFFFF;
    uint32_t hash = vsid ^ page;
    uint32_t group =
        (state.sdr1 & 0xFFFF0000) | (((secondary ? ~hash : hash) & (0x3FF | (state.sdr1 & 511) << 10)) << 6);
    uint32_t location = group + slot * 8;
    bus.entries[location] = {0x80000000 | vsid << 7 | unsigned(secondary) << 6 | page >> 10, low};
    return location;
}

static void hash_and_protection()
{
    for (unsigned secondary = 0; secondary < 2; ++secondary)
        for (unsigned slot = 0; slot < 8; ++slot)
            for (unsigned pp = 0; pp < 4; ++pp)
                for (unsigned keys = 0; keys < 4; ++keys)
                    for (unsigned user = 0; user < 2; ++user)
                        for (unsigned access : {0u, write, fetch}) {
                            ROMlib_ppc_mmu750 state = {};
                            state.msr = 0x30 | (user ? 0x4000 : 0);
                            state.sdr1 = 0x10000;
                            state.sr[10] = 0x123456 | (keys & 1 ? 0x40000000 : 0) | (keys & 2 ? 0x20000000 : 0);
                            table_bus bus;
                            // Independently fixed primary/secondary PTEG addresses
                            // for VSID=123456 and EA page index=5678.
                            uint32_t location = (secondary ? 0x17440 : 0x18B80) + slot * 8;
                            bus.entries[location] = {0x891A2B15 | secondary << 6, 0x87654010 | pp};
                            auto physical = bus.bus();
                            ROMlib_ppc_mmu_result result;
                            bool key = keys & (user ? 2 : 1);
                            bool allowed = key ? pp == 2 || (pp != 0 && access != write) : pp != 3 || access != write;
                            int success = ROMlib_ppc_mmu_translate(&state, &physical, 0xA56789AB, access, &result);
                            assert(!!success == allowed && result.address == 0xA56789AB);
                            assert(bus.reads == slot + 1 + secondary * 8 && bus.writes == 1);
                            assert(bus.entries[location][1] & 0x100);
                            assert(!!(bus.entries[location][1] & 0x80) == (allowed && access == write));
                            if (allowed) {
                                assert(result.physical == 0x876549AB && result.wimg == 2 && result.bat == -1);
                                unsigned reads = bus.reads, writes = bus.writes;
                                assert(ROMlib_ppc_mmu_translate(&state, &physical, 0xA5678FFF, access, &result));
                                assert(result.physical == 0x87654FFF && result.tlb_hit);
                                assert(bus.reads == reads && bus.writes == writes);
                            }
                            else {
                                assert(result.fault == ROMLIB_PPC_MMU_PROTECTION);
                                assert(result.vector == (access == fetch ? 0x400 : 0x300));
                                assert(result.cause == (0x08000000u | (access == write ? 0x02000000u : 0)));
                            }
                            ++cases;
                        }
}

static void bats_and_real()
{
    for (unsigned access : {0u, write, fetch})
        for (uint32_t address : {0u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
            ROMlib_ppc_mmu750 state = {};
            ROMlib_ppc_mmu_result result;
            assert(ROMlib_ppc_mmu_translate(&state, nullptr, address, access, &result));
            assert(result.physical == address && result.wimg == 3 && result.bat == -1);
            ++cases;
        }
    for (unsigned size = 0; size <= 11; ++size)
        for (unsigned user = 0; user < 2; ++user)
            for (unsigned pp = 0; pp < 4; ++pp)
                for (unsigned access : {0u, write, fetch})
                    for (unsigned validity = 0; validity < 4; ++validity) {
                        ROMlib_ppc_mmu750 state = {};
                        state.msr = 0x30 | (user ? 0x4000 : 0);
                        unsigned bl = (1u << size) - 1;
                        uint32_t mask = bl << 17 | 0x1FFFF;
                        state.ibat[2][0] = state.dbat[2][0] = 0x20000000 | bl << 2 | validity;
                        state.ibat[2][1] = state.dbat[2][1] = 0x80000010 | pp;
                        ROMlib_ppc_mmu_result result;
                        table_bus bus;
                        auto physical = bus.bus();
                        bool valid = validity & (user ? 1 : 2);
                        bool allowed = valid && pp && (access != write || pp == 2);
                        assert(!!ROMlib_ppc_mmu_translate(&state, &physical, 0x20000000 | mask, access, &result) ==
                               allowed);
                        if (valid) {
                            assert(result.bat == 2 && !bus.reads && !bus.writes);
                            if (allowed)
                                assert(result.physical == (0x80000000 | mask) && result.wimg == 2);
                            else
                                assert(result.fault == ROMLIB_PPC_MMU_PROTECTION);
                        }
                        else
                            assert(result.fault == ROMLIB_PPC_MMU_MISSING && bus.reads == 16);
                        ++cases;
                    }
}

static void history_and_tlbie()
{
    ROMlib_ppc_mmu750 state = {};
    state.msr = 0x30;
    state.sdr1 = 0x10000;
    table_bus bus;
    auto physical = bus.bus();
    ROMlib_ppc_mmu_result result;
    auto location = entry(bus, state, 0x1234, 7, true, 0xABCD0002);
    assert(ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, 0, &result));
    assert(result.physical == 0xABCD0234 && bus.reads == 16 && bus.writes == 1);
    assert(bus.entries[location][1] == 0xABCD0102);
    // Clearing R without tlbie must not make a read hit update it again.
    bus.entries[location][1] &= ~0x100u;
    assert(ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, 0, &result));
    assert(result.tlb_hit && bus.reads == 16 && bus.writes == 1);
    // C=0 in the TLB forces an actual table search before a first store.
    bus.fail_read = true;
    assert(!ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, write, &result));
    assert(result.fault == ROMLIB_PPC_MMU_BUS && result.vector == 0x200);
    bus.fail_read = false;
    assert(ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, write, &result));
    assert(bus.entries[location][1] == 0xABCD0182 && bus.writes == 2);
    unsigned reads = bus.reads;
    assert(ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, write, &result));
    assert(bus.reads == reads);
    ROMlib_ppc_mmu_tlbie(&state, 0xF0001000);
    assert(ROMlib_ppc_mmu_translate(&state, &physical, 0x1234, 0, &result));
    assert(!result.tlb_hit && bus.reads == reads + 16);
    // tlbie invalidates all four entries in its set, including different VSIDs.
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned set = 0; set < 64; ++set)
            for (unsigned way = 0; way < 2; ++way)
                state.tlb[kind][set][way].valid = 1;
    ROMlib_ppc_mmu_tlbie(&state, 19 << 12);
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned set = 0; set < 64; ++set)
            for (unsigned way = 0; way < 2; ++way)
                assert(state.tlb[kind][set][way].valid == (set != 19));
    ROMlib_ppc_mmu_flush(&state);
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned set = 0; set < 64; ++set)
            for (unsigned way = 0; way < 2; ++way)
                assert(!state.tlb[kind][set][way].valid);
    ++cases;
}

static void faults_and_hints()
{
    for (unsigned scenario = 0; scenario < 8; ++scenario)
        for (unsigned access : {0u, write, fetch}) {
            ROMlib_ppc_mmu750 state = {};
            state.msr = 0x30;
            state.sdr1 = 0x10000;
            table_bus bus;
            auto physical = bus.bus();
            ROMlib_ppc_mmu_result result;
            auto location = entry(bus, state, 0x4000, 0, false, 0x80000002);
            unsigned expected = 0;
            if (scenario == 0) {
                state.sr[0] |= 0x80000000;
                expected = ROMLIB_PPC_MMU_DIRECT_STORE;
            }
            if (scenario == 1) {
                state.sr[0] |= 0x10000000;
                expected = access == fetch ? ROMLIB_PPC_MMU_NO_EXECUTE : 0;
            }
            if (scenario == 2) {
                bus.entries[location][1] |= 8;
                expected = access == fetch ? ROMLIB_PPC_MMU_NO_EXECUTE : 0;
            }
            if (scenario == 3) {
                bus.fail_read = true;
                expected = ROMLIB_PPC_MMU_BUS;
            }
            if (scenario == 4) {
                bus.fail_write = true;
                expected = ROMLIB_PPC_MMU_BUS;
            }
            if (scenario == 5) {
                state.sdr1 |= 2; // Noncontiguous mask.
                expected = ROMLIB_PPC_MMU_CONFIGURATION;
            }
            if (scenario == 6) {
                bus.entries.clear();
                expected = ROMLIB_PPC_MMU_MISSING;
            }
            assert(!!ROMlib_ppc_mmu_translate(&state, &physical, 0x4000, access, &result) == !expected);
            assert(result.fault == expected);
            if (scenario == 0 || (scenario == 1 && access == fetch))
                assert(!bus.reads && !bus.writes);
            ++cases;
        }
    for (unsigned attributes = 0; attributes < 16; ++attributes) {
        ROMlib_ppc_mmu750 state = {};
        state.msr = 0x30;
        state.sdr1 = 0x10000;
        table_bus bus;
        auto physical = bus.bus();
        ROMlib_ppc_mmu_result result;
        assert(!ROMlib_ppc_mmu_translate(&state, &physical, 0, ROMLIB_PPC_MMU_PROBE, &result));
        assert(!result.vector && !bus.reads && !bus.writes);
        auto location = entry(bus, state, 0, 0, false, 0xA0000002 | attributes << 3);
        assert(!!ROMlib_ppc_mmu_translate(&state, &physical, 0, write | ROMLIB_PPC_MMU_DCBZ, &result) ==
               !(attributes & 12));
        assert(!!(bus.entries[location][1] & 0x80) == !(attributes & 12));
        assert(!!result.vector == !!(attributes & 12));
        ++cases;
    }
    for (unsigned attributes = 0; attributes < 16; ++attributes)
        for (unsigned protection = 0; protection < 4; ++protection)
            for (unsigned path = 0; path < 3; ++path) {
                ROMlib_ppc_mmu750 state = {};
                state.msr = 0x10;
                state.sdr1 = 0x10000;
                state.sr[0] = 0x40000000;
                table_bus bus;
                auto physical = bus.bus();
                ROMlib_ppc_mmu_result result;
                uint32_t location = entry(bus, state, 0, 0, false, 0xA0000000 | attributes << 3 | protection);
                if (!path) {
                    state.dbat[0][0] = 2;
                    state.dbat[0][1] = 0xA0000000 | attributes << 3 | protection;
                }
                if (path == 2) {
                    ROMlib_ppc_mmu_translate(&state, &physical, 0, 0, &result);
                    assert(state.tlb[1][0][0].valid);
                }
                unsigned expected = attributes & 12   ? ROMLIB_PPC_MMU_ALIGNMENT
                                    : protection == 2 ? ROMLIB_PPC_MMU_NONE
                                                      : ROMLIB_PPC_MMU_PROTECTION;
                assert(!!ROMlib_ppc_mmu_translate(&state, &physical, 0, write | ROMLIB_PPC_MMU_DCBZ, &result) ==
                       !expected);
                assert(result.fault == expected);
                if (path) {
                    assert(bus.entries[location][1] & 0x100);
                    assert(bool(bus.entries[location][1] & 0x80) == !expected);
                }
                else
                    assert(!bus.reads && !bus.writes);
                ++cases;
            }
    ROMlib_ppc_mmu_result result;
    assert(!ROMlib_ppc_mmu_translate(nullptr, nullptr, 0, 0, &result));
    assert(result.fault == ROMLIB_PPC_MMU_CONFIGURATION);
    assert(!ROMlib_ppc_mmu_translate(nullptr, nullptr, 0, 0, nullptr));
}

static void table_sizes_and_replacement()
{
    for (unsigned bits = 0; bits <= 9; ++bits)
        for (uint32_t vsid : {0u, 0x123456u, 0xFFFFFFu})
            for (uint32_t address : {0u, 0x12345678u, 0xFFFFFFFFu})
                for (bool secondary : {false, true}) {
                    ROMlib_ppc_mmu750 state = {};
                    state.msr = 0x30;
                    state.sdr1 = 0x40000000 | ((1u << bits) - 1);
                    state.sr[address >> 28] = vsid;
                    table_bus bus;
                    auto physical = bus.bus();
                    auto location = entry(bus, state, address, 7, secondary, 0xFFFFF002);
                    ROMlib_ppc_mmu_result result;
                    assert(ROMlib_ppc_mmu_translate(&state, &physical, address, write, &result));
                    assert(result.physical == (0xFFFFF000 | (address & 4095)));
                    assert(bus.entries[location][1] == 0xFFFFF182);
                    ++cases;
                }
    ROMlib_ppc_mmu750 state = {};
    state.msr = 0x30;
    state.sdr1 = 0x10000;
    table_bus bus;
    auto physical = bus.bus();
    ROMlib_ppc_mmu_result result;
    for (unsigned vsid = 0; vsid < 3; ++vsid) {
        state.sr[0] = vsid;
        entry(bus, state, 0, 0, false, 0x80000002 + vsid * 4096);
    }
    auto translate = [&](unsigned vsid, unsigned access) {
        state.sr[0] = vsid;
        assert(ROMlib_ppc_mmu_translate(&state, &physical, 0, access, &result));
        assert(result.physical == 0x80000000 + vsid * 4096);
    };
    translate(0, 0);
    assert(!result.tlb_hit);
    translate(1, 0);
    assert(!result.tlb_hit);
    translate(0, 0);
    assert(result.tlb_hit);
    translate(2, 0); // The hit made way 1 the replacement victim.
    assert(!result.tlb_hit);
    translate(0, 0);
    assert(result.tlb_hit);
    translate(1, 0);
    assert(!result.tlb_hit);
    translate(1, fetch); // Instruction TLB remains independent.
    assert(!result.tlb_hit);
    translate(1, fetch);
    assert(result.tlb_hit);
    ROMlib_ppc_mmu_tlbie(&state, 0);
    translate(1, 0);
    assert(!result.tlb_hit);
    translate(1, fetch);
    assert(!result.tlb_hit);
    ++cases;
}

static void store_preflight()
{
    constexpr uint32_t address = 0x4002;
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        ROMlib_ppc_mmu750 state = {};
        state.msr = 0x10;
        state.sdr1 = 0x10000;
        table_bus table;
        auto bus = table.bus();
        uint32_t location = entry(table, state, address, 0, false, 0xA0000002);
        ROMlib_ppc_mmu_result result;
        if (scenario >= 4)
            assert(ROMlib_ppc_mmu_translate(&state, &bus, address, scenario == 5 ? write : 0, &result));
        if (scenario == 1 || scenario == 4)
            table.entries[location][1] |= 1; // Read-only; a warm TLB[C]=0 store must rewalk.
        if (scenario == 2)
            table.entries.clear();
        table.fail_read = scenario == 3;
        if (scenario == 5)
            state.replace[0][address >> 12] = 0;
        table.reads = table.writes = 0;
        auto before = state;
        auto original = table.entries;
        bool allowed = scenario == 0 || scenario == 5;
        assert(!!ROMlib_ppc_mmu_translate(&state, &bus, address, write | ROMLIB_PPC_MMU_PREFLIGHT, &result) == allowed);
        assert(!memcmp(&before, &state, sizeof state) && table.entries == original && !table.writes);
        assert(result.address == address && table.reads == (scenario == 5 ? 0u : scenario == 2 ? 16u : 1u));
        if (allowed)
            assert(result.physical == 0xA0000002 && !result.vector);
        else if (scenario == 3)
            assert(result.vector == 0x200 && result.fault == ROMLIB_PPC_MMU_BUS);
        else
            assert(result.vector == 0x300 && result.cause == (scenario == 2 ? 0x42000000 : 0x0A000000));
        if (scenario == 0) {
            assert(ROMlib_ppc_mmu_translate(&state, &bus, address, write, &result));
            assert(table.writes == 1 && (table.entries[location][1] & 0x180) == 0x180);
        }
        ++cases;
    }
}

} // namespace

void mmu750_checks()
{
    hash_and_protection();
    bats_and_real();
    history_and_tlbie();
    faults_and_hints();
    table_sizes_and_replacement();
    store_preflight();
    printf("%u PowerPC 750 BAT/hash/protection/history/TLB/fault cases passed\n", cases);
}
