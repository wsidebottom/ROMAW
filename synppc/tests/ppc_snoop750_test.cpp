/*+===================================================================
  File:      ppc_snoop750_test.cpp

  Summary:   Verify 750 external snoop qualification, MEI transitions and physical reservation cancellation.

  Classes:   fixture, event; uses ROMlib_ppc_system750 and ROMlib_ppc_physical_bus.

  Functions: snoop750_checks

  Origin:    Original tests from MPC750UM 3.3.5.3, 3.6.3 and tables 3-5/3-6.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/bus750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct event {
    uint32_t address;
    unsigned size, access, wimg;
};

struct fixture {
    std::unique_ptr<ROMlib_ppc_system750> state{new ROMlib_ppc_system750{}};
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x10000, 0x11);
    std::vector<event> events;
    ROMlib_ppc_physical_bus bus = {this, transfer, history};
    ROMlib_ppc_bus_result result = {};
    unsigned fail = 0;
    uint32_t failure_cause = 0x20000;
    bool interrupt_load = false;

    void clear()
    {
        memset(state.get(), 0, sizeof *state);
        memset(memory.data(), 0x11, memory.size());
        events.clear();
        fail = 0;
        failure_cause = 0x20000;
        interrupt_load = false;
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        f.events.push_back({address, size, access, wimg});
        if (f.events.size() == f.fail || uint64_t(address) + size > f.memory.size()) {
            *cause = f.failure_cause;
            return 0;
        }
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(f.memory.data() + address, bytes, size);
        else
            memcpy(bytes, f.memory.data() + address, size);
        if (f.interrupt_load && (access & ROMLIB_PPC_PHYSICAL_ATOMIC) && !(access & ROMLIB_PPC_MMU_WRITE)) {
            f.interrupt_load = false;
            ROMlib_ppc_bus_result observed = {};
            assert(ROMlib_ppc_750_snoop(f.state.get(), &f.bus, address, 2, 0, 0, &observed));
        }
        return 1;
    }

    static int history(void*, uint32_t, uint32_t, uint32_t*)
    {
        assert(false);
        return 0;
    }

    int snoop(unsigned type, int global, int inhibited = 0, uint32_t address = 0x211F)
    {
        return ROMlib_ppc_750_snoop(state.get(), &bus, address, type, global, inhibited, &result);
    }
};

} // namespace

void snoop750_checks()
{
    fixture f;
    unsigned cases = 0;
    for (unsigned type = 0; type < 32; ++type)
        for (unsigned global = 0; global < 2; ++global)
            for (unsigned inhibited = 0; inhibited < 2; ++inhibited)
                for (unsigned enabled = 0; enabled < 2; ++enabled)
                    for (unsigned locked = 0; locked < 2; ++locked)
                        for (unsigned coherence = 0; coherence < 3; ++coherence)
                            for (unsigned reservation = 0; reservation < 3; ++reservation) {
                                f.clear();
                                f.state->mmcr0 = 12;        // Qualified L1 snoops, independent of hits/state.
                                f.state->mmcr1 = 12u << 22; // Retry events.
                                f.state->hid0 = enabled * 0x4000 | locked * 0x1000;
                                f.state->reservation_valid = reservation != 0;
                                f.state->reservation_address = reservation == 2 ? 0x3000 : 0x2100;
                                auto& line = f.state->cache[1].lines[8][0];
                                line.address = 0x2100;
                                line.state = coherence;
                                memset(line.data, 0xAA, 32);
                                auto& instruction = f.state->cache[0].lines[8][0];
                                instruction.address = 0x2100;
                                instruction.state = 1;
                                bool cancels =
                                    type == 18 || ((type == 2 || type == 6 || type == 12) && reservation == 1);
                                bool retain = type == 11 || ((type == 10 || type == 26) && inhibited);
                                bool kills = type == 6;
                                bool snoops = type == 2 || type == 6 || type == 10 || type == 11 || type == 12 ||
                                              type == 14 || type == 18 || type == 26 || type == 30;
                                bool changes = global && enabled && snoops && coherence;
                                bool pushes = changes && coherence == 2 && !kills;
                                assert(f.snoop(type, global, inhibited));
                                assert(!f.result.mapping.fault);
                                assert(f.state->pmc[1] == unsigned(global && enabled && snoops) && !f.state->pmc[3]);
                                assert(f.state->reservation_valid == (reservation && !cancels));
                                assert(line.state == (changes ? (retain ? 1 : 0) : coherence));
                                assert(instruction.state == 1 && f.state->cache[1].plru[8] == 0);
                                assert(f.events.size() == (pushes ? 4u : 0u));
                                for (unsigned i = 0; i < 32; ++i)
                                    assert(f.memory[0x2100 + i] == (pushes ? 0xAA : 0x11));
                                for (unsigned beat = 0; beat < f.events.size(); ++beat) {
                                    auto e = f.events[beat];
                                    assert(e.address == 0x2100 + beat * 8 && e.size == 8 && e.access == 1 &&
                                           e.wimg == 8);
                                }
                                ++cases;
                            }
    for (unsigned beat = 1; beat <= 4; ++beat)
        for (uint32_t cause : {0u, 0x20000u}) {
            f.clear();
            f.state->hid0 = 0x5000;
            f.state->reservation_valid = 1;
            f.state->reservation_address = 0x2100;
            auto& line = f.state->cache[1].lines[8][0];
            line.address = 0x2100;
            line.state = 2;
            memset(line.data, 0xAA, 32);
            f.fail = beat;
            f.failure_cause = cause;
            assert(!f.snoop(2, 1));
            assert(!f.state->reservation_valid && line.state == 2);
            assert(f.result.mapping.vector == 0x200 && f.result.mapping.cause == cause);
            assert(f.state->bus_check_pending && f.state->bus_check_cause == cause);
            assert(f.result.mapping.physical == 0x2100 + (beat - 1) * 8);
            f.fail = 0;
            assert(f.snoop(2, 1) && !line.state);
            assert(f.state->bus_check_pending); // A retry cannot retract a reported bus error.
            ++cases;
        }
    for (bool enabled : {false, true})
        for (bool little : {false, true})
            for (bool mapped : {false, true})
                for (bool interrupt : {false, true}) {
                    f.clear();
                    f.state->hid0 = enabled ? 0x4000 : 0;
                    f.state->mmu.msr = little | (mapped ? 0x10 : 0);
                    f.state->mmu.dbat[0][0] = 0x90000002;
                    f.state->mmu.dbat[0][1] = 2;
                    f.memory[0x80] = 0x12;
                    f.memory[0x81] = 0x34;
                    f.memory[0x82] = 0x56;
                    f.memory[0x83] = 0x78;
                    f.memory[0x84] = 0x9A;
                    f.memory[0x85] = 0xBC;
                    f.memory[0x86] = 0xDE;
                    f.memory[0x87] = 0xF0;
                    f.interrupt_load = interrupt;
                    uint32_t value = 0;
                    assert(ROMlib_ppc_750_load_reserve(f.state.get(), &f.bus, mapped ? 0x90000080 : 0x80, &value,
                                                       &f.result));
                    assert(value == (little ? 0x9ABCDEF0u : 0x12345678u));
                    assert(f.state->reservation_address == 0x80 && f.state->reservation_valid == unsigned(!interrupt));
                    assert(f.events.size() == (enabled ? 4u : 1u));
                    for (auto e : f.events)
                        assert(e.access == ROMLIB_PPC_PHYSICAL_ATOMIC);
                    if (!interrupt) {
                        f.state->reservation_valid = 0; // CPU accepted and consumed the reservation.
                        assert(ROMlib_ppc_750_store_conditional(f.state.get(), &f.bus, mapped ? 0x90000080 : 0x80,
                                                                0x11223344, &f.result));
                        assert(f.events.back().access == (ROMLIB_PPC_PHYSICAL_ATOMIC | ROMLIB_PPC_MMU_WRITE));
                        assert(f.memory[little ? 0x84 : 0x80] == 0x11);
                    }
                    ++cases;
                }
    for (unsigned failure = 0; failure < 3; ++failure) {
        f.clear();
        f.state->reservation_valid = 1;
        f.state->reservation_address = 0x2100;
        f.fail = failure == 0 ? 1 : 0;
        f.state->mmu.msr = failure == 2 ? 0x10 : 0;
        f.state->mmu.sdr1 = 0x10000;
        uint32_t value = 0xAABBCCDD;
        assert(!ROMlib_ppc_750_load_reserve(f.state.get(), &f.bus, failure == 1 ? 0x81 : 0x80, &value, &f.result));
        assert(value == 0xAABBCCDD && !f.state->reservation_valid && f.result.size == 4);
        assert(f.result.mapping.vector == (failure == 0 ? 0x200u : failure == 1 ? 0x600u : 0x200u));
        ++cases;
    }
    for (unsigned invalid = 0; invalid < 6; ++invalid) {
        f.clear();
        f.state->reservation_valid = 1;
        f.state->reservation_address = 0x2100;
        f.state->checkstop = invalid == 0;
        assert(!ROMlib_ppc_750_snoop(invalid == 1 ? nullptr : f.state.get(), &f.bus, 0x211F, invalid == 2 ? 32 : 2,
                                     invalid == 3 ? 2 : 1, invalid == 4 ? -1 : 0, invalid == 5 ? nullptr : &f.result));
        assert(f.state->reservation_valid && f.events.empty());
        if (invalid != 5)
            assert(f.result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION);
        ++cases;
    }
    for (unsigned selector : {0x800000u, 0x400000u, 0x200000u}) {
        f.clear();
        ROMlib_ppc_750_set_hid0(f.state.get(), selector);
        ROMlib_ppc_750_set_msr(f.state.get(), 0x40000, 0);
        assert(f.snoop(6, 1));
        ROMlib_ppc_750_set_qack(f.state.get(), 1);
        assert(bool(f.snoop(6, 1)) == (selector == 0x800000));
        ROMlib_ppc_750_set_qack(f.state.get(), 0);
        for (unsigned delay = 0; delay <= 8; ++delay) {
            f.state->reservation_valid = 1;
            f.state->reservation_address = 0x2100;
            bool accepted = selector == 0x800000 || (selector == 0x400000 && delay == 8);
            assert(bool(f.snoop(6, 1)) == accepted);
            assert(f.result.mapping.fault == (accepted ? 0u : unsigned(ROMLIB_PPC_MMU_RETRY)));
            assert(bool(f.state->reservation_valid) == !accepted);
            ROMlib_ppc_750_advance_bus(f.state.get(), 1);
            ++cases;
        }
        ROMlib_ppc_750_set_qack(f.state.get(), 1);
        assert(bool(f.snoop(6, 1)) == (selector == 0x800000));
        ROMlib_ppc_750_set_msr(f.state.get(), 0, 0);
        assert(f.snoop(6, 1) && !ROMlib_ppc_750_qreq(f.state.get()));
    }
    for (unsigned selector : {0x800000u, 0x400000u, 0x200000u})
        for (bool automatic_zz : {false, true}) {
            f.clear();
            std::unique_ptr<ROMlib_ppc_l2cache750> l2(new ROMlib_ppc_l2cache750{});
            assert(ROMlib_ppc_750_set_l2(f.state.get(), l2.get()));
            assert(ROMlib_ppc_750_set_l2cr(f.state.get(), 0x99000000 | (automatic_zz ? 0x00100000 : 0)));
            ROMlib_ppc_750_set_hid0(f.state.get(), selector);
            ROMlib_ppc_750_set_msr(f.state.get(), 0x40000, 0);
            assert(!ROMlib_ppc_750_l2zz(f.state.get()));
            ROMlib_ppc_750_set_qack(f.state.get(), 1);
            assert(bool(ROMlib_ppc_750_l2zz(f.state.get())) == (automatic_zz && selector != 0x800000));
            ROMlib_ppc_750_set_qack(f.state.get(), 0);
            ROMlib_ppc_750_advance_bus(f.state.get(), 8);
            assert(bool(f.snoop(6, 1)) == (selector == 0x800000 || (selector == 0x400000 && !automatic_zz)));
            ROMlib_ppc_750_set_msr(f.state.get(), 0, 0);
            assert(!ROMlib_ppc_750_l2zz(f.state.get()) && f.snoop(6, 1));
            ++cases;
        }
    printf("%u PowerPC 750 external snoop/physical reservation/atomic bus cases passed\n", cases);
}
