/*+===================================================================
  File:      mapped-cpu.c

  Summary:   Implements mapped cpu support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_mapped_cpu_run, syn68k_mapped_exception_enter

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Logical instruction-stream execution over the explicit 68040 memory bus. */
#include "mapped-cpu.h"
#include "ea.h"
#include "fpu.h"
#include "jit-x64.h"
#include "native040.h"
#include "mapping.h"
#include "trap.h"
#include <string.h>

typedef struct {
    Syn68kMMU040Memory* original;
    unsigned write_attempted;
    /* Separate callback An/null effects from instruction-owned EA/entry changes. */
    unsigned callback_state_changed;
} InstructionBus;

static int instruction_read(void* opaque, uint32 physical, unsigned char* bytes, unsigned size, uint32 attributes,
                            unsigned fc)
{
    InstructionBus* bus = opaque;
    M68kReg before[8];
    unsigned before_null = cpu_state.fpu.null_state;
    memcpy(before, cpu_state.regs + 8, sizeof before);
    int ok = bus->original->read && bus->original->read(bus->original->opaque, physical, bytes, size, attributes, fc);
    bus->callback_state_changed |=
        memcmp(before, cpu_state.regs + 8, sizeof before) != 0 || before_null != cpu_state.fpu.null_state;
    return ok;
}

static int instruction_table_read(void* opaque, uint32 physical, uint32* value)
{
    InstructionBus* bus = opaque;
    M68kReg before[8];
    unsigned before_null = cpu_state.fpu.null_state;
    memcpy(before, cpu_state.regs + 8, sizeof before);
    int ok = bus->original->tables.read32(bus->original->tables.opaque, physical, value);
    bus->callback_state_changed |=
        memcmp(before, cpu_state.regs + 8, sizeof before) != 0 || before_null != cpu_state.fpu.null_state;
    return ok;
}

static int instruction_table_write(void* opaque, uint32 physical, uint32 value, int locked)
{
    InstructionBus* bus = opaque;
    M68kReg before[8];
    unsigned before_null = cpu_state.fpu.null_state;
    memcpy(before, cpu_state.regs + 8, sizeof before);
    int ok = bus->original->tables.update32(bus->original->tables.opaque, physical, value, locked);
    bus->callback_state_changed |=
        memcmp(before, cpu_state.regs + 8, sizeof before) != 0 || before_null != cpu_state.fpu.null_state;
    return ok;
}

static int instruction_write(void* opaque, uint32 physical, const unsigned char* bytes, unsigned size,
                             uint32 attributes, unsigned fc)
{
    InstructionBus* bus = opaque;
    bus->write_attempted = 1;
    return bus->original->write && bus->original->write(bus->original->opaque, physical, bytes, size, attributes, fc);
}

/* Cache fills and pushes can bypass the scalar operand callbacks. Observe
   host state changes at their physical callback boundary too, without
   confusing a victim push with an attempted logical operand store. */
static int instruction_cache_read(void* opaque, uint32 physical, unsigned char* bytes, unsigned size,
                                  const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    InstructionBus* bus = opaque;
    M68kReg before[8];
    unsigned before_null = cpu_state.fpu.null_state;
    memcpy(before, cpu_state.regs + 8, sizeof before);
    const Syn68kCache040Bus* physical_bus = bus->original->cache->bus;
    int ok;
    if (physical_bus && physical_bus->read_cycle)
        ok = physical_bus->read_cycle(physical_bus->opaque, physical, bytes, size, cycle, inhibit);
    else if (physical_bus)
        ok = physical_bus->read && physical_bus->read(physical_bus->opaque, physical, bytes, size, cycle->attributes,
                                                      cycle->fc, cycle->burst, cycle->beat, cycle->way, inhibit);
    else {
        *inhibit = 0;
        ok = bus->original->read &&
             bus->original->read(bus->original->opaque, physical, bytes, size, cycle->attributes, cycle->fc);
    }
    bus->callback_state_changed |=
        memcmp(before, cpu_state.regs + 8, sizeof before) != 0 || before_null != cpu_state.fpu.null_state;
    return ok;
}

static int instruction_cache_write(void* opaque, uint32 physical, const unsigned char* bytes, unsigned size,
                                   const Syn68kCache040Cycle* cycle)
{
    InstructionBus* bus = opaque;
    M68kReg before[8];
    unsigned before_null = cpu_state.fpu.null_state;
    memcpy(before, cpu_state.regs + 8, sizeof before);
    const Syn68kCache040Bus* physical_bus = bus->original->cache->bus;
    int ok;
    if (physical_bus && physical_bus->write_cycle)
        ok = physical_bus->write_cycle(physical_bus->opaque, physical, bytes, size, cycle);
    else if (physical_bus)
        ok = physical_bus->write && physical_bus->write(physical_bus->opaque, physical, bytes, size, cycle->attributes,
                                                        cycle->fc, cycle->burst, cycle->beat, cycle->way);
    else
        ok = bus->original->write &&
             bus->original->write(bus->original->opaque, physical, bytes, size, cycle->attributes, cycle->fc);
    bus->callback_state_changed |=
        memcmp(before, cpu_state.regs + 8, sizeof before) != 0 || before_null != cpu_state.fpu.null_state;
    return ok;
}

typedef struct {
    M68kReg regs[16];
    CCRElement ccr[5];
    Syn68kFPUState fpu;
    uint16 sr;
    uint32 banks[3];
} ReadCheckpoint;

static ReadCheckpoint read_checkpoint(void)
{
    ReadCheckpoint result = {.ccr = {cpu_state.ccnz, cpu_state.ccn, cpu_state.ccc, cpu_state.ccv, cpu_state.ccx},
                             .fpu = cpu_state.fpu,
                             .sr = cpu_state.sr,
                             .banks = {cpu_state.usp, cpu_state.isp, cpu_state.msp}};
    memcpy(result.regs, cpu_state.regs, sizeof result.regs);
    return result;
}

static int unchanged_results(const ReadCheckpoint* before, unsigned opcode, unsigned registers)
{
    ReadCheckpoint after = read_checkpoint();
    /* An FPU instruction becomes non-null before reading its EA/operand.
       This entry transition is retained across a fault; it is not a result
       that prevents restarting an otherwise uncommitted read. Do not mask
       a callback's reverse transition or any register/status/frame change. */
    if ((opcode & 0xfe00) == 0xf200 && before->fpu.null_state == 1 && !after.fpu.null_state)
        after.fpu.null_state = 1;
    /* RTE reads and store abort/replay require all sixteen registers.
       Other pre-execute reads can restore speculative address updates. */
    return before->sr == after.sr && !memcmp(before->regs, after.regs, registers * sizeof before->regs[0]) &&
           !memcmp(before->banks, after.banks, sizeof before->banks) &&
           !memcmp(before->ccr, after.ccr, sizeof before->ccr) && !memcmp(&before->fpu, &after.fpu, sizeof before->fpu);
}

