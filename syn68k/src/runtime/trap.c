/*+===================================================================
  File:      trap.c

  Summary:   Implements trap support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_integer_resume_movem, syn68k_integer_step, syn68k_integer_step_decoded,
             syn68k_invalidate_checked_write, syn68k_set_reset_handler, trap_direct, trap_exception, trap_forwarded,
             trap_init, trap_install_handler, trap_remove_handler

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "trap.h"
#include "callback.h"
#include "ea.h"
#include "integer.h"
#include "translate.h"
#include "hash.h"
#include "jit-x64.h"
#include "control040.h"
#include <stdio.h>
#include <stdlib.h>

/* Pointer to the array of trap vectors. */
uint32* trap_vector_array;
static syn68k_reset_handler_t reset_handler;
static void* reset_argument;

void syn68k_set_reset_handler(syn68k_reset_handler_t handler, void* opaque)
{
    reset_handler = handler;
    reset_argument = handler ? opaque : NULL;
}

void trap_init()
{
    volatile TrapHandlerInfo* thi;
    int i;

    syn68k_set_reset_handler(NULL, NULL);

    cpu_state.vbr = US_TO_SYN68K(trap_vector_array);
    for (i = 63, thi = &cpu_state.trap_handler_info[63]; i >= 0; thi--, i--) {
        thi->callback_address = callback_install(trap_forwarded, (void*)thi);
        WRITE_LONG(&trap_vector_array[i], thi->callback_address);
        thi->func = NULL;
        thi->arg = NULL;
    }
}

void trap_install_handler(unsigned trap_number, callback_handler_t func, void* arbitrary_argument)
{
    cpu_state.trap_handler_info[trap_number].func = func;
    cpu_state.trap_handler_info[trap_number].arg = arbitrary_argument;
}

void trap_remove_handler(unsigned trap_number)
{
    cpu_state.trap_handler_info[trap_number].func = NULL;
}

uint32 trap_forwarded(uint32 m68k_address, void* arg)
{
    const TrapHandlerInfo* thi = (TrapHandlerInfo*)arg;

    if (thi->func == NULL) /* If no handler installed, just rte. */
    {
        static const char* trap_description[] = {/* I've only put the really interesting ones in. */
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 "Illegal instruction",
                                                 "Integer divide by zero",
                                                 "CHK/CHK2",
                                                 "FTRAPcc, TRAPcc, TRAPV",
                                                 "Privilege violation",
                                                 "Trace",
                                                 "A-line",
                                                 "F-line",
                                                 NULL,
                                                 NULL,
                                                 "Format error"};
        int which_trap = thi - cpu_state.trap_handler_info;

        fprintf(stderr, "Unhandled trap %d!", which_trap);
        if (which_trap >= 0 && which_trap < sizeof trap_description / sizeof trap_description[0] &&
            trap_description[which_trap] != NULL)
            fprintf(stderr, " (%s)", trap_description[which_trap]);
        putc('\n', stderr);

        return MAGIC_RTE_ADDRESS;
    }
    return thi->func(READUL(EM_A7 + 2), thi->arg);
}

uint32 trap_exception(uint32 trap_number, uint32 exception_pc, uint32 exception_address, unsigned format)
{
    unsigned length = format == 0 ? 8 : format == 7 ? 60 : 12;
    uint16 old_sr = COMPUTE_SR_FROM_CPU_STATE();
    uint32 trap_addr = READUL(cpu_state.vbr + (trap_number << 2));
    cpu_state.sr = (cpu_state.sr & 0x3fff) | SR_SUPERVISOR_BIT;
    SWITCH_A7(old_sr, cpu_state.sr);
    EM_A7 -= length;
    for (unsigned offset = 0; offset < length; offset += 2)
        WRITEUW(EM_A7 + offset, 0);
    WRITEUW(EM_A7, old_sr);
    WRITEUL(EM_A7 + 2, exception_pc);
    WRITEUW(EM_A7 + 6, (format << 12) | (trap_number << 2));
    if (format == 7) {
        /* No buffered writebacks in the synchronous interpreter. The complete
         MMU/restart protocol is outside this bounded FPU access-fault path. */
        WRITEUL(EM_A7 + 8, exception_address);
        WRITEUL(EM_A7 + 20, exception_address);
    }
    else if (format != 0)
        WRITEUL(EM_A7 + 8, exception_address);
    const volatile TrapHandlerInfo* thi = &cpu_state.trap_handler_info[trap_number];
    if (trap_addr == thi->callback_address && thi->func != NULL)
        return thi->func(exception_pc, thi->arg);
    return trap_addr;
}

/* Keep the live stack update before selecting another SR stack bank. */
static void load_status_register(uint16 value)
{
    uint16 old_sr = COMPUTE_SR_FROM_CPU_STATE();
    value &= 0xf71f;
    cpu_state.sr = value & ~31;
    cpu_state.ccc = value & 1;
    cpu_state.ccv = value & 2;
    cpu_state.ccnz = (~value) & 4;
    cpu_state.ccn = value & 8;
    cpu_state.ccx = value & 16;
    SWITCH_A7(old_sr, value);
    interrupt_note_if_present();
}

static void push_short_frame(uint16 sr, uint32 pc, unsigned vector, unsigned format)
{
    EM_A7 -= 8;
    uint16* frame = SYN68K_TO_US(CLEAN(EM_A7));
    WRITE_WORD(frame, sr);
    WRITE_LONG(frame + 1, pc);
    WRITE_WORD(frame + 3, (format << 12) | (vector << 2));
}

static int condition_true(unsigned condition)
{
    int c = !!cpu_state.ccc, v = !!cpu_state.ccv, n = !!cpu_state.ccn;
    int z = !cpu_state.ccnz;
    switch (condition) {
    case 0:
        return 1;
    case 1:
        return 0;
    case 2:
        return !c && !z;
    case 3:
        return c || z;
    case 4:
        return !c;
    case 5:
        return c;
    case 6:
        return !z;
    case 7:
        return z;
    case 8:
        return !v;
    case 9:
        return v;
    case 10:
        return !n;
    case 11:
        return n;
    case 12:
        return n == v;
    case 13:
        return n != v;
    case 14:
        return !z && n == v;
    default:
        return z || n != v;
    }
}

/* Cache only an existing return block. Never fetch/compile a return site
   before the callee actually returns to it. Any block destruction clears the
   entire return cache, so entries cannot outlive their compiled blocks. */
static void cache_return(uint32 pc)
{
    Block* block = hash_lookup(pc);
    if (block && block->compiled_code) {
        unsigned index = (cpu_state.jsr_stack_byte_index + sizeof cpu_state.jsr_stack - sizeof(jsr_stack_elt_t)) %
                         sizeof cpu_state.jsr_stack;
        cpu_state.jsr_stack_byte_index = index;
        jsr_stack_elt_t* entry = (jsr_stack_elt_t*)((char*)cpu_state.jsr_stack + index);
        entry->tag = SWAPUL_IFLE(pc);
        entry->code = block->compiled_code;
    }
}

