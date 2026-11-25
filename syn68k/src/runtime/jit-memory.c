/*+===================================================================
  File:      jit-memory.c

  Summary:   Implements checked integer memory operations for interpreter data buses and native execution.

  Classes:   None.

  Functions: syn68k_checked_memory_opcode, syn68k_checked_memory_run, syn68k_integer_operand_step,
             syn68k_x64_complex_opcode, syn68k_x64_complex_run,
             syn68k_x64_fast_memory_opcode, syn68k_x64_indexed_address, syn68k_x64_memory_opcode, syn68k_x64_memory_run,
             syn68k_x64_native_memory_words, syn68k_x64_prepare_pair

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared checked integer operands for mapped stepping and Win64 native tails. */
#include "jit-x64.h"
#include "ea.h"
#include "trap.h"
#include "destroyblock.h"
#include "integer.h"

typedef struct {
    CCRElement c, v, nz, n, x;
} integer_flags;

static int readable(unsigned code, unsigned size, int address)
{
    unsigned mode = code >> 3;
    return mode == 0 || (mode == 1 && address && size != 1) || (mode >= 2 && mode <= 6) ||
           (mode == 7 && (code & 7) <= 4);
}

static int writable(unsigned code)
{
    return code < 8 || (code >= 16 && code < 56) || code == 56 || code == 57;
}

#ifdef GENERATE_X64_CODE
static int fast_ea_words(unsigned code, int write)
{
    unsigned mode = code >> 3;
    if (mode >= 2 && mode <= 4)
        return 0;
    if (mode == 5 || mode == 6 || code == 56 || ((code == 58 || code == 59) && !write))
        return 1;
    return code == 57 ? 2 : -1;
}

static unsigned fast_memory_words(unsigned op)
{
    unsigned family = op >> 12, code = op & 63, field = (op >> 6) & 3;
    int extensions;
    if (family >= 1 && family <= 3) {
        unsigned dest = ((op >> 3) & 56) | ((op >> 9) & 7);
        if (dest < 8 || (dest < 16 && family != 1))
            extensions = fast_ea_words(code, 0);
        else if (code < 8)
            extensions = fast_ea_words(dest, 1);
        else
            return 0;
        return extensions < 0 ? 0 : 1 + extensions;
    }
    /* These encodings select two-memory-operand instructions, rather than
     ordinary ALU effective addresses. Keep their ordering in the decoder. */
    if ((op & 0xF138) == 0x9108 || (op & 0xF138) == 0xD108 || (op & 0xF138) == 0xB108)
        return 0;
    if (field == 3 && (family == 9 || family == 11 || family == 13))
        extensions = fast_ea_words(code, 0);
    else if ((family == 5 && field < 3) ||
             (field < 3 && (family == 8 || family == 9 || family == 11 || family == 12 || family == 13)))
        extensions = fast_ea_words(code, family == 5 || (op & 256));
    else {
        unsigned unary = op & 0xFF00;
        if (field < 3 && (unary == 0x4200 || unary == 0x4400 || unary == 0x4600 || unary == 0x4A00))
            extensions = fast_ea_words(code, unary != 0x4A00);
        else if ((op & 0xFFC0) == 0x4AC0 || (op & 0xF0C0) == 0x50C0)
            extensions = fast_ea_words(code, 1);
        else if ((op & 0xF100) == 0x0100)
            extensions = fast_ea_words(code, (op >> 6) & 3);
        else
            return 0;
    }
    return extensions < 0 ? 0 : 1 + extensions;
}

int syn68k_x64_fast_memory_opcode(unsigned op, uint32 pc)
{
    unsigned words = fast_memory_words(op);
    if (!words || !syn68k_memory_span(pc, words * 2 + 2))
        return 0;
    unsigned code = op & 63;
    if ((op >> 12) >= 1 && (op >> 12) <= 3 && code < 8)
        code = ((op >> 3) & 56) | ((op >> 9) & 7);
    if (((code >> 3) == 6 || code == 59) && (READUW(pc + 2) & 0x100))
        return 0; /* Full-format lengths and reserved encodings stay live tails. */
    unsigned next = READUW(pc + words * 2);
    return fast_memory_words(next) || (next & 0xF000) == 0x6000 || (next & 0xF0F8) == 0x50C8 ||
                   (next & 0xFF80) == 0x4E80 || next == 0x4E75 || next == 0x4E77 || next == 0x4E74
               ? words
               : 0;
}

unsigned syn68k_x64_native_memory_words(unsigned op)
{
    return fast_memory_words(op);
}

int syn68k_x64_indexed_address(unsigned code, uint32 extension_pc, uint32 result[2])
{
    if (!result || ((code >> 3) != 6 && code != 59))
        return 0;
    /* Indexed modes neither change address registers nor write memory. A NULL
     mapped-memory context uses only bounded flat/alias reads: no exception
     delivery, code invalidation, interrupt polling or guest callbacks. */
    Syn68kEAContext context = {.pc = extension_pc};
    Syn68kEA ea = syn68k_effective_address(&context, code, 1, 0, 0);
    if (context.error)
        return 0;
    result[0] = ea.address;
    result[1] = context.pc;
    return 1;
}

#endif

