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
} InstructionBus;

static int instruction_read(void* opaque, uint32 physical, unsigned char* bytes, unsigned size, uint32 attributes,
                            unsigned fc)
{
    InstructionBus* bus = opaque;
    return bus->original->read && bus->original->read(bus->original->opaque, physical, bytes, size, attributes, fc);
}

static int instruction_write(void* opaque, uint32 physical, const unsigned char* bytes, unsigned size,
                             uint32 attributes, unsigned fc)
{
    InstructionBus* bus = opaque;
    bus->write_attempted = 1;
    return bus->original->write && bus->original->write(bus->original->opaque, physical, bytes, size, attributes, fc);
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
    return record->vector == 2 && record->format == 7 && record->store_state == 4 &&
           record->store_next == record->exception_pc && record->next == record->exception_pc &&
           record->fault_address == access->logical && !record->store_ccr && !record->store_trace_pc &&
           !(record->exception_pc & 1) && !record->movem_state && !record->fpu_vector && !record->store_tail_valid &&
           !record->store_locked && !record->restartable_read && access->writing && !access->rmw &&
           access->completed < access->size && !access->bus_error && (access->size == 2 || access->size == 4) &&
           access->logical == (uint32)(access->request + access->completed) && (access->fc == 1 || access->fc == 5) &&
           syn68k_mmu040_access_ssw(access, &ssw) && (ssw & 0x400) &&
           (!access->completed || !(access->logical & 0xfff));
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

static int scalar_store_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    uint16 ssw;
    /* FPU offers reach this boundary in retired state. Integer candidates
       still require alignment before the runner can retire their flags. */
    return record->vector == 2 && record->format == 7 && record->store_state && record->store_state <= 3 &&
           !record->restartable_read && record->store_locked <= 1 && access->rmw == record->store_locked &&
           (!record->store_locked || (!record->fpu_vector && !record->movem_state)) &&
           (!record->fpu_vector || (record->fpu_vector >= 48 && record->fpu_vector <= 55 && record->store_state == 2 &&
                                    !record->movem_state)) &&
           (!record->store_tail_valid ||
            (!record->movem_state && (record->store_tail_fc == 1 || record->store_tail_fc == 5) &&
             ((record->store_tail_valid == 1 && !record->store_locked && access->size == 4 &&
               record->store_state >= 2) ||
              (record->store_tail_valid == 2 && record->store_locked && (access->size == 2 || access->size == 4) &&
               !(record->store_tail_address & (access->size - 1)))))) &&
           !(record->store_next & 1) && access->writing && access->completed < access->size &&
           access->logical == (uint32)(access->request + access->completed) &&
           (access->size == 1 || access->size == 2 || access->size == 4) &&
           (!(access->request & (access->size - 1)) || (!record->store_locked && record->store_state >= 2)) &&
           (access->fc == 1 || access->fc == 5) && syn68k_mmu040_access_ssw(access, &ssw) &&
           !(access->bus_error && (ssw & 0x400)) &&
           (!access->completed || (!record->store_locked && record->store_state >= 2 &&
                                   (access->bus_error || ((ssw & 0x400) && !(access->logical & 0xfff)))));
}

/* T0 also traces pipeline-serializing instructions; comparing PCs cannot
   identify that class or a taken branch whose target is its fallthrough. */
static int instruction_traced(uint16 entry_sr, unsigned flow)
{
    return (entry_sr & 0x8000) || ((entry_sr & 0x4000) && flow);
}

static int mmu_trace_opcode(unsigned opcode)
{
    return (opcode & 0xfff0) == 0xf500 || opcode == 0xf510 || opcode == 0xf518 || (opcode & 0xffd8) == 0xf548 ||
           (opcode & 0xfffe) == 0x4e7a;
}

static void exception(Syn68kMappedCPU* cpu, Syn68kMappedRun* result)
{
    result->reason = SYN68K_MAPPED_EXCEPTION;
    cpu->pc = result->step.exception_pc;
}

static int movem_read_fault(const Syn68kIntegerStep* record)
{
    uint16 ssw;
    const Syn68kMMU040Access* access = &record->access;
    /* Opcode, mask, full index and two long displacements occupy at most
       fourteen bytes. A split long extension can have a completed first half. */
    uint32 offset = access->request - record->exception_pc;
    int instruction =
        (access->fc == 2 || access->fc == 6) && !(access->request & 1) && offset < 14 && access->size <= 14 - offset;
    return record->vector == 2 && record->format == 7 && record->movem_state == 1 && !record->store_state &&
           !(record->exception_pc & 1) && !access->writing && !access->rmw &&
           (access->size == 2 || access->size == 4) && (instruction || access->fc == 1 || access->fc == 5) &&
           syn68k_mmu040_access_ssw(access, &ssw);
}

