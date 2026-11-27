/* SPDX-License-Identifier: MIT */
/* Shared checked and native MOVEM transfer logic. */
/* MOVEM uses its own auto-update loop rather than a single-EA increment.
   Completed register transfers survive a later bounded access fault. */
static uint32 move_multiple(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
    unsigned load = !!(opcode & 0x400), size = opcode & 0x40 ? 4 : 2;
    if (mode < 2 || (load ? mode == 4 : mode == 3) || (mode == 7 && reg > (load ? 3 : 1)))
        return integer_exception(step, context, 4, pc, 0, 0);
    unsigned mask = syn68k_ea_fetch_memory(context, context->pc, 2);
    if (context->error)
        return integer_exception(step, context, 2, pc, context->fault_address, 7);
    context->pc += 2;
    uint32 initial = mode == 4 && context->resume_ea ? *context->resume_ea : EM_AREG(reg), address = initial;
    if (mode == 3 && context->resume_ea)
        address = *context->resume_ea;
    if (mode != 3 && mode != 4) {
        Syn68kEA ea = syn68k_effective_address(context, opcode & 63, size, !load, 0);
        if (context->error)
            return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc,
                                     context->fault_address, context->error == 2 ? 7 : 0);
        address = ea.address;
    }
    uint32 calculated_ea = address;
    for (unsigned bit = 0; bit < 16; ++bit)
        if (mask & (1u << bit)) {
            unsigned selected = mode == 4 ? 15 - bit : bit;
            if (mode == 4)
                EM_AREG(reg) = address -= size;
            if (load) {
                uint32 value = syn68k_ea_read_memory(context, address, size);
                if (!context->error && !(mode == 3 && selected == 8 + reg))
                    cpu_state.regs[selected].ul.n = size == 2 ? (uint32)(int32)(int16)value : value;
            }
            else {
                uint32 value = mode == 4 && selected == 8 + reg ? initial - size : cpu_state.regs[selected].ul.n;
                write_checked_memory(context, address, size, value);
            }
            if (context->error) {
                uint32 next = integer_exception(step, context, 2, pc, context->fault_address, 7);
                if (step && context->memory) {
                    step->movem_state = 1;
                    step->movem_ea = calculated_ea;
                }
                return next;
            }
            if (mode != 4)
                address += size;
            if (mode == 3)
                EM_AREG(reg) = address;
        }
    return context->pc;
}