static int memory_opcode(unsigned op)
{
    unsigned family = op >> 12, code = op & 63, mode = code >> 3;
    unsigned field = (op >> 6) & 3, size = 1U << field;
    if (family >= 1 && family <= 3) {
        size = family == 1 ? 1 : family == 3 ? 2 : 4;
        unsigned dest = ((op >> 3) & 56) | ((op >> 9) & 7);
        return readable(code, size, 1) && (writable(dest) || (dest >= 8 && dest < 16 && size != 1)) &&
               ((code >= 16 && code != 60) || dest >= 16);
    }
    if (family == 9 || family == 11 || family == 13) {
        if (field == 3)
            return code >= 16 && code != 60 && readable(code, op & 256 ? 4 : 2, 1);
        if ((op & 0xF138) == 0x9108 || (op & 0xF138) == 0xD108 || (op & 0xF138) == 0xB108)
            return 1; /* Predecrement ADDX/SUBX or postincrement CMPM. */
    }
    if (field < 3 && (family == 8 || family == 9 || family == 11 || family == 12 || family == 13)) {
        if (op & 256)
            return code >= 16 && writable(code);
        return code >= 8 && readable(code, size, family == 9 || family == 11 || family == 13);
    }
    if (field < 3 && (op & 0xFF00) <= 0x0C00) {
        unsigned immediate = op & 0xFF00;
        if (immediate == 0 || immediate == 0x0200 || immediate == 0x0400 || immediate == 0x0600 ||
            immediate == 0x0A00 || immediate == 0x0C00)
            return code >= 16 && (writable(code) || (immediate == 0x0C00 && (code == 58 || code == 59)));
    }
    unsigned unary = op & 0xFF00;
    if (field < 3 && (unary == 0x4000 || unary == 0x4200 || unary == 0x4400 || unary == 0x4600 || unary == 0x4A00))
        return code >= 16 && (unary == 0x4A00 ? readable(code, size, 0) : writable(code));
    if ((op & 0xF000) == 0x5000 && field < 3)
        return code >= 16 && writable(code);
    if ((op & 0xF0C0) == 0x50C0 || (op & 0xFFC0) == 0x4AC0)
        return code >= 16 && writable(code);
    if ((op & 0xF100) == 0x0100 || (op & 0xFF00) == 0x0800)
        return code >= 16 && ((op & 0xC0) ? writable(code) : code != 60 && readable(code, 1, 0));
    return (op & 0xF8C0) == 0xE0C0 && mode >= 2 && writable(code);
}

int syn68k_checked_memory_opcode(unsigned op)
{
    return (syn68k_bus_read || syn68k_native_code_enabled()) && memory_opcode(op);
}

#ifdef GENERATE_X64_CODE
int syn68k_x64_memory_opcode(unsigned op)
{
    return memory_opcode(op);
}

static void* native_span(uint32 address, unsigned size, int writing)
{
    address &= syn68k_address_mask;
    if ((uint64)address + size > (uint64)syn68k_address_mask + 1 ||
        (syn68k_host_alias_count && address < (uint64)syn68k_host_alias_base + syn68k_host_alias_size &&
         (uint64)address + size > syn68k_host_alias_base) ||
        !syn68k_memory_span(address, size))
        return NULL;
    if (writing)
        for (unsigned page = address >> SYN68K_X64_PAGE_SHIFT; page <= (address + size - 1) >> SYN68K_X64_PAGE_SHIFT;
             ++page)
            if (syn68k_x64_code_pages[page])
                return NULL;
    return SYN68K_TO_US_RAW(address);
}

int syn68k_x64_prepare_pair(unsigned op, uint32 pc, Syn68kX64Pair* result)
{
    unsigned family = op >> 12, source_code = op & 63;
    unsigned destination_code, size;
    int move = family >= 1 && family <= 3;
    int compare = (op & 0xF138) == 0xB108;
    if (!result)
        return 0;
    if (move) {
        size = family == 1 ? 1 : family == 3 ? 2 : 4;
        destination_code = ((op >> 3) & 56) | ((op >> 9) & 7);
        if (!readable(source_code, size, 1) ||
            (!writable(destination_code) && !(destination_code >= 8 && destination_code < 16 && size != 1)))
            return 0;
    }
    else {
        if ((!compare && (op & 0xF138) != 0x9108 && (op & 0xF138) != 0xD108) || (op & 0xC0) == 0xC0)
            return 0;
        size = 1U << ((op >> 6) & 3);
        source_code = (compare ? 24 : 32) + (op & 7);
        destination_code = (compare ? 24 : 32) + ((op >> 9) & 7);
    }
    uint32 address_registers[8];
    for (unsigned i = 0; i < 8; ++i)
        address_registers[i] = EM_AREG(i);
    Syn68kEAContext context = {.pc = pc + 2};
    Syn68kEA source = syn68k_effective_address(&context, source_code, size, 0, 2);
    if (context.error || (source.reg < 0 && !native_span(source.address, size, 0)))
        goto failed;
    uint32 value = syn68k_ea_read_operand(&context, source, size);
    Syn68kEA destination = syn68k_effective_address(&context, destination_code, size, !compare, 2);
    if (context.error)
        goto failed;
    void* host = NULL;
    if (destination.reg < 0) {
        host = native_span(destination.address, size, !compare);
        if (!host)
            goto failed;
    }
    uint32 previous = 0;
    if (!move)
        previous = syn68k_ea_read_operand(&context, destination, size);
    if (context.error)
        goto failed;
    *result = (Syn68kX64Pair){value, previous, context.pc, host};
    return 1;
failed:
    /* No guest data or flags were written. The decoder will repeat the EA
     sequence and deliver the exact partial-access/fault address updates. */
    for (unsigned i = 0; i < 8; ++i)
        EM_AREG(i) = address_registers[i];
    return 0;
}

#endif

static integer_flags get_flags(void)
{
    integer_flags flags = {cpu_state.ccc, cpu_state.ccv, cpu_state.ccnz, cpu_state.ccn, cpu_state.ccx};
    return flags;
}