/* Validate the instruction-owned EA update and unchanged source state.
   Numeric offers also retain captured output FPSR and instruction FPIAR;
   a store callback changing them must deny replay without undoing it. */
static int fpu_replay_results(const ReadCheckpoint* before, unsigned opcode, const Syn68kFPUStep* fp)
{
    ReadCheckpoint expected = *before;
    /* Class-0 stores have already cleared null state before writing. Require
       that owned transition; a callback restoring null must deny replay. */
    if ((opcode & ~63u) == 0xf200 && expected.fpu.null_state == 1)
        expected.fpu.null_state = 0;
    if (fp->store_state == 3) {
        if ((opcode & ~63u) != 0xf200 || before->fpu.frame_size || fp->access.size != 2)
            return 0;
        expected.fpu.fpsr = fp->replay_fpsr;
        expected.fpu.fpiar = fp->exception_pc;
    }
    if (!fp->store_ea_size)
        return unchanged_results(&expected, opcode, 16);
    unsigned mode = opcode >> 3 & 7, reg = 8 + (opcode & 7), size = fp->store_ea_size;
    if (mode != 3 && mode != 4)
        return 0;
    if ((opcode & ~63u) == 0xf200) {
        if (size < 12 || size > 96 || size % 12 ||
            (mode == 3 && (size != 12 || fp->access.size != (fp->store_state == 3 ? 2u : 4u))))
            return 0;
    }
    else if ((opcode & ~63u) == 0xf300 && mode == 4) {
        unsigned expected_size = before->fpu.frame_size;
        if (expected_size == 100 && (before->fpu.frame[0x48] & 7) == 5 && (before->fpu.frame[0x49] & 0x80) &&
            (before->fpu.pending_vector == 50 || before->fpu.pending_vector == 52 || before->fpu.pending_vector == 54))
            expected_size -= 48;
        if ((expected_size != 52 && expected_size != 100) || size != expected_size || fp->access.size != 4)
            return 0;
    }
    else
        return 0;
    uint32 original = before->regs[reg].ul.n;
    uint32 ea = original - (mode == 4 ? size : 0);
    expected.regs[reg].ul.n = original + (mode == 3 ? size : -size);
    uint32 offset = fp->access.request - ea;
    return offset < size - 8 && fp->access.size <= size - offset && unchanged_results(&expected, opcode, 16);
}

static int replay_store_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    uint16 ssw;
    uint16 push_ssw;
    int push = syn68k_mmu040_cache_push_ssw(access, &push_ssw);
    return record->vector == 2 && record->format == 7 && record->store_state == 4 &&
           record->store_next == record->exception_pc && record->next == record->exception_pc &&
           record->fault_address == access->logical && !record->store_ccr && !record->store_trace_pc &&
           !(record->exception_pc & 1) && !record->movem_state && !record->fpu_vector && !record->store_tail_valid &&
           !record->store_locked && !record->restartable_read && access->writing && !access->rmw &&
           (access->completed < access->size || push) && (access->size == 2 || access->size == 4) &&
           access->logical == (uint32)(access->request + access->completed) && (access->fc == 1 || access->fc == 5) &&
           syn68k_mmu040_operand_status(access, &ssw) && !(access->bus_error && (ssw & 0x400)) &&
           (push || access->bus_error || ((ssw & 0x400) && (!access->completed || !(access->logical & 0xfff))));
}

/* New line packets retain a scalar failing-word diagnostic for the frame
   interface. Verify its independent physical burst progress and PD snapshot. */
static int move16_cycle_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    const Syn68kCache040Access* cache = &access->cache;
    if (!access->cache_exception || cache->fault != CACHE040_OPERAND_FAULT)
        return 1; /* Legacy uncached diagnostic or an ATC fault. */
    const Syn68kCache040Cycle* cycle = &cache->cycle;
    if (access->cache_table || access->translation.fault != MMU040_OK || !access->bus_error || cache->completed > 12 ||
        (cache->completed & 3) || cycle->traffic != CACHE040_MOVE16 || cycle->kind != CACHE040_OPERAND ||
        cycle->burst != 16 || cycle->beat != cache->completed / 4 || cycle->way > 3 || cycle->locked ||
        cycle->lock_end || cache->interrupt_level || cycle->fc != access->fc ||
        cycle->attributes != access->translation.attributes || cache->bus_address != access->translation.physical ||
        (access->request & 15) != ((record->move16.address + cache->completed) & 15))
        return 0;
    if (access->writing) {
        if (cache->push_size != 16 || cache->push_word != ((cache->bus_address >> 2) & 3) ||
            cache->push_address != ((cache->bus_address & ~15u) | (record->move16.address & 15)))
            return 0;
        for (unsigned byte = 0; byte < 16; ++byte)
            if (cache->push_data[byte] != (unsigned char)(record->move16.data[byte / 4] >> (24 - (byte & 3) * 8)))
                return 0;
    }
    return 1;
}

/* MOVE16's bus callback boundary still reports the failing aligned scalar.
   Its format-7 offer separately retains the original line request and PD0-3. */
static int move16_write_fault(const Syn68kIntegerStep* record, int candidate)
{
    const Syn68kMMU040Access* access = &record->access;
    const Syn68kMOVE16Writeback* line = &record->move16;
    uint16 ssw;
    if (record->vector != 2 || record->format != 7 || record->store_state || record->store_next ||
        record->store_trace_pc || record->store_ccr || record->store_tail || record->store_tail_valid ||
        record->store_tail_fc || record->store_tail_address || record->store_locked ||
        record->store_link_displacement || record->fpu_vector || record->fpu_ea || record->movem_state ||
        record->restartable_read || record->stopped || (record->exception_pc & 1) || (line->next & 1) ||
        (line->address & 3) || !access->writing || access->rmw || access->size != 4 || access->completed ||
        access->cache.fault == CACHE040_FILL_FAULT || !move16_cycle_fault(record) || (access->request & 3) ||
        access->logical != access->request || record->fault_address != access->logical ||
        (access->fc != 1 && access->fc != 5) || (access->request & ~15u) != (line->address & ~15u) ||
        access->value != line->data[(access->request >> 2) & 3] || !syn68k_mmu040_access_ssw(access, &ssw) ||
        (access->bus_error && (ssw & 0x400)))
        return 0;
    if (candidate) {
        unsigned mode = (line->command >> 3) & 7;
        return line->state == 1 && !line->trace_pc && line->command < 512 && mode <= 4 &&
               (mode == 4 || !(line->command >> 6)) && line->address == (line->destination & ~3u) &&
               line->next - record->exception_pc == (mode == 4 ? 4u : 6u) && record->next == record->exception_pc;
    }
    if (line->source || line->destination || line->command || record->next != record->exception_pc)
        return 0;
    if (line->state == 4)
        return !access->bus_error && (ssw & 0x400) && !line->trace_pc &&
               (line->next - record->exception_pc == 4 || line->next - record->exception_pc == 6);
    return access->bus_error && (line->state == 2 || line->state == 3) && line->next == record->exception_pc &&
           (line->state == 3
                ? !(line->trace_pc & 1) && (line->next - line->trace_pc == 4 || line->next - line->trace_pc == 6)
                : !line->trace_pc);
}