int syn68k_mapped_cpu_run(Syn68kMappedCPU* cpu, unsigned budget, Syn68kMappedRun* result)
{
    if (!result)
        return 0;
    *result = (Syn68kMappedRun){.reason = SYN68K_MAPPED_BAD_REQUEST};
    if (!cpu || !cpu->memory || !cpu->memory->mmu || cpu->native > 1)
        return 0;
#ifndef GENERATE_X64_CODE
    if (cpu->native)
        return 0;
#endif
    result->reason = SYN68K_MAPPED_BUDGET;
    while (result->completed < budget) {
        result->step = (Syn68kIntegerStep){.next = cpu->pc};
        result->opcode = 0;
        result->interrupt_level = cpu->movem_pending ? 0 : pending_interrupt();
        if (result->interrupt_level) {
            result->reason = SYN68K_MAPPED_INTERRUPT;
            return 1;
        }
        if (cpu->pc & 1) {
            result->step.vector = 3;
            result->step.format = 2;
            result->step.exception_pc = result->step.fault_address = cpu->pc;
            exception(cpu, result);
            return 1;
        }
        InstructionBus bus = {.original = cpu->memory};
        Syn68kMMU040Memory memory = *cpu->memory;
        memory.opaque = &bus;
        memory.read = cpu->memory->read ? instruction_read : NULL;
        memory.write = cpu->memory->write ? instruction_write : NULL;
        Syn68kMMU040Native native;
#ifdef GENERATE_X64_CODE
        if (cpu->native && !memory.native)
            (void)syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0});
