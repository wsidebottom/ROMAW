/*+===================================================================
  File:      ppc_clock750_test.cpp

  Summary:   Verify 750 core/TB clocks, thermal/performance monitoring and asynchronous interrupt priorities.

  Classes:   Uses ROMlib_ppc_system750 and ROMlib_ppc_registers.

  Functions: clock750_checks, thermal_checks, performance_checks

  Origin:    Original tests from MPC750UM exception priorities and MPCFPE decrementer rules.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/system750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>

namespace {

static unsigned thermal_checks()
{
    std::unique_ptr<ROMlib_ppc_system750> owner(new ROMlib_ppc_system750{});
    auto& s = *owner;
    unsigned cases = 0;
    for (unsigned channel : {0u, 1u})
        for (unsigned temperature = 0; temperature < 128; ++temperature)
            for (unsigned threshold = 0; threshold < 128; ++threshold)
                for (unsigned below : {0u, 1u}) {
                    s.thrm[0] = s.thrm[1] = s.thrm[2] = 0;
                    s.thermal_remaining = s.thermal_pending = s.thermal_halted = s.thermal_channel = 0;
                    ROMlib_ppc_750_set_temperature(&s, temperature);
                    ROMlib_ppc_750_set_thermal(&s, channel, threshold << 23 | below * 4 | 1);
                    ROMlib_ppc_750_set_thermal(&s, 2, 7); // Three core cycles per sample.
                    ROMlib_ppc_750_advance_core(&s, 2);
                    assert(!(s.thrm[channel] & 0x40000000));
                    ROMlib_ppc_750_advance_core(&s, 1);
                    bool crossed = below ? temperature < threshold : temperature > threshold;
                    assert(bool(s.thrm[channel] >> 31) == crossed && (s.thrm[channel] & 0x40000000));
                    assert(!s.thermal_pending && !s.thermal_halted);
                    ++cases;
                }
    for (unsigned valid = 1; valid < 4; ++valid)
        for (unsigned directions = 0; directions < 4; ++directions)
            for (unsigned enables = 0; enables < 4; ++enables)
                for (unsigned period : {1u, 3u, 8191u})
                    for (uint64_t cycles :
                         {UINT64_C(0), UINT64_C(1), UINT64_C(2), UINT64_C(3), UINT64_C(30), UINT64_MAX}) {
                        memset(&s, 0, sizeof s);
                        ROMlib_ppc_750_set_temperature(&s, 65);
                        for (unsigned channel = 0; channel < 2; ++channel)
                            ROMlib_ppc_750_set_thermal(&s, channel,
                                                       (60 + 10 * channel) << 23 | ((directions >> channel) & 1) * 4 |
                                                           ((enables >> channel) & 1) * 2 | ((valid >> channel) & 1));
                        ROMlib_ppc_750_set_thermal(&s, 2, period * 2 | 1);
                        std::unique_ptr<ROMlib_ppc_system750> split(new ROMlib_ppc_system750(s));
                        ROMlib_ppc_750_advance_core(&s, cycles);
                        uint64_t part = cycles / 3;
                        ROMlib_ppc_750_advance_core(split.get(), part);
                        ROMlib_ppc_750_advance_core(split.get(), part);
                        ROMlib_ppc_750_advance_core(split.get(), cycles - 2 * part);
                        assert(!memcmp(&s, split.get(), sizeof s));
                        unsigned visited = 0, halted = 0, turn = 0;
                        // Only status/interrupt results matter; without interrupts
                        // both channels have their final result within two visits.
                        uint64_t samples = cycles / period;
                        for (uint64_t step = 0; step < samples && step < 4; ++step) {
                            unsigned available = valid & ~halted;
                            if (!available)
                                break;
                            if (!(available & (1u << turn)))
                                turn ^= 1;
                            visited |= 1u << turn;
                            bool crossed = (directions & (1u << turn)) ? turn == 1 : turn == 0;
                            if (crossed && (enables & (1u << turn)))
                                halted |= 1u << turn;
                            turn ^= 1;
                        }
                        assert(s.thermal_pending == halted && s.thermal_halted == halted);
                        for (unsigned channel = 0; channel < 2; ++channel) {
                            bool crossed = (directions & (1u << channel)) ? channel == 1 : channel == 0;
                            assert(bool(s.thrm[channel] & 0x40000000) == bool(visited & (1u << channel)));
                            assert(bool(s.thrm[channel] & 0x80000000) == (bool(visited & (1u << channel)) && crossed));
                        }
                        ++cases;
                    }
    for (unsigned calibration = 0; calibration < 32; ++calibration)
        for (int temperature : {INT32_MIN, -1, 0, 25, 65, 127, 128, INT32_MAX})
            for (unsigned below : {0u, 1u}) {
                memset(&s, 0, sizeof s);
                ROMlib_ppc_750_set_temperature(&s, temperature);
                ROMlib_ppc_750_set_thermal(&s, 0, 65u << 23 | below * 4 | 1);
                ROMlib_ppc_750_set_thermal(&s, 2, calibration << 25 | 3);
                ROMlib_ppc_750_advance_core(&s, 1);
                int delta = ((calibration & 8) ? 20 : 0) + ((calibration & 4) ? 8 : 0) + ((calibration & 2) ? 4 : 0) +
                            ((calibration & 1) ? 2 : 0);
                int64_t adjusted = int64_t(temperature) + ((calibration & 16) ? delta : -delta);
                bool crossed = below ? adjusted < 65 : adjusted > 65;
                assert(bool(s.thrm[0] >> 31) == crossed && (s.thrm[0] & 0x40000000));
                ++cases;
            }
    for (unsigned index = 0; index < 3; ++index)
        for (unsigned bit = 0; bit < 32; ++bit)
            for (unsigned complement : {0u, 1u}) {
                memset(&s, 0, sizeof s);
                s.thrm[0] = s.thrm[1] = 0xC0000000;
                s.thermal_pending = s.thermal_halted = 3;
                uint32_t value = complement ? ~(1u << bit) : 1u << bit;
                assert(ROMlib_ppc_750_set_thermal(&s, index, value));
                for (unsigned position = 0; position < 32; ++position) {
                    bool writable = index == 2 ? position < 14 || (position >= 25 && position <= 29)
                                               : position < 3 || (position >= 23 && position <= 29);
                    bool expected = writable && (value & (1u << position));
                    expected |= index < 2 && position == 31;
                    assert(bool(s.thrm[index] & (1u << position)) == expected);
                }
                assert(s.thermal_pending == (index == 2 ? 0u : 3u & ~(1u << index)));
                assert(s.thermal_halted == s.thermal_pending);
                if (index == 2)
                    assert(!(s.thrm[0] & 0x40000000) && !(s.thrm[1] & 0x40000000));
                ++cases;
            }
    for (unsigned inputs = 0; inputs < 32; ++inputs)
        for (unsigned ee : {0u, 1u})
            for (unsigned dec : {0u, 1u}) {
                memset(&s, 0, sizeof s);
                s.hid0 = ROMLIB_PPC_750_HID0_EMCP;
                s.mmu.msr = 0x1000 | ee * 0x8000;
                s.decrementer_pending = dec;
                ROMlib_ppc_750_set_temperature(&s, 65);
                ROMlib_ppc_750_set_thermal(&s, 0, 60u << 23 | 3);
                ROMlib_ppc_750_set_thermal(&s, 1, 70u << 23 | 7);
                ROMlib_ppc_750_set_thermal(&s, 2, 3);
                ROMlib_ppc_750_advance_core(&s, 2);
                assert(s.thermal_pending == 3 && s.thermal_halted == 3);
                uint32_t first = s.thrm[0], second = s.thrm[1];
                ROMlib_ppc_750_set_temperature(&s, -100);
                ROMlib_ppc_750_advance_core(&s, UINT64_MAX);
                assert(s.thrm[0] == first && s.thrm[1] == second && s.thermal_pending == 3);
                ROMlib_ppc_750_set_inputs(&s, inputs);
                ROMlib_ppc_registers r = {};
                r.pc = 0x2340;
                unsigned vector = inputs & 1   ? 0x200
                                  : !ee        ? 0
                                  : inputs & 2 ? 0x1400
                                  : inputs & 4 ? 0x500
                                  : inputs & 8 ? 0xF00
                                  : dec        ? 0x900
                                               : 0x1700;
                assert(ROMlib_ppc_750_interrupt(&s, &r) == int(vector != 0));
                assert(r.pc == (vector ? vector : 0x2340));
                assert(s.thermal_pending == (vector == 0x1700 ? 0u : 3u) && s.thermal_halted == 3);
                s.mmu.msr = 0x9000;
                s.decrementer_pending = 0;
                ROMlib_ppc_750_set_inputs(&s, 0);
                assert(ROMlib_ppc_750_interrupt(&s, &r) == int(vector != 0x1700));
                assert(!s.thermal_pending && s.thermal_halted == 3);
                ROMlib_ppc_750_set_thermal(&s, 0, 60u << 23 | 3);
                assert(!(s.thrm[0] & 0x40000000) && s.thermal_halted == 2);
                ROMlib_ppc_750_advance_core(&s, 1);
                assert((s.thrm[0] & 0xC0000000) == 0x40000000 && s.thrm[1] == second && !s.thermal_pending);
                ROMlib_ppc_750_set_temperature(&s, 100);
                ROMlib_ppc_750_advance_core(&s, 1);
                assert(s.thermal_pending == 1 && s.thermal_halted == 3);
                ROMlib_ppc_750_set_thermal(&s, 2, 0);
                ROMlib_ppc_750_advance_core(&s, UINT64_MAX);
                assert(!s.thermal_pending && !s.thermal_halted && !(s.thrm[0] & 0x40000000) &&
                       !(s.thrm[1] & 0x40000000));
                ++cases;
            }
    for (unsigned pll = 0; pll < 16; ++pll) {
        assert(ROMlib_ppc_750_set_pll(&s, pll) && s.hid1 == uint32_t(pll) << 28);
        ++cases;
    }
    auto before = s;
    assert(!ROMlib_ppc_750_set_pll(&s, 16) && !ROMlib_ppc_750_set_pll(&s, UINT32_MAX));
    assert(!ROMlib_ppc_750_set_thermal(&s, 3, 0) && !ROMlib_ppc_750_set_thermal(&s, UINT32_MAX, 0));
    assert(!memcmp(&s, &before, sizeof s));
    s.checkstop = 1;
    before = s;
    ROMlib_ppc_750_set_temperature(&s, 100);
    ROMlib_ppc_750_advance_core(&s, UINT64_MAX);
    assert(!ROMlib_ppc_750_set_pll(&s, 3) && !ROMlib_ppc_750_set_thermal(&s, 0, 0));
    assert(!memcmp(&s, &before, sizeof s));
    ROMlib_ppc_750_set_temperature(nullptr, 100);
    ROMlib_ppc_750_advance_core(nullptr, 100);
    assert(!ROMlib_ppc_750_set_pll(nullptr, 0) && !ROMlib_ppc_750_set_thermal(nullptr, 0, 0));
    return cases;
}

static unsigned performance_checks()
{
    std::unique_ptr<ROMlib_ppc_system750> owner(new ROMlib_ppc_system750{});
    auto& s = *owner;
    unsigned cases = 0;
    // Independently transcribed MPC750UM chapter 11 select tables, including
    // reserved holes and all out-of-table encodings of each selector width.
    const unsigned table[4][18] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
                                   {0, 1, 2, 3, 4, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
                                   {0, 1, 2, 3, 4, 25, 26, 27, 28, 29, 30, 31, 32, 33, 0, 34, 35, 36},
                                   {0, 1, 2, 3, 4, 37, 38, 0, 39, 40, 41, 42, 43, 44, 45}};
    for (unsigned counter = 0; counter < 4; ++counter)
        for (unsigned select = 0; select < (counter == 0 ? 128u : counter == 1 ? 64u : 32u); ++select)
            for (unsigned event = 1; event <= 45; ++event) {
                memset(&s, 0, sizeof s);
                s.mmcr0 = counter == 0 ? select << 6 : counter == 1 ? select : 0;
                s.mmcr1 = counter == 2 ? select << 27 : counter == 3 ? select << 22 : 0;
                assert(ROMlib_ppc_750_performance_event(&s, event, 5, 0x1234));
                for (unsigned i = 0; i < 4; ++i)
                    assert(s.pmc[i] == (i == counter && select < 18 && table[counter][select] == event
                                            ? (event == ROMLIB_PPC_750_PME_LOAD_LATENCY ? 1u : 5u)
                                            : 0u));
                ++cases;
            }
    // All combinations of freeze controls in supervisor/user and marked modes.
    for (unsigned controls = 0; controls < 32; ++controls)
        for (unsigned mode = 0; mode < 4; ++mode) {
            memset(&s, 0, sizeof s);
            s.mmcr0 = controls << 27 | 65;
            s.mmcr1 = 1u << 27 | 1u << 22;
            s.mmu.msr = (mode & 1 ? 0x4000 : 0) | (mode & 2 ? 4 : 0);
            ROMlib_ppc_750_advance_core(&s, 7);
            bool hold = (controls & 16) || ((controls & 8) && !(mode & 1)) || ((controls & 4) && (mode & 1)) ||
                        ((controls & 2) && (mode & 2)) || ((controls & 1) && !(mode & 2));
            for (auto pmc : s.pmc)
                assert(pmc == (hold ? 0u : 7u));
            ++cases;
        }
    for (unsigned threshold = 0; threshold < 64; ++threshold)
        for (unsigned latency = 0; latency < 66; ++latency) {
            memset(&s, 0, sizeof s);
            s.mmcr0 = threshold << 16 | 10 << 6;
            ROMlib_ppc_750_performance_event(&s, ROMLIB_PPC_750_PME_LOAD_LATENCY, latency, 0x1000);
            assert(s.pmc[0] == unsigned(latency > threshold));
            ++cases;
        }
    // Trigger excludes the sign-changing event from secondary counters. Freeze
    // requires ENINT; it neither sets DIS nor clears TRIGGER on the 750.
    for (unsigned enabled = 0; enabled < 2; ++enabled)
        for (unsigned other = 0; other < 2; ++other)
            for (unsigned freeze = 0; freeze < 2; ++freeze)
                for (unsigned trigger = 0; trigger < 2; ++trigger) {
                    memset(&s, 0, sizeof s);
                    s.mmcr0 =
                        enabled * 0x04000000 | freeze * 0x02000000 | 0x8000 | other * 0x4000 | trigger * 0x2000 | 65;
                    s.mmcr1 = 1u << 27 | 1u << 22;
                    s.pmc[0] = 0x7FFFFFFE;
                    ROMlib_ppc_750_advance_core(&s, 10);
                    assert(s.pmc[0] == (enabled && freeze ? 0x80000000u : 0x80000008u));
                    for (unsigned i = 1; i < 4; ++i)
                        assert(s.pmc[i] == (enabled && freeze && !other ? (trigger ? 0u : 2u) : (trigger ? 8u : 10u)));
                    assert(bool(s.performance_pending) == bool(enabled));
                    assert(!(s.mmcr0 & 0x84000000) && bool(s.mmcr0 & 0x2000) == bool(trigger));
                    if (enabled) {
                        ROMlib_ppc_registers r = {};
                        assert(s.sia == 0 && !ROMlib_ppc_750_interrupt(&s, &r));
                        r.pc = 0x5678;
                        s.mmu.msr = 0x8000;
                        assert(ROMlib_ppc_750_interrupt(&s, &r) == 1 && r.pc == 0xF00 && s.srr0 == 0x5678);
                        assert(!s.performance_pending);
                    }
                    ++cases;
                }
    // Bulk input must agree with small advances at sign changes, wraparound,
    // TB edges and internal freezes, including UINT64_MAX (bounded execution).
    for (unsigned tb = 0; tb < 4; ++tb)
        for (unsigned mode = 0; mode < 16; ++mode)
            for (uint64_t ticks : {UINT64_C(0), UINT64_C(1), UINT64_C(200000), UINT64_MAX}) {
                memset(&s, 0, sizeof s);
                s.mmcr0 = tb << 23 | ((mode & 1) ? 0x04000000 : 0) | ((mode & 2) ? 0x02000000 : 0) |
                          ((mode & 4) ? 0x00400000 : 0) | ((mode & 8) ? 0x2000 : 0) | 0x8000 | (3 << 6) | 3;
                s.mmcr1 = 3u << 27 | 3u << 22;
                s.pmc[0] = 0x7FFFFFFE;
                s.performance_last_pc = 0x1234;
                std::unique_ptr<ROMlib_ppc_system750> split(new ROMlib_ppc_system750(s));
                ROMlib_ppc_750_advance(&s, ticks);
                ROMlib_ppc_750_advance(split.get(), ticks / 3);
                ROMlib_ppc_750_advance(split.get(), ticks - ticks / 3);
                assert(!memcmp(&s, split.get(), sizeof s));
                ++cases;
            }
    for (unsigned tb = 0; tb < 4; ++tb) {
        memset(&s, 0, sizeof s);
        unsigned bit = tb == 0 ? 0 : tb == 1 ? 8 : tb == 2 ? 12 : 16;
        s.mmcr0 = tb << 23 | (3 << 6);
        ROMlib_ppc_750_advance(&s, (UINT64_C(1) << bit) - 1);
        assert(!s.pmc[0]);
        ROMlib_ppc_750_advance(&s, 1);
        assert(s.pmc[0] == 1);
        ROMlib_ppc_750_set_time_base(&s, 0);
        ROMlib_ppc_750_set_time_base(&s, UINT64_C(1) << bit);
        assert(s.pmc[0] == 2);
        ++cases;
    }
    memset(&s, 0, sizeof s);
    s.mmcr0 = 0x04000000 | 0x8000;
    assert(ROMlib_ppc_750_performance_write(&s, 953, 0x80000000, 0x7770));
    assert(s.performance_pending && s.sia == 0x7770 && !(s.mmcr0 & 0x04000000));
    assert(ROMlib_ppc_750_performance_write(&s, 956, UINT32_MAX, 0));
    assert(s.mmcr1 == 0xFFC00000);
    assert(!ROMlib_ppc_750_performance_write(&s, 951, 42, 0));
    assert(!ROMlib_ppc_750_performance_event(&s, 46, 1, 0));
    assert(!ROMlib_ppc_750_performance_event(nullptr, 1, 1, 0));
    ++cases;
    memset(&s, 0, sizeof s);
    s.mmcr0 = 9;                   // PMC2 privilege transitions.
    s.mmcr1 = 9u << 27 | 9u << 22; // User/supervisor marked transitions.
    ROMlib_ppc_750_set_msr(&s, 0x4004, 0x1000);
    ROMlib_ppc_750_set_msr(&s, 0, 0x1004);
    assert(s.pmc[1] == 2 && s.pmc[2] == 1 && s.pmc[3] == 1);
    ++cases;
    return cases;
}

} // namespace

void clock750_checks()
{
    unsigned cases = thermal_checks() + performance_checks();
    for (uint32_t dec : {0u, 1u, 123u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu})
        for (uint64_t ticks : {UINT64_C(0), UINT64_C(1), UINT64_C(2), UINT64_C(0x7FFFFFFF), UINT64_C(0x80000000),
                               UINT64_C(0xFFFFFFFF), UINT64_C(0x100000000), UINT64_MAX}) {
            ROMlib_ppc_system750 state = {};
            state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
            state.time_base = UINT64_MAX - 2;
            state.decrementer = dec;
            ROMlib_ppc_750_advance(&state, ticks);
            assert(state.time_base == UINT64_MAX - 2 + ticks);
            assert(state.decrementer == dec - uint32_t(ticks));
            assert(state.decrementer_pending == unsigned(ticks >= uint64_t(dec) + 1));
            ROMlib_ppc_750_advance(&state, UINT64_C(0x100000000));
            assert(state.decrementer_pending);
            ++cases;
        }
    for (unsigned selector = 0; selector < 4; ++selector)
        for (unsigned power : {ROMLIB_PPC_750_RUN, ROMLIB_PPC_750_DOZE, ROMLIB_PPC_750_NAP})
            for (uint64_t ticks : {UINT64_C(1), UINT64_C(0x100000000), UINT64_MAX}) {
                const unsigned bits[] = {0, 8, 12, 16};
                const uint64_t before_edge = (UINT64_C(1) << bits[selector]) - 1;
                ROMlib_ppc_system750 state = {};
                ROMlib_ppc_registers r = {};
                state.time_base = before_edge;
                state.power_mode = power;
                state.mmu.msr = 0x8000;
                state.mmcr0 = selector << 23 | (3 << 6) | 0x04400000;
                ROMlib_ppc_750_set_tben(&state, 0);
                ROMlib_ppc_750_advance(&state, ticks);
                assert(state.time_base == before_edge && !state.pmc[0] && !state.performance_pending);
                assert(state.decrementer == 0u - uint32_t(ticks) && state.decrementer_pending);
                assert(ROMlib_ppc_750_interrupt(&state, &r) == 1 && r.pc == 0x900);
                assert(!state.decrementer_pending && state.power_mode == ROMLIB_PPC_750_RUN);
                ROMlib_ppc_750_advance(&state, 1);
                assert(state.time_base == before_edge);
                ROMlib_ppc_750_set_tben(&state, 1);
                ROMlib_ppc_750_advance(&state, 1);
                assert(state.time_base == before_edge + 1 && state.pmc[0] == 1 && state.performance_pending);
                ++cases;
            }
    for (uint32_t old : {0u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu})
        for (uint32_t value : {0u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu})
            for (unsigned pending : {0u, 1u}) {
                ROMlib_ppc_system750 state = {};
                state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
                state.decrementer = old;
                state.decrementer_pending = pending;
                ROMlib_ppc_750_set_decrementer(&state, value);
                assert(state.decrementer == value);
                assert(state.decrementer_pending == (pending || (!(old >> 31) && (value >> 31))));
                ++cases;
            }
    for (unsigned inputs = 0; inputs < 32; ++inputs)
        for (unsigned ee : {0u, 1u})
            for (unsigned me : {0u, 1u})
                for (unsigned dec : {0u, 1u}) {
                    ROMlib_ppc_system750 state = {};
                    state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
                    ROMlib_ppc_registers registers = {};
                    registers.pc = 0x12345678;
                    registers.gpr[3] = 0xABCDEF01;
                    state.mmu.msr = ee * 0x8000 | me * 0x1000;
                    state.decrementer_pending = dec;
                    ROMlib_ppc_750_set_inputs(&state, inputs);
                    unsigned vector = 0;
                    if (inputs & 1)
                        vector = 0x200;
                    else if (ee) {
                        if (inputs & 2)
                            vector = 0x1400;
                        else if (inputs & 4)
                            vector = 0x500;
                        else if (inputs & 8)
                            vector = 0xF00;
                        else if (dec)
                            vector = 0x900;
                        else if (inputs & 16)
                            vector = 0x1700;
                    }
                    int result = ROMlib_ppc_750_interrupt(&state, &registers);
                    assert(registers.gpr[3] == 0xABCDEF01);
                    if (vector == 0x200 && !me) {
                        assert(result == -1 && state.checkstop && registers.pc == 0x12345678);
                        assert(ROMlib_ppc_750_interrupt(&state, &registers) == -1);
                    }
                    else if (vector) {
                        assert(result == 1 && registers.pc == vector && state.srr0 == 0x12345678);
                        assert(state.decrementer_pending == (dec && vector != 0x900));
                        assert(!(state.mmu.msr & 0x8000));
                    }
                    else {
                        assert(!result && registers.pc == 0x12345678 && state.decrementer_pending == dec);
                    }
                    ++cases;
                }
    {
        ROMlib_ppc_system750 state = {};
        state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
        ROMlib_ppc_registers registers = {};
        state.mmu.msr = 0x1000;
        ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP);
        ROMlib_ppc_750_set_inputs(&state, 0); // A pulse remains latched.
        assert(ROMlib_ppc_750_interrupt(&state, &registers) == 1 && registers.pc == 0x200);
        assert(state.srr1 == 0x81000 && !state.machine_check_pending);
        assert(!ROMlib_ppc_750_interrupt(&state, &registers));
        state.mmu.msr = 0x1000;
        ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP);
        assert(ROMlib_ppc_750_interrupt(&state, &registers) == 1);
        state.mmu.msr = 0x1000;
        ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP); // Held assertion is not another edge.
        assert(!ROMlib_ppc_750_interrupt(&state, &registers));
        ++cases;
    }
    {
        ROMlib_ppc_system750 state = {};
        state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
        ROMlib_ppc_registers registers = {};
        state.decrementer = 0;
        ROMlib_ppc_750_advance(&state, 1);
        ROMlib_ppc_750_set_decrementer(&state, 123); // Reprogramming does not cancel a pending event.
        assert(state.decrementer_pending && !ROMlib_ppc_750_interrupt(&state, &registers));
        state.mmu.msr = 0x8000;
        assert(ROMlib_ppc_750_interrupt(&state, &registers) == 1 && registers.pc == 0x900);
        assert(!state.decrementer_pending);
        ++cases;
    }
    for (bool me : {false, true})
        for (bool pulse : {false, true}) {
            ROMlib_ppc_system750 state = {};
            ROMlib_ppc_registers registers = {};
            state.mmu.msr = me ? 0x1000 : 0;
            ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP);
            if (pulse)
                ROMlib_ppc_750_set_inputs(&state, 0);
            assert(!state.machine_check_pending && !ROMlib_ppc_750_interrupt(&state, &registers));
            assert(!state.checkstop);
            ROMlib_ppc_750_set_hid0(&state, ROMLIB_PPC_750_HID0_EMCP);
            assert(!ROMlib_ppc_750_interrupt(&state, &registers));
            ROMlib_ppc_750_set_inputs(&state, 0);
            ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP);
            assert(state.machine_check_pending);
            assert(ROMlib_ppc_750_interrupt(&state, &registers) == (me ? 1 : -1));
            assert(state.checkstop == unsigned(!me));
            ++cases;
        }

    for (bool direct : {false, true}) {
        ROMlib_ppc_system750 state = {};
        ROMlib_ppc_registers registers = {};
        state.mmu.msr = 0x1000;
        state.hid0 = ROMLIB_PPC_750_HID0_EMCP;
        ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_MCP);
        state.bus_check_pending = 1;
        state.bus_check_cause = 0x20000;
        int result = direct ? ROMlib_ppc_750_exception(&state, &registers, 0x200, 0x40000, 0)
                            : ROMlib_ppc_750_interrupt(&state, &registers);
        assert(result == 1 && state.srr1 == (direct ? 0xE1000u : 0xA1000u));
        assert(!state.machine_check_pending && !state.bus_check_pending && !state.bus_check_cause);
        assert(!ROMlib_ppc_750_interrupt(&state, &registers));
        ++cases;
    }
    {
        ROMlib_ppc_system750 state = {};
        ROMlib_ppc_registers registers = {};
        registers.pc = 0x1234;
        state.bus_check_pending = 1; // A zero cause is still an unmaskable bus error.
        assert(ROMlib_ppc_750_interrupt(&state, &registers) == -1);
        assert(state.checkstop && registers.pc == 0x1234 && state.bus_check_pending);
        ++cases;
    }
    for (unsigned controls = 0; controls < 8; ++controls)
        for (unsigned lanes : {4u, 8u})
            for (unsigned lane = 0; lane < lanes; ++lane)
                for (bool enabled : {false, true}) {
                    ROMlib_ppc_system750 state = {};
                    ROMlib_ppc_registers regs = {};
                    ROMlib_ppc_750_set_hid0(&state, controls << 28);
                    state.mmu.msr = enabled ? 0x1000 : 0;
                    uint64_t value = UINT64_C(1) << (8 * lane + 7);
                    unsigned expected = ((1u << lanes) - 1) ^ (1u << lane);
                    assert(ROMlib_ppc_750_drive_parity(&state, lanes, value) == (controls & 4 ? 0 : expected));
                    ROMlib_ppc_750_sample_parity(&state, lanes, value, expected);
                    assert(!state.bus_check_pending);
                    ROMlib_ppc_750_sample_parity(&state, lanes, value, expected ^ (1u << lane));
                    bool checked = controls & (lanes == 4 ? 2 : 1);
                    assert(ROMlib_ppc_750_interrupt(&state, &regs) == (!checked ? 0 : enabled ? 1 : -1));
                    if (checked && enabled)
                        assert(regs.pc == 0x200 && state.srr1 == (lanes == 4 ? 0x11000u : 0x21000u));
                    ++cases;
                }
    for (unsigned selector : {0u, 0x100000u, 0x200000u, 0x400000u, 0x800000u})
        for (bool early_ack : {false, true}) {
            ROMlib_ppc_system750 state = {};
            ROMlib_ppc_registers regs = {};
            ROMlib_ppc_750_set_hid0(&state, selector);
            ROMlib_ppc_750_set_qack(&state, early_ack);
            ROMlib_ppc_750_set_msr(&state, 0x48000, 0);
            bool request = selector == 0x200000 || selector == 0x400000;
            assert(bool(ROMlib_ppc_750_qreq(&state)) == request);
            ROMlib_ppc_750_advance(&state, 10);
            ROMlib_ppc_750_advance_core(&state, 100);
            ROMlib_ppc_750_set_qack(&state, 1);
            ROMlib_ppc_750_advance(&state, 20);
            ROMlib_ppc_750_advance_core(&state, 200);
            uint64_t ticks = selector != 0x200000 ? 30 : early_ack ? 0 : 10;
            assert(state.time_base == ticks && state.decrementer == 0u - uint32_t(ticks));
            assert(state.core_cycles == ticks * 10 && bool(state.decrementer_pending) == bool(ticks));
            ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_SMI);
            regs.pc = 0x1004;
            assert(ROMlib_ppc_750_interrupt(&state, &regs) == 1 && regs.pc == 0x1400);
            assert(!state.power_mode && !ROMlib_ppc_750_qreq(&state) && !(state.mmu.msr & 0x40000));
            ROMlib_ppc_750_advance(&state, 1);
            assert(state.time_base == ticks + 1);
            ++cases;
        }
    for (bool prefix : {false, true})
        for (bool machine : {false, true})
            for (bool ee : {false, true}) {
                ROMlib_ppc_system750 state = {};
                ROMlib_ppc_registers regs = {};
                ROMlib_ppc_750_set_hid0(&state, 0x80200000);
                ROMlib_ppc_750_set_qack(&state, 1);
                ROMlib_ppc_750_set_msr(&state, 0x41000 | (prefix ? 0x40 : 0) | (ee ? 0x8000 : 0), 0);
                regs.pc = 0x1234;
                state.sprg[0] = 0xCAFE;
                state.cache[1].lines[0][0].state = 2;
                unsigned inputs = ROMLIB_PPC_750_SRESET | ROMLIB_PPC_750_INT | (machine ? ROMLIB_PPC_750_MCP : 0);
                ROMlib_ppc_750_set_inputs(&state, inputs);
                ROMlib_ppc_750_set_inputs(&state, 0); // Both edge-triggered inputs survive deassertion.
                uint32_t base = prefix ? 0xFFF00000 : 0;
                if (machine) {
                    assert(ROMlib_ppc_750_interrupt(&state, &regs) == 1 && regs.pc == base + 0x200);
                    assert(state.soft_reset_pending);
                }
                assert(ROMlib_ppc_750_interrupt(&state, &regs) == 1 && regs.pc == base + 0x100);
                assert(state.srr0 == (machine ? base + 0x200 : 0x1234));
                assert(!state.soft_reset_pending && !state.power_mode && state.sprg[0] == 0xCAFE);
                assert(state.cache[1].lines[0][0].state == 2);
                ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_SRESET);
                assert(ROMlib_ppc_750_interrupt(&state, &regs) == 1);
                ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_SRESET);
                assert(!ROMlib_ppc_750_interrupt(&state, &regs)); // Held low is not another edge.
                ++cases;
            }
    for (bool enabled : {false, true}) {
        ROMlib_ppc_system750 state = {};
        ROMlib_ppc_registers regs = {};
        ROMlib_ppc_750_set_hid0(&state, 0x80400000);
        ROMlib_ppc_750_set_qack(&state, 1);
        ROMlib_ppc_750_set_msr(&state, enabled ? 0x49000 : 0x40000, 0);
        ROMlib_ppc_750_set_l2cr(&state, 0x00100000);
        assert(ROMlib_ppc_750_qreq(&state) && ROMlib_ppc_750_l2zz(&state));
        ROMlib_ppc_750_set_inputs(&state, ROMLIB_PPC_750_CKSTP_IN | ROMLIB_PPC_750_SRESET | ROMLIB_PPC_750_MCP);
        assert(state.checkstop && !ROMlib_ppc_750_qreq(&state) && !ROMlib_ppc_750_l2zz(&state));
        ROMlib_ppc_750_set_inputs(&state, 0);
        ROMlib_ppc_750_advance(&state, 100);
        ROMlib_ppc_750_advance_core(&state, 100);
        assert(!state.time_base && !state.core_cycles && ROMlib_ppc_750_interrupt(&state, &regs) == -1);
        ROMlib_ppc_750_reset(&state, &regs);
        assert(!state.checkstop && regs.pc == 0xFFF00100);
        ++cases;
    }
    printf("%u PowerPC 750 clock/decrementer/thermal/performance/input priority cases passed\n", cases);
}