/* Read aborts have no pending write or retired result. Keep the original
   burst address separately from the callback's failing scalar. */
static int move16_read_fault(const Syn68kIntegerStep* record, int candidate)
{
    const Syn68kMMU040Access* access = &record->access;
    const Syn68kMOVE16Writeback* line = &record->move16;
    uint16 ssw;
    if (record->vector != 2 || record->format != 7 || record->store_state || record->store_next ||
        record->store_trace_pc || record->store_ccr || record->store_tail || record->store_tail_valid ||
        record->store_tail_fc || record->store_tail_address || record->store_locked ||
        record->store_link_displacement || record->fpu_vector || record->fpu_ea || record->movem_state ||
        record->restartable_read || record->stopped || record->forced_trace || record->forced_trace_pc ||
        (record->exception_pc & 1) || (line->address & 3) || line->trace_pc || access->writing || access->rmw ||
        access->size != 4 || access->completed || (access->request & 3) || access->logical != access->request ||
        record->fault_address != access->logical || (access->fc != 1 && access->fc != 5) ||
        (access->request & ~15u) != (line->address & ~15u) || line->data[0] || line->data[1] || line->data[2] ||
        line->data[3] || !move16_cycle_fault(record) || !syn68k_mmu040_access_ssw(access, &ssw) ||
        (access->bus_error && (ssw & 0x400)) || record->next != record->exception_pc)
        return 0;
    if (candidate) {
        unsigned mode = (line->command >> 3) & 7;
        return line->state == 5 && line->command < 512 && mode <= 4 && (mode == 4 || !(line->command >> 6)) &&
               line->address == (line->source & ~3u) && line->next - record->exception_pc == (mode == 4 ? 4u : 6u);
    }
    return line->state == 6 && !line->source && !line->destination && !line->command &&
           (line->next - record->exception_pc == 4 || line->next - record->exception_pc == 6);
}

static unsigned pending_interrupt(void)
{
    if (cpu_state.interrupt_pending[7])
        return 7;
    unsigned mask = (cpu_state.sr >> 8) & 7;
    for (unsigned level = 6; level > mask; --level)
        if (cpu_state.interrupt_pending[level])
            return level;
    return 0;
}

/* A locked write can stop only between its naturally aligned physical beats.
   The logical writeback remains the original full scalar. Cached diagnostics
   must independently describe the exact failed bus beat and LOCKE boundary. */
static int locked_store_progress(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    if (!record->store_locked)
        return 1;
    if (access->completed >= access->size)
        return 0;
    unsigned position = 0;
    while (position < access->completed) {
        uint32 address = access->request + position;
        unsigned left = access->size - position;
        position += (address & 1) || left < 2 ? 1 : (address & 2) || left < 4 ? 2 : 4;
    }
    if (position != access->completed)
        return 0;
    if (!access->bus_error || access->cache.fault == CACHE040_PUSH_FAULT)
        return 1; /* ATC and physical-push validators retain their own contracts. */
    if (!access->cache_exception)
        return access->cache.fault == CACHE040_OK;
    const Syn68kCache040Access* cache = &access->cache;
    const Syn68kCache040Cycle* cycle = &cache->cycle;
    uint32 address = access->request + position;
    unsigned left = access->size - position;
    unsigned width = (address & 1) || left < 2 ? 1 : (address & 2) || left < 4 ? 2 : 4;
    unsigned end = !record->store_tail_valid && position + width == access->size;
    return cache->fault == CACHE040_OPERAND_FAULT && !access->cache_table && !cache->interrupt_level &&
           cycle->kind == CACHE040_OPERAND && cycle->locked == 1 && cycle->lock_end == end && !cycle->burst &&
           !cycle->beat && cycle->way < 4 &&
           (cycle->traffic == CACHE040_LOCKED || cycle->traffic == CACHE040_LOCKED_CONTINUE) &&
           cycle->fc == access->fc && (cycle->attributes & 0x60) == 0x40 &&
           (cycle->attributes & ~0x60u) == (access->translation.attributes & ~0x60u) &&
           cache->completed <= access->completed &&
           cache->bus_address == (uint32)(access->translation.physical + cache->completed) &&
           (cache->bus_address & 0xfff) == (address & 0xfff);
}

static int scalar_store_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    uint16 ssw;
    uint16 push_ssw;
    int push = syn68k_mmu040_cache_push_ssw(access, &push_ssw);
    /* Producers finish nonmemory work or capture LINK's deferred assignments.
       Writeback slots retain the full operand, including unaligned locked
       writes whose earlier physical subcycles have already completed. */
    return record->vector == 2 && record->format == 7 && record->store_state && record->store_state <= 3 &&
           !record->restartable_read && record->store_locked <= 1 && access->rmw == record->store_locked &&
           (!record->store_link_displacement ||
            (record->store_state == 1 && !record->store_locked && !record->fpu_vector && !record->store_tail_valid &&
             !record->movem_state)) &&
           (!record->store_locked || (!record->fpu_vector && !record->movem_state)) &&
           (!record->fpu_vector || (record->fpu_vector >= 48 && record->fpu_vector <= 55 && record->store_state == 2 &&
                                    !record->movem_state)) &&
           (!record->store_tail_valid ||
            (!record->movem_state && (record->store_tail_fc == 1 || record->store_tail_fc == 5) &&
             ((record->store_tail_valid == 1 && !record->store_locked && access->size == 4 &&
               record->store_state >= 2) ||
              (record->store_tail_valid == 2 && record->store_locked && (access->size == 2 || access->size == 4))))) &&
           !(record->store_next & 1) && access->writing == 1 && (access->completed < access->size || push) &&
           access->logical == (uint32)(access->request + access->completed) &&
           (access->size == 1 || access->size == 2 || access->size == 4) && locked_store_progress(record) &&
           syn68k_mmu040_operand_status(access, &ssw) &&
           ((access->fc == 1 || access->fc == 5) ||
            (!record->store_locked && !record->fpu_vector && !record->store_tail_valid && access->bus_error &&
             (ssw & 0x18) == 0x10)) &&
           !(access->bus_error && (ssw & 0x400)) &&
           (!access->completed || push || access->bus_error || ((ssw & 0x400) && !(access->logical & 0xfff)));
}