/* Checked stores can overwrite translated code, including their own block.
   Native guards tail out before this write/invalidation path. */
void syn68k_invalidate_checked_write(uint32 address, unsigned size)
{
    address &= syn68k_address_mask;
    if ((uint64)address + size <= (uint64)syn68k_address_mask + 1)
        destroy_blocks(address, size);
    else
        for (unsigned i = 0; i < size; ++i)
            destroy_blocks((address + i) & syn68k_address_mask, 1);
}

static void write_checked_memory(Syn68kEAContext* context, uint32 address, unsigned size, uint32 value)
{
    syn68k_ea_write_memory(context, address, size, value);
    if (!context->error && !context->memory)
        syn68k_invalidate_checked_write(address, size);
}

static void write_stack_long(Syn68kEAContext* context, uint32 value)
{
    write_checked_memory(context, EM_A7, 4, value);
}

/* A mapped step reports exceptions without using the flat vector/stack bus.
   Ordinary dispatch retains its existing exception delivery paths. */
static uint32 integer_exception(Syn68kIntegerStep* step, const Syn68kEAContext* context, unsigned vector, uint32 pc,
                                uint32 address, unsigned format)
{
    if (!step)
        return vector == 5 ? trap_direct(vector, pc, address) : trap_exception(vector, pc, address, format);
    step->vector = vector;
    step->exception_pc = pc;
    step->fault_address = address;
    step->format = format;
    step->access = context->access;
    return pc;
}

/* These stores leave CCR unchanged. Preserve component-step fault state; the
   mapped runner alone retires their final scalar and any captured LINK updates.
   Capturing live CCR also retains callback side effects. */
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

static uint32 call_store_exception(Syn68kIntegerStep* step, const Syn68kEAContext* context, uint32 pc, uint32 target)
{
    uint32 next = final_store_exception(step, context, 2, pc);
    if (step && step->store_state == 1)
        step->store_next = target;
    return next;
}

/* A failed frame read has not popped the current frame. Resume at the live
   selected SP; earlier throwaway frames and callback effects stay committed. */
static uint32 rte_read_exception(uint32 pc, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    uint32 next = integer_exception(step, context, 2, pc, context->fault_address, 7);
    uint16 ssw;
    if (step && context->memory && context->access.fc == (cpu_state.sr & SR_SUPERVISOR_BIT ? 5u : 1u) &&
        !context->access.writing && !context->access.rmw) {
        if (syn68k_mmu040_access_ssw(&context->access, &ssw))
            step->restartable_read = 1;
        else if (syn68k_mmu040_cache_push_ssw(&context->access, &ssw))
            step->cache_restart = 2; /* Keep the live SP after completed throwaway frames. */
    }
    return next;
}

static uint32 move16(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, reg = opcode & 7, other = 0;
    unsigned length = mode == 4 ? 4 : 6;
    if (mode > 4)
        return integer_exception(step, context, 11, pc, 0, 0);
    if (!context->memory && !syn68k_memory_span(pc, length))
        return integer_exception(step, context, 2, pc, pc + 2, 7);
    uint32 source, destination;
    if (mode == 4) {
        unsigned extension = syn68k_ea_fetch_memory(context, pc + 2, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        if ((extension & 0x8FFF) != 0x8000)
            return integer_exception(step, context, 11, pc, 0, 0);
        other = (extension >> 12) & 7;
        source = EM_AREG(reg);
        destination = EM_AREG(other);
    }
    else {
        uint32 absolute = syn68k_ea_fetch_memory(context, pc + 2, 4);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        uint32 indirect = EM_AREG(reg);
        source = mode & 1 ? absolute : indirect;
        destination = mode & 1 ? indirect : absolute;
    }
    uint32 source_line = source & ~15u, destination_line = destination & ~15u;
    if (!context->memory) {
        if (!syn68k_memory_span(source_line, 16))
            return integer_exception(step, context, 2, pc, source_line, 7);
        if (!syn68k_memory_span(destination_line, 16))
            return integer_exception(step, context, 2, pc, destination_line, 7);
    }
    /* Each burst starts at the longword selected by its own EA and wraps
       within the line. Keep holding-register slots in memory-address order. */
    uint32 line[4];
    unsigned char bytes[16];
    unsigned read_cached = 0;
    uint32 source_physical = 0;
    if (context->memory) {
        unsigned fc = cpu_state.sr & SR_SUPERVISOR_BIT ? 5 : 1;
        context->access = syn68k_mmu040_move16(context->memory, source, fc, 0, bytes);
        if (context->access.translation.fault != MMU040_OK || context->access.bus_error) {
            context->error = 2;
            context->fault_address = context->access.logical;
        }
        else {
            source_physical = context->access.translation.physical;
            read_cached = context->access.cache.completed == 16 && !context->access.cache.cycle.burst;
            for (unsigned word = 0; word < 4; ++word)
                line[word] = (uint32)bytes[word * 4] << 24 | (uint32)bytes[word * 4 + 1] << 16 |
                             (uint32)bytes[word * 4 + 2] << 8 | bytes[word * 4 + 3];
        }
    }
    else
        for (unsigned i = 0; i < 4; ++i) {
            unsigned word = ((source >> 2) + i) & 3;
            line[word] = syn68k_ea_read_memory(context, source_line + word * 4, 4);
            if (context->error)
                break;
        }
    if (context->error) {
        uint32 next = integer_exception(step, context, 2, pc, context->fault_address, 7);
        if (step && context->memory)
            step->move16 = (Syn68kMOVE16Writeback){.address = source & ~3u,
                                                   .next = pc + length,
                                                   .source = source,
                                                   .destination = destination,
                                                   .command = (opcode & 63) | (other << 6),
                                                   .state = 5};
        return next;
    }
    if (context->memory) {
        unsigned fc = cpu_state.sr & SR_SUPERVISOR_BIT ? 5 : 1;
        context->access = syn68k_mmu040_move16(context->memory, destination, fc, 1, bytes);
        if (context->access.translation.fault != MMU040_OK || context->access.bus_error) {
            context->error = 2;
            context->fault_address = context->access.logical;
            /* UMAD item 6: a write-protected same-register MOVE16 loses its
               dirty cached source even though the write never reaches memory. */
            if (mode == 4 && reg == other && read_cached &&
                context->access.translation.fault == MMU040_WRITE_PROTECTED && context->memory->cache) {
                Syn68kCache040* cache = context->memory->cache->state;
                if (!cache->active)
                    for (unsigned way = 0; way < 4; ++way) {
                        Syn68kCache040Line* entry = &cache->lines[0][(source_physical >> 4) & 63][way];
                        if (entry->valid && entry->dirty && entry->address == (source_physical & ~15u))
                            entry->valid = 0;
                    }
            }
        }
    }
    else
        for (unsigned i = 0; i < 4; ++i) {
            unsigned word = ((destination >> 2) + i) & 3;
            syn68k_ea_write_memory(context, destination_line + word * 4, 4, line[word]);
            if (context->error)
                break;
        }
    if (context->error) {
        uint32 next = integer_exception(step, context, 2, pc, context->fault_address, 7);
        if (step && context->memory) {
            step->move16 = (Syn68kMOVE16Writeback){.address = destination & ~3u,
                                                   .next = pc + length,
                                                   .source = source,
                                                   .destination = destination,
                                                   .command = (opcode & 63) | (other << 6),
                                                   .state = read_cached && context->access.cache_table &&
                                                                    context->access.cache.fault == CACHE040_PUSH_FAULT
                                                                ? 7
                                                                : 1};
            for (unsigned j = 0; j < 4; ++j)
                step->move16.data[j] = line[j];
        }
        return next;
    }
    if (!context->memory)
        syn68k_invalidate_checked_write(destination_line, 16);
    if (mode == 4) {
        EM_AREG(reg) = source + 16;
        EM_AREG(other) = destination + 16;
    }
    else if (mode < 2)
        EM_AREG(reg) += 16;
    return pc + length;
}

static uint32 divide_word(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
    if (mode == 1 || (mode == 7 && ea_reg > 4))
        return integer_exception(step, context, 4, pc, 0, 0);
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 2, 0, 1);
    uint32 source = syn68k_ea_read_operand(context, ea, 2);
    if (context->error)
        return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc, context->fault_address,
                                 context->error == 2 ? 7 : 0);
    unsigned reg = (opcode >> 9) & 7;
    int sign = !!(opcode & 0x0100);
    uint32 original = cpu_state.regs[reg].ul.n;
    cpu_state.ccc = 0;
    if (!(uint16)source)
        return integer_exception(step, context, 5, context->pc, pc, 2);
    /* Widen signed division before INT32_MIN / -1; a 16-bit quotient
     overflow must set V, not raise a host arithmetic exception. */
    int64 dividend = sign ? (int64)(int32)original : (int64)original;
    int64 divisor = sign ? (int16)source : (uint16)source;
    int64 quotient = dividend / divisor, remainder = dividend % divisor;
    cpu_state.ccv = sign ? quotient < -32768 || quotient > 32767 : quotient > 65535;
    /* The old x86 signed-divide fast paths tested quotient + 32768 when
     reporting overflow. Preserve those undefined N/Z values for the forms
     that previously used them; interpreted/generic forms tested quotient. */
    uint32 flag_result = quotient;
    cpu_state.ccn = sign ? (flag_result >> 31) : (flag_result >> 15) & 1;
    cpu_state.ccnz = flag_result != 0;
    if (!cpu_state.ccv)
        cpu_state.regs[reg].ul.n = ((uint32)(uint16)remainder << 16) | (uint16)quotient;
    return context->pc;
}

