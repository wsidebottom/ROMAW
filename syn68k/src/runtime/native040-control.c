/*+===================================================================
  File:      native040-control.c

  Summary:   Instantiate fixed native MOVEC and STOP status roots after privilege and command-fetch guards.

  Classes:   None.

  Functions: Build-time native040_movec_read, native040_movec_write and native040_stop roots.

  Origin:    Original ARDI Executor direct 68040 lowering, sharing maintained control-register semantics.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040-control.h"
#include "control040.h"
#include "native040-memory.h"

static inline void transfer(Syn68kNative040Control* frame, int writing)
{
    frame->ok = 0;
    if (!frame->cpu || frame->command > 65535)
        return;
    unsigned reg = frame->command >> 12;
    uint32 value = frame->cpu->regs[reg].ul.n;
    if (!syn68k_control040_transfer(frame->cpu, frame->memory, frame->command & 0xfff, writing, &value))
        return;
    if (!writing)
        frame->cpu->regs[reg].ul.n = value;
    frame->ok = 1;
}

__attribute__((used, noinline)) void native040_movec_read(Syn68kNative040Control* frame)
{
    transfer(frame, 0);
}

__attribute__((used, noinline)) void native040_movec_write(Syn68kNative040Control* frame)
{
    transfer(frame, 1);
}

static void rte_frame_load_status(CPUState* cpu, uint16 sr)
{
    unsigned old_sr = cpu->sr, value = sr & 0xf71f;
    cpu->sr = value & ~31;
    cpu->ccc = value & 1;
    cpu->ccv = value & 2;
    cpu->ccnz = (~value) & 4;
    cpu->ccn = value & 8;
    cpu->ccx = value & 16;
    if ((old_sr ^ value) & (SR_SUPERVISOR_BIT | SR_MASTER_BIT)) {
        uint32* old_bank = USE_USP(old_sr) ? &cpu->usp : USE_MSP(old_sr) ? &cpu->msp : &cpu->isp;
        uint32* next_bank = USE_USP(value) ? &cpu->usp : USE_MSP(value) ? &cpu->msp : &cpu->isp;
        *old_bank = cpu->regs[15].ul.n;
        cpu->regs[15].ul.n = *next_bank;
    }
    for (unsigned i = 1; i <= 7; ++i)
        if (cpu->interrupt_pending[i]) {
            cpu->interrupt_status_changed = INTERRUPT_STATUS_CHANGED;
            break;
        }
}

/* Privilege and the immediate fetch precede this closed body. The mapped
   dispatcher owns trace, the stopped latch and interrupt delivery. */
__attribute__((used, noinline)) void native040_stop(Syn68kNative040Control* frame)
{
    frame->ok = 0;
    CPUState* cpu = frame->cpu;
    if (!cpu || frame->command > 65535)
        return;
    rte_frame_load_status(cpu, frame->command);
    frame->ok = 1;
}

typedef Syn68kNative040MMU RTEBus;
static int rte_frame_read(RTEBus* bus, CPUState* cpu, uint32 address, unsigned size, uint32* value)
{
    bus->address = address;
    bus->fc = cpu->sr & SR_SUPERVISOR_BIT ? 5 : 1;
    bus->size = size;
    native040_read(bus);
    *value = bus->value;
    return !bus->access.bus_error && bus->access.translation.fault == MMU040_OK;
}

#include "rte-frame.h"

__attribute__((used, noinline)) void native040_rte(Syn68kNative040RTE* packet)
{
    Syn68kNative040MMU* bus = packet->memory;
    int mapped = !(bus->memory->native && bus->memory->native->flat);
    Syn68kRTEFrameResult frame = rte_frame_execute(packet->cpu, bus, packet->pc, mapped);
    Syn68kIntegerStep* step = &packet->step;
    step->next = frame.pc;
    if (frame.backing_fault) {
        step->vector = 2;
        step->format = 7;
        step->exception_pc = packet->pc;
        step->fault_address = bus->access.logical;
        step->access = bus->access;
        if (mapped)
            rte_mark_read_fault(packet->cpu, step, frame.restoring);
    }
    else if (frame.vector) {
        step->vector = frame.vector;
        step->format = frame.format;
        step->exception_pc = frame.pc;
        step->fault_address = frame.ea;
    }
    else if (frame.movem) {
        step->movem_state = 2;
        step->movem_ea = frame.ea;
    }
}