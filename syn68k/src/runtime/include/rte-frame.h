/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The includer defines RTEBus, rte_frame_read and rte_frame_load_status.
   Fetches use the live A7 and function code on every operand. */
typedef struct {
    uint32 pc, ea;
    unsigned vector, format, movem;
    unsigned backing_fault, restoring;
} Syn68kRTEFrameResult;

static Syn68kRTEFrameResult rte_frame_execute(CPUState* cpu, RTEBus* bus, uint32 pc, int mapped)
{
    if (!(cpu->sr & SR_SUPERVISOR_BIT))
        return (Syn68kRTEFrameResult){.pc = pc, .vector = 8};

    for (;;) {
        uint32 saved_sr, saved_pc, format_word, frame_ea = 0, continuation = 0;
        if (!rte_frame_read(bus, cpu, cpu->regs[15].ul.n, 2, &saved_sr) ||
            !rte_frame_read(bus, cpu, cpu->regs[15].ul.n + 2, 4, &saved_pc) ||
            !rte_frame_read(bus, cpu, cpu->regs[15].ul.n + 6, 2, &format_word))
            return (Syn68kRTEFrameResult){.pc = pc, .backing_fault = 1};
        unsigned format = format_word >> 12;
        unsigned length = format <= 1 ? 8 : format <= 3 ? 12 : format == 7 ? 60 : 0;
        if (!length)
            return (Syn68kRTEFrameResult){.pc = pc, .vector = 14};

        if (format == 7) {
            if (!rte_frame_read(bus, cpu, cpu->regs[15].ul.n + 12, 2, &continuation))
                return (Syn68kRTEFrameResult){.pc = pc, .backing_fault = 1};
            unsigned pending = continuation & 0xf000;
            if (pending == 0x1000 || pending == 0x2000 || pending == 0x4000 || pending == 0x8000)
                if (!rte_frame_read(bus, cpu, cpu->regs[15].ul.n + 8, 4, &frame_ea))
                    return (Syn68kRTEFrameResult){.pc = pc, .backing_fault = 1, .restoring = 1};
            if (!mapped)
                continuation = 0;
        }
        else if (length == 12 && !rte_frame_read(bus, cpu, cpu->regs[15].ul.n + 8, 4, &frame_ea))
            return (Syn68kRTEFrameResult){.pc = pc, .backing_fault = 1};

        unsigned pending = continuation & 0xf000, fpu_vector = 0;
        if (pending == 0x4000)
            fpu_vector = 11;
        else if (pending == 0x8000) {
            fpu_vector = cpu->fpu.pending_vector;
            if (fpu_vector < 48 || fpu_vector > 55)
                return (Syn68kRTEFrameResult){.pc = pc, .vector = 14};
        }
        cpu->regs[15].ul.n += length;
        rte_frame_load_status(cpu, saved_sr);

        if (fpu_vector)
            return (Syn68kRTEFrameResult){.pc = saved_pc, .ea = frame_ea, .vector = fpu_vector,
                                         .format = fpu_vector == 11 ? 2 : 3};
        if (pending == 0x2000)
            return (Syn68kRTEFrameResult){.pc = saved_pc, .ea = frame_ea, .vector = 9, .format = 2};
        if (format != 1)
            return (Syn68kRTEFrameResult){.pc = saved_pc, .ea = frame_ea, .movem = pending == 0x1000};
    }
}

static void rte_mark_read_fault(CPUState* cpu, Syn68kIntegerStep* step, int restoring)
{
    const Syn68kMMU040Access* access = &step->access;
    if (restoring && access->bus_error && access->translation.fault == MMU040_OK) {
        step->rte_internal_fault = 1;
        return;
    }
    uint16 ssw;
    if (access->fc == (cpu->sr & SR_SUPERVISOR_BIT ? 5u : 1u) && !access->writing && !access->rmw) {
        if (syn68k_mmu040_access_ssw(access, &ssw))
            step->restartable_read = 1;
        else if (syn68k_mmu040_cache_push_ssw(access, &ssw))
            step->cache_restart = 2;
    }
}