/* T0 also traces pipeline-serializing instructions; comparing PCs cannot
   identify that class or a taken branch whose target is its fallthrough. */
static int instruction_traced(uint16 entry_sr, unsigned flow)
{
    return (entry_sr & 0x8000) || ((entry_sr & 0x4000) && flow);
}

static int serializing_trace_opcode(unsigned opcode)
{
    return (opcode & 0xff00) == 0xf400 || (opcode & 0xfff0) == 0xf500 || opcode == 0xf510 || opcode == 0xf518 ||
           (opcode & 0xffd8) == 0xf548 || (opcode & 0xfffe) == 0x4e7a ||
           ((opcode & 0xff00) == 0x0e00 && (opcode & 0xc0) != 0xc0) || opcode == 0x4e71 || opcode == 0x4e72 ||
           (opcode & 0xfff0) == 0x4e60 || (opcode & 0xffc0) == 0x46c0 || opcode == 0x007c || opcode == 0x027c ||
           opcode == 0x0a7c || ((opcode & 0xf9c0) == 0x08c0 && (opcode & 0x0600));
}

static int unconditional_flow_opcode(unsigned opcode)
{
    return (opcode & 0xff00) == 0x6100 || (opcode & 0xffc0) == 0x4e80 || (opcode & 0xffc0) == 0x4ec0 ||
           opcode == 0x4e73 || opcode == 0x4e74 || opcode == 0x4e75 || opcode == 0x4e77;
}

/* These instructions execute and force an exception as their result.
   Illegal, privileged, access/address and FPU numeric/preinstruction faults
   do not belong here. T0 alone does not trace this exception flow. */
static int forced_exception(const Syn68kIntegerStep* record)
{
    return (record->format == 0 && record->vector >= 32 && record->vector <= 47) ||
           (record->format == 2 && record->vector >= 5 && record->vector <= 7);
}

static int descriptor_lockup(const Syn68kMMU040Access* access)
{
    return access->cache_exception && access->cache_table && access->cache.fault == CACHE040_DESCRIPTOR_LOCKUP &&
           access->translation.fault == MMU040_PHYSICAL_ABORT;
}

static void halt_cpu(Syn68kMappedCPU* cpu, enum Syn68kMappedHalt reason)
{
    cpu->halted = reason;
    cpu->stopped = 0;
}

static void exception(Syn68kMappedCPU* cpu, Syn68kMappedRun* result)
{
    if (descriptor_lockup(&result->step.access)) {
        halt_cpu(cpu, SYN68K_HALT_DESCRIPTOR_LOCKUP);
        result->reason = SYN68K_MAPPED_HALTED;
        return;
    }
    result->reason = SYN68K_MAPPED_EXCEPTION;
    cpu->pc = result->step.exception_pc;
}

static int movem_fault(const Syn68kIntegerStep* record)
{
    uint16 ssw;
    const Syn68kMMU040Access* access = &record->access;
    uint16 push_ssw;
    int push = syn68k_mmu040_cache_push_ssw(access, &push_ssw);
    /* Opcode, mask, full index and two long displacements occupy at most
       fourteen bytes. A split long extension can have a completed first half. */
    uint32 offset = access->request - record->exception_pc;
    int instruction =
        (access->fc == 2 || access->fc == 6) && !(access->request & 1) && offset < 14 && access->size <= 14 - offset;
    int data = access->fc == 1 || access->fc == 5;
    if (record->vector != 2 || record->format != 7 || record->movem_state != 1 || record->store_state ||
        (record->exception_pc & 1) || access->rmw || (access->size != 2 && access->size != 4) ||
        !syn68k_mmu040_operand_status(access, &ssw))
        return 0;
    if (!access->writing)
        return instruction || data;
    /* A CM store is not retired. The guest drains its captured scalar through
       WB1/WB2 before RTE repeats the list from the saved EA. */
    return data && !record->restartable_read && record->fault_address == access->logical &&
           access->logical == (uint32)(access->request + access->completed) && !(access->bus_error && (ssw & 0x400)) &&
           (push || access->bus_error || !access->completed || !(access->logical & 0xfff));
}

static int cache_push_restart(const Syn68kIntegerStep* record);

int syn68k_mapped_cpu_run(Syn68kMappedCPU* cpu, unsigned budget, Syn68kMappedRun* result)
{
    if (!result)
        return 0;
    *result = (Syn68kMappedRun){.reason = SYN68K_MAPPED_BAD_REQUEST};
    if (!cpu || !cpu->memory || !cpu->memory->mmu || cpu->native > 1 || cpu->stopped > 1 ||
        (unsigned)cpu->halted > SYN68K_HALT_DOUBLE_FAULT || (cpu->stopped && cpu->movem_pending))
        return 0;
#ifndef GENERATE_X64_CODE
    if (cpu->native)
        return 0;
#endif
    result->reason = SYN68K_MAPPED_BUDGET;
    while (result->completed < budget) {
        result->step = (Syn68kIntegerStep){.next = cpu->pc};
        result->opcode = 0;
        if (cpu->halted) {
            result->reason = SYN68K_MAPPED_HALTED;
            return 1;
        }
        if (cpu->stopped) {
            result->interrupt_level = pending_interrupt();
            result->reason = result->interrupt_level ? SYN68K_MAPPED_INTERRUPT : SYN68K_MAPPED_STOPPED;
            return 1;
        }
        if (cpu->pc & 1) {
            result->step.vector = 3;
            result->step.format = 2;
            result->step.exception_pc = cpu->pc;
            result->step.fault_address = cpu->pc & ~1u;
            exception(cpu, result);
            return 1;
        }
        result->interrupt_level = cpu->movem_pending ? 0 : pending_interrupt();
        if (result->interrupt_level) {
            result->reason = SYN68K_MAPPED_INTERRUPT;
            return 1;
        }
        InstructionBus bus = {.original = cpu->memory};
        Syn68kMMU040Memory memory = *cpu->memory;
        memory.opaque = &bus;
        memory.read = cpu->memory->read ? instruction_read : NULL;
        memory.write = cpu->memory->write ? instruction_write : NULL;
        memory.tables.opaque = &bus;
        memory.tables.read32 = cpu->memory->tables.read32 ? instruction_table_read : NULL;
        memory.tables.update32 = cpu->memory->tables.update32 ? instruction_table_write : NULL;
        Syn68kMMU040Cache cache;
        Syn68kCache040Bus cache_bus;
        if (memory.cache) {
            cache = *memory.cache;
            cache.interrupt = (Syn68kCache040Interrupt){cpu_state.interrupt_pending, &cpu_state.sr};
            cache_bus = (Syn68kCache040Bus){
                .opaque = &bus, .read_cycle = instruction_cache_read, .write_cycle = instruction_cache_write};
            cache.bus = &cache_bus;
            memory.cache = &cache;
        }
        Syn68kMMU040Native native = {0};
#ifdef GENERATE_X64_CODE
        if (cpu->native && !memory.native)
            (void)syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0});
