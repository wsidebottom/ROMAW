/* SPDX-License-Identifier: MIT */
/* Shared checked and generated 68040 operand logic. */
static uint32 final_store_exception(Syn68kIntegerStep* step, const Syn68kEAContext* context, unsigned vector, uint32 pc)
{
    uint32 next = integer_exception(step, context, vector, pc, context->fault_address, vector == 2 ? 7 : 0);
    if (step && context->memory && vector == 2 && context->access.writing) {
        step->store_state = 1;
        step->store_next = context->pc;
        step->store_ccr = COMPUTE_SR_FROM_CPU_STATE() & 31;
    }
    return next;
}
