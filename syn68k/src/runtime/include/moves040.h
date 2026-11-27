/* SPDX-License-Identifier: MIT */
/* Shared checked and generated 68040 operand logic. */
static uint32 move_alternate(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
    if (mode < 2 || (mode == 7 && ea_reg >= 2))
        return integer_exception(step, context, 4, pc, 0, 0);
    if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
        return integer_exception(step, context, 8, pc, 0, 0);
    unsigned extension = syn68k_ea_fetch_memory(context, context->pc, 2);
    if (!context->error)
        context->pc += 2;
    if (!context->error && (extension & 0x07ff))
        context->error = 4;
    unsigned size = 1u << ((opcode >> 6) & 3);
    unsigned reg = extension >> 12;
    int writing = (extension & 0x0800) != 0;
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, size, writing, 0);
    if (!context->error) {
        unsigned fc = (writing ? cpu_state.dfc : cpu_state.sfc) & 7;
        /* The flat RAM bus supports user/supervisor data and code
         references. Alternate physical bus spaces have no device
         mapping and must fault, rather than aliasing ordinary RAM.
         MC68040UM 3.2.5 merges instruction FC 2/6 into data FC 1/5. */
        if (fc == 2 || fc == 6)
            --fc;
#ifndef SYN68K_NATIVE040_TEMPLATE
        if (!context->memory && fc != 1 && fc != 5) {
            context->error = 2;
            context->fault_address = ea.address;
        }
        else
#endif
        if (writing) {
            /* For An,(An)+ and An,-(An), the 68040 stores the updated
             address register (M68000PRM MOVES implementation note). */
            syn68k_ea_write_memory_fc(context, ea.address, size, cpu_state.regs[reg].ul.n, fc);
#ifndef SYN68K_NATIVE040_TEMPLATE
            if (!context->error && !context->memory)
                syn68k_invalidate_checked_write(ea.address, size);
#endif
        }
        else {
            uint32 value = syn68k_ea_read_memory_fc(context, ea.address, size, fc, 0);
            if (!context->error) {
                if (reg >= 8)
                    cpu_state.regs[reg].ul.n = size == 1 ? (int8)value : size == 2 ? (int16)value : value;
                else {
                    uint32 mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                    cpu_state.regs[reg].ul.n = (cpu_state.regs[reg].ul.n & ~mask) | (value & mask);
                }
            }
        }
    }
    if (context->error)
        return final_store_exception(step, context, context->error, pc);
    return context->pc;
}