#endif
        if (!memory.native && memory.cache)
            memory.native = &native;
        if (memory.native) {
            native = *memory.native;
            native.write_attempted = &bus.write_attempted;
            memory.native = &native;
        }
        ReadCheckpoint before = read_checkpoint();
        Syn68kEAContext context = {.pc = cpu->pc, .memory = &memory};
        unsigned opcode = syn68k_ea_fetch_memory(&context, cpu->pc, 2);
        result->opcode = opcode;
        if (context.error) {
            result->step.vector = 2;
            result->step.format = 7;
            result->step.exception_pc = cpu->pc;
            result->step.fault_address = context.fault_address;
            result->step.access = context.access;
            if (cpu->movem_pending) {
                /* Preserve the saved effective address across another handler.
                   Exception entry suspends it; the new CM frame restores it. */
                result->step.movem_state = 1;
                result->step.movem_ea = cpu->movem_ea;
            }
            uint16 push_ssw;
            if (!cpu->movem_pending && syn68k_mmu040_cache_push_ssw(&context.access, &push_ssw) &&
                !bus.write_attempted && !bus.callback_state_changed && unchanged_results(&before, 0, 16)) {
                result->step.next = cpu->pc;
                result->step.cache_restart = 1;
            }
            exception(cpu, result);
            return 1;
        }
        int handled;
        if (cpu->movem_pending) {
            if (cpu->pc != cpu->movem_pc ||
                !syn68k_integer_resume_movem(cpu->pc, opcode, &memory, cpu->movem_ea, &result->step)) {
                result->reason = SYN68K_MAPPED_UNSUPPORTED;
                return 1;
            }
            cpu->movem_pending = 0;
            handled = 1;
        }
        else {
            handled = 0;
#ifdef GENERATE_X64_CODE
            if (cpu->native)
                handled = syn68k_x64_mapped_mmu(cpu->pc, opcode, &memory, &result->step);
            if (handled < 0) {
                result->reason = SYN68K_MAPPED_BAD_REQUEST;
                return 0;
            }
#endif
            if (!handled)
                handled = syn68k_integer_step_decoded(cpu->pc, opcode, &memory, &result->step);
        }
        if (handled && result->step.interrupt_level) {
            result->reason = SYN68K_MAPPED_INTERRUPT;
            result->interrupt_level = result->step.interrupt_level;
            return 1;
        }
        unsigned trace_flow = handled && (serializing_trace_opcode(opcode) || unconditional_flow_opcode(opcode) ||
                                          result->step.flow_changed);
        unsigned trace_scope = handled;
        if (!handled) {
            if ((opcode & 0xff00) == 0xf400 && (opcode & 0x18) && (cpu_state.sr & SR_SUPERVISOR_BIT) && !memory.cache) {
                result->reason = SYN68K_MAPPED_UNSUPPORTED;
                result->step.next = cpu->pc;
                return 1;
            }
            Syn68kFPUStep fp;
            int fpu_handled = 0;
            if ((opcode & 0xFE00) == 0xF200) {
#ifdef GENERATE_X64_CODE
                if (cpu->native)
                    fpu_handled = syn68k_x64_mapped_fpu(cpu->pc, opcode, &memory, &fp);
#endif
                if (fpu_handled < 0) {
                    result->reason = SYN68K_MAPPED_BAD_REQUEST;
                    return 0;
                }
                if (!fpu_handled)
                    fpu_handled = syn68k_fpu_step_decoded(cpu->pc, opcode, &memory, &fp);
            }
            if (fpu_handled) {
                syn68k_fpu_trace_result(&fp, before.sr, cpu->pc);
                result->step = (Syn68kIntegerStep){.next = fp.next,
                                                   .exception_pc = fp.exception_pc,
                                                   .fault_address = fp.fault_address,
                                                   .vector = fp.vector,
                                                   .format = fp.format,
                                                   .access = fp.access};
                if (fp.store_state == 1 && fp.store_frame_clear <= 1 &&
                    (!fp.store_frame_clear || ((opcode & ~63u) == 0xf300 && !fp.post_vector))) {
                    result->step.store_state = 2;
                    result->step.store_next = fp.store_next;
                    result->step.fpu_vector = fp.post_vector;
                    result->step.fpu_ea = fp.post_ea;
                    result->step.store_tail = fp.store_tail;
                    result->step.store_tail_valid = fp.store_tail_valid;
                    result->step.store_tail_fc = fp.store_tail_fc;
                    if (scalar_store_fault(&result->step)) {
                        result->step.store_trace_pc = fp.exception_pc;
                        if (!fp.post_vector && instruction_traced(before.sr, fp.trace_flow))
                            result->step.store_state = 3;
                        if (fp.store_frame_clear) {
                            cpu_state.fpu.frame_size = 0;
                            cpu_state.fpu.pending_vector = 0;
                        }
                        result->step.next = result->step.exception_pc = fp.store_next;
                    }
                    else
                        result->step.store_state = 0;
                }
                else if ((fp.store_state == 2 || fp.store_state == 3) && !fp.store_frame_clear && !fp.post_vector &&
                         !fp.store_tail_valid && fpu_replay_results(&before, opcode, &fp)) {
                    result->step.store_state = 4;
                    result->step.store_next = fp.store_next;
                    if (!replay_store_fault(&result->step))
                        result->step.store_state = 0;
                    else if (fp.store_ea_size) {
                        unsigned reg = 8 + (opcode & 7);
                        cpu_state.regs[reg] = before.regs[reg];
                    }
                }
            }
            else if (opcode == 0x4e76 && !cpu_state.ccv) {
                // Untaken TRAPV completes normally and participates in T1.
                result->step.next = cpu->pc + 2;
                trace_scope = 1;
            }
            else {
                unsigned vector = opcode >> 12 == 10 ? 10 : opcode >> 12 == 15 ? 11 : 0;
                uint32 pc = cpu->pc;
                unsigned format = 0;
                if ((opcode & 0xFFF0) == 0x4E40)
                    vector = 32 + (opcode & 15), pc += 2;
                else if (opcode == 0x4E76) {
                    vector = 7, pc += 2, format = 2;
                }
                else if (!vector &&
                         ((opcode & 0xFFF8) == 0x4848 || opcode_map_index[opcode] == opcode_map_index[0x4AFC]))
                    vector = 4;
                if (!vector) {
                    result->reason = SYN68K_MAPPED_UNSUPPORTED;
                    result->step.next = cpu->pc;
                    return 1;
                }
                result->step = (Syn68kIntegerStep){.next = pc,
                                                   .exception_pc = pc,
                                                   .vector = vector,
                                                   .format = format,
                                                   .fault_address = format ? cpu->pc : 0};
            }
        }
        if (!result->step.vector && ((result->step.prefetch_address | result->step.next) & 1)) {
            /* Address errors precede trace. A conditional branch can prefetch
               an odd target even when its architectural next PC is even. */
            uint32 reference = result->step.prefetch_address & 1 ? result->step.prefetch_address : result->step.next;
            result->step.vector = 3;
            result->step.format = 2;
            result->step.exception_pc = cpu->pc;
            result->step.fault_address = reference & ~1u;
        }
        if (!result->step.vector && trace_scope && instruction_traced(before.sr, trace_flow)) {
            result->step.vector = 9;
            result->step.format = 2;
            result->step.exception_pc = result->step.next;
            result->step.fault_address = cpu->pc;
        }
        if (result->step.vector) {
            if (descriptor_lockup(&result->step.access)) {
                exception(cpu, result);
                return 1;
            }
            result->step.stopped = 0;
            if (result->step.move16.state == 5) {
                Syn68kMOVE16Writeback* line = &result->step.move16;
                if (move16_read_fault(&result->step, 1) && !bus.write_attempted && !bus.callback_state_changed &&
                    unchanged_results(&before, opcode, 16)) {
                    line->state = 6;
                    line->source = line->destination = line->command = 0;
                }
                else
                    *line = (Syn68kMOVE16Writeback){0};
            }
            if (result->step.move16.state == 1) {
                Syn68kMOVE16Writeback* line = &result->step.move16;
                if (move16_write_fault(&result->step, 1) &&
                    (result->step.access.bus_error ||
                     (!bus.callback_state_changed && unchanged_results(&before, opcode, 16)))) {
                    if (result->step.access.bus_error) {
                        unsigned mode = (line->command >> 3) & 7, reg = line->command & 7;
                        if (mode == 4) {
                            EM_AREG(reg) = line->source + 16;
                            EM_AREG(line->command >> 6) = line->destination + 16;
                        }
                        else if (mode < 2)
                            EM_AREG(reg) += 16;
                        line->state = instruction_traced(before.sr, 0) ? 3 : 2;
                        line->trace_pc = line->state == 3 ? cpu->pc : 0;
                        result->step.next = result->step.exception_pc = line->next;
                    }
                    else
                        line->state = 4;
                    line->source = line->destination = line->command = 0;
                }
                else
                    *line = (Syn68kMOVE16Writeback){0};
            }
            if (result->step.store_state == 1 && (result->step.store_next & 1) &&
                ((opcode & 0xff00) == 0x6100 || (opcode & 0xffc0) == 0x4e80)) {
                /* The data fault precedes an odd target's address error. Abort
                   the call, drain its scalar through the guest, then restart
                   the original instruction without a private continuation bit. */
                Syn68kIntegerStep offer = result->step;
                offer.store_next = cpu->pc;
                ReadCheckpoint expected = before;
                expected.regs[15].ul.n -= 4;
                if (!bus.callback_state_changed && scalar_store_fault(&offer) &&
                    unchanged_results(&expected, opcode, 16)) {
                    offer.next = offer.store_next = offer.exception_pc;
                    offer.store_state = 4;
                    offer.store_ccr = 0;
                    offer.prefetch_address = 0;
                    if (replay_store_fault(&offer)) {
                        result->step = offer;
                        cpu_state.regs[15] = before.regs[15];
                    }
                }
            }
            if (result->step.store_state == 1 && scalar_store_fault(&result->step)) {
                unsigned ccr = result->step.store_ccr;
                cpu_state.ccc = ccr & 1;
                cpu_state.ccv = ccr & 2;
                cpu_state.ccnz = !(ccr & 4);
                cpu_state.ccn = ccr & 8;
                cpu_state.ccx = ccr & 16;
                if ((opcode & 0xfff8) == 0x4e50 || (opcode & 0xfff8) == 0x4808) {
                    /* Match normal LINK's work after its queued final store.
                       Callback changes to the live SP feed both assignments. */
                    EM_AREG(opcode & 7) = EM_A7;
                    EM_A7 += result->step.store_link_displacement;
                }
                result->step.store_link_displacement = 0;
                result->step.store_trace_pc = result->step.exception_pc;
                result->step.store_state = instruction_traced(before.sr, trace_flow) ? 3 : 2;
                result->step.next = result->step.exception_pc = result->step.store_next;
            }
            /* Abort pre-execute reads: restore speculative EA register updates,
             preserving completed physical reads and descriptor history. Callback
             An/null changes must remain live, including descriptor effects.
             RTE's separate live-SP certificate does not request this rollback. No
             data/CCR/status/FPU result or attempted store may be discarded. */
            uint16 read_ssw;
            if (result->step.vector == 2 && result->step.format == 7 && !result->step.access.writing &&
                (!result->step.access.rmw || result->step.access.fc == 1 || result->step.access.fc == 5) &&
                syn68k_mmu040_access_ssw(&result->step.access, &read_ssw) &&
                ((opcode & 0xFFC0) != 0xF600 || result->step.access.fc == 2 || result->step.access.fc == 6) &&
                !result->step.movem_state && !bus.write_attempted && !bus.callback_state_changed &&
                unchanged_results(&before, opcode,
                                  opcode == 0x4e73 || ((opcode & 0xfb80) == 0x4880 && (opcode & 0x38) >= 0x10) ? 16
                                                                                                               : 8)) {
                for (unsigned i = 8; i < 16; ++i)
                    cpu_state.regs[i] = before.regs[i];
                result->step.restartable_read = 1;
            }
            uint16 push_ssw;
            unsigned cache_op = (opcode & 0xff20) == 0xf420 && (opcode & 0x18);
            unsigned ptest = (opcode & 0xffd8) == 0xf548;
            if (syn68k_mmu040_cache_push_ssw(&result->step.access, &push_ssw) &&
                (!result->step.access.writing || cache_op || ptest) && !bus.write_attempted &&
                !bus.callback_state_changed && !result->step.cache_restart && !result->step.store_state &&
                !result->step.movem_state && !result->step.move16.state &&
                unchanged_results(&before, opcode, cache_op || ptest || opcode == 0x4e73 ? 16 : 8)) {
                Syn68kIntegerStep offer = result->step;
                offer.cache_restart = 1;
                if (cache_push_restart(&offer)) {
                    for (unsigned i = 8; i < 16; ++i)
                        cpu_state.regs[i] = before.regs[i];
                    result->step = offer;
                }
            }
            if ((before.sr & 0x8000) && forced_exception(&result->step)) {
                result->step.forced_trace = 1;
                result->step.forced_trace_pc = cpu->pc;
            }
            exception(cpu, result);
            return 1;
        }
        cpu->pc = result->step.next;
        if (result->step.movem_state == 2) {
            cpu->movem_pc = cpu->pc;
            cpu->movem_ea = result->step.movem_ea;
            cpu->movem_pending = 1;
        }
        ++result->completed;
        if (result->step.stopped) {
            cpu->stopped = 1;
            result->interrupt_level = pending_interrupt();
            result->reason = result->interrupt_level ? SYN68K_MAPPED_INTERRUPT : SYN68K_MAPPED_STOPPED;
            return 1;
        }
    }
    result->step.next = cpu->pc;
    return 1;
}