static uint32 multiply_word(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
    if (mode == 1 || (mode == 7 && ea_reg > 4))
        return integer_exception(step, context, 4, pc, 0, 0);
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 2, 0, 1);
    uint32 source = syn68k_ea_read_operand(context, ea, 2);
    if (context->error)
        return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc, context->fault_address,
                                 context->error == 2 ? 7 : 0);
    unsigned reg = (opcode >> 9) & 7;
    uint32 original = cpu_state.regs[reg].ul.n;
    /* The complete product of two signed/unsigned words fits 32 bits. */
    uint32 result = opcode & 0x0100 ? (int32)(int16)original * (int32)(int16)source
                                    : (uint32)(uint16)original * (uint32)(uint16)source;
    cpu_state.regs[reg].ul.n = result;
    cpu_state.ccn = result >> 31;
    cpu_state.ccnz = result != 0;
    cpu_state.ccv = cpu_state.ccc = 0;
    return context->pc;
}

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

static uint32 multiply_long(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
    if (mode == 1 || (mode == 7 && ea_reg > 4))
        return integer_exception(step, context, 4, pc, 0, 0);
    unsigned extension = syn68k_ea_fetch_memory(context, context->pc, 2);
    if (context->error)
        return integer_exception(step, context, 2, pc, context->fault_address, 7);
    if (extension & 0x83f8)
        return integer_exception(step, context, 4, pc, 0, 0);
    context->pc += 2;
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 4, 0, 1);
    uint32 source = syn68k_ea_read_operand(context, ea, 4);
    if (context->error)
        return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc, context->fault_address,
                                 context->error == 2 ? 7 : 0);
    unsigned low = (extension >> 12) & 7, high = extension & 7;
    uint32 multiplicand = cpu_state.regs[low].ul.n;
    int signed_product = !!(extension & 0x0800), wide = !!(extension & 0x0400);
    /* Widen before multiplication; all signed 32-by-32 products fit int64. */
    uint64 product =
        signed_product ? (uint64)((int64)(int32)multiplicand * (int32)source) : (uint64)multiplicand * source;
    uint32 result = product;
    cpu_state.regs[low].ul.n = result;
    if (wide) {
        /* Dh == Dl is architecturally undefined; retain the legacy high word. */
        cpu_state.regs[high].ul.n = product >> 32;
        cpu_state.ccn = product >> 63;
        cpu_state.ccnz = product != 0;
        cpu_state.ccv = 0;
    }
    else {
        cpu_state.ccn = result >> 31;
        cpu_state.ccnz = result != 0;
        cpu_state.ccv = signed_product ? (int64)product != (int64)(int32)result : (product >> 32) != 0;
    }
    cpu_state.ccc = 0;
    return context->pc;
}

/* Long division uses unsigned magnitudes so INT64_MIN / -1 never reaches
   undefined host signed division. Fetch everything before changing Dq/Dr. */