static void put_flags(integer_flags flags)
{
    cpu_state.ccc = flags.c;
    cpu_state.ccv = flags.v;
    cpu_state.ccnz = flags.nz;
    cpu_state.ccn = flags.n;
    cpu_state.ccx = flags.x;
}

static uint32 mask_for(unsigned size)
{
    return size == 1 ? 255 : size == 2 ? 65535 : UINT32_MAX;
}

static void logical(integer_flags* flags, uint32 value, unsigned size)
{
    flags->c = flags->v = 0;
    flags->nz = (value & mask_for(size)) != 0;
    flags->n = !!(value & ((uint32)1 << (size * 8 - 1)));
}

static uint32 arithmetic(integer_flags* flags, uint32 a, uint32 b, unsigned size, int subtract, unsigned carry,
                         int extend, int sticky)
{
    uint32 mask = mask_for(size), sign = (uint32)1 << (size * 8 - 1);
    a &= mask;
    b &= mask;
    uint32 result = (subtract ? a - b - carry : a + b + carry) & mask;
    int64 sa = a & sign ? (int64)a - ((int64)1 << (size * 8)) : a;
    int64 sb = b & sign ? (int64)b - ((int64)1 << (size * 8)) : b;
    int64 signed_result = subtract ? sa - sb - carry : sa + sb + carry;
    flags->c = subtract ? (uint64)a < (uint64)b + carry : ((uint64)a + b + carry) >> (size * 8);
    flags->v = signed_result < -(int64)sign || signed_result >= (int64)sign;
    flags->n = !!(result & sign);
    flags->nz = sticky ? flags->nz || result != 0 : result != 0;
    if (extend)
        flags->x = flags->c;
    return result;
}

static uint32 read_locked_operand(Syn68kEAContext* context, Syn68kEA ea, unsigned size)
{
    if (ea.reg >= 0 || !context->memory)
        return syn68k_ea_read_operand(context, ea, size);
    return syn68k_ea_read_memory_fc(context, ea.address, size, cpu_state.sr & 0x2000 ? 5 : 1, 1);
}

static void write_access(Syn68kEAContext* context, Syn68kEA ea, unsigned size, uint32 value, int locked)
{
    if (context->error)
        return;
    if (ea.reg >= 0) {
        uint32 mask = mask_for(size);
        cpu_state.regs[ea.reg].ul.n = (cpu_state.regs[ea.reg].ul.n & ~mask) | (value & mask);
        return;
    }
    if (locked && context->memory) {
        /* A successful CAS2's first write must not release the locked sequence.
           Keep this invocation's traffic private instead of mutating the board's
           cache binding across callbacks. 1 is final; 2 is a nonfinal operand. */
        Syn68kMMU040Memory* request = context->memory;
        Syn68kMMU040Memory memory;
        Syn68kMMU040Cache cache;
        if (locked == 2 && request->cache) {
            memory = *request;
            cache = *memory.cache;
            cache.traffic = CACHE040_LOCKED_CONTINUE;
            memory.cache = &cache;
            request = &memory;
        }
        context->access = syn68k_mmu040_write_rmw(request, ea.address, cpu_state.sr & 0x2000 ? 5 : 1, size, value);
        if (context->access.translation.fault != MMU040_OK || context->access.bus_error) {
            context->error = 2;
            context->fault_address = context->access.logical;
        }
    }
    else
        syn68k_ea_write_memory(context, ea.address, size, value);
    if (!context->error && !context->memory) {
        uint32 address = ea.address & syn68k_address_mask;
        if ((uint64)address + size <= (uint64)syn68k_address_mask + 1)
            destroy_blocks(address, size);
        else
            for (unsigned i = 0; i < size; ++i)
                destroy_blocks((address + i) & syn68k_address_mask, 1);
    }
}

static void write_operand(Syn68kEAContext* context, Syn68kEA ea, unsigned size, uint32 value)
{
    write_access(context, ea, size, value, 0);
}