static int delivery_fault(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* record, Syn68kMappedDelivery* result,
                          Syn68kMMU040Access access, int stacking)
{
    result->reason =
        record->vector == 2 || record->vector == 3 ? SYN68K_DELIVERY_DOUBLE_FAULT : SYN68K_DELIVERY_ACCESS_FAULT;
    if (descriptor_lockup(&access)) {
        halt_cpu(cpu, SYN68K_HALT_DESCRIPTOR_LOCKUP);
        result->reason = SYN68K_DELIVERY_HALTED;
    }
    else if (result->reason == SYN68K_DELIVERY_DOUBLE_FAULT)
        halt_cpu(cpu, SYN68K_HALT_DOUBLE_FAULT);
    result->stacking = stacking;
    result->access = access;
    return 0;
}

static Syn68kMMU040Access exception_access(Syn68kMappedCPU* cpu, uint32 address, unsigned size, int writing,
                                           uint32* value)
{
    Syn68kMMU040Memory memory = *cpu->memory;
    Syn68kMMU040Cache cache;
    if (memory.cache) {
        cache = *memory.cache;
        cache.traffic = CACHE040_EXCEPTION;
        memory.cache = &cache;
    }
    return writing ? syn68k_mmu040_write(&memory, address, 5, size, *value)
                   : syn68k_mmu040_read(&memory, address, 5, size, 0, value);
}