static uint32 divide_long(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
    if (mode == 1 || (mode == 7 && ea_reg > 4))
        return integer_exception(step, context, 4, pc, 0, 0);
    unsigned extension = syn68k_ea_fetch_memory(context, context->pc, 2);
    if (context->error)
        return integer_exception(step, context, 2, pc, context->fault_address, 7);
    if (extension & 0x83f8)
        return integer_exception(step, context, 4, pc, 0, 0);
    context->pc += 2;
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 4, 0, 1);
    uint32 divisor = syn68k_ea_read_operand(context, ea, 4);
    if (context->error)
        return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc, context->fault_address,
                                 context->error == 2 ? 7 : 0);
    unsigned quotient_reg = (extension >> 12) & 7;
    unsigned remainder_reg = extension & 7;
    int signed_divide = !!(extension & 0x0800);
    int wide = !!(extension & 0x0400);
    uint64 dividend = cpu_state.regs[quotient_reg].ul.n;
    if (wide)
        dividend |= (uint64)cpu_state.regs[remainder_reg].ul.n << 32;
    else if (signed_divide && (dividend & 0x80000000u))
        dividend |= (uint64)0xffffffffu << 32;

    /* C is defined even on zero/overflow; retain undefined N/Z/V on zero. */
    cpu_state.ccc = 0;
    if (!divisor)
        return integer_exception(step, context, 5, context->pc, pc, 2);
    int negative_dividend = signed_divide && (dividend >> 63);
    int negative_divisor = signed_divide && (divisor & 0x80000000u);
    if (negative_dividend)
        dividend = (uint64)0 - dividend;
    if (negative_divisor)
        divisor = (uint32)0 - divisor;
    uint64 quotient = dividend / divisor;
    uint32 remainder = dividend % divisor;
    int negative_quotient = negative_dividend != negative_divisor;
    uint64 maximum = signed_divide ? (negative_quotient ? 0x80000000u : 0x7fffffffu) : 0xffffffffu;
    cpu_state.ccv = quotient > maximum;
    if (!cpu_state.ccv) {
        uint32 result = (uint32)quotient;
        if (negative_quotient)
            result = (uint32)0 - result;
        if (negative_dividend)
            remainder = (uint32)0 - remainder;
        /* Dr == Dq retains only the quotient, including 64-bit dividends. */
        if (remainder_reg != quotient_reg)
            cpu_state.regs[remainder_reg].ul.n = remainder;
        cpu_state.regs[quotient_reg].ul.n = result;
        cpu_state.ccn = result >> 31;
        cpu_state.ccnz = result != 0;
    }
    return context->pc;
}

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
        if (!context->memory && fc != 1 && fc != 5) {
            context->error = 2;
            context->fault_address = ea.address;
        }
        else if (writing) {
            /* For An,(An)+ and An,-(An), the 68040 stores the updated
             address register (M68000PRM MOVES implementation note). */
            syn68k_ea_write_memory_fc(context, ea.address, size, cpu_state.regs[reg].ul.n, fc);
            if (!context->error && !context->memory)
                syn68k_invalidate_checked_write(ea.address, size);
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

static uint32 control_address(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
    if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3))
        return integer_exception(step, context, 4, pc, 0, 0);
    Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 4, 0, 0);
    if (context->error)
        return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc, context->fault_address,
                                 context->error == 2 ? 7 : 0);
    if ((opcode & 0xf1c0) == 0x41c0) {
        /* Resolve the complete EA before replacing a source/index register. */
        EM_AREG((opcode >> 9) & 7) = ea.address;
        return context->pc;
    }
    int pea = (opcode & 0xffc0) == 0x4840;
    uint32 next = context->memory ? context->pc : CLEAN(context->pc);
    if (pea || !(opcode & 0x40)) {
        /* Resolve the complete EA before changing SP, including an A7 base. */
        EM_A7 -= 4;
        write_stack_long(context, pea ? ea.address : next);
        if (context->error) {
            if (pea)
                return final_store_exception(step, context, 2, pc);
            return call_store_exception(step, context, pc, ea.address);
        }
        if (!pea && !context->memory)
            cache_return(next);
    }
    return pea ? context->pc : context->memory ? ea.address : CLEAN(ea.address);
}

static uint32 move_peripheral(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    int16 displacement = syn68k_ea_fetch_memory(context, context->pc, 2);
    if (context->error)
        return integer_exception(step, context, 2, pc, context->fault_address, 7);
    uint32 address = EM_AREG(opcode & 7) + displacement;
    unsigned reg = (opcode >> 9) & 7;
    unsigned count = opcode & 0x40 ? 4 : 2;
    uint32 value = cpu_state.regs[reg].ul.n, result = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (opcode & 0x80)
            write_checked_memory(context, address + i * 2, 1, value >> ((count - 1 - i) * 8));
        else
            result = (result << 8) | syn68k_ea_read_memory(context, address + i * 2, 1);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
    }
    /* Commit loads only after every byte is available; stores retain
     completed byte transfers if a later sparse address faults. */
    if (!(opcode & 0x80))
        cpu_state.regs[reg].ul.n = count == 4 ? result : (value & 0xffff0000u) | result;
    return pc + 4;
}

static uint32 integer_pc(const Syn68kEAContext* context, uint32 value)
{
    return context->memory ? value : CLEAN(value);
}

static uint32 execute_cache(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    if (!(opcode & 0x18))
        return integer_exception(step, context, 4, pc, 0, 0);
    if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
        return integer_exception(step, context, 8, pc, 0, 0);
    context->access = syn68k_mmu040_cache_maintain(context->memory, (opcode >> 6) & 3, (opcode >> 3) & 3,
                                                   EM_AREG(opcode & 7), !!(opcode & 0x20));
    if (context->access.cache.interrupt_level) {
        step->interrupt_level = context->access.cache.interrupt_level;
        return pc;
    }
    if (context->access.translation.fault || context->access.bus_error)
        return integer_exception(step, context, 2, pc, context->access.logical, 7);
    return pc + 2;
}

static int control_opcode(unsigned opcode)
{
    return (opcode & 0xFFF0) == 0xF500 || opcode == 0xF510 || opcode == 0xF518 || (opcode & 0xFFD8) == 0xF548 ||
           (opcode & 0xFFFE) == 0x4E7A || (opcode >> 12) == 6 || (opcode & 0xF0F8) == 0x50C8 ||
           (opcode & 0xFFF8) == 0x4E50 || (opcode & 0xFFF8) == 0x4808 || (opcode & 0xFFF8) == 0x4E58 ||
           opcode == 0x4E75 || opcode == 0x4E74 || opcode == 0x4E77 || opcode == 0x4E73 || opcode == 0x4E70 ||
           (opcode & 0xFFF0) == 0x4E60 || (opcode & 0xFFC0) == 0x42C0 || (opcode & 0xFFC0) == 0x44C0 ||
           (opcode & 0xFFC0) == 0x40C0 || (opcode & 0xFFC0) == 0x46C0 || opcode == 0x007C || opcode == 0x027C ||
           opcode == 0x0A7C || ((opcode & 0xF0FF) >= 0x50FA && (opcode & 0xF0FF) <= 0x50FC);
}