#endif
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
        unsigned trace_scope = handled && mmu_trace_opcode(opcode);
        if (!handled) {
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
            else {
                unsigned vector = opcode >> 12 == 10 ? 10 : opcode >> 12 == 15 ? 11 : 0;
                uint32 pc = cpu->pc;
                unsigned format = 0;
                if ((opcode & 0xFFF0) == 0x4E40)
                    vector = 32 + (opcode & 15), pc += 2;
                else if (opcode == 0x4E76) {
                    if (cpu_state.ccv)
                        vector = 7, pc += 2, format = 2;
                    else {
                        cpu->pc += 2;
                        ++result->completed;
                        continue;
                    }
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
        if (!result->step.vector && trace_scope && instruction_traced(before.sr, 1)) {
            result->step.vector = 9;
            result->step.format = 2;
            result->step.exception_pc = result->step.next;
            result->step.fault_address = cpu->pc;
        }
        if (result->step.vector) {
            if (result->step.store_state == 1 && scalar_store_fault(&result->step)) {
                unsigned ccr = result->step.store_ccr;
                cpu_state.ccc = ccr & 1;
                cpu_state.ccv = ccr & 2;
                cpu_state.ccnz = !(ccr & 4);
                cpu_state.ccn = ccr & 8;
                cpu_state.ccx = ccr & 16;
                result->step.store_trace_pc = result->step.exception_pc;
                result->step.store_state = cpu_state.sr & 0x8000 ? 3 : 2;
                result->step.next = result->step.exception_pc = result->step.store_next;
            }
            /* Abort pre-execute reads: restore speculative EA register updates,
             preserving completed physical reads and descriptor history. No
             data/CCR/status/FPU result or attempted store may be discarded. */
            uint16 read_ssw;
            if (result->step.vector == 2 && result->step.format == 7 && !result->step.access.writing &&
                (!result->step.access.rmw || result->step.access.fc == 1 || result->step.access.fc == 5) &&
                syn68k_mmu040_access_ssw(&result->step.access, &read_ssw) && (opcode & 0xFFC0) != 0xF600 &&
                !((opcode & 0xFB80) == 0x4880 && (opcode & 0x38) >= 0x10) && !bus.write_attempted &&
                unchanged_results(&before, opcode, opcode == 0x4e73 ? 16 : 8)) {
                for (unsigned i = 8; i < 16; ++i)
                    cpu_state.regs[i] = before.regs[i];
                result->step.restartable_read = 1;
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
    }
    result->step.next = cpu->pc;
    return 1;
}

static int delivery_fault(const Syn68kIntegerStep* record, Syn68kMappedDelivery* result, Syn68kMMU040Access access,
                          int stacking)
{
    result->reason =
        record->vector == 2 || record->vector == 3 ? SYN68K_DELIVERY_DOUBLE_FAULT : SYN68K_DELIVERY_ACCESS_FAULT;
    result->stacking = stacking;
    result->access = access;
    return 0;
}

static int push_frame(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* record, uint16 sr, unsigned format,
                      Syn68kMappedDelivery* result)
{
    unsigned length = format <= 1 ? 8 : format == 7 ? 60 : 12;
    EM_A7 -= length;
    const uint32 values[] = {sr, record->exception_pc, format << 12 | record->vector * 4, record->fault_address};
    const unsigned sizes[] = {2, 4, 2, 4}, offsets[] = {0, 2, 6, 8};
    for (unsigned i = 0; i < (length == 8 || format == 7 ? 3U : 4U); ++i) {
        Syn68kMMU040Access access = syn68k_mmu040_write(cpu->memory, EM_A7 + offsets[i], 5, sizes[i], values[i]);
        if (access.translation.fault != MMU040_OK || access.bus_error)
            return delivery_fault(record, result, access, 1);
    }
    if (format == 7) {
        uint16 ssw = 0;
        /* Read restarts have no pending writes. A retired scalar store uses
         WB1 (physical bus fault) or WB2 (translation fault), with an optional
         following FPU longword or CAS2 operand in the next slot. WB1 data follows memory
         lanes; WB2 and WB3 data stay register aligned. A certified FPU store
         abort retains its original PC and one WB2, with no continuation bits. */
        syn68k_mmu040_access_ssw(&record->access, &ssw);
        int pending = record->store_state == 2 || record->store_state == 3 || record->store_state == 4;
        if (record->fpu_vector)
            ssw |= 0x8000; // The FPU handler owns any stacked trace condition.
        else if (record->store_state == 3)
            ssw |= 0x2000; /* Trace the retired instruction after store cleanup. */
        if (record->movem_state == 1)
            ssw |= 0x1000;
        int wb1 = pending && record->access.bus_error;
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
            Syn68kMMU040Access access = syn68k_mmu040_write(cpu->memory, EM_A7 + offset, 5, 4, value);
            if (access.translation.fault != MMU040_OK || access.bus_error)
                return delivery_fault(record, result, access, 1);
        }
    }
    return 1;
}

static int restartable_read_fault(const Syn68kIntegerStep* record)
{
    const Syn68kMMU040Access* access = &record->access;
    uint16 ssw;
    int opcode = access->request == record->exception_pc && access->size == 2 && !access->completed &&
                 (access->fc == 2 || access->fc == 6);
    return record->vector == 2 && record->format == 7 && (opcode || record->restartable_read) &&
           !(record->exception_pc & 1) && !access->writing && (!access->rmw || access->fc == 1 || access->fc == 5) &&
           (access->size == 1 || access->size == 2 || access->size == 4) &&
           (access->fc == 1 || access->fc == 2 || access->fc == 5 || access->fc == 6) &&
           syn68k_mmu040_access_ssw(access, &ssw);
}

int syn68k_mapped_exception_enter(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* record, unsigned interrupt_level,
                                  Syn68kMappedDelivery* result)
{
    if (!result)
        return 0;
    *result = (Syn68kMappedDelivery){.reason = SYN68K_DELIVERY_BAD_REQUEST};
    if (!cpu || !cpu->memory || !cpu->memory->mmu || !record || record->vector < 2 || record->vector > 255 ||
        interrupt_level > 7 || (interrupt_level && (record->vector != 24 + interrupt_level || record->format)))
        return 0;
    if ((record->movem_state && !movem_read_fault(record)) ||
        ((record->store_state || record->fpu_vector || record->store_tail_valid || record->store_locked) &&
         !scalar_store_fault(record) && !replay_store_fault(record)) ||
        (record->format != 0 && record->format != 2 && record->format != 3 && !restartable_read_fault(record) &&
         !((record->store_state == 2 || record->store_state == 3) && scalar_store_fault(record)) &&
         !replay_store_fault(record) && !movem_read_fault(record))) {
        result->reason = SYN68K_DELIVERY_UNSUPPORTED;
        return 1;
    }
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
    Syn68kMMU040Access access = syn68k_mmu040_read(cpu->memory, cpu_state.vbr + record->vector * 4, 5, 4, 0, &target);
    if (access.translation.fault != MMU040_OK || access.bus_error) {
        delivery_fault(record, result, access, 0);
        return 1;
    }
    cpu->pc = target;
    cpu->movem_pending = 0; // Continuation now belongs to the guest CM frame.
    if (interrupt_level)
        cpu_state.interrupt_pending[interrupt_level] = 0;
    result->reason = SYN68K_DELIVERY_COMPLETE;
    return 1;
}