static int push_frame(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* record, uint16 sr, unsigned format,
                      Syn68kMappedDelivery* result)
{
    unsigned length = format <= 1 ? 8 : format == 7 ? 60 : 12;
    EM_A7 -= length;
    const uint32 values[] = {sr, record->exception_pc, format << 12 | record->vector * 4, record->fault_address};
    const unsigned sizes[] = {2, 4, 2, 4}, offsets[] = {0, 2, 6, 8};
    for (unsigned i = 0; i < (length == 8 || format == 7 ? 3U : 4U); ++i) {
        uint32 value = values[i];
        Syn68kMMU040Access access = exception_access(cpu, EM_A7 + offsets[i], sizes[i], 1, &value);
        if (access.translation.fault != MMU040_OK || access.bus_error)
            return delivery_fault(cpu, record, result, access, 1);
    }
    if (format == 7) {
        uint16 ssw = 0;
        /* Read restarts have no pending writes. A retired scalar store uses
         WB1 (physical bus fault) or WB2 (translation fault), with an optional
         following FPU longword or CAS2 operand in the next slot. WB1 data follows memory
         lanes; WB2 and WB3 data stay register aligned. A certified FPU store
         abort retains its original PC and one WB1/WB2, with no continuation bits.
         A MOVEM store instead retains CM and its original EA for list replay. */
        syn68k_mmu040_operand_status(&record->access, &ssw);
        uint16 push_ssw = 0;
        int physical_push = syn68k_mmu040_cache_push_ssw(&record->access, &push_ssw);
        int pending = record->store_state == 2 || record->store_state == 3 || record->store_state == 4 ||
                      (record->movem_state == 1 && record->access.writing);
        if (record->fpu_vector)
            ssw |= 0x8000; // The FPU handler owns any stacked trace condition.
        else if (record->store_state == 3)
            ssw |= 0x2000; /* Trace the retired instruction after store cleanup. */
        if (record->movem_state == 1)
            ssw |= 0x1000;
        if (record->move16.state == 6)
            ssw = 0x160 | record->access.fc | (ssw & 0x400);
        else if (record->move16.state)
            ssw = 0x68 | record->access.fc | (record->move16.state == 4 ? 0x400 : 0) |
                  (record->move16.state == 3 ? 0x2000 : 0);
        /* Allocation fills have not issued the pending logical store on the
           write bus. Keep its full register-aligned operand in WB2. */
        int wb1 = pending && record->access.bus_error && record->access.cache.fault != CACHE040_FILL_FAULT;
        int tail = pending && record->store_tail_valid;
        unsigned wb_status = 0x80 | (ssw & 0x7F);
        unsigned tail_status = 0x80 | (ssw & 0x78) | record->store_tail_fc;
        for (unsigned offset = 8; offset < 60; offset += 4) {
            uint32 value = offset == 8 && record->fpu_vector         ? record->fpu_ea
                           : offset == 8 && record->store_state == 3 ? record->store_trace_pc
                           : offset == 8 && record->movem_state == 1 ? record->movem_ea
                           : offset == 12                            ? (uint32)ssw << 16
                           : offset == 20                            ? record->access.request
                                                                     : 0;
            if (pending) {
                if (offset == 12 && tail && !wb1)
                    value |= tail_status;
                if (offset == 16)
                    value = wb1 ? wb_status | (tail ? tail_status << 16 : 0) : wb_status << 16;
                else if (offset == (wb1 ? 40U : 32U))
                    value = record->access.request;
                else if (offset == (wb1 ? 44U : 36U)) {
                    value = record->access.value;
                    if (wb1) {
                        /* Table 8-5: WB1 follows bus lanes, including a word
                           or longword that wraps past the final lane. */
                        value <<= (4 - record->access.size) * 8;
                        unsigned rotate = (record->access.request & 3) * 8;
                        if (rotate)
                            value = (value >> rotate) | (value << (32 - rotate));
                    }
                }
                if (tail && offset == (wb1 ? 32U : 24U))
                    value = record->store_tail_valid == 2 ? record->store_tail_address : record->access.request + 4;
                else if (tail && offset == (wb1 ? 36U : 28U))
                    value = record->store_tail;
            }
            if (physical_push) {
                const Syn68kCache040Access* push = &record->access.cache;
                int operand = pending && record->access.completed < record->access.size;
                if (offset != 8) {
                    value =
                        offset == 12   ? (uint32)(push_ssw | (ssw & 0xf000)) << 16 | (operand && tail ? tail_status : 0)
                        : offset == 16 ? (operand ? wb_status
                                          : tail  ? tail_status
                                                  : 0)
                                             << 16
                        : offset == 20 || offset == 40 ? push->push_address
                        : offset == 32 && operand      ? record->access.request
                        : offset == 36 && operand      ? record->access.value
                        : offset == (operand ? 24u : 32u) && tail
                            ? (record->store_tail_valid == 2 ? record->store_tail_address : record->access.request + 4)
                        : offset == (operand ? 28u : 36u) && tail ? record->store_tail
                                                                  : 0;
                }
                if (offset >= 44)
                    for (unsigned byte = 0; byte < 4; ++byte)
                        value = (value << 8) | push->push_data[offset - 44 + byte];
            }
            else if (record->move16.state == 6) {
                value = offset == 12 ? (uint32)ssw << 16 : offset == 20 ? record->move16.address : 0;
            }
            else if (record->move16.state) {
                int physical = record->move16.state != 4;
                value = offset == 8 && record->move16.state == 3 ? record->move16.trace_pc
                        : offset == 12                           ? (uint32)ssw << 16
                        : offset == 16                           ? (physical ? 0 : (0xe8 | record->access.fc) << 16)
                        : offset == 20 || offset == (physical ? 40u : 32u) ? record->move16.address
                        : offset >= 44                                     ? record->move16.data[(offset - 44) / 4]
                                                                           : 0;
            }
            Syn68kMMU040Access access = exception_access(cpu, EM_A7 + offset, 4, 1, &value);
            if (access.translation.fault != MMU040_OK || access.bus_error)
                return delivery_fault(cpu, record, result, access, 1);
        }
    }
    return 1;
}