static uint32 execute_control(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    if ((opcode & 0xFF00) == 0xF500) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        if (!context->memory || !context->memory->mmu)
            return integer_exception(step, context, 11, pc, 0, 0);
        unsigned fc = cpu_state.dfc & 7;
        if ((opcode & 0xFFD8) == 0xF548) {
            Syn68kMMU040Access access =
                syn68k_mmu040_memory_translate(context->memory, EM_AREG(opcode & 7), fc, !(opcode & 0x20), 1);
            if (access.cache_exception) {
                context->access = access;
                return integer_exception(step, context, 2, pc, access.logical, 7);
            }
        }
        else {
            int all = !!(opcode & 16);
            /* Per-address flushing with a nonordinary DFC is undefined.
             Select no entries rather than inventing a privilege alias. */
            if (all || fc == 1 || fc == 2 || fc == 5 || fc == 6)
                syn68k_mmu040_flush(context->memory->mmu, all, EM_AREG(opcode & 7), !!(fc & 4), !!(opcode & 8));
        }
        return pc + 2;
    }
    if ((opcode & 0xFFFE) == 0x4E7A) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        unsigned extension = syn68k_ea_fetch_memory(context, pc + 2, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        unsigned reg = extension >> 12, selector = extension & 0xfff;
        int writing = opcode & 1;
        uint32 value = cpu_state.regs[reg].ul.n;
        if (!syn68k_control040_transfer(&cpu_state, context->memory, selector, writing, &value))
            return integer_exception(step, context, 4, pc, 0, 0);
        if (!writing)
            cpu_state.regs[reg].ul.n = value;
        return pc + 4;
    }
    if ((opcode & 0xf0f8) == 0x50c8) {
        context->pc = pc + 2;
        int32 displacement = (int16)syn68k_ea_fetch_memory(context, context->pc, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        /* The conditional-branch prefetch rule also applies on fallthrough. */
        if (step && (displacement & 1))
            step->prefetch_address = integer_pc(context, pc + 2 + displacement);
        if (!condition_true((opcode >> 8) & 15)) {
            unsigned reg = opcode & 7;
            uint32 original = cpu_state.regs[reg].ul.n;
            uint16 counter = original - 1;
            cpu_state.regs[reg].ul.n = (original & 0xffff0000u) | counter;
            if (counter != 0xffff) {
                if (step)
                    step->flow_changed = 1;
                return integer_pc(context, pc + 2 + displacement);
            }
        }
        return integer_pc(context, pc + 4);
    }
    if ((opcode >> 12) == 6 && ((opcode >> 8) & 15) != 1) {
        context->pc = pc + 2;
        unsigned size = (opcode & 255) == 0 ? 2 : (opcode & 255) == 255 ? 4 : 0;
        uint32 displacement = (int8)opcode;
        if (size) {
            displacement = syn68k_ea_fetch_memory(context, context->pc, size);
            if (context->error)
                return integer_exception(step, context, 2, pc, context->fault_address, 7);
            if (size == 2)
                displacement = (int16)displacement;
            context->pc += size;
        }
        uint32 target = integer_pc(context, pc + 2 + displacement);
        unsigned taken = condition_true((opcode >> 8) & 15);
        if (step) {
            step->flow_changed = taken;
            if (target & 1)
                step->prefetch_address = target;
        }
        return taken ? target : integer_pc(context, context->pc);
    }
    if ((opcode & 0xff00) == 0x6100) {
        context->pc = pc + 2;
        uint32 displacement = (int8)opcode;
        unsigned size = (opcode & 255) == 0 ? 2 : (opcode & 255) == 255 ? 4 : 0;
        if (size) {
            displacement = syn68k_ea_fetch_memory(context, context->pc, size);
            if (context->error)
                return integer_exception(step, context, 2, pc, context->fault_address, 7);
            if (size == 2)
                displacement = (int16)displacement;
            context->pc += size;
        }
        EM_A7 -= 4;
        write_stack_long(context, integer_pc(context, context->pc));
        if (context->error)
            return call_store_exception(step, context, pc, integer_pc(context, pc + 2 + displacement));
        if (!context->memory)
            cache_return(integer_pc(context, context->pc));
        return integer_pc(context, pc + 2 + displacement);
    }
    if ((opcode & 0xfff8) == 0x4e50 || (opcode & 0xfff8) == 0x4808) {
        unsigned reg = 8 + (opcode & 7);
        unsigned size = (opcode & 0xfff8) == 0x4e50 ? 2 : 4;
        context->pc = pc + 2;
        uint32 displacement = syn68k_ea_fetch_memory(context, context->pc, size);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        if (size == 2)
            displacement = (int16)displacement;
        context->pc += size;
        /* Resolve the register source before the implicit stack EA update,
           including LINK A7. The displacement was already fetched once. */
        uint32 saved = cpu_state.regs[reg].ul.n;
        EM_A7 -= 4;
        write_checked_memory(context, EM_A7, 4, saved);
        if (context->error) {
            uint32 next = final_store_exception(step, context, 2, pc);
            if (step && step->store_state == 1)
                step->store_link_displacement = displacement;
            return next;
        }
        cpu_state.regs[reg].ul.n = EM_A7;
        EM_A7 += displacement;
        return context->pc;
    }
    if ((opcode & 0xfff8) == 0x4e58) {
        unsigned reg = 8 + (opcode & 7);
        context->pc = pc + 2;
        EM_A7 = cpu_state.regs[reg].ul.n;
        uint32 saved = syn68k_ea_read_memory(context, EM_A7, 4);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        cpu_state.regs[reg].ul.n = saved;
        EM_A7 += 4;
        return pc + 2;
    }
    if (opcode == 0x4e75) {
        context->pc = pc + 2;
        uint32 target = syn68k_ea_read_memory(context, EM_A7, 4);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        EM_A7 += 4;
        return target;
    }
    if (opcode == 0x4e77) {
        context->pc = pc + 2;
        uint16 ccr = syn68k_ea_read_memory(context, EM_A7, 2);
        uint32 target = syn68k_ea_read_memory(context, EM_A7 + 2, 4);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        EM_A7 += 6;
        cpu_state.ccc = ccr & 1;
        cpu_state.ccv = ccr & 2;
        cpu_state.ccnz = (~ccr) & 4;
        cpu_state.ccn = ccr & 8;
        cpu_state.ccx = ccr & 16;
        return target;
    }
    if (opcode == 0x4e74) {
        context->pc = pc + 2;
        int16 displacement = syn68k_ea_fetch_memory(context, context->pc, 2);
        uint32 target = syn68k_ea_read_memory(context, EM_A7, 4);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        EM_A7 += 4 + displacement;
        return target;
    }
    if ((opcode & 0xf0ff) >= 0x50fa && (opcode & 0xf0ff) <= 0x50fc) {
        unsigned bytes = (opcode & 255) == 0xfc ? 0 : (opcode & 255) == 0xfa ? 2 : 4;
        context->pc = pc + 2 + bytes;
        if (bytes)
            syn68k_ea_fetch_memory(context, pc + 2, bytes);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        return condition_true((opcode >> 8) & 15) ? integer_exception(step, context, 7, context->pc, pc, 2)
                                                  : context->pc;
    }
    if (opcode == 0x4e73) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        for (;;) {
            context->pc = pc + 2;
            uint16 saved_sr = syn68k_ea_read_memory(context, EM_A7, 2);
            uint32 saved_pc = syn68k_ea_read_memory(context, EM_A7 + 2, 4);
            unsigned format = syn68k_ea_read_memory(context, EM_A7 + 6, 2) >> 12;
            if (context->error)
                return rte_read_exception(pc, context, step);
            unsigned length = format <= 1 ? 8 : format <= 3 ? 12 : format == 7 ? 60 : 0;
            if (!length)
                return integer_exception(step, context, 14, pc, 0, 0);
            uint32 frame_ea = 0;
            unsigned continuation = 0;
            /* The access-error writebacks belong to the guest handler.
               MC68040 UM 8.4.6.7 reads only offsets $0 through $D on RTE. */
            unsigned read_end = format == 7 ? 14 : length;
            for (unsigned i = 8; i < read_end && !context->error; i += 4) {
                uint32 value = syn68k_ea_read_memory(context, EM_A7 + i, format == 7 && i == 12 ? 2 : 4);
                if (i == 8)
                    frame_ea = value;
                else if (i == 12)
                    continuation = value;
            }
            if (context->error)
                return rte_read_exception(pc, context, step);
            unsigned fpu_vector = 0;
            if (context->memory && format == 7) {
                if ((continuation & 0xf000) == 0x4000)
                    fpu_vector = 11;
                else if ((continuation & 0xf000) == 0x8000) {
                    fpu_vector = cpu_state.fpu.pending_vector;
                    if (fpu_vector < 48 || fpu_vector > 55)
                        return integer_exception(step, context, 14, pc, 0, 0);
                }
            }
            EM_A7 += length;
            load_status_register(saved_sr);
            /* MC68040 UM 8.4.6.7: CP/CU immediately dispatch the pending
               exception using the restored EA, without executing saved PC.
               Preserve stacked trace bits for the FPU exception handler. */
            if (fpu_vector)
                return integer_exception(step, context, fpu_vector, saved_pc, frame_ea, fpu_vector == 11 ? 2 : 3);
            /* A completed mapped store with CT pending becomes a normal trace
             exception after handler cleanup, before the next instruction.
             External entry recreates the format-2 frame at old SP + $30. */
            if (context->memory && format == 7 && (continuation & 0xF000) == 0x2000)
                return integer_exception(step, context, 9, saved_pc, frame_ea, 2);
            if (context->memory && step && format == 7 && (continuation & 0xF000) == 0x1000) {
                step->movem_state = 2;
                step->movem_ea = frame_ea;
            }
            if (format != 1)
                return saved_pc;
        }
    }
    if (opcode == 0x4e72) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        unsigned value = syn68k_ea_fetch_memory(context, pc + 2, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        load_status_register(value);
        if (step)
            step->stopped = 1;
        return pc + 4;
    }
    if (opcode == 0x4e70) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        if (reset_handler)
            reset_handler(reset_argument);
        return pc + 2;
    }
    if ((opcode & 0xfff0) == 0x4e60) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        unsigned reg = opcode & 7;
        if (opcode & 8)
            EM_AREG(reg) = FETCH_USP();
        else
            SET_USP(EM_AREG(reg));
        return pc + 2;
    }
    if ((opcode & 0xffc0) == 0x42c0 || (opcode & 0xffc0) == 0x44c0) {
        int from_ccr = (opcode & 0xffc0) == 0x42c0;
        unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
        if (mode == 1 || (mode == 7 && reg >= (from_ccr ? 2u : 5u)))
            return integer_exception(step, context, 4, pc, 0, 0);
        context->pc = pc + 2;
        Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 2, from_ccr, 1);
        if (!context->error) {
            if (from_ccr) {
                uint32 value = COMPUTE_SR_FROM_CPU_STATE() & 31;
                if (ea.reg >= 0)
                    cpu_state.regs[ea.reg].ul.n = (cpu_state.regs[ea.reg].ul.n & 0xFFFF0000U) | value;
                else
                    write_checked_memory(context, ea.address, 2, value);
            }
            else {
                uint32 value = syn68k_ea_read_operand(context, ea, 2);
                if (!context->error) {
                    cpu_state.ccc = value & 1;
                    cpu_state.ccv = value & 2;
                    cpu_state.ccnz = !(value & 4);
                    cpu_state.ccn = value & 8;
                    cpu_state.ccx = value & 16;
                }
            }
        }
        if (context->error)
            return final_store_exception(step, context, context->error == 11 ? 4 : context->error, pc);
        return context->pc;
    }
    if ((opcode & 0xffc0) == 0x40c0 || (opcode & 0xffc0) == 0x46c0) {
        int from_sr = (opcode & 0xffc0) == 0x40c0;
        unsigned mode = (opcode >> 3) & 7, reg = opcode & 7;
        /* Reject non-instructions before the privilege test. */
        if (mode == 1 || (mode == 7 && reg >= (from_sr ? 2u : 5u)))
            return integer_exception(step, context, 4, pc, 0, 0);
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        context->pc = pc + 2;
        Syn68kEA ea = syn68k_effective_address(context, opcode & 63, 2, from_sr, 1);
        if (!context->error) {
            if (from_sr) {
                uint16 value = COMPUTE_SR_FROM_CPU_STATE() & 0xf71f;
                if (ea.reg >= 0)
                    cpu_state.regs[ea.reg].ul.n = (cpu_state.regs[ea.reg].ul.n & 0xffff0000u) | value;
                else
                    write_checked_memory(context, ea.address, 2, value);
            }
            else {
                uint16 value = syn68k_ea_read_operand(context, ea, 2);
                if (!context->error)
                    load_status_register(value);
            }
        }
        if (context->error) {
            unsigned vector = context->error == 11 ? 4 : context->error;
            return final_store_exception(step, context, vector, pc);
        }
        return context->pc;
    }
    if (opcode == 0x027c || opcode == 0x007c || opcode == 0x0a7c) {
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return integer_exception(step, context, 8, pc, 0, 0);
        uint16 old_sr = COMPUTE_SR_FROM_CPU_STATE();
        uint16 immediate = syn68k_ea_fetch_memory(context, pc + 2, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        uint16 value = opcode == 0x027c   ? old_sr & immediate
                       : opcode == 0x007c ? old_sr | immediate
                                          : old_sr ^ immediate;
        load_status_register(value);
        return pc + 4;
    }
    return integer_exception(step, context, 4, pc, 0, 0);
}