static int condition(unsigned cc)
{
    int c = !!cpu_state.ccc, v = !!cpu_state.ccv;
    int z = !cpu_state.ccnz, n = !!cpu_state.ccn;
    switch (cc) {
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

static uint32 finish(Syn68kEAContext* context, uint32 pc, integer_flags flags, const uint32 address_registers[8],
                     int* completed, Syn68kIntegerStep* step)
{
    if (context->error == 4 || context->error == 11)
        for (unsigned i = 0; i < 8; ++i)
            EM_AREG(i) = address_registers[i];
    if (context->error) {
        unsigned vector = context->error == 11 ? 4 : context->error;
        if (!step)
            return trap_exception(vector, pc, context->fault_address, vector == 2 ? 7 : 0);
        step->vector = vector;
        step->format = vector == 2 ? 7 : 0;
        step->exception_pc = pc;
        step->fault_address = context->fault_address;
        step->access = context->access;
        return pc;
    }
    put_flags(flags);
    *completed = 1;
    return context->pc;
}

static uint32 finish_scalar(Syn68kEAContext* context, uint32 pc, integer_flags flags, const uint32 address_registers[8],
                            int* completed, Syn68kIntegerStep* step)
{
    uint32 next = finish(context, pc, flags, address_registers, completed, step);
    if (step && context->memory && step->vector == 2 && context->access.writing) {
        /* All extension/operand reads are complete. This is the final store,
         or CAS2 has captured its remaining store. Earlier stores stay committed.
         The component API retains its old fault state. The stream runner
         decides whether this store can be represented by a writeback slot. */
        step->store_state = 1;
        step->store_next = context->pc;
        step->store_ccr = !!flags.c | (!!flags.v << 1) | (!flags.nz << 2) | (!!flags.n << 3) | (!!flags.x << 4);
        step->store_locked = !!context->access.rmw;
    }
    return next;
}

static uint32 memory_step(uint32 pc, unsigned op, int* completed, Syn68kMMU040Memory* memory, Syn68kIntegerStep* step)
{
    Syn68kEAContext context = {.pc = pc + 2, .memory = memory};
    integer_flags flags = get_flags();
    uint32 address_registers[8];
    for (unsigned i = 0; i < 8; ++i)
        address_registers[i] = EM_AREG(i);
    unsigned family = op >> 12, code = op & 63, field = (op >> 6) & 3;
    unsigned size = 1U << field, reg = (op >> 9) & 7;
    Syn68kEA source, dest;
    uint32 a, b, result;
    if (op == 0x003C || op == 0x023C || op == 0x0A3C || (op & 0xFFC0) == 0x42C0 || (op & 0xFFC0) == 0x44C0) {
        unsigned ccr = (!!flags.c) | ((!!flags.v) << 1) | ((!flags.nz) << 2) | ((!!flags.n) << 3) | ((!!flags.x) << 4);
        if ((op & 0xFFC0) == 0x42C0) {
            dest = syn68k_effective_address(&context, code, 2, 1, 1);
            write_operand(&context, dest, 2, ccr);
        }
        else {
            source = syn68k_effective_address(&context, (op & 255) == 0x3C ? 60 : code, 2, 0, 1);
            result = syn68k_ea_read_operand(&context, source, 2);
            if (op == 0x003C)
                result |= ccr;
            else if (op == 0x023C)
                result &= ccr;
            else if (op == 0x0A3C)
                result ^= ccr;
            flags.c = result & 1;
            flags.v = result & 2;
            flags.nz = !(result & 4);
            flags.n = result & 8;
            flags.x = result & 16;
        }
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if (family >= 1 && family <= 3) {
        size = family == 1 ? 1 : family == 3 ? 2 : 4;
        unsigned destination = ((op >> 3) & 56) | reg;
        source = syn68k_effective_address(&context, code, size, 0, 2);
        result = syn68k_ea_read_operand(&context, source, size);
        if (!context.error) {
            if (destination >= 8 && destination < 16) {
                if (size == 2)
                    result = (int16)result;
                cpu_state.regs[destination].ul.n = result;
            }
            else {
                dest = syn68k_effective_address(&context, destination, size, 1, 1);
                write_operand(&context, dest, size, result);
                logical(&flags, result, size);
            }
        }
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if ((op & 0xF138) == 0xD108 || (op & 0xF138) == 0x9108 || (op & 0xF138) == 0xB108) {
        int compare = family == 11;
        source = syn68k_effective_address(&context, (compare ? 24 : 32) | (op & 7), size, 0, 0);
        b = syn68k_ea_read_operand(&context, source, size);
        if (!context.error) {
            dest = syn68k_effective_address(&context, (compare ? 24 : 32) | reg, size, !compare, 0);
            a = syn68k_ea_read_operand(&context, dest, size);
            result = arithmetic(&flags, a, b, size, family != 13, compare ? 0 : !!flags.x, !compare, !compare);
            if (!compare)
                write_operand(&context, dest, size, result);
        }
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if ((family == 9 || family == 11 || family == 13) && field == 3) {
        size = op & 256 ? 4 : 2;
        source = syn68k_effective_address(&context, code, size, 0, 2);
        b = syn68k_ea_read_operand(&context, source, size);
        if (size == 2)
            b = (int16)b;
        if (!context.error) {
            a = EM_AREG(reg);
            if (family == 11)
                arithmetic(&flags, a, b, 4, 1, 0, 0, 0);
            else
                EM_AREG(reg) = family == 13 ? a + b : a - b;
        }
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if (field < 3 && (family == 8 || family == 9 || family == 11 || family == 12 || family == 13)) {
        int reverse = !!(op & 256), compare = family == 11 && !reverse;
        if (reverse) {
            b = EM_DREG(reg);
            dest = syn68k_effective_address(&context, code, size, 1, 1);
            a = syn68k_ea_read_operand(&context, dest, size);
        }
        else {
            source = syn68k_effective_address(&context, code, size, 0, 2);
            b = syn68k_ea_read_operand(&context, source, size);
            dest = (Syn68kEA){.reg = reg};
            a = EM_DREG(reg);
        }
        if (family == 9 || family == 13 || compare)
            result = arithmetic(&flags, a, b, size, family != 13, 0, !compare, 0);
        else {
            result = family == 8 ? a | b : family == 12 ? a & b : a ^ b;
            logical(&flags, result, size);
        }
        if (!compare)
            write_operand(&context, dest, size, result);
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    unsigned immediate = op & 0xFF00;
    if (field < 3 && (immediate == 0 || immediate == 0x0200 || immediate == 0x0400 || immediate == 0x0600 ||
                      immediate == 0x0A00 || immediate == 0x0C00)) {
        source = syn68k_effective_address(&context, 60, size, 0, 1);
        b = syn68k_ea_read_operand(&context, source, size);
        if (!context.error) {
            unsigned kind = (op >> 8) & 15;
            dest = syn68k_effective_address(&context, code, size, kind != 12, 1);
            a = syn68k_ea_read_operand(&context, dest, size);
            if (kind == 4 || kind == 6 || kind == 12)
                result = arithmetic(&flags, a, b, size, kind != 6, 0, kind != 12, 0);
            else {
                result = kind == 0 ? a | b : kind == 2 ? a & b : a ^ b;
                logical(&flags, result, size);
            }
            if (kind != 12)
                write_operand(&context, dest, size, result);
        }
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if ((op & 0xF100) == 0x0100 || (op & 0xFF00) == 0x0800) {
        b = op & 0x100 ? EM_DREG(reg) : syn68k_ea_fetch_memory(&context, context.pc, 2);
        if (!(op & 0x100))
            context.pc += 2;
        unsigned kind = (op >> 6) & 3;
        size = code < 8 ? 4 : 1;
        dest = syn68k_effective_address(&context, code, size, kind != 0, 1);
        a = syn68k_ea_read_operand(&context, dest, size);
        uint32 bit = 1U << (b & (code < 8 ? 31 : 7));
        flags.nz = !!(a & bit);
        if (kind)
            write_operand(&context, dest, size, kind == 1 ? a ^ bit : kind == 2 ? a & ~bit : a | bit);
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if (family == 5 && field < 3) {
        b = reg ? reg : 8;
        if (code >= 8 && code < 16) {
            EM_AREG(code & 7) += op & 256 ? -b : b;
            return finish_scalar(&context, pc, flags, address_registers, completed, step);
        }
        dest = syn68k_effective_address(&context, code, size, 1, 1);
        a = syn68k_ea_read_operand(&context, dest, size);
        result = arithmetic(&flags, a, b, size, !!(op & 256), 0, 1, 0);
        write_operand(&context, dest, size, result);
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if ((op & 0xF0C0) == 0x50C0) {
        dest = syn68k_effective_address(&context, code, 1, 1, 1);
        write_operand(&context, dest, 1, condition((op >> 8) & 15) ? 255 : 0);
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    if (family == 14) {
        unsigned kind = (op >> 9) & 3, left = !!(op & 256);
        dest = syn68k_effective_address(&context, code, 2, 1, 0);
        a = syn68k_ea_read_operand(&context, dest, 2);
        unsigned old_sign = a >> 15, carry = left ? old_sign : a & 1;
        result = left ? ((a << 1) & 65535) | (kind == 2   ? !!flags.x
                                              : kind == 3 ? carry
                                                          : 0)
                      : (a >> 1) | ((kind == 0   ? old_sign
                                     : kind == 2 ? !!flags.x
                                     : kind == 3 ? carry
                                                 : 0)
                                        ? 0x8000
                                        : 0);
        logical(&flags, result, 2);
        flags.c = carry;
        flags.v = kind == 0 && left && old_sign != (result >> 15);
        if (kind != 3)
            flags.x = carry;
        write_operand(&context, dest, 2, result);
        return finish_scalar(&context, pc, flags, address_registers, completed, step);
    }
    unsigned unary = op & 0xFF00;
    if ((op & 0xFFC0) == 0x4AC0)
        size = 1;
    dest = syn68k_effective_address(&context, code, size, unary != 0x4A00 || field == 3,
                                    unary == 0x4A00 && field < 3 ? 2 : 1);
    if ((op & 0xFFC0) == 0x4AC0) {
        /* TAS uses a byte operand despite the encoding's size field. */
        a = read_locked_operand(&context, dest, 1);
        logical(&flags, a, 1);
        write_access(&context, dest, 1, a | 128, 1);
    }
    else {
        a = unary == 0x4200 ? 0 : syn68k_ea_read_operand(&context, dest, size);
        if (unary == 0x4000 || unary == 0x4400)
            result = arithmetic(&flags, 0, a, size, 1, unary == 0x4000 && flags.x, 1, unary == 0x4000);
        else {
            result = unary == 0x4200 ? 0 : unary == 0x4600 ? ~a : a;
            logical(&flags, result, size);
        }
        if (unary != 0x4A00)
            write_operand(&context, dest, size, result);
    }
    return finish_scalar(&context, pc, flags, address_registers, completed, step);
}

uint32 syn68k_checked_memory_run(uint32 pc, unsigned op)
{
    /* There is no native return frame after the tail call. Fetch every next
     opcode afresh, even when a store destroyed the originating block. Bound
     the batch and return immediately for faults, tracing or pending IRQs. */
    for (unsigned count = 0; count < 64; ++count) {
        int completed = 0;
        pc = memory_step(pc, op, &completed, NULL, NULL);
        if (!completed || (cpu_state.sr & 0xC000))
            break;
        if (INTERRUPT_PENDING())
            break;
        if (!syn68k_memory_span(pc, 2))
            break;
        op = READUW(pc);
        if (!memory_opcode(op))
            break;
    }
    return pc;
}

#ifdef GENERATE_X64_CODE
uint32 syn68k_x64_memory_run(uint32 pc, unsigned op)
{
    return syn68k_checked_memory_run(pc, op);
}

#endif

static int complex_opcode(unsigned op)
{
    return (op & 0xF1F0) == 0x8100 || (op & 0xF1F0) == 0xC100 || (op & 0xF1F0) == 0x8140 || (op & 0xF1F0) == 0x8180 ||
           ((op & 0xFFC0) == 0x4800 && writable(op & 63)) || ((op & 0xF9C0) == 0x08C0 && (op & 0x600)) ||
           (op & 0xF8C0) == 0xE8C0;
}

static int compare_swap(unsigned op, Syn68kEAContext* context, integer_flags* flags, Syn68kIntegerStep* step)
{
    unsigned size = (op & 0x600) == 0x200 ? 1 : (op & 0x600) == 0x400 ? 2 : 4;
    unsigned extension = syn68k_ea_fetch_memory(context, context->pc, 2);
    context->pc += 2;
    if (op == 0x0CFC || op == 0x0EFC) {
        unsigned second = syn68k_ea_fetch_memory(context, context->pc, 2);
        context->pc += 2;
        Syn68kEA first_ea = {.reg = -1, .address = EM_DREG(extension >> 12)};
        Syn68kEA second_ea = {.reg = -1, .address = EM_DREG(second >> 12)};
        unsigned c1 = extension & 7, c2 = second & 7;
        uint32 u1 = EM_DREG((extension >> 6) & 7);
        uint32 u2 = EM_DREG((second >> 6) & 7);
        uint32 m1 = read_locked_operand(context, first_ea, size);
        uint32 m2 = read_locked_operand(context, second_ea, size);
        uint32 difference = arithmetic(flags, m1, EM_DREG(c1), size, 1, 0, 0, 0);
        if (!difference)
            difference = arithmetic(flags, m2, EM_DREG(c2), size, 1, 0, 0, 0);
        if (!difference) {
            write_access(context, first_ea, size, u1, 2);
            if (context->error) {
                if (!step || !context->memory || context->error != 2 || !context->access.writing ||
                    !context->access.rmw)
                    return 0;
                step->store_tail = u2 & mask_for(size);
                step->store_tail_address = second_ea.address;
                step->store_tail_fc = ((cpu_state.sr >> 11) & 4) | 1;
                step->store_tail_valid = 2;
                return 1;
            }
            write_access(context, second_ea, size, u2, 1);
            return 1;
        }
        else {
            /* PRM CAS2: Dc1 takes precedence when both compare registers alias. */
            write_operand(context, (Syn68kEA){.reg = c2}, size, m2);
            write_operand(context, (Syn68kEA){.reg = c1}, size, m1);
            /* MC68040UM 7.4.5: a failed CAS2 still terminates its locked
               sequence by writing only the second value read. */
            if (context->memory) {
                write_access(context, second_ea, size, m2, 1);
                return 1;
            }
        }
    }
    else {
        if ((op & 63) < 16 || !writable(op & 63))
            context->error = 4;
        Syn68kEA ea = syn68k_effective_address(context, op & 63, size, 1, 0);
        unsigned cmp = extension & 7;
        uint32 update = EM_DREG((extension >> 6) & 7);
        uint32 value = read_locked_operand(context, ea, size);
        uint32 difference = arithmetic(flags, value, EM_DREG(cmp), size, 1, 0, 0, 0);
        if (difference) {
            write_operand(context, (Syn68kEA){.reg = cmp}, size, value);
            /* The 68040 always ends CAS with a write, including mismatch. */
            if (context->memory)
                write_access(context, ea, size, value, 1);
        }
        else
            write_access(context, ea, size, update, 1);
        return 1;
    }
    return 0;
}

static void bitfield(unsigned op, Syn68kEAContext* context, integer_flags* flags)
{
    unsigned ext = syn68k_ea_fetch_memory(context, context->pc, 2);
    context->pc += 2;
    unsigned code = op & 63, mode = code >> 3, kind = (op >> 8) & 7;
    /* The synthetic decoder ignores unused extension bits. Keep that contract
     for existing code and CRC fixtures rather than inventing a new trap. */
    if (!(mode == 0 || mode == 2 || mode == 5 || mode == 6 || (mode == 7 && (code & 7) < 4))) {
        context->error = 4;
        return;
    }
    unsigned raw_width = ext & 32 ? EM_DREG(ext & 7) : ext & 31;
    unsigned width = ((raw_width - 1) & 31) + 1;
    uint32 raw_offset = ext & 0x800 ? EM_DREG((ext >> 6) & 7) : (ext >> 6) & 31;
    uint32 input = EM_DREG((ext >> 12) & 7), original = 0, field = 0;
    uint32 address = 0;
    unsigned offset = raw_offset & 31, bytes = 0;
    uint8 data[5] = {0};
    if (!mode)
        original = EM_DREG(code & 7);
    else {
        Syn68kEA ea = syn68k_effective_address(context, code, 1, 0, 0);
        int64 signed_offset = (int32)raw_offset;
        int64 byte_offset = signed_offset / 8 - (signed_offset < 0 && signed_offset % 8);
        address = ea.address + (uint32)byte_offset;
        offset = raw_offset & 7;
        bytes = (offset + width + 7) / 8;
        for (unsigned i = 0; i < bytes; ++i)
            data[i] = syn68k_ea_read_memory(context, address + i, 1);
    }
    for (unsigned i = 0; i < width; ++i) {
        unsigned bit = mode ? (data[(offset + i) / 8] >> (7 - (offset + i) % 8)) & 1
                            : (original >> (31 - ((offset + i) & 31))) & 1;
        field = (field << 1) | bit;
    }
    uint32 mask = width == 32 ? UINT32_MAX : ((uint32)1 << width) - 1;
    uint32 selected = kind == 7 ? input & mask : field;
    flags->c = flags->v = 0;
    flags->nz = selected != 0;
    flags->n = (selected >> (width - 1)) & 1;
    if (kind == 1 || kind == 3) {
        uint32 result = field;
        if (kind == 3 && width < 32 && flags->n)
            result |= ~mask;
        write_operand(context, (Syn68kEA){.reg = (ext >> 12) & 7}, 4, result);
    }
    else if (kind == 5) {
        unsigned first = 0;
        while (first < width && !(field & ((uint32)1 << (width - first - 1))))
            ++first;
        write_operand(context, (Syn68kEA){.reg = (ext >> 12) & 7}, 4, raw_offset + first);
    }
    else if (kind != 0) {
        uint32 replacement = kind == 2 ? field ^ mask : kind == 4 ? 0 : kind == 6 ? mask : input;
        uint32 result = original;
        for (unsigned i = 0; i < width; ++i) {
            unsigned bit = (replacement >> (width - i - 1)) & 1;
            if (!mode) {
                uint32 bit_mask = (uint32)1 << (31 - ((offset + i) & 31));
                result = (result & ~bit_mask) | (bit ? bit_mask : 0);
            }
            else {
                unsigned byte = (offset + i) / 8, bit_mask = 1 << (7 - (offset + i) % 8);
                data[byte] = (data[byte] & ~bit_mask) | (bit ? bit_mask : 0);
            }
        }
        if (!mode)
            write_operand(context, (Syn68kEA){.reg = code & 7}, 4, result);
        else
            for (unsigned i = 0; i < bytes && !context->error; ++i)
                write_operand(context, (Syn68kEA){.reg = -1, .address = address + i}, 1, data[i]);
    }
}

static uint32 complex_step(uint32 pc, unsigned op, Syn68kMMU040Memory* memory, Syn68kIntegerStep* step)
{
    Syn68kEAContext context = {.pc = pc + 2, .memory = memory};
    integer_flags flags = get_flags();
    uint32 address_registers[8];
    for (unsigned i = 0; i < 8; ++i)
        address_registers[i] = EM_AREG(i);
    unsigned src = op & 7, dst = (op >> 9) & 7;
    Syn68kEA source = {.reg = src}, dest = {.reg = dst};
    unsigned result = 0;
    int final_store = 0;
    if ((op & 0xF9C0) == 0x08C0 && (op & 0x600))
        final_store = compare_swap(op, &context, &flags, step);
    else if ((op & 0xF8C0) == 0xE8C0)
        bitfield(op, &context, &flags);
    else if ((op & 0xF1F0) == 0x8140 || (op & 0xF1F0) == 0x8180) {
        int pack = !(op & 0x80);
        unsigned source_size = pack ? 2 : 1, dest_size = pack ? 1 : 2;
        uint16 adjust = syn68k_ea_fetch_memory(&context, context.pc, 2);
        context.pc += 2;
        if (op & 8)
            source = syn68k_effective_address(&context, 32 + src, source_size, 0, 0);
        unsigned value = syn68k_ea_read_operand(&context, source, source_size);
        if (pack) {
            value = (uint16)(value + adjust);
            result = ((value >> 4) & 0xF0) | (value & 15);
        }
        else
            result = (uint16)(((value & 0xF0) << 4) + (value & 15) + adjust);
        if (op & 8)
            dest = syn68k_effective_address(&context, 32 + dst, dest_size, 1, 0);
        write_operand(&context, dest, dest_size, result);
    }
    else {
        int negate = (op & 0xFFC0) == 0x4800;
        if (negate) {
            if (!writable(op & 63))
                context.error = 4;
            dest = syn68k_effective_address(&context, op & 63, 1, 1, 1);
        }
        else if (op & 8) {
            source = syn68k_effective_address(&context, 32 + src, 1, 0, 0);
            /* Fetch the source before a same-register destination decrement. */
        }
        unsigned a = negate ? 0 : syn68k_ea_read_operand(&context, source, 1);
        if (!negate && (op & 8))
            dest = syn68k_effective_address(&context, 32 + dst, 1, 1, 0);
        unsigned b = syn68k_ea_read_operand(&context, dest, 1), x = !!flags.x;
        if (negate) {
            static const int correction[16] = {0x9A, -6, -6, -6, -6, -6, -6, -6, -6, -6, 0xFA, -6, -6, -6, -6, -6};
            unsigned value = b + x;
            result = 0x9A - value;
            if (!x && !(value & 15))
                result -= correction[(value >> 4) & 15];
            flags.c = flags.x = value != 0;
        }
        else if ((op >> 12) == 12) {
            unsigned low = (a & 15) + (b & 15) + x;
            result = low + (((a >> 4) + (b >> 4)) << 4) + (low >= 10 ? 6 : 0);
            flags.c = flags.x = result >= 0xA0;
            if (flags.c)
                result -= 0xA0;
        }
        else {
            flags.c = (int)x + (int)(a & 15) - (int)(b & 15) + 10 * ((int)(a >> 4) - (int)(b >> 4)) > 0;
            result = b - a - x;
            if ((int)(a & 15) > (int)(b & 15) - (int)x) {
                result -= 6;
                if ((int)(a & 0xF0) >= (int)(b & 0xF0) - (int)x)
                    result -= 0x60;
            }
            else if ((a & 0xF0) > (b & 0xF0)) {
                result -= 0x60;
                flags.c = 1;
            }
            flags.x = flags.c;
        }
        result &= 255;
        flags.nz |= result;
        if (!negate)
            flags.v = 0, flags.n = result & 128;
        write_operand(&context, dest, 1, result);
    }
    int completed = 0;
    return final_store ? finish_scalar(&context, pc, flags, address_registers, &completed, step)
                       : finish(&context, pc, flags, address_registers, &completed, step);
}

static int register_step(uint32 pc, unsigned op, Syn68kIntegerStep* step)
{
    integer_flags flags = get_flags();
    unsigned src = op & 7, dst = (op >> 9) & 7, field = (op >> 6) & 3;
    unsigned size = 1U << field;
    uint32 value;
    if (op == 0x4E71)
        value = 0; /* NOP preserves all state. */
    else if ((op & 0xF100) == 0x7000) {
        value = (int8)op;
        EM_DREG(dst) = value;
        logical(&flags, value, 4);
    }
    else if ((op & 0xFFF8) == 0x4840) {
        value = EM_DREG(src);
        value = value << 16 | value >> 16;
        EM_DREG(src) = value;
        logical(&flags, value, 4);
    }
    else if ((op & 0xFFF8) == 0x4880 || (op & 0xFFF8) == 0x48C0 || (op & 0xFFF8) == 0x49C0) {
        size = (op & 0xFFF8) == 0x4880 ? 2 : 4;
        value = (op & 0xFFF8) == 0x48C0 ? (int16)EM_DREG(src) : (int8)EM_DREG(src);
        EM_DREG(src) = (EM_DREG(src) & ~mask_for(size)) | (value & mask_for(size));
        logical(&flags, value, size);
    }
    else if ((op & 0xF1F8) == 0xC140 || (op & 0xF1F8) == 0xC148 || (op & 0xF1F8) == 0xC188) {
        unsigned left = dst + ((op & 0xF1F8) == 0xC148 ? 8 : 0);
        unsigned right = src + ((op & 0xF1F8) == 0xC140 ? 0 : 8);
        value = EM_DREG(left);
        EM_DREG(left) = EM_DREG(right);
        EM_DREG(right) = value;
    }
    else if (((op & 0xF138) == 0xD100 || (op & 0xF138) == 0x9100) && field < 3) {
        value = arithmetic(&flags, EM_DREG(dst), EM_DREG(src), size, (op >> 12) == 9, !!flags.x, 1, 1);
        EM_DREG(dst) = (EM_DREG(dst) & ~mask_for(size)) | value;
    }
    else if (((op & 0xF000) == 0xE000 && field < 3) || (op & 0xFCF8) == 0xE4C0) {
        int alias = field == 3;
        unsigned kind = alias ? (op >> 9) & 3 : (op >> 3) & 3;
        unsigned count = alias ? 1 : op & 32 ? EM_DREG(dst) & 63 : dst ? dst : 8;
        int left = !!(op & 256);
        size = alias ? 2 : size;
        uint32 mask = mask_for(size), sign = (uint32)1 << (size * 8 - 1);
        unsigned carry = kind == 2 ? !!flags.x : 0, overflow = 0;
        value = EM_DREG(src) & mask;
        for (unsigned i = 0; i < count; ++i) {
            unsigned old_sign = !!(value & sign);
            unsigned next_carry = left ? old_sign : value & 1;
            uint32 insert = kind == 2 ? carry : kind == 3 ? next_carry : 0;
            value = left ? ((value << 1) | insert) & mask : (value >> 1) | ((kind == 0 ? old_sign : insert) ? sign : 0);
            overflow |= kind == 0 && left && old_sign != !!(value & sign);
            carry = next_carry;
        }
        logical(&flags, value, size);
        flags.c = carry;
        flags.v = overflow;
        if (count && kind != 3)
            flags.x = carry;
        EM_DREG(src) = (EM_DREG(src) & ~mask) | value;
    }
    else
        return 0;
    put_flags(flags);
    step->next = pc + 2;
    return 1;
}

static int scalar_opcode(unsigned op)
{
    if (memory_opcode(op))
        return 1;
    unsigned family = op >> 12, code = op & 63, field = (op >> 6) & 3;
    unsigned size = 1U << field;
    if (family >= 1 && family <= 3) {
        size = family == 1 ? 1 : family == 3 ? 2 : 4;
        unsigned dest = ((op >> 3) & 56) | ((op >> 9) & 7);
        return readable(code, size, 1) && (writable(dest) || (dest >= 8 && dest < 16 && size != 1));
    }
    if ((family == 9 || family == 11 || family == 13) && field == 3)
        return readable(code, op & 256 ? 4 : 2, 1);
    if (field < 3 && (family == 8 || family == 9 || family == 11 || family == 12 || family == 13))
        return op & 256 ? writable(code)
                        : readable(code, size, family == 9 || family == 11 || family == 13) ||
                              (family == 11 && code >= 8 && code < 16);
    if (code < 8) {
        unsigned unary = op & 0xFF00;
        if (field < 3 && (unary == 0 || unary == 0x0200 || unary == 0x0400 || unary == 0x0600 || unary == 0x0A00 ||
                          unary == 0x0C00 || unary == 0x4000 || unary == 0x4200 || unary == 0x4400 || unary == 0x4600 ||
                          unary == 0x4A00 || family == 5))
            return 1;
        if ((op & 0xF0C0) == 0x50C0 || (op & 0xFFC0) == 0x4AC0 || (op & 0xF100) == 0x0100 || (op & 0xFF00) == 0x0800)
            return 1;
    }
    if (code >= 8 && code < 16 && (field == 1 || field == 2) && (family == 5 || (op & 0xFF00) == 0x4A00))
        return 1;
    return op == 0x003C || op == 0x023C || op == 0x0A3C || ((op & 0xFFC0) == 0x42C0 && writable(code)) ||
           ((op & 0xFFC0) == 0x44C0 && readable(code, 2, 0));
}

int syn68k_integer_operand_step(uint32 pc, unsigned op, Syn68kMMU040Memory* memory, Syn68kIntegerStep* result)
{
    if (!result)
        return 0;
    if (register_step(pc, op, result))
        return 1;
    if (complex_opcode(op))
        result->next = complex_step(pc, op, memory, result);
    else if (scalar_opcode(op)) {
        int completed = 0;
        result->next = memory_step(pc, op, &completed, memory, result);
    }
    else
        return 0;
    return 1;
}

#ifdef GENERATE_X64_CODE
int syn68k_x64_complex_opcode(unsigned op)
{
    return complex_opcode(op);
}

uint32 syn68k_x64_complex_run(uint32 pc, unsigned op)
{
    return complex_step(pc, op, NULL, NULL);
}
#endif
