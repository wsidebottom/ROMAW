/*+===================================================================
  File:      system750.cpp

  Summary:   Implement 750 reset, exception/return state, clocks, HID/L2 controls, thermal sampling and performance monitoring.

  Classes:   Uses ROMlib_ppc_system750 and ROMlib_ppc_registers.

  Functions: ROMlib_ppc_750_reset, ROMlib_ppc_750_exception, ROMlib_ppc_750_rfi, ROMlib_ppc_750_advance,
             ROMlib_ppc_750_set_decrementer, ROMlib_ppc_750_set_tben, ROMlib_ppc_750_set_inputs, ROMlib_ppc_750_interrupt,
             ROMlib_ppc_750_set_hid0,
             ROMlib_ppc_750_set_pll, ROMlib_ppc_750_set_temperature, ROMlib_ppc_750_set_thermal,
             ROMlib_ppc_750_advance_core, ROMlib_ppc_750_performance_write, ROMlib_ppc_750_performance_event,
             ROMlib_ppc_750_performance_complete, ROMlib_ppc_750_set_msr, ROMlib_ppc_750_set_time_base,
             ROMlib_ppc_750_write_spr, ROMlib_ppc_750_set_l2, ROMlib_ppc_750_set_l2cr, ROMlib_ppc_750_drive_parity, ROMlib_ppc_750_sample_parity,
             ROMlib_ppc_750_qreq, ROMlib_ppc_750_l2zz, ROMlib_ppc_750_set_qack, ROMlib_ppc_750_advance_bus

  Origin:    MPC750UM 2.1.6, 3.4, 4.3, 4.5 and chapters 9/10/11; AN1800 calibration; MPCFPE RFI/exception definitions.
             Architectural reference links are recorded in docs/750-status.md.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/system750.h"
#include <cstring>

namespace {
constexpr uint32_t msr_pr = 0x4000, msr_me = 0x1000, msr_ip = 0x40, msr_ile = 0x10000;
constexpr uint32_t exception_clear = 0x0004EF37; // POW/EE/PR/FP/FE/SE/BE/IR/DR/PM/RI/LE.

constexpr uint32_t pm_enable = 0x04000000, pm_discount = 0x02000000, pm_tb_interrupt = 0x00400000;
constexpr uint32_t pm_pmc1_interrupt = 0x8000, pm_other_interrupt = 0x4000, pm_trigger = 0x2000;

unsigned performance_selection(const ROMlib_ppc_system750& s, unsigned index)
{
    return index == 0   ? (s.mmcr0 >> 6) & 127
           : index == 1 ? s.mmcr0 & 63
           : index == 2 ? s.mmcr1 >> 27
                        : (s.mmcr1 >> 22) & 31;
}

unsigned performance_source(unsigned index, unsigned select)
{
    static const unsigned events[4][18] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
                                           {0, 1, 2, 3, 4, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
                                           {0, 1, 2, 3, 4, 25, 26, 27, 28, 29, 30, 31, 32, 33, 0, 34, 35, 36},
                                           {0, 1, 2, 3, 4, 37, 38, 0, 39, 40, 41, 42, 43, 44, 45}};
    return select < 18 ? events[index][select] : 0;
}

void performance_signal(ROMlib_ppc_system750& s, uint32_t pc)
{
    s.performance_pending = 1;
    s.performance_triggered = 1;
    s.sia = pc;
    if (s.mmcr0 & pm_discount)
        s.performance_frozen |= (s.mmcr0 & pm_other_interrupt) ? 1 : 15;
    s.mmcr0 &= ~pm_enable;
}

void performance_negative(ROMlib_ppc_system750& s, uint32_t pc)
{
    if (s.pmc[0] & 0x80000000)
        s.performance_triggered = 1;
    bool signaled = ((s.mmcr0 & pm_pmc1_interrupt) && (s.pmc[0] & 0x80000000)) ||
                    ((s.mmcr0 & pm_other_interrupt) && ((s.pmc[1] | s.pmc[2] | s.pmc[3]) & 0x80000000));
    if ((s.mmcr0 & pm_enable) && signaled)
        performance_signal(s, pc);
}

bool performance_counting(const ROMlib_ppc_system750& s, uint32_t msr)
{
    uint32_t control = s.mmcr0;
    return !(control & 0x80000000) && !((control & 0x40000000) && !(msr & 0x4000)) &&
           !((control & 0x20000000) && (msr & 0x4000)) && !((control & 0x10000000) && (msr & 4)) &&
           !((control & 0x08000000) && !(msr & 4));
}

void performance_event(ROMlib_ppc_system750& s, unsigned event, uint64_t count, uint32_t pc, uint32_t msr)
{
    if (!count)
        return;
    if (event == ROMLIB_PPC_750_PME_LOAD_LATENCY)
        count = count > ((s.mmcr0 >> 16) & 63) ? 1 : 0;
    if (!count)
        return;
    performance_negative(s, pc);
    if (!performance_counting(s, msr))
        return;
    while (count) {
        unsigned active = 0;
        for (unsigned i = 0; i < 4; ++i)
            if (!(s.performance_frozen & (1u << i)) && (!i || !(s.mmcr0 & pm_trigger) || s.performance_triggered) &&
                performance_source(i, performance_selection(s, i)) == event)
                active |= 1u << i;
        if (!active)
            return;
        uint64_t step = count;
        for (unsigned i = 0; i < 4; ++i) {
            bool trigger = i == 0 && (s.mmcr0 & pm_trigger) && !s.performance_triggered;
            bool interrupt = (s.mmcr0 & pm_enable) && (s.mmcr0 & (i ? pm_other_interrupt : pm_pmc1_interrupt));
            if ((active & (1u << i)) && !(s.pmc[i] & 0x80000000) && (trigger || interrupt)) {
                uint64_t distance = uint64_t(0x80000000u) - s.pmc[i];
                if (distance < step)
                    step = distance;
            }
        }
        for (unsigned i = 0; i < 4; ++i)
            if (active & (1u << i))
                s.pmc[i] += uint32_t(step);
        count -= step;
        performance_negative(s, pc);
    }
}

unsigned time_base_bit(const ROMlib_ppc_system750& s)
{
    // MMCR0 field descriptions (tables 2-7/11-2), also used by Linux's TBSEL.
    // The event tables reverse this order; the control-field definition wins.
    static const unsigned bits[] = {0, 8, 12, 16};
    return bits[(s.mmcr0 >> 23) & 3];
}

void performance_time_base(ROMlib_ppc_system750& s, uint64_t pulses)
{
    if (!pulses)
        return;
    if ((s.mmcr0 & pm_enable) && (s.mmcr0 & pm_tb_interrupt)) {
        // Apply the first edge before its interrupt freezes/starts counters;
        // subsequent edges observe the new state, even in one bulk advance.
        performance_event(s, ROMLIB_PPC_750_PME_TIME_BASE, 1, s.performance_last_pc, s.mmu.msr);
        if (s.mmcr0 & pm_enable)
            performance_signal(s, s.performance_last_pc);
        --pulses;
    }
    performance_event(s, ROMLIB_PPC_750_PME_TIME_BASE, pulses, s.performance_last_pc, s.mmu.msr);
}

bool valid_vector(unsigned vector)
{
    switch (vector) {
    case 0x100:
    case 0x200:
    case 0x300:
    case 0x400:
    case 0x500:
    case 0x600:
    case 0x700:
    case 0x800:
    case 0x900:
    case 0xC00:
    case 0xD00:
    case 0xF00:
    case 0x1300:
    case 0x1400:
    case 0x1700:
        return true;
    default:
        return false;
    }
}
}

extern "C" void ROMlib_ppc_750_reset(ROMlib_ppc_system750* state, ROMlib_ppc_registers* registers)
{
    if (!state || !registers)
        return;
    memset(state, 0, sizeof *state);
    memset(registers, 0, sizeof *registers);
    state->mmu.msr = msr_ip;
    registers->pc = 0xFFF00100;
}

extern "C" int ROMlib_ppc_750_exception(ROMlib_ppc_system750* state, ROMlib_ppc_registers* registers, unsigned vector,
                                        uint32_t cause, uint32_t address)
{
    if (!state || !registers || !valid_vector(vector) || state->checkstop)
        return 0;
    uint32_t old_msr = state->mmu.msr;
    if (vector == 0x200 && !(old_msr & msr_me)) {
        state->checkstop = 1;
        return 0;
    }
    if (vector == 0x100 || vector == 0x200)
        state->tlbsync_wait = 0;
    state->forward_valid = 0;
    state->srr0 = registers->pc + (vector == 0xC00 ? 4 : 0);
    if (vector == 0x200)
        cause |= state->bus_check_cause | (state->machine_check_pending ? 0x80000 : 0);
    state->srr1 = old_msr & ROMLIB_PPC_750_SRR1_MSR_MASK;
    if (vector == 0x200)
        state->srr1 = (old_msr & 0xFFFF) | (cause & 0x001F0000);
    else if (vector == 0x400)
        state->srr1 |= cause & 0x58000000;
    else if (vector == 0x700)
        state->srr1 |= cause & 0x001F0000;
    if (vector == 0x300 || vector == 0x600) {
        state->dar = address;
        state->dsisr = cause;
    }
    if (vector == 0xF00)
        state->performance_pending = 0;
    ROMlib_ppc_750_set_msr(state, (old_msr & ~exception_clear) | ((old_msr & msr_ile) >> 16),
                           state->performance_last_pc);
    if (vector == 0x200)
        state->mmu.msr &= ~msr_me;
    if (vector == 0x200) {
        state->machine_check_pending = 0;
        state->bus_check_pending = 0;
        state->bus_check_cause = 0;
    }
    if (vector == 0x1700)
        state->thermal_pending = 0;
    state->iabr_pending = 0;
    registers->pc = (old_msr & msr_ip ? 0xFFF00000 : 0) | vector;
    return 1;
}

extern "C" int ROMlib_ppc_750_rfi(ROMlib_ppc_system750* state, ROMlib_ppc_registers* registers)
{
    if (!state || !registers || state->checkstop)
        return 0;
    if (state->mmu.msr & msr_pr)
        return ROMlib_ppc_750_exception(state, registers, 0x700, 0x00040000, 0);
    uint32_t value = (state->mmu.msr & ~ROMLIB_PPC_750_SRR1_MSR_MASK) | (state->srr1 & ROMLIB_PPC_750_SRR1_MSR_MASK);
    ROMlib_ppc_750_set_msr(state, value, registers->pc);
    state->iabr_pending = 0;
    registers->pc = state->srr0 & ~3u;
    return 1;
}

extern "C" void ROMlib_ppc_750_advance(ROMlib_ppc_system750* state, uint64_t ticks)
{
    if (!state || state->checkstop || state->power_mode == ROMLIB_PPC_750_SLEEP)
        return;
    if (!state->tben_disabled) {
        unsigned bit = time_base_bit(*state);
        uint64_t period = UINT64_C(1) << (bit + 1);
        uint64_t first = ((UINT64_C(1) << bit) - (state->time_base & (period - 1))) & (period - 1);
        if (!first)
            first = period;
        performance_time_base(*state, ticks >= first ? 1 + (ticks - first) / period : 0);
        state->time_base += ticks;
    }
    // Count to the next unsigned zero -> FFFFFFFF transition. This remains
    // correct for initially negative DEC and for ticks spanning many wraps.
    if (ticks >= uint64_t(state->decrementer) + 1)
        state->decrementer_pending = 1;
    state->decrementer -= uint32_t(ticks);
}

extern "C" void ROMlib_ppc_750_set_tben(ROMlib_ppc_system750* state, int asserted)
{
    // MPC750UM 7.2.9.7.4 defines a count enable for the time base.
    if (state)
        state->tben_disabled = !asserted;
}

extern "C" void ROMlib_ppc_750_set_decrementer(ROMlib_ppc_system750* state, uint32_t value)
{
    if (!state || state->checkstop)
        return;
    if (!(state->decrementer & 0x80000000) && (value & 0x80000000))
        state->decrementer_pending = 1;
    state->decrementer = value;
}

extern "C" void ROMlib_ppc_750_set_inputs(ROMlib_ppc_system750* state, uint32_t inputs)
{
    if (!state || state->checkstop)
        return;
    inputs &= 255;
    if (inputs & ROMLIB_PPC_750_CKSTP_IN) {
        state->inputs = inputs;
        state->checkstop = 1;
        return;
    }
    if (!(inputs & ROMLIB_PPC_750_TLBISYNC))
        state->tlbsync_wait = 0;
    if (inputs & ~state->inputs & ROMLIB_PPC_750_SRESET)
        state->soft_reset_pending = 1;
    if (state->hid0 & ROMLIB_PPC_750_HID0_EMCP)
        state->machine_check_pending |= (inputs & ~state->inputs) & ROMLIB_PPC_750_MCP;
    state->inputs = inputs;
}

extern "C" void ROMlib_ppc_750_set_hid0(ROMlib_ppc_system750* state, uint32_t value)
{
    if (!state || state->checkstop)
        return;
    value &= 0xFBF1FFED;
    if ((value & 0x8800) == 0x8800) {
        ROMlib_ppc_cache750_reset(&state->cache[0]);
        value &= ~0x800u;
    }
    if ((value & 0x4400) == 0x4400) {
        ROMlib_ppc_cache750_reset(&state->cache[1]);
        value &= ~0x400u;
    }
    state->hid0 = value;
    if (!(value & ROMLIB_PPC_750_HID0_EMCP))
        state->machine_check_pending = 0;
}

extern "C" int ROMlib_ppc_750_interrupt(ROMlib_ppc_system750* state, ROMlib_ppc_registers* registers)
{
    if (!state || !registers)
        return 0;
    if (state->checkstop)
        return -1;
    unsigned vector = 0;
    if (state->machine_check_pending || state->bus_check_pending)
        vector = 0x200;
    else if (state->soft_reset_pending)
        vector = 0x100;
    else if (state->mmu.msr & 0x8000) {
        if (state->inputs & ROMLIB_PPC_750_SMI)
            vector = 0x1400;
        else if (state->inputs & ROMLIB_PPC_750_INT)
            vector = 0x500;
        else if ((state->inputs & ROMLIB_PPC_750_PERFORMANCE) || state->performance_pending)
            vector = 0xF00;
        else if (state->decrementer_pending)
            vector = 0x900;
        else if ((state->inputs & ROMLIB_PPC_750_THERMAL) || state->thermal_pending)
            vector = 0x1700;
    }
    if (!vector)
        return 0;
    uint32_t cause = 0;
    if (vector == 0x200) {
        cause = (state->machine_check_pending ? 0x80000 : 0) | state->bus_check_cause;
        state->machine_check_pending = 0;
    }
    if (!ROMlib_ppc_750_exception(state, registers, vector, cause, 0))
        return -1;
    if (vector == 0x100)
        state->soft_reset_pending = 0;
    if (vector == 0x900)
        state->decrementer_pending = 0;
    return 1;
}

extern "C" int ROMlib_ppc_750_set_pll(ROMlib_ppc_system750* state, unsigned value)
{
    if (!state || state->checkstop || value > 15)
        return 0;
    state->hid1 = uint32_t(value) << 28;
    return 1;
}

extern "C" void ROMlib_ppc_750_set_temperature(ROMlib_ppc_system750* state, int32_t value)
{
    if (state && !state->checkstop)
        state->junction_temperature = value;
}

extern "C" int ROMlib_ppc_750_set_thermal(ROMlib_ppc_system750* state, unsigned index, uint32_t value)
{
    if (!state || state->checkstop || index > 2)
        return 0;
    if (index == 2) {
        // AN1800 supplements MPC750UM with the calibration field at bits 2..6.
        state->thrm[2] = value & 0x3E003FFF;
        state->thrm[0] &= ~0x40000000u;
        state->thrm[1] &= ~0x40000000u;
        state->thermal_halted = state->thermal_pending = 0;
        state->thermal_channel = state->thermal_remaining = 0;
    }
    else {
        state->thrm[index] = (state->thrm[index] & 0x80000000u) | (value & 0x3F800007);
        state->thermal_halted &= ~(1u << index);
        state->thermal_pending &= ~(1u << index);
        if (state->thermal_channel == index)
            state->thermal_remaining = 0;
    }
    return 1;
}

extern "C" void ROMlib_ppc_750_advance_core(ROMlib_ppc_system750* state, uint64_t cycles)
{
    if (!state || state->checkstop || !cycles || state->power_mode == ROMLIB_PPC_750_SLEEP)
        return;
    performance_event(*state, ROMLIB_PPC_750_PME_CYCLES, cycles, state->performance_last_pc, state->mmu.msr);
    state->core_cycles += cycles;
    if (state->l2invalidate_remaining) {
        if (cycles >= state->l2invalidate_remaining) {
            if (!state->l2 || ROMlib_ppc_l2cache750_invalidate(state->l2)) {
                state->l2invalidate_remaining = 0;
                state->l2cr &= ~1u;
            }
            else
                state->l2invalidate_remaining = 1; // Finish after the active callback unwinds.
        }
        else
            state->l2invalidate_remaining -= uint32_t(cycles);
    }
    unsigned samples = 0;
    while (cycles) {
        unsigned active = ((state->thrm[0] & 1) ? 1u : 0u) | ((state->thrm[1] & 1) ? 2u : 0u);
        active &= ~state->thermal_halted & 3;
        if (!(state->thrm[2] & 1) || !active) {
            state->thermal_remaining = 0;
            return;
        }
        unsigned channel = state->thermal_channel & 1;
        if (!(active & (1u << channel))) {
            channel ^= 1;
            state->thermal_remaining = 0;
        }
        state->thermal_channel = channel;
        unsigned period = (state->thrm[2] >> 1) & 8191;
        if (!period)
            period = 1; // Deterministic digital choice below the documented settling-time minimum.
        if (!state->thermal_remaining)
            state->thermal_remaining = period;
        if (cycles < state->thermal_remaining) {
            state->thermal_remaining -= uint32_t(cycles);
            return;
        }
        cycles -= state->thermal_remaining;
        state->thermal_remaining = 0;
        if (samples >= 2) {
            // The sensor is constant within this call, and every continuing
            // channel has been sampled. Skip whole periods without unbounded work.
            uint64_t completed = 1 + cycles / period;
            if (active == 3)
                state->thermal_channel ^= unsigned(completed & 1);
            state->thermal_remaining = period - unsigned(cycles % period);
            return;
        }
        int64_t temperature = state->junction_temperature;
        uint32_t calibration = state->thrm[2];
        int adjustment = ((calibration & 0x10000000) ? 20 : 0) + ((calibration & 0x08000000) ? 8 : 0) +
                         ((calibration & 0x04000000) ? 4 : 0) + ((calibration & 0x02000000) ? 2 : 0);
        temperature += (calibration & 0x20000000) ? adjustment : -adjustment;
        auto& threshold = state->thrm[channel];
        unsigned limit = (threshold >> 23) & 127;
        bool crossed = (threshold & 4) ? temperature < limit : temperature > limit;
        threshold = (threshold & ~0x80000000u) | 0x40000000 | (crossed ? 0x80000000u : 0);
        if (crossed && (threshold & 2)) {
            state->thermal_pending |= 1u << channel;
            state->thermal_halted |= 1u << channel;
        }
        state->thermal_channel ^= 1;
        ++samples;
    }
    // Canonicalize an exact sample boundary so partitioned clock advances
    // retain the same next comparison and remaining interval.
    unsigned active = ((state->thrm[0] & 1) ? 1u : 0u) | ((state->thrm[1] & 1) ? 2u : 0u);
    active &= ~state->thermal_halted & 3;
    if ((state->thrm[2] & 1) && active) {
        if (!(active & (1u << state->thermal_channel)))
            state->thermal_channel ^= 1;
        state->thermal_remaining = (state->thrm[2] >> 1) & 8191;
        if (!state->thermal_remaining)
            state->thermal_remaining = 1;
    }
}

extern "C" int ROMlib_ppc_750_performance_write(ROMlib_ppc_system750* state, unsigned spr, uint32_t value, uint32_t pc)
{
    if (!state || state->checkstop || spr < 952 || spr > 958)
        return 0;
    if (spr == 952) {
        state->mmcr0 = value;
        state->performance_frozen = 0;
        state->performance_triggered = state->performance_pending || (state->pmc[0] >> 31);
    }
    else if (spr == 956)
        state->mmcr1 = value & 0xFFC00000;
    else if (spr == 955) {
        state->sia = value;
        return 1;
    }
    else
        state->pmc[spr < 955 ? spr - 953 : spr - 955] = value;
    performance_negative(*state, pc);
    return 1;
}

extern "C" int ROMlib_ppc_750_performance_event(ROMlib_ppc_system750* state, unsigned event, uint64_t count,
                                                uint32_t pc)
{
    if (!state || state->checkstop || event < 1 || event > ROMLIB_PPC_750_PME_BRANCH_STALL)
        return 0;
    performance_event(*state, event, count, pc, state->mmu.msr);
    return 1;
}

extern "C" void ROMlib_ppc_750_performance_complete(ROMlib_ppc_system750* state, uint32_t pc, uint32_t msr,
                                                    unsigned events)
{
    if (!state || state->checkstop)
        return;
    state->performance_last_pc = pc;
    if (!(state->mmcr0 & 0x1FFF) && !state->mmcr1)
        return;
    performance_event(*state, ROMLIB_PPC_750_PME_COMPLETED, 1, pc, msr);
    static const unsigned sources[] = {
        ROMLIB_PPC_750_PME_INTEGER, ROMLIB_PPC_750_PME_FLOATING, ROMLIB_PPC_750_PME_LOAD_STORE,
        ROMLIB_PPC_750_PME_LWARX,   ROMLIB_PPC_750_PME_STWCX,    ROMLIB_PPC_750_PME_STWCX_SUCCESS,
        ROMLIB_PPC_750_PME_SYNC,    ROMLIB_PPC_750_PME_EIEIO,    ROMLIB_PPC_750_PME_SYSTEM};
    for (unsigned i = 0; i < sizeof sources / sizeof *sources; ++i)
        if (events & (1u << i))
            performance_event(*state, sources[i], 1, pc, msr);
    if (events & ROMLIB_PPC_750_COMPLETE_BRANCH)
        performance_event(*state,
                          (events & ROMLIB_PPC_750_COMPLETE_TAKEN) ? ROMLIB_PPC_750_PME_BRANCH_TAKEN
                                                                   : ROMLIB_PPC_750_PME_BRANCH_NOT_TAKEN,
                          1, pc, msr);
    uint32_t iabr = state->iabr_pending ? state->iabr_previous : state->iabr;
    if ((iabr & ~3u) == pc && (iabr & 1) == ((msr >> 5) & 1))
        performance_event(*state, ROMLIB_PPC_750_PME_IABR_MATCH, 1, pc, msr);
}

extern "C" int ROMlib_ppc_750_qreq(const ROMlib_ppc_system750* state)
{
    return state && !state->checkstop && state->power_mode >= ROMLIB_PPC_750_NAP_REQUEST;
}

extern "C" int ROMlib_ppc_750_l2zz(const ROMlib_ppc_system750* state)
{
    return state && !state->checkstop && (state->l2cr & 0x00100000) &&
           (state->power_mode == ROMLIB_PPC_750_NAP || state->power_mode == ROMLIB_PPC_750_SLEEP);
}

extern "C" void ROMlib_ppc_750_set_qack(ROMlib_ppc_system750* state, int asserted)
{
    if (!state || state->checkstop)
        return;
    if (state->power_mode == ROMLIB_PPC_750_NAP && state->qack && !asserted)
        state->nap_snoop_delay = 8;
    state->qack = !!asserted;
    if (asserted && state->power_mode == ROMLIB_PPC_750_NAP_REQUEST)
        state->power_mode = ROMLIB_PPC_750_NAP;
    if (asserted && state->power_mode == ROMLIB_PPC_750_SLEEP_REQUEST)
        state->power_mode = ROMLIB_PPC_750_SLEEP;
}

extern "C" void ROMlib_ppc_750_advance_bus(ROMlib_ppc_system750* state, uint64_t clocks)
{
    if (state && !state->qack && !state->checkstop)
        state->nap_snoop_delay = clocks >= state->nap_snoop_delay ? 0 : state->nap_snoop_delay - uint32_t(clocks);
}

extern "C" void ROMlib_ppc_750_set_msr(ROMlib_ppc_system750* state, uint32_t value, uint32_t pc)
{
    if (!state || state->checkstop)
        return;
    uint32_t old = state->mmu.msr;
    state->mmu.msr = value;
    if (!(value & 0x40000)) {
        state->power_mode = ROMLIB_PPC_750_RUN;
        state->nap_snoop_delay = 0;
    }
    else if (!(old & 0x40000)) {
        switch (state->hid0 & 0x00E00000) {
        case 0x00800000:
            state->power_mode = ROMLIB_PPC_750_DOZE;
            break;
        case 0x00400000:
            state->power_mode = state->qack ? ROMLIB_PPC_750_NAP : ROMLIB_PPC_750_NAP_REQUEST;
            break;
        case 0x00200000:
            state->power_mode = state->qack ? ROMLIB_PPC_750_SLEEP : ROMLIB_PPC_750_SLEEP_REQUEST;
            break;
        }
    }
    if ((old ^ value) & 0x4000)
        performance_event(*state, ROMLIB_PPC_750_PME_PR_CHANGE, 1, pc, old);
    if ((old ^ value) & 4)
        performance_event(*state,
                          (old & 0x4000) ? ROMLIB_PPC_750_PME_USER_PM_CHANGE : ROMLIB_PPC_750_PME_SUPERVISOR_PM_CHANGE,
                          1, pc, old);
}

extern "C" void ROMlib_ppc_750_set_time_base(ROMlib_ppc_system750* state, uint64_t value)
{
    if (!state || state->checkstop)
        return;
    unsigned bit = time_base_bit(*state);
    bool rise = !(state->time_base & (UINT64_C(1) << bit)) && (value & (UINT64_C(1) << bit));
    state->time_base = value;
    performance_time_base(*state, rise);
}

extern "C" int ROMlib_ppc_750_set_l2(ROMlib_ppc_system750* state, ROMlib_ppc_l2cache750* bank)
{
    if (!state || state->checkstop || (state->l2cr & 0x80000000) || state->l2invalidate_remaining ||
        (state->l2 && state->l2->transactions) || (bank && bank->transactions) || state->cache[0].transactions ||
        state->cache[1].transactions)
        return 0;
    if (bank && !ROMlib_ppc_l2cache750_invalidate(bank))
        return 0;
    state->l2 = bank;
    return 1;
}

extern "C" int ROMlib_ppc_750_set_l2cr(ROMlib_ppc_system750* state, uint32_t value)
{
    if (!state || state->checkstop || ((value & 0x80000000) && !state->l2) || (state->l2 && state->l2->transactions) ||
        state->cache[0].transactions || state->cache[1].transactions)
        return 0;
    uint32_t old = state->l2cr;
    state->l2cr = (value & ~1u) | (state->l2invalidate_remaining ? 1u : 0u);
    if ((value & 0x80200000) == 0x00200000 && !(old & 0x00200000) && !state->l2invalidate_remaining) {
        state->l2invalidate_remaining = 32768;
        state->l2cr |= 1;
    }
    return 1;
}

static unsigned bus_parity(unsigned lanes, uint64_t value)
{
    unsigned parity = 0;
    for (unsigned lane = 0; lane < lanes; ++lane, value >>= 8) {
        unsigned byte = value & 255;
        byte ^= byte >> 4;
        byte ^= byte >> 2;
        byte ^= byte >> 1;
        parity |= ((~byte) & 1u) << lane;
    }
    return parity;
}

extern "C" unsigned ROMlib_ppc_750_drive_parity(const ROMlib_ppc_system750* state, unsigned lanes, uint64_t value)
{
    if (!state || (state->hid0 & ROMLIB_PPC_750_HID0_DBP) ||
        (lanes != ROMLIB_PPC_750_ADDRESS_LANES && lanes != ROMLIB_PPC_750_DATA_LANES))
        return 0;
    return bus_parity(lanes, value);
}

extern "C" void ROMlib_ppc_750_sample_parity(ROMlib_ppc_system750* state, unsigned lanes, uint64_t value,
                                             unsigned parity)
{
    if (!state || state->checkstop)
        return;
    bool address = lanes == ROMLIB_PPC_750_ADDRESS_LANES;
    if ((!address && lanes != ROMLIB_PPC_750_DATA_LANES) ||
        !(state->hid0 & (address ? ROMLIB_PPC_750_HID0_EBA : ROMLIB_PPC_750_HID0_EBD)))
        return;
    if (bus_parity(lanes, value) != (parity & ((1u << lanes) - 1))) {
        state->bus_check_pending = 1;
        state->bus_check_cause |= address ? 0x10000 : 0x20000;
    }
}

extern "C" int ROMlib_ppc_750_write_spr(ROMlib_ppc_system750* state, unsigned number, uint32_t value, uint32_t pc)
{
    if (!state)
        return 0;
    auto& s = *state;
    switch (number) {
    case 952:
    case 953:
    case 954:
    case 955:
    case 956:
    case 957:
    case 958:
        ROMlib_ppc_750_performance_write(&s, number, value, pc);
        break;
    case 22:
        ROMlib_ppc_750_set_decrementer(&s, value);
        break;
    case 1010:
        if (!s.iabr_pending)
            s.iabr_previous = s.iabr;
        s.iabr = value;
        s.iabr_pending = 1;
        break;
    case 1017:
        if (!ROMlib_ppc_750_set_l2cr(&s, value))
            return -1;
        break;
    case 1008:
        ROMlib_ppc_750_set_hid0(&s, value);
        break;
    case 1009:
    case 287:
        // MPC750UM 2.3.2.4.3: supervisor writes to HID1/PVR are no-ops.
        break;
    case 1019:
        s.ictc = value & 0x1FF;
        s.forward_valid = 0;
        break;
    case 1020:
    case 1021:
    case 1022:
        ROMlib_ppc_750_set_thermal(&s, number - 1020, value);
        break;
    case 284:
    case 285:
        if (number == 284)
            ROMlib_ppc_750_set_time_base(&s, (s.time_base & UINT64_C(0xFFFFFFFF00000000)) | value);
        else
            ROMlib_ppc_750_set_time_base(&s, uint64_t(value) << 32 | uint32_t(s.time_base));
        break;
    default:
        return 0;
    }
    return 1;
}
