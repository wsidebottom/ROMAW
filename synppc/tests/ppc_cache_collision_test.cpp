/*+===================================================================
  File:      ppc_cache_collision_test.cpp

  Summary:   Verify re-entrant 750 snoop retries around active fills, stores, copybacks and PTE updates.

  Classes:   fixture; uses ROMlib_ppc_system750, ROMlib_ppc_cache750 and ROMlib_ppc_physical_bus.

  Functions: collision750_checks

  Origin:    Original callback-driven tests from MPC750UM section 3.6.3 pipeline/memory queue collisions.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/bus750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

struct fixture {
    std::unique_ptr<ROMlib_ppc_system750> state{new ROMlib_ppc_system750{}};
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x40000, 0x11);
    ROMlib_ppc_physical_bus bus = {this, transfer, history};
    ROMlib_ppc_bus_result result = {};
    uint32_t target = 0x2180, victim = 0, second = 0;
    uint32_t operand_target = 0;
    unsigned transfers = 0, fail = 0, throwing = 0, checks = 0;
    bool observe = true, in_victim = false, nested = false, in_nested = false;

    void clear()
    {
        assert(!state->cache[1].transactions);
        memset(state.get(), 0, sizeof *state);
        memset(memory.data(), 0x11, memory.size());
        state->hid0 = 0x4000;
        state->mmcr0 = 12;
        state->mmcr1 = 12u << 22;
        transfers = fail = throwing = 0;
        target = 0x2180;
        victim = second = 0;
        operand_target = 0;
        observe = true;
        in_victim = nested = in_nested = false;
    }

    void query(uint32_t base, bool busy)
    {
        for (unsigned offset : {0u, 31u, 32u}) {
            uint32_t address = base + offset;
            bool collision = busy && offset != 32;
            collision |= second && (address & ~31u) == second;
            assert(bool(ROMlib_ppc_cache750_busy(&state->cache[1], address)) == collision);
            for (unsigned type = 0; type < 32; ++type)
                for (int global : {0, 1})
                    for (int inhibited : {0, 1}) {
                        bool action = type == 2 || type == 6 || type == 10 || type == 11 || type == 12 || type == 14 ||
                                      type == 18 || type == 26 || type == 30;
                        bool retry = global && action && collision;
                        unsigned before = transfers;
                        uint32_t snoops = state->pmc[1], retries = state->pmc[3];
                        ROMlib_ppc_bus_result observed = {};
                        int ok = ROMlib_ppc_750_snoop(state.get(), &bus, address, type, global, inhibited, &observed);
                        assert(ok == !retry && observed.mapping.fault == (retry ? ROMLIB_PPC_MMU_RETRY : 0));
                        assert(!observed.mapping.vector && !observed.mapping.cause && !state->bus_check_pending);
                        assert(transfers == before);
                        assert(state->pmc[1] - snoops == unsigned(global && action));
                        assert(state->pmc[3] - retries == unsigned(retry));
                        ++checks;
                    }
        }
    }

    void watch()
    {
        if (!observe)
            return;
        query(target, true);
        if (second)
            query(second, true);
        if (victim)
            query(victim, in_victim);
        if (in_nested)
            query(0x2440, true);
        if (nested && !in_nested) {
            nested = false;
            in_nested = true;
            ROMlib_ppc_bus_result observed = {};
            assert(ROMlib_ppc_750_snoop(state.get(), &bus, 0x2440, 2, 1, 0, &observed));
            in_nested = false;
            assert(!ROMlib_ppc_cache750_busy(&state->cache[1], 0x2440));
            query(target, true); // Popping the nested push must retain the outer fill.
            if (victim)
                query(victim, in_victim);
        }
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned,
                        uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        unsigned count = ++f.transfers;
        if (f.operand_target && !(access & ROMLIB_PPC_PHYSICAL_TABLE))
            f.target = f.operand_target;
        bool writing = access & ROMLIB_PPC_MMU_WRITE;
        bool was_victim = f.in_victim;
        f.in_victim |= writing && f.victim && (address & ~31u) == f.victim;
        bool failed = count == f.fail || uint64_t(address) + size > f.memory.size();
        if (!failed) {
            if (writing)
                memcpy(f.memory.data() + address, bytes, size);
            else
                memcpy(bytes, f.memory.data() + address, size);
        }
        f.watch();
        f.in_victim = was_victim;
        if (count == f.throwing)
            throw std::runtime_error("cache callback fixture");
        if (failed)
            *cause = 0x20000;
        return !failed;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask, uint32_t*)
    {
        auto& f = *static_cast<fixture*>(opaque);
        ++f.transfers;
        for (unsigned i = 0; i < 4; ++i)
            f.memory[address + 4 + i] |= mask >> (24 - i * 8);
        f.watch();
        return 1;
    }

    void resident(uint32_t address, unsigned way = 0)
    {
        auto& line = state->cache[1].lines[(address >> 5) & 127][way];
        line.address = address;
        line.state = 2;
        memset(line.data, 0xAA, 32);
    }

    void idle()
    {
        assert(!state->cache[1].transactions);
        assert(!ROMlib_ppc_cache750_busy(&state->cache[1], target));
        if (victim)
            assert(!ROMlib_ppc_cache750_busy(&state->cache[1], victim));
        if (second)
            assert(!ROMlib_ppc_cache750_busy(&state->cache[1], second));
    }
};

} // namespace

void collision750_checks()
{
    fixture f;
    unsigned scenarios = 0;
    for (bool evict : {false, true})
        for (bool nested : {false, true}) {
            f.clear();
            if (evict) {
                f.target = 0x9180;
                f.victim = 0x1180;
                for (unsigned way = 0; way < 8; ++way)
                    f.resident(0x1180 + way * 4096, way);
            }
            if (nested)
                f.resident(0x2440);
            f.nested = nested;
            uint64_t value = 0;
            assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, f.target + 4, 4, 0, &value, &f.result));
            assert(value == 0x11111111);
            f.idle();
            if (evict)
                for (unsigned i = 0; i < 32; ++i)
                    assert(f.memory[f.victim + i] == 0xAA);
            if (nested)
                for (unsigned i = 0; i < 32; ++i)
                    assert(f.memory[0x2440 + i] == 0xAA);
            f.observe = false;
            // The deferred external write may now complete; the next CPU read sees it.
            assert(ROMlib_ppc_750_snoop(f.state.get(), &f.bus, f.target, 2, 1, 0, &f.result));
            memset(f.memory.data() + f.target, 0x22, 32);
            assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, f.target + 4, 4, 0, &value, &f.result));
            assert(value == 0x22222222 && !f.state->bus_check_pending);
            ++scenarios;
        }
    for (unsigned operation : {1u, 2u, 5u, 6u}) {
        f.clear();
        f.resident(f.target);
        ROMlib_ppc_cache750_result result = {};
        assert(ROMlib_ppc_cache750_block(&f.state->cache[1], &f.bus, f.target, operation, 0, 0, 1, &result));
        assert(f.transfers == 4);
        f.idle();
        ++scenarios;
    }
    for (bool hit : {false, true}) {
        f.clear();
        if (hit)
            f.resident(f.target);
        assert(ROMlib_ppc_750_store_conditional(f.state.get(), &f.bus, f.target, 0x12345678, &f.result));
        assert(f.memory[f.target] == 0x12 && f.memory[f.target + 3] == 0x78 && f.transfers == 1);
        f.idle();
        ++scenarios;
    }
    {
        f.clear();
        f.target = 0x20000;
        f.second = 0x20020;
        f.state->mmu.msr = 0x10;
        f.state->mmu.dbat[0][0] = 0x90000002;
        f.state->mmu.dbat[0][1] = 0x20022; // Inhibited, misaligned read spans two pending granules.
        f.resident(f.target);
        f.resident(f.second);
        uint64_t value = 0;
        assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, 0x9000001F, 8, 0, &value, &f.result));
        assert(value == UINT64_C(0x1111111111111111) && f.transfers == 1);
        f.idle();
        ++scenarios;
    }
    {
        f.clear();
        f.state->hid0 = 0x5000;
        ROMlib_ppc_cache750_result result = {};
        assert(ROMlib_ppc_cache750_history(&f.state->cache[1], &f.bus, f.target, 0x180, 3, &result));
        assert(f.transfers == 1 && !result.fault);
        f.idle();
        ++scenarios;
    }
    {
        f.clear();
        f.target = 0x108C0;
        f.operand_target = 0x2180;
        f.state->mmu.msr = 0x10;
        f.state->mmu.sdr1 = 0x10000;
        f.state->mmu.sr[9] = 0x23;
        const unsigned char pte[] = {0x80, 0, 0x11, 0x80, 0, 0, 0x20, 2};
        memcpy(f.memory.data() + 0x108C0, pte, sizeof pte);
        uint64_t value = 0;
        assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, 0x90000184, 4, 0, &value, &f.result));
        assert(value == 0x11111111 && f.transfers == 8 && f.target == 0x2180);
        f.idle();
        ++scenarios;
    }
    for (unsigned phase = 1; phase <= 8; ++phase)
        for (bool throwing : {false, true}) {
            f.clear();
            f.target = 0x9180;
            f.victim = 0x1180;
            for (unsigned way = 0; way < 8; ++way)
                f.resident(0x1180 + way * 4096, way);
            if (throwing)
                f.throwing = phase;
            else
                f.fail = phase;
            uint64_t value = UINT64_C(0x123456789ABCDEF0);
            bool threw = false;
            try {
                assert(!ROMlib_ppc_750_read(f.state.get(), &f.bus, f.target, 4, 0, &value, &f.result));
            }
            catch (const std::runtime_error&) {
                threw = true;
            }
            assert(threw == throwing && value == UINT64_C(0x123456789ABCDEF0));
            f.idle();
            assert(!f.state->bus_check_pending); // Retry responses are not errors on the original master.
            ++scenarios;
        }
    {
        f.clear();
        f.resident(0x2440);
        f.nested = true;
        f.throwing = 2; // Fail inside the nested snoop push, while the outer fill is active.
        uint64_t value = 0;
        bool threw = false;
        try {
            ROMlib_ppc_750_read(f.state.get(), &f.bus, f.target, 4, 0, &value, &f.result);
        }
        catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && !f.state->cache[1].transactions);
        assert(!ROMlib_ppc_cache750_busy(&f.state->cache[1], 0x2440));
        f.idle();
        ++scenarios;
    }
    printf("%u PowerPC 750 collision scenarios; %u re-entrant transfer-type/address retry checks passed\n", scenarios,
           f.checks);
}
