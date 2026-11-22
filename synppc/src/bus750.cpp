/*+===================================================================
  File:      bus750.cpp

  Summary:   Implement scalar/page/endian, L1 and external-control transactions over the 750 physical bus.

  Classes:   physical_context; uses ROMlib_ppc_system750, ROMlib_ppc_physical_bus and ROMlib_ppc_bus_result.

  Functions: ROMlib_ppc_750_read, ROMlib_ppc_750_write, ROMlib_ppc_750_transfer_bytes, ROMlib_ppc_750_translate, ROMlib_ppc_750_external,
             ROMlib_ppc_750_load_reserve, ROMlib_ppc_750_store_conditional, ROMlib_ppc_750_cache_control,
             ROMlib_ppc_750_snoop, ROMlib_ppc_750_host_sync

  Origin:    Original MPC750 bus implementation; MPCFPE 3.1.4 defines endian lanes and 2.3.16 external control.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/bus750.h"
#include <algorithm>
#include "ppc-physical-bus.h"

namespace {
unsigned cache_control(const ROMlib_ppc_system750& state, bool instruction)
{
    unsigned shift = instruction ? 1 : 0;
    return ((state.hid0 & (0x4000u << shift)) ? ROMLIB_PPC_CACHE750_ENABLE : 0) |
           ((state.hid0 & (0x1000u << shift)) ? ROMLIB_PPC_CACHE750_LOCK : 0) |
           ((!instruction && (state.hid0 & 0x40)) ? ROMLIB_PPC_CACHE750_FLUSH_ASSIST : 0);
}

bool l2_enabled(const ROMlib_ppc_system750& state)
{
    return (state.l2cr & 0x80000001) == 0x80000000;
}

unsigned l2_capacity(const ROMlib_ppc_system750& state)
{
    unsigned size = (state.l2cr >> 28) & 3;
    return size ? 131072u << size : 0;
}

unsigned l2_control(const ROMlib_ppc_system750& state)
{
    return (l2_enabled(state) ? 1u : 0u) | ((state.l2cr & 0x00400000) ? 2u : 0u) |
           ((state.l2cr & 0x00080000) ? 4u : 0u) | ((state.l2cr & 0x00040000) ? 8u : 0u) |
           ((state.l2cr & 0x40000000) ? unsigned(ROMLIB_PPC_L2CACHE750_PARITY) : 0u);
}

struct physical_context {
    const ROMlib_ppc_physical_bus* bus;
    ROMlib_ppc_system750* state;
    uint32_t cause = 0x00040000, physical = 0;
    unsigned completed = 0, fault = 0;
    bool table = false, walking = false, instruction_walk = false, instruction_data = false;
    uint32_t pc = 0;

    // 0 ordinary traffic, 1 guest cache maintenance, 2 cached snoop,
    // 3 uncached snoop, 4 host fence (not a guest performance event).
    unsigned maintenance = 0;
    bool lower_failed = false;
    ROMlib_ppc_l2cache750_result lower_error = {};

    struct observation {
        physical_context* context;
        bool instruction, request;

        static void event(void* opaque, unsigned event, unsigned count)
        {
            auto& o = *static_cast<observation*>(opaque);
            auto& self = *o.context;
            if (self.maintenance == 4)
                return;
            bool snoop = self.maintenance == 2 || self.maintenance == 3;
            if (event == ROMLIB_PPC_L2CACHE750_HIT && !snoop) {
                ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_L2_HIT, count, self.pc);
                if (self.maintenance == 1)
                    ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_CACHE_L2_HIT, count, self.pc);
            }
            if (event == ROMLIB_PPC_L2CACHE750_MISS && o.request && !snoop)
                ROMlib_ppc_750_performance_event(
                    self.state, o.instruction ? ROMLIB_PPC_750_PME_L2I_MISS : ROMLIB_PPC_750_PME_L2D_MISS, count,
                    self.pc);
            if (event == ROMLIB_PPC_L2CACHE750_PUSH) {
                ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_L2_CASTOUT, count, self.pc);
                if (snoop)
                    ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_L2_SNOOP_CASTOUT, count, self.pc);
            }
        }
    };

    bool configured() const
    {
        unsigned clock = (state->l2cr >> 25) & 7;
        unsigned ram = (state->l2cr >> 23) & 3;
        return state->l2 && l2_capacity(*state) && clock && clock != 3 && clock != 7 && ram != 1 &&
               !(ram == 0 && (clock == 1 || clock == 2));
    }

    int lower_transfer(uint32_t address, void* bytes, unsigned size, unsigned critical, unsigned access, unsigned wimg,
                       ROMlib_ppc_l2cache750_result& result)
    {
        if (!configured()) {
            result = {};
            result.physical = address;
            result.fault = ROMLIB_PPC_MMU_CONFIGURATION;
            return 0;
        }
        observation event_context = {this, bool(access & ROMLIB_PPC_MMU_INSTRUCTION), true};
        ROMlib_ppc_l2cache750_observer observer = {&event_context, observation::event};
        unsigned control = l2_control(*state);
        if (maintenance == 4)
            control &= ~ROMLIB_PPC_L2CACHE750_TEST;
        return ROMlib_ppc_l2cache750_transfer_observed(state->l2, bus, l2_capacity(*state), address, bytes, size,
                                                       critical, access, wimg, control, &result, &observer);
    }

    static int scalar(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                      uint32_t* cause)
    {
        auto& self = *static_cast<physical_context*>(opaque);
        if ((access & (ROMLIB_PPC_PHYSICAL_ATOMIC | ROMLIB_PPC_MMU_WRITE)) ==
                (ROMLIB_PPC_PHYSICAL_ATOMIC | ROMLIB_PPC_MMU_WRITE) &&
            (self.state->hid0 & ROMLIB_PPC_750_HID0_DCE) && !(wimg & 4)) {
            for (const auto& line : self.state->cache[1].lines[(address >> 5) & 127])
                if (line.state && line.address == (address & ~31u)) {
                    access |= ROMLIB_PPC_L2CACHE750_L1_HIT;
                    break;
                }
        }
        ROMlib_ppc_l2cache750_result result = {};
        int ok = self.lower_transfer(address, bytes, size, 0, access, wimg, result);
        *cause = result.cause;
        if (!ok) {
            self.lower_failed = true;
            self.lower_error = result;
        }
        return ok;
    }

    static int line(void* opaque, uint32_t address, void* bytes, unsigned critical, unsigned access, unsigned wimg,
                    ROMlib_ppc_cache750_result* output)
    {
        auto& self = *static_cast<physical_context*>(opaque);
        if ((access & ROMLIB_PPC_MMU_WRITE) && (wimg & 8)) {
            if (self.maintenance == 2 || self.maintenance == 3) {
                access |= ROMLIB_PPC_L2CACHE750_SNOOP_PUSH;
                if (self.maintenance == 3)
                    access |= ROMLIB_PPC_L2CACHE750_RETAIN;
            }
            else
                access |= ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH;
        }
        ROMlib_ppc_l2cache750_result result = {};
        int ok = self.lower_transfer(address, bytes, 32, critical, access, wimg, result);
        *output = {};
        output->physical = result.physical;
        output->cause = result.cause;
        output->fault = result.fault;
        return ok;
    }

    static int lower_history(void* opaque, uint32_t address, uint32_t mask, uint32_t* cause)
    {
        auto& self = *static_cast<physical_context*>(opaque);
        ROMlib_ppc_l2cache750_result result = {};
        if (!self.configured()) {
            self.lower_failed = true;
            self.lower_error = {};
            self.lower_error.physical = address + 4;
            self.lower_error.fault = ROMLIB_PPC_MMU_CONFIGURATION;
            *cause = 0;
            return 0;
        }
        observation event_context = {&self, false, true};
        ROMlib_ppc_l2cache750_observer observer = {&event_context, observation::event};
        int ok = ROMlib_ppc_l2cache750_history(self.state->l2, self.bus, l2_capacity(*self.state), address, mask,
                                               l2_control(*self.state), &result, &observer);
        *cause = result.cause;
        if (!ok) {
            self.lower_failed = true;
            self.lower_error = result;
        }
        return ok;
    }

    int block(uint32_t address, unsigned operation, ROMlib_ppc_cache750_result& result)
    {
        lower_failed = false;
        ROMlib_ppc_physical_bus routed = {this, scalar, lower_history};
        ROMlib_ppc_cache750_layer layer = {this, line};
        ROMlib_ppc_cache750_observer observer = {this, observe};
        bool enabled = l2_enabled(*state);
        int ok = ROMlib_ppc_cache750_block_layered(&state->cache[1], enabled ? &routed : bus, address, operation, 0, 0,
                                                   cache_control(*state, false), &result,
                                                   maintenance == 4 ? nullptr : &observer, enabled ? &layer : nullptr);
        if (!ok && lower_failed) {
            result.physical = lower_error.physical;
            result.cause = lower_error.cause;
            result.fault = lower_error.fault;
        }
        return ok;
    }

    int lower_block(uint32_t address, unsigned operation, ROMlib_ppc_l2cache750_result& result)
    {
        if (!configured()) {
            result = {};
            result.physical = address;
            result.fault = ROMLIB_PPC_MMU_CONFIGURATION;
            return 0;
        }
        observation event_context = {this, false, false};
        ROMlib_ppc_l2cache750_observer observer = {&event_context, observation::event};
        return ROMlib_ppc_l2cache750_block_observed(state->l2, bus, l2_capacity(*state), address, operation,
                                                    l2_control(*state), &result, &observer);
    }

    static void observe(void* opaque, unsigned misses, unsigned castouts)
    {
        auto& self = *static_cast<physical_context*>(opaque);
        ROMlib_ppc_750_performance_event(
            self.state, self.instruction_data ? ROMLIB_PPC_750_PME_L1I_MISS : ROMLIB_PPC_750_PME_L1D_MISS, misses,
            self.pc);
        if (self.instruction_data)
            ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_FETCH_L1_MISS, misses, self.pc);
        ROMlib_ppc_750_performance_event(self.state, ROMLIB_PPC_750_PME_L1_CASTOUT, castouts, self.pc);
    }

    int translate(uint32_t address, unsigned access, ROMlib_ppc_mmu_result* result)
    {
        walking = false;
        instruction_walk = access & ROMLIB_PPC_MMU_INSTRUCTION;
        pc = state->performance_current_pc;
        ROMlib_ppc_mmu_bus table_bus = {this, read_pte, history};
        uint64_t before = state->core_cycles;
        int ok = ROMlib_ppc_mmu_translate(&state->mmu, &table_bus, address, access, result);
        // An embedding exception unwinds before search-cycle observation in
        // the checked implementation. A native callback returns an abort flag
        // instead; preserve that same boundary while unwinding native frames.
        if (walking && !(bus && bus->callback_aborted && *bus->callback_aborted))
            ROMlib_ppc_750_performance_event(
                state, instruction_walk ? ROMLIB_PPC_750_PME_ITLB_SEARCH : ROMLIB_PPC_750_PME_DTLB_SEARCH,
                state->core_cycles - before, pc);
        return ok;
    }

    int cached(uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg, bool through = false,
               ROMlib_ppc_cache750_result* capture = nullptr)
    {
        bool instruction = access & ROMLIB_PPC_MMU_INSTRUCTION;
        instruction_data = instruction;
        ROMlib_ppc_cache750_observer observer = {this, observe};
        ROMlib_ppc_cache750_result result = {};
        uint64_t before = state->core_cycles;
        lower_failed = false;
        ROMlib_ppc_physical_bus routed = {this, scalar, lower_history};
        ROMlib_ppc_cache750_layer layer = {this, line};
        bool enabled = l2_enabled(*state);
        int ok = ROMlib_ppc_cache750_transfer_layered(
            &state->cache[instruction ? 0 : 1], enabled ? &routed : bus, address, bytes, size, access, wimg,
            cache_control(*state, instruction) | (through ? ROMLIB_PPC_CACHE750_FORCE_THROUGH : 0), &result, &observer,
            enabled ? &layer : nullptr);
        if (!ok && lower_failed) {
            result.physical = lower_error.physical;
            result.cause = lower_error.cause;
            result.fault = lower_error.fault;
            result.completed += lower_error.completed;
        }
        if (ok && result.misses &&
            !(access & (ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE))) {
            uint64_t cycles = state->core_cycles - before;
            ROMlib_ppc_750_performance_event(state, ROMLIB_PPC_750_PME_LOAD_MISS_CYCLES, cycles, pc);
            // A scalar transaction can span two lines. Aggregate service time
            // is not either miss's latency, so threshold only a single miss.
            if (result.misses == 1)
                ROMlib_ppc_750_performance_event(state, ROMLIB_PPC_750_PME_LOAD_LATENCY, cycles, pc);
        }
        fault = result.fault;
        cause = result.cause;
        physical = result.physical;
        completed = result.completed;
        table = access & ROMLIB_PPC_PHYSICAL_TABLE;
        if (capture)
            *capture = result;
        return ok;
    }

    int transfer(uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg)
    {
        cause = 0x00040000; // TEA transfer-error acknowledge.
        physical = address;
        table = access & ROMLIB_PPC_PHYSICAL_TABLE;
        bool ok = romaw_ppc_physical_transfer(bus, address, bytes, size, access, wimg, &cause);
        if (!ok)
            fault = romaw_ppc_physical_failure(bus, cause);
        return ok;
    }

    static int read_pte(void* opaque, uint32_t address, uint32_t words[2])
    {
        auto& self = *static_cast<physical_context*>(opaque);
        if (!self.walking) {
            self.walking = true;
            ROMlib_ppc_750_performance_event(
                self.state, self.instruction_walk ? ROMLIB_PPC_750_PME_ITLB_MISS : ROMLIB_PPC_750_PME_DTLB_MISS, 1,
                self.pc);
        }
        unsigned char bytes[8];
        if (!self.cached(address, bytes, 8, ROMLIB_PPC_PHYSICAL_TABLE, 2))
            return 0;
        for (unsigned index = 0; index < 2; ++index) {
            const auto* p = bytes + index * 4;
            words[index] = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        }
        return 1;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask)
    {
        auto& self = *static_cast<physical_context*>(opaque);
        self.cause = 0x00040000;
        self.physical = address + 4;
        self.table = true;
        ROMlib_ppc_cache750_result result = {};
        ROMlib_ppc_physical_bus routed = {&self, scalar, lower_history};
        self.lower_failed = false;
        int ok = ROMlib_ppc_cache750_history(&self.state->cache[1], l2_enabled(*self.state) ? &routed : self.bus,
                                             address, mask, cache_control(*self.state, false), &result);
        self.cause = result.cause;
        self.fault = self.lower_failed ? self.lower_error.fault : result.fault;
        self.physical = self.lower_failed ? self.lower_error.physical : result.physical;
        return ok;
    }
};

int failure(ROMlib_ppc_bus_result& result, unsigned fault)
{
    result.mapping.fault = fault;
    if (fault == ROMLIB_PPC_MMU_BUS)
        result.mapping.vector = 0x200;
    return 0;
}

int cache_failure(ROMlib_ppc_system750* state, ROMlib_ppc_bus_result& output, uint32_t address, uint32_t cause,
                  unsigned fault, bool latch)
{
    output.mapping.physical = address;
    output.mapping.cause = cause & 0x001F0000;
    if (latch && fault == ROMLIB_PPC_MMU_BUS) {
        state->bus_check_pending = 1;
        state->bus_check_cause |= output.mapping.cause;
    }
    return failure(output, fault);
}

int transfer(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus, uint32_t address, unsigned size,
             unsigned access, uint64_t& value, ROMlib_ppc_bus_result* output, bool through = false, bool atomic = false)
{
    if (!output)
        return 0;
    *output = {};
    auto& result = *output;
    result.address = address;
    result.size = size;
    result.mapping.address = address;
    result.mapping.bat = -1;
    bool instruction = access & ROMLIB_PPC_MMU_INSTRUCTION;
    bool writing = access & ROMLIB_PPC_MMU_WRITE;
    if (!state || (size != 1 && size != 2 && size != 4 && size != 8) ||
        (access & ~(ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION)) ||
        (instruction && (writing || size != 4 || (address & 3))))
        return failure(result, ROMLIB_PPC_MMU_CONFIGURATION);
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    bool little = state->mmu.msr & 1;
    bool byte_lanes = little && (address & (size - 1));
    uint64_t loaded = 0;
    unsigned char bytes[8] = {};
    if (writing)
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = value >> (8 * (little && byte_lanes ? i : size - i - 1));
    while (result.completed < size) {
        uint32_t logical = address + result.completed;
        if (!context.translate(logical, access, &result.mapping)) {
            if (result.mapping.fault == ROMLIB_PPC_MMU_BUS) {
                result.mapping.cause = context.cause & 0x001F0000;
                result.mapping.physical = context.physical;
                result.table = context.table;
                result.mapping.fault = context.fault;
                if (context.fault != ROMLIB_PPC_MMU_BUS)
                    result.mapping.vector = 0;
            }
            return 0;
        }
        unsigned amount = byte_lanes ? 1 : std::min(size - result.completed, 4096 - (logical & 4095));
        uint32_t physical = result.mapping.physical ^ (little ? byte_lanes ? 7 : 8 - size : 0);
        result.mapping.physical = physical;
        if (!context.cached(physical, bytes + result.completed, amount,
                            access | (atomic ? ROMLIB_PPC_PHYSICAL_ATOMIC : 0), result.mapping.wimg, through)) {
            result.completed += context.completed;
            result.mapping.address = address + result.completed;
            result.table = context.table;
            result.mapping.physical = context.physical;
            result.mapping.cause = context.cause & 0x001F0000;
            return failure(result, context.fault);
        }
        result.completed += amount;
    }
    if (!writing) {
        for (unsigned i = 0; i < size; ++i) {
            if (byte_lanes)
                loaded |= uint64_t(bytes[i]) << (8 * i);
            else
                loaded = loaded << 8 | bytes[i];
        }
        value = loaded;
    }
    return 1;
}
}

extern "C" int ROMlib_ppc_750_read(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus, uint32_t address,
                                   unsigned size, unsigned access, uint64_t* value, ROMlib_ppc_bus_result* result)
{
    if (!value || (access & ROMLIB_PPC_MMU_WRITE)) {
        if (result) {
            *result = {};
            result->mapping.fault = ROMLIB_PPC_MMU_CONFIGURATION;
        }
        return 0;
    }
    return transfer(state, bus, address, size, access, *value, result);
}

extern "C" int ROMlib_ppc_750_transfer_bytes(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                             uint32_t address, void* bytes, uint32_t size, unsigned access,
                                             ROMlib_ppc_bus_result* result)
{
    if (!result)
        return 0;
    *result = {};
    result->address = result->mapping.address = address;
    result->size = size;
    result->mapping.bat = -1;
    if (!state || (size && !bytes) || (access & ~ROMLIB_PPC_MMU_WRITE))
        return failure(*result, ROMLIB_PPC_MMU_CONFIGURATION);
    auto* buffer = static_cast<unsigned char*>(bytes);
    bool little = state->mmu.msr & 1;
    for (uint32_t offset = 0; offset < size;) {
        uint32_t cursor = address + offset;
        // Align grouped transfers to a doubleword before using numeric lanes.
        unsigned width = !(cursor & 7) && size - offset >= 8 ? 8 : 1;
        uint64_t value = 0;
        if (access & ROMLIB_PPC_MMU_WRITE)
            for (unsigned i = 0; i < width; ++i)
                value |= uint64_t(buffer[offset + i]) << (8 * (little ? i : width - 1 - i));
        ROMlib_ppc_bus_result part;
        if (!transfer(state, bus, cursor, width, access, value, &part)) {
            result->mapping = part.mapping;
            result->table = part.table;
            result->completed = offset + ((access & ROMLIB_PPC_MMU_WRITE) ? part.completed : 0);
            return 0;
        }
        if (!(access & ROMLIB_PPC_MMU_WRITE))
            for (unsigned i = 0; i < width; ++i)
                buffer[offset + i] = value >> (8 * (little ? i : width - 1 - i));
        offset += width;
        result->completed = offset;
    }
    return 1;
}

extern "C" int ROMlib_ppc_750_translate(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                        uint32_t address, unsigned access, ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->address = output->mapping.address = address;
    output->mapping.bat = -1;
    if (!state || (access & ~(ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_MMU_DCBZ |
                              ROMLIB_PPC_MMU_PROBE | ROMLIB_PPC_MMU_PREFLIGHT)))
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    int ok = context.translate(address, access, &output->mapping);
    if (!ok && output->mapping.fault == ROMLIB_PPC_MMU_BUS) {
        output->mapping.cause = context.cause & 0x001F0000;
        output->mapping.physical = context.physical;
        output->table = context.table;
        output->mapping.fault = context.fault;
        if (context.fault != ROMLIB_PPC_MMU_BUS)
            output->mapping.vector = 0;
    }
    return ok;
}

extern "C" int ROMlib_ppc_750_write(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus, uint32_t address,
                                    unsigned size, uint64_t value, ROMlib_ppc_bus_result* result)
{
    return transfer(state, bus, address, size, ROMLIB_PPC_MMU_WRITE, value, result);
}

extern "C" int ROMlib_ppc_750_external(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                       uint32_t address, int writing, uint32_t* value, ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->address = output->mapping.address = address;
    output->size = 4;
    output->mapping.bat = -1;
    if (!state || !value || (writing != 0 && writing != 1))
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    if (!(state->ear & 0x80000000)) {
        output->mapping.vector = 0x300;
        output->mapping.cause = 0x00100000 | (writing ? 0x02000000 : 0);
        return failure(*output, ROMLIB_PPC_MMU_EXTERNAL_DISABLED);
    }
    if (address & 3) {
        output->mapping.vector = 0x600;
        return failure(*output, ROMLIB_PPC_MMU_ALIGNMENT);
    }
    int translated = ROMlib_ppc_750_translate(state, bus, address, writing ? ROMLIB_PPC_MMU_WRITE : 0, output);
    output->size = 4;
    if (!translated)
        return 0;
    output->mapping.wimg = 5;
    unsigned char bytes[4] = {};
    if (writing)
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = *value >> (24 - 8 * i);
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    unsigned access = ROMLIB_PPC_PHYSICAL_EXTERNAL | ((state->ear & 15) << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT) |
                      (writing ? ROMLIB_PPC_MMU_WRITE : 0);
    if (!context.transfer(output->mapping.physical, bytes, 4, access, 5)) {
        output->mapping.cause = context.cause & 0x001F0000;
        return failure(*output, context.fault);
    }
    if (!writing)
        *value = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
    output->completed = 4;
    return 1;
}

extern "C" int ROMlib_ppc_750_store_conditional(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                                uint32_t address, uint32_t value, ROMlib_ppc_bus_result* result)
{
    uint64_t scalar = value;
    return transfer(state, bus, address, 4, ROMLIB_PPC_MMU_WRITE, scalar, result, true, true);
}

extern "C" int ROMlib_ppc_750_load_reserve(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                           uint32_t address, uint32_t* value, ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->address = output->mapping.address = address;
    output->size = 4;
    output->mapping.bat = -1;
    if (!state || !value || state->checkstop)
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    state->reservation_valid = 0;
    if (address & 3) {
        output->mapping.vector = 0x600;
        return failure(*output, ROMLIB_PPC_MMU_ALIGNMENT);
    }
    int translated = ROMlib_ppc_750_translate(state, bus, address, 0, output);
    output->size = 4;
    if (!translated)
        return 0;
    state->reservation_address = output->mapping.physical & ~31u;
    state->reservation_valid = 1;
    uint64_t scalar = 0;
    if (!transfer(state, bus, address, 4, 0, scalar, output, false, true)) {
        if (!bus || !bus->callback_aborted || !*bus->callback_aborted)
            state->reservation_valid = 0;
        return 0;
    }
    *value = scalar;
    return 1;
}

extern "C" int ROMlib_ppc_750_cache_control(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                            uint32_t address, unsigned opcode, ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->address = output->mapping.address = address;
    output->mapping.bat = -1;
    if (!state)
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    if (opcode == 982) {
        ROMlib_ppc_cache750_invalidate_set(&state->cache[0], address);
        return 1;
    }
    bool barrier = opcode == 598 || opcode == 854;
    bool hint = opcode == 278 || opcode == 246;
    bool zero = opcode == 1014;
    if (!barrier && !hint && !zero && opcode != 54 && opcode != 86 && opcode != 470)
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    unsigned control = cache_control(*state, false);
    if (hint && (!(control & ROMLIB_PPC_CACHE750_ENABLE) || (control & ROMLIB_PPC_CACHE750_LOCK) ||
                 (state->hid0 & ROMLIB_PPC_750_HID0_NOOPTI)))
        return 1;
    if (!barrier) {
        unsigned access = opcode == 470 ? ROMLIB_PPC_MMU_WRITE : 0;
        if (hint || (zero && !(control & ROMLIB_PPC_CACHE750_ENABLE)))
            access = ROMLIB_PPC_MMU_PROBE;
        else if (zero)
            access = ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_DCBZ;
        int translated = ROMlib_ppc_750_translate(state, bus, address, access, output);
        if (hint || output->mapping.fault == ROMLIB_PPC_MMU_DIRECT_STORE) {
            if (!translated || (hint && (output->mapping.wimg & 5))) {
                output->mapping.fault = output->mapping.vector = output->mapping.cause = 0;
                return 1;
            }
        }
        if (zero && !(control & ROMLIB_PPC_CACHE750_ENABLE)) {
            output->mapping.vector = 0x600;
            output->mapping.cause = 0;
            return failure(*output, ROMLIB_PPC_MMU_ALIGNMENT);
        }
        if (!translated)
            return 0;
    }
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    unsigned broadcast = 0;
    if (zero && (output->mapping.wimg & 2))
        broadcast = ROMLIB_PPC_BUS_DCBZ;
    else if (state->hid0 & ROMLIB_PPC_750_HID0_ABE) {
        if (opcode == 54)
            broadcast = ROMLIB_PPC_BUS_DCBST;
        if (opcode == 86)
            broadcast = ROMLIB_PPC_BUS_DCBF;
        if (opcode == 470)
            broadcast = ROMLIB_PPC_BUS_DCBI;
        if (opcode == 598)
            broadcast = ROMLIB_PPC_BUS_SYNC;
        if (opcode == 854)
            broadcast = ROMLIB_PPC_BUS_EIEIO;
    }
    bool enabled_l2 = l2_enabled(*state);
    if (broadcast && (barrier || !enabled_l2)) {
        uint32_t physical = barrier ? 0 : output->mapping.physical & (zero ? ~7u : ~31u);
        if (!context.transfer(physical, nullptr, 0,
                              ROMLIB_PPC_PHYSICAL_CONTROL | (broadcast << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT),
                              barrier ? 0 : output->mapping.wimg)) {
            output->mapping.cause = context.cause & 0x001F0000;
            output->mapping.physical = context.physical;
            return failure(*output, context.fault);
        }
    }
    if (barrier)
        return 1;
    context.maintenance = 1;
    ROMlib_ppc_cache750_result cached = {};
    int ok;
    if (hint) {
        unsigned char byte;
        ok = context.cached(output->mapping.physical, &byte, 1, 0, output->mapping.wimg, false, &cached);
    }
    else {
        unsigned operation = zero           ? ROMLIB_PPC_CACHE750_ZERO
                             : opcode == 86 ? ROMLIB_PPC_CACHE750_FLUSH
                             : opcode == 54 ? ROMLIB_PPC_CACHE750_STORE
                                            : ROMLIB_PPC_CACHE750_INVALIDATE;
        ok = context.block(output->mapping.physical, operation, cached);
    }
    if (!ok)
        return cache_failure(state, *output, cached.physical, cached.cause, cached.fault, false);
    if (enabled_l2 && !hint) {
        ROMlib_ppc_l2cache750_result lower = {};
        unsigned operation = zero           ? ROMLIB_PPC_CACHE750_ZERO
                             : opcode == 86 ? ROMLIB_PPC_CACHE750_FLUSH
                             : opcode == 54 ? ROMLIB_PPC_CACHE750_STORE
                                            : ROMLIB_PPC_CACHE750_INVALIDATE;
        if (!cached.pushes || zero || opcode == 470) {
            if (!context.lower_block(output->mapping.physical, operation, lower))
                return cache_failure(state, *output, lower.physical, lower.cause, lower.fault, false);
        }
        if (zero) {
            if (lower.hits || (state->l2cr & 0x00040000))
                broadcast = 0;
        }
        else if (opcode != 470 && (cached.pushes || lower.pushes))
            broadcast = 0;
        if (broadcast &&
            !context.transfer(output->mapping.physical & (zero ? ~7u : ~31u), nullptr, 0,
                              ROMLIB_PPC_PHYSICAL_CONTROL | (broadcast << ROMLIB_PPC_PHYSICAL_RESOURCE_SHIFT),
                              output->mapping.wimg))
            return cache_failure(state, *output, context.physical, context.cause, context.fault, false);
    }
    return 1;
}

extern "C" int ROMlib_ppc_750_snoop(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus, uint32_t physical,
                                    unsigned type, int global, int inhibited, ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->address = output->mapping.address = output->mapping.physical = physical;
    output->mapping.bat = -1;
    if (!state || state->checkstop || type > 31 || (global != 0 && global != 1) || (inhibited != 0 && inhibited != 1))
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    if (state->power_mode == ROMLIB_PPC_750_SLEEP ||
        (state->power_mode == ROMLIB_PPC_750_NAP &&
         (state->qack || state->nap_snoop_delay || (l2_enabled(*state) && ROMlib_ppc_750_l2zz(state)))))
        return failure(*output, ROMLIB_PPC_MMU_RETRY);
    bool write = type == 2 || type == 6 || type == 12 || type == 18;
    if (write && (type == 18 || (physical & ~31u) == state->reservation_address))
        state->reservation_valid = 0;
    if (!global || (!(state->hid0 & ROMLIB_PPC_750_HID0_DCE) && !l2_enabled(*state)))
        return 1;
    unsigned operation;
    switch (type) {
    case 6: // Write-with-kill discards even modified data.
        operation = ROMLIB_PPC_CACHE750_INVALIDATE;
        break;
    case 2:  // Write-with-flush.
    case 12: // Kill-block (DCBZ).
    case 18: // Write-with-flush-atomic (STWCX).
    case 14: // Read with intent to modify.
    case 30: // Atomic RWITM.
        operation = ROMLIB_PPC_CACHE750_SNOOP;
        break;
    case 10: // Read.
    case 26: // Atomic read (LWARX).
        operation = inhibited ? ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ : ROMLIB_PPC_CACHE750_SNOOP;
        break;
    case 11: // Read with no intent to cache.
        operation = ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ;
        break;
    default: // Cache controls other than kill, and reserved types, do nothing.
        return 1;
    }
    ROMlib_ppc_750_performance_event(state, ROMLIB_PPC_750_PME_SNOOP, 1, state->performance_current_pc);
    if (ROMlib_ppc_cache750_busy(&state->cache[1], physical) ||
        (l2_enabled(*state) && state->l2 && ROMlib_ppc_l2cache750_busy(state->l2, physical))) {
        ROMlib_ppc_750_performance_event(state, ROMLIB_PPC_750_PME_SNOOP_RETRY, 1, state->performance_current_pc);
        return failure(*output, ROMLIB_PPC_MMU_RETRY);
    }
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    context.maintenance = operation == ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ ? 3 : 2;
    ROMlib_ppc_cache750_result cached = {};
    if ((state->hid0 & ROMLIB_PPC_750_HID0_DCE) && !context.block(physical, operation, cached))
        return cache_failure(state, *output, cached.physical, cached.cause, cached.fault, true);
    if (l2_enabled(*state)) {
        ROMlib_ppc_l2cache750_result lower = {};
        if (!context.lower_block(physical, operation, lower))
            return cache_failure(state, *output, lower.physical, lower.cause, lower.fault, true);
    }
    return 1;
}

extern "C" int ROMlib_ppc_750_host_sync(ROMlib_ppc_system750* state, const ROMlib_ppc_physical_bus* bus,
                                        ROMlib_ppc_bus_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->mapping.bat = -1;
    if (!state || state->checkstop)
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    state->reservation_valid = 0;
    physical_context context;
    context.bus = bus;
    context.state = state;
    context.pc = state->performance_current_pc;
    context.maintenance = 4;
    bool enabled_l2 = l2_enabled(*state);
    if (enabled_l2 && !context.configured())
        return failure(*output, ROMLIB_PPC_MMU_CONFIGURATION);
    if (((state->hid0 & ROMLIB_PPC_750_HID0_DCE) && state->cache[1].transactions) ||
        (enabled_l2 && state->l2->transactions))
        return failure(*output, ROMLIB_PPC_MMU_RETRY);
    if (state->hid0 & ROMLIB_PPC_750_HID0_DCE) {
        for (auto& set : state->cache[1].lines)
            for (auto& line : set) {
                if (!line.state)
                    continue;
                ROMlib_ppc_cache750_result cached = {};
                if (!context.block(line.address, ROMLIB_PPC_CACHE750_FLUSH, cached)) {
                    output->address = output->mapping.address = cached.physical;
                    return cache_failure(state, *output, cached.physical, cached.cause, cached.fault, true);
                }
            }
    }
    if (enabled_l2) {
        unsigned sectors = l2_capacity(*state) == 1048576 ? 4 : 2;
        unsigned sets = l2_capacity(*state) == 262144 ? 2048 : 4096;
        for (unsigned set = 0; set < sets; ++set)
            for (auto& tag : state->l2->tags[set])
                for (unsigned sector = 0; sector < sectors; ++sector) {
                    if (!tag.state[sector])
                        continue;
                    ROMlib_ppc_l2cache750_result lower = {};
                    if (!context.lower_block(tag.address + sector * 32, ROMLIB_PPC_CACHE750_FLUSH, lower)) {
                        output->address = output->mapping.address = lower.physical;
                        return cache_failure(state, *output, lower.physical, lower.cause, lower.fault, true);
                    }
                }
    }
    return 1;
}