static uint32 checked_bounds(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    if ((opcode & 0xF140) == 0x4100) {
        unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
        if (mode == 1 || (mode == 7 && ea_reg > 4))
            return integer_exception(step, context, 4, pc, 0, 0);
        unsigned size = opcode & 0x80 ? 2 : 4;
        context->pc = pc + 2;
        Syn68kEA ea = syn68k_effective_address(context, opcode & 63, size, 0, 1);
        uint32 bound = syn68k_ea_read_operand(context, ea, size);
        if (context->error)
            return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc,
                                     context->fault_address, context->error == 2 ? 7 : 0);
        uint32 raw = cpu_state.regs[(opcode >> 9) & 7].ul.n;
        int32 value = size == 2 ? (int16)raw : (int32)raw;
        int32 upper = size == 2 ? (int16)bound : (int32)bound;
        // Fetch the bound even for a negative Dn; access errors precede CHK.
        // X is preserved; the undefined Z/V/C retain their incoming values.
        cpu_state.ccn = value < 0;
        if (value < 0 || value > upper)
            return integer_exception(step, context, 6, context->pc, pc, 2);
        return context->pc;
    }
    if ((opcode & 0xF9C0) == 0x00C0 && (opcode & 0x0600) != 0x0600) {
        unsigned mode = (opcode >> 3) & 7, ea_reg = opcode & 7;
        if (mode != 2 && mode != 5 && mode != 6 && (mode != 7 || ea_reg > 3))
            return integer_exception(step, context, 4, pc, 0, 0);
        context->pc = pc + 2;
        unsigned extension = syn68k_ea_fetch_memory(context, context->pc, 2);
        if (context->error)
            return integer_exception(step, context, 2, pc, context->fault_address, 7);
        if (extension & 0x07ff)
            return integer_exception(step, context, 4, pc, 0, 0);
        context->pc += 2;
        unsigned size = 1u << ((opcode >> 9) & 3), reg = extension >> 12;
        Syn68kEA ea = syn68k_effective_address(context, opcode & 63, size, 0, 0);
        uint32 lower = syn68k_ea_read_memory(context, ea.address, size);
        uint32 upper = syn68k_ea_read_memory(context, ea.address + size, size);
        if (context->error)
            return integer_exception(step, context, context->error == 11 ? 4 : context->error, pc,
                                     context->fault_address, context->error == 2 ? 7 : 0);
        uint32 value = cpu_state.regs[reg].ul.n;
        if (reg & 8) {
            if (size == 1) {
                lower = (int8)lower;
                upper = (int8)upper;
            }
            else if (size == 2) {
                lower = (int16)lower;
                upper = (int16)upper;
            }
        }
        else if (size < 4)
            value &= size == 1 ? 0xff : 0xffff;
        uint32 mask = reg & 8 || size == 4 ? ~(uint32)0 : size == 1 ? 0xff : 0xffff;
        cpu_state.ccnz = value != lower && value != upper;
        cpu_state.ccc = ((value - lower) & mask) > ((upper - lower) & mask);
        if ((extension & 0x0800) && cpu_state.ccc)
            return integer_exception(step, context, 6, context->pc, pc, 2);
        return context->pc;
    }
    return integer_exception(step, context, 4, pc, 0, 0);
}