static int cache_push_restart(const Syn68kIntegerStep* record)
{
    uint16 ssw;
    return record->vector == 2 && record->format == 7 &&
           (record->cache_restart == 1 ||
            (record->cache_restart == 2 && record->access.fc == 5 && !record->access.writing && !record->access.rmw &&
             (record->access.size == 2 || record->access.size == 4) &&
             record->access.completed < record->access.size)) &&
           record->next == record->exception_pc && !(record->exception_pc & 1) &&
           record->fault_address == record->access.logical && !record->restartable_read && !record->store_state &&
           !record->store_next && !record->store_trace_pc && !record->store_ccr && !record->store_tail &&
           !record->store_tail_valid && !record->store_tail_fc && !record->store_tail_address &&
           !record->store_locked && !record->store_link_displacement && !record->movem_state && !record->movem_ea &&
           !record->move16.state && !record->fpu_vector && !record->fpu_ea && !record->stopped &&
           !record->forced_trace && !record->forced_trace_pc && !record->prefetch_address && !record->flow_changed &&
           syn68k_mmu040_cache_push_ssw(&record->access, &ssw);
}

static int restartable_read_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    uint16 ssw;
    int opcode = access->request == record->exception_pc && access->size == 2 && !access->completed &&
                 (access->fc == 2 || access->fc == 6);
    return record->vector == 2 && record->format == 7 && (opcode || record->restartable_read) &&
           !(record->exception_pc & 1) && !access->writing && (!access->rmw || access->fc == 1 || access->fc == 5) &&
           (access->size == 1 || access->size == 2 || access->size == 4) && syn68k_mmu040_access_ssw(access, &ssw);
}

int syn68k_mapped_exception_enter(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* record, unsigned interrupt_level,
                                  Syn68kMappedDelivery* result)
{
    if (!result)
        return 0;
    *result = (Syn68kMappedDelivery){.reason = SYN68K_DELIVERY_BAD_REQUEST};
    if (!cpu || !cpu->memory || !cpu->memory->mmu || !record || (unsigned)cpu->halted > SYN68K_HALT_DOUBLE_FAULT ||
        record->vector < 2 || record->vector > 255 || interrupt_level > 7 ||
        (interrupt_level && (record->vector != 24 + interrupt_level || record->format)))
        return 0;
    if (cpu->halted) {
        result->reason = SYN68K_DELIVERY_HALTED;
        return 1;
    }
    if ((!record->forced_trace && record->forced_trace_pc) ||
        (record->forced_trace &&
         (record->forced_trace != 1 || (record->forced_trace_pc & 1) || !forced_exception(record) ||
          record->restartable_read || record->movem_state || record->move16.state || record->store_state ||
          record->fpu_vector || record->store_tail_valid || record->store_locked || record->stopped))) {
        result->reason = SYN68K_DELIVERY_UNSUPPORTED;
        return 1;
    }
    if ((record->cache_restart && !cache_push_restart(record)) ||
        (record->move16.state && !move16_write_fault(record, 0) && !move16_read_fault(record, 0)) ||
        (record->movem_state && !movem_fault(record)) ||
        ((record->store_state || record->fpu_vector || record->store_tail_valid || record->store_locked) &&
         !scalar_store_fault(record) && !replay_store_fault(record)) ||
        (record->format != 0 && record->format != 2 && record->format != 3 && !restartable_read_fault(record) &&
         !((record->store_state == 2 || record->store_state == 3) && scalar_store_fault(record)) &&
         !replay_store_fault(record) && !movem_fault(record) && !move16_write_fault(record, 0) &&
         !move16_read_fault(record, 0) && !cache_push_restart(record))) {
        result->reason = SYN68K_DELIVERY_UNSUPPORTED;
        return 1;
    }
    cpu->stopped = 0; // Accepted exception entry resumes a stopped processor.
    uint16 old_sr = COMPUTE_SR_FROM_CPU_STATE();
    uint16 next_sr = (old_sr & 0x3FFF) | SR_SUPERVISOR_BIT;
    if (interrupt_level)
        next_sr = (next_sr & ~0x700) | interrupt_level << 8;
    cpu_state.sr = next_sr & ~31;
    SWITCH_A7(old_sr, next_sr);
    unsigned format = record->format;
    if (interrupt_level) {
        if ((old_sr & 0x3000) == 0x3000) {
            if (!push_frame(cpu, record, old_sr, 0, result))
                return 1;
            format = 1;
        }
        uint16 previous_sr = next_sr;
        next_sr &= ~SR_MASTER_BIT;
        cpu_state.sr = next_sr & ~31;
        SWITCH_A7(previous_sr, next_sr);
    }
    if (!push_frame(cpu, record, old_sr, format, result))
        return 1;
    uint32 target = 0;
    Syn68kMMU040Access access = exception_access(cpu, cpu_state.vbr + record->vector * 4, 4, 0, &target);
    if (access.translation.fault != MMU040_OK || access.bus_error) {
        delivery_fault(cpu, record, result, access, 0);
        return 1;
    }
    cpu->pc = target;
    cpu->movem_pending = 0; // Continuation now belongs to the guest CM frame.
    if (interrupt_level)
        cpu_state.interrupt_pending[interrupt_level] = 0;
    if (record->forced_trace) {
        /* The forced frame and vector fetch have completed. Trace saves the
           handler PC and the now-supervisor SR, above that intact frame. */
        Syn68kIntegerStep trace = {
            .vector = 9, .format = 2, .exception_pc = target, .fault_address = record->forced_trace_pc};
        return syn68k_mapped_exception_enter(cpu, &trace, 0, result);
    }
    result->reason = SYN68K_DELIVERY_COMPLETE;
    return 1;
}