static int integer_opcode(unsigned opcode)
{
    return (opcode & 0xFFC0) == 0xF600 || (opcode & 0xF140) == 0x4100 ||
           ((opcode & 0xF9C0) == 0x00C0 && (opcode & 0x600) != 0x600) || control_opcode(opcode) ||
           (opcode & 0xf0c0) == 0xc0c0 || (opcode & 0xf0c0) == 0x80c0 || (opcode & 0xffc0) == 0x4c00 ||
           (opcode & 0xffc0) == 0x4c40 || ((opcode & 0xfb80) == 0x4880 && (opcode & 0x38) >= 0x10) ||
           ((opcode & 0xff00) == 0x0e00 && (opcode & 0xc0) != 0xc0) || (opcode & 0xff80) == 0x4e80 ||
           ((opcode & 0xf1c0) == 0x41c0 && (opcode & 0xFFF8) != 0x49C0) ||
           ((opcode & 0xffc0) == 0x4840 && (opcode & 0x30)) || (opcode & 0xf138) == 0x0108;
}

static uint32 execute_integer(uint32 pc, unsigned opcode, Syn68kEAContext* context, Syn68kIntegerStep* step)
{
    if ((opcode & 0xFFC0) == 0xF600)
        return move16(pc, opcode, context, step);
    if ((opcode & 0xF140) == 0x4100 || ((opcode & 0xF9C0) == 0x00C0 && (opcode & 0x600) != 0x600))
        return checked_bounds(pc, opcode, context, step);
    if (control_opcode(opcode) || (opcode == 0x4e72 && context->memory))
        return execute_control(pc, opcode, context, step);
    if ((opcode & 0xf0c0) == 0xc0c0)
        return multiply_word(pc, opcode, context, step);
    if ((opcode & 0xf0c0) == 0x80c0)
        return divide_word(pc, opcode, context, step);
    if ((opcode & 0xffc0) == 0x4c00)
        return multiply_long(pc, opcode, context, step);
    if ((opcode & 0xffc0) == 0x4c40)
        return divide_long(pc, opcode, context, step);
    if ((opcode & 0xff00) == 0x0e00)
        return move_alternate(pc, opcode, context, step);
    if ((opcode & 0xfb80) == 0x4880 && (opcode & 0x38) >= 0x10)
        return move_multiple(pc, opcode, context, step);
    if ((opcode & 0xf138) == 0x0108)
        return move_peripheral(pc, opcode, context, step);
    return control_address(pc, opcode, context, step);
}

int syn68k_integer_step_decoded(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result)
{
    if (!result)
        return 0;
    *result = (Syn68kIntegerStep){0};
    if (memory && (opcode & 0xff00) == 0xf400) {
        /* A missing board cache is unsupported, not an empty hardware cache.
           Static illegal encodings and privilege still have their priority. */
        if ((opcode & 0x18) && (cpu_state.sr & SR_SUPERVISOR_BIT) && !memory->cache)
            return 0;
        Syn68kEAContext context = {.pc = pc + 2, .memory = memory};
        result->next = execute_cache(pc, opcode, &context, result);
        return 1;
    }
    if (!integer_opcode(opcode) && !(opcode == 0x4e72 && memory))
        return syn68k_integer_operand_step(pc, opcode, memory, result);
    Syn68kEAContext context = {.pc = pc + 2, .memory = memory};
    result->next = execute_integer(pc, opcode, &context, result);
    return 1;
}

int syn68k_integer_resume_movem(uint32 pc, unsigned opcode, Syn68kMMU040Memory* memory, uint32 ea,
                                Syn68kIntegerStep* result)
{
    unsigned mode = (opcode >> 3) & 7, reg = opcode & 7, load = !!(opcode & 0x400);
    if (!memory || !result || (opcode & 0xFB80) != 0x4880 || mode < 2 || (load ? mode == 4 : mode == 3) ||
        (mode == 7 && reg > (load ? 3u : 1u)))
        return 0;
    *result = (Syn68kIntegerStep){0};
    Syn68kEAContext context = {.pc = pc + 2, .memory = memory, .resume_ea = &ea};
    result->next = move_multiple(pc, opcode, &context, result);
    if (result->vector == 2) {
        /* A live mask/extension fetch can fault before the operand loop has
           another opportunity to publish the saved continuation address. */
        result->movem_state = 1;
        result->movem_ea = ea;
    }
    return 1;
}

int syn68k_integer_step(uint32 pc, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result)
{
    if (!result)
        return 0;
    *result = (Syn68kIntegerStep){0};
    if (!memory && !syn68k_memory_span(pc, 2))
        return 0;
    Syn68kEAContext context = {.pc = pc + 2, .memory = memory};
    unsigned opcode = syn68k_ea_fetch_memory(&context, pc, 2);
    if (context.error) {
        result->next = integer_exception(result, &context, 2, pc, context.fault_address, 7);
        return 1;
    }
    return syn68k_integer_step_decoded(pc, opcode, memory, result);
}

uint32 trap_direct(uint32 trap_number, uint32 exception_pc, uint32 exception_address)
{
    if (trap_number == 4 && syn68k_memory_span(exception_pc, 2) && syn68k_checked_memory_opcode(READUW(exception_pc)))
        return syn68k_checked_memory_run(exception_pc, READUW(exception_pc));
    if (trap_number == 4) {
        unsigned opcode = syn68k_memory_span(exception_pc, 2) ? READUW(exception_pc) : 0;
        if (integer_opcode(opcode)) {
            Syn68kEAContext context = {.pc = exception_pc + 2};
            return execute_integer(exception_pc, opcode, &context, NULL);
        }
    }
    if (trap_number == 11 && syn68k_memory_span(exception_pc, 2) && (READUW(exception_pc) & 0xffc0) == 0xf600) {
        Syn68kEAContext context = {.pc = exception_pc + 2};
        return move16(exception_pc, READUW(exception_pc), &context, NULL);
    }
    if (trap_number == 11 && syn68k_memory_span(exception_pc, 2) && (READUW(exception_pc) & 0xff00) == 0xf400) {
        unsigned opcode = READUW(exception_pc);
        if (!(opcode & 0x18)) /* Scope 00 is explicitly illegal in the PRM. */
            return trap_exception(4, exception_pc, 0, 0);
        if (!(cpu_state.sr & SR_SUPERVISOR_BIT))
            return trap_exception(8, exception_pc, 0, 0);
        /* Guest data already resides in backing RAM: there are no dirty data
         cache lines to push. An instruction-cache operation invalidates
         translated code conservatively at all scopes. This provides code
         coherency, not a physical cache/MMU or cycle-accurate cache model. */
        if (opcode & 0x80)
            destroy_blocks(0, -1);
        return exception_pc + 2;
    }
    uint32 fpu_next;
    if (trap_number == 11 && syn68k_fpu_execute(exception_pc, &fpu_next))
        return fpu_next;
    if (trap_number == 2)
        return trap_exception(trap_number, exception_pc, exception_address, 7);
    if (trap_number >= 48 && trap_number <= 55)
        return trap_exception(trap_number, exception_pc, exception_address, 0);
    const volatile TrapHandlerInfo* thi;
    uint16 *p, old_sr;
    uint32 trap_addr;
    uint32 retval;
    unsigned short_format = 0;

    /* Fetch trap address from vector table. */
    trap_addr = READUL(cpu_state.vbr + (trap_number << 2));

    switch (trap_number) {
    case 4:  /* Illegal instruction. */
    case 8:  /* Privilege violation. */
    case 10: /* A-line. */
    case 11: /* F-line. */
    case 14: /* Format error. */
    case 15: /* Uninitialized interrupt. */
    case 24: /* Spurious interrupt. */
    case 25: /* Level 1 interrupt autovector. */
    case 26: /* Level 2 interrupt autovector. */
    case 27: /* Level 3 interrupt autovector. */
    case 28: /* Level 4 interrupt autovector. */
    case 29: /* Level 5 interrupt autovector. */
    case 30: /* Level 6 interrupt autovector. */
    case 31: /* Level 7 interrupt autovector. */
    case 32: /* TRAP #0 vector. */
    case 33: /* TRAP #1 vector. */
    case 34: /* TRAP #2 vector. */
    case 35: /* TRAP #3 vector. */
    case 36: /* TRAP #4 vector. */
    case 37: /* TRAP #5 vector. */
    case 38: /* TRAP #6 vector. */
    case 39: /* TRAP #7 vector. */
    case 40: /* TRAP #8 vector. */
    case 41: /* TRAP #9 vector. */
    case 42: /* TRAP #10 vector. */
    case 43: /* TRAP #11 vector. */
    case 44: /* TRAP #12 vector. */
    case 45: /* TRAP #13 vector. */
    case 46: /* TRAP #14 vector. */
    case 47: /* TRAP #15 vector. */
        /* Save away the old SR. */
        old_sr = COMPUTE_SR_FROM_CPU_STATE();

        /* Clear TR0 and TR1. */
        cpu_state.sr &= 0x3FFF;

        /* If this is an actual interrupt, adjust the SR appropriately. */
        if (trap_number >= 15 && trap_number <= 31) {
            /* Interrupts taken in master state first retain the real frame on
             MSP. Format 1 on ISP selects that bank again during RTE. Keep
             the saved M bit, as required by the format-1 RTE contract. */
            if ((old_sr & (SR_SUPERVISOR_BIT | SR_MASTER_BIT)) == (SR_SUPERVISOR_BIT | SR_MASTER_BIT)) {
                push_short_frame(old_sr, exception_pc, trap_number, 0);
                short_format = 1;
            }
            /* Bump up interrupt mask priority. */
            if (trap_number >= 24 && trap_number <= 31)
                cpu_state.sr = ((cpu_state.sr & ~(7 << 8)) | ((trap_number - 24) << 8));

            /* Switch into interrupt mode. */
            cpu_state.sr &= ~SR_MASTER_BIT;
        }

        cpu_state.sr |= SR_SUPERVISOR_BIT;

        /* Switch stacks appropriately. */
        SWITCH_A7(old_sr, cpu_state.sr);

        /* Push the exception frame. */
        push_short_frame(old_sr, exception_pc, trap_number, short_format);
        break;

    case 3: /* Address error. */
    case 6: /* CHK/CHK2 instruction. */
    case 7: /* FTRAPcc, TRAPcc, TRAPV instructions. */
    case 9: /* Trace. */
    case 5: /* Integer divide by zero. */
        /* Save away the old SR. */
        old_sr = COMPUTE_SR_FROM_CPU_STATE();

        /* Clear TR0 and TR1. */
        cpu_state.sr &= 0x3FFF;

        /* Switch into supervisor mode. */
        cpu_state.sr |= SR_SUPERVISOR_BIT;

        /* Switch stacks. */
        SWITCH_A7(old_sr, cpu_state.sr);

        EM_A7 -= 12;
        p = SYN68K_TO_US(CLEAN(EM_A7));
        WRITE_WORD(p, old_sr);
        WRITE_LONG(p + 1, exception_pc);
        WRITE_WORD(p + 3, 0x2000 | (trap_number << 2));
        WRITE_LONG(p + 4, exception_address);
        break;

        /* I don't know how to handle these or they can't happen. */
    case 0:  /* Reset initial interrupt stack pointer. */
    case 1:  /* Reset initial program counter. */
    case 13: /* ? */
    default:
        abort();
    }

    /* See if they haven't set up their own vector.  If they haven't,
   * and we are interested in the trap, dispatch it directly to the handling
   * function.
   */
    thi = &cpu_state.trap_handler_info[trap_number];

    if (trap_addr == thi->callback_address && thi->func != NULL)
        retval = thi->func(exception_pc, thi->arg);
    else /* Otherwise, trap back to m68k space. */
        retval = trap_addr;

    return retval;
}
