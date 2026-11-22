/*+===================================================================
  File:      ea.c

  Summary:   Implements ea support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_ea_fetch_memory, syn68k_ea_read_memory, syn68k_ea_read_memory_fc, syn68k_ea_read_operand,
             syn68k_ea_write_memory, syn68k_ea_write_memory_fc, syn68k_effective_address

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared bounded classic 68K effective-address decoding. */
#include "ea.h"
#include "syn68k_private.h"
#include "blockinfo.h"
#include <string.h>

int (*syn68k_bus_read)(uint32 address, unsigned size, uint32* value);

/* Closed native EA templates specialize this to their required mapped bus. */
#ifndef SYN68K_EA_MAPPED
#define SYN68K_EA_MAPPED(context) ((context)->memory != NULL)
#endif

/* A flat transfer can use one bounded host copy. Bus wrap and host aliases
   retain the bytewise path, since adjacent guest bytes can map separately. */
static void* flat_span(uint32 address, unsigned size)
{
    address &= syn68k_address_mask;
    if ((uint64)address + size > (uint64)syn68k_address_mask + 1 ||
        (syn68k_host_alias_count && address < (uint64)syn68k_host_alias_base + syn68k_host_alias_size &&
         (uint64)address + size > syn68k_host_alias_base) ||
        !syn68k_memory_span(address, size))
        return NULL;
    return SYN68K_TO_US_RAW(address);
}

static int span(Syn68kEAContext* context, uint32 address, unsigned size)
{
    if (context->error)
        return 0;
    for (unsigned i = 0; i < size; ++i)
        if (!syn68k_memory_span(address + i, 1)) {
            context->fault_address = address + i;
            context->error = 2;
            return 0;
        }
    return 1;
}

uint32 syn68k_ea_read_memory_fc(Syn68kEAContext* context, uint32 address, unsigned size, unsigned fc, int rmw)
{
    if (context->error)
        return 0;
    if (size != 1 && size != 2 && size != 4) {
        if (!context->error)
            context->error = 11;
        return 0;
    }
    if (SYN68K_EA_MAPPED(context)) {
        uint32 value = 0;
        context->access = syn68k_mmu040_read(context->memory, address, fc, size, rmw, &value);
        if (context->access.translation.fault != MMU040_OK || context->access.bus_error) {
            context->error = 2;
            context->fault_address = context->access.logical;
        }
        return value;
    }
    const void* host = flat_span(address, size);
    if (host) {
        if (size == 1)
            return *(const uint8*)host;
        if (size == 2) {
            uint16 value;
            memcpy(&value, host, sizeof value);
            return SWAPUW_IFLE(value);
        }
        uint32 value;
        memcpy(&value, host, sizeof value);
        return SWAPUL_IFLE(value);
    }
    uint32 bus_value;
    if (syn68k_bus_read && (fc == 1 || fc == 5) &&
        (uint64)(address & syn68k_address_mask) + size <= (uint64)syn68k_address_mask + 1 &&
        syn68k_bus_read(address & syn68k_address_mask, size, &bus_value))
        return bus_value;
    if (!span(context, address, size))
        return 0;
    uint32 value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = (value << 8) | READUB(address + i);
    return value;
}

uint32 syn68k_ea_read_memory(Syn68kEAContext* context, uint32 address, unsigned size)
{
    return syn68k_ea_read_memory_fc(context, address, size, cpu_state.sr & 0x2000 ? 5 : 1, 0);
}

uint32 syn68k_ea_fetch_memory(Syn68kEAContext* context, uint32 address, unsigned size)
{
    return syn68k_ea_read_memory_fc(context, address, size, cpu_state.sr & 0x2000 ? 6 : 2, 0);
}

uint32 syn68k_ea_read_operand(Syn68kEAContext* context, Syn68kEA ea, unsigned size)
{
    if (context->error)
        return 0;
    if (ea.reg < 0)
        return ea.instruction ? syn68k_ea_fetch_memory(context, ea.address, size)
                              : syn68k_ea_read_memory(context, ea.address, size);
    if ((unsigned)ea.reg >= 16 || (size != 1 && size != 2 && size != 4)) {
        context->error = 11;
        return 0;
    }
    uint32 value = cpu_state.regs[ea.reg].ul.n;
    return size == 1 ? value & 0xff : size == 2 ? value & 0xffff : value;
}

void syn68k_ea_write_memory_fc(Syn68kEAContext* context, uint32 address, unsigned size, uint32 value, unsigned fc)
{
    if (context->error)
        return;
    if (size != 1 && size != 2 && size != 4) {
        if (!context->error)
            context->error = 11;
        return;
    }
    if (SYN68K_EA_MAPPED(context)) {
        context->access = syn68k_mmu040_write(context->memory, address, fc, size, value);
        if (context->access.translation.fault != MMU040_OK || context->access.bus_error) {
            context->error = 2;
            context->fault_address = context->access.logical;
        }
        return;
    }
    void* host = flat_span(address, size);
    if (host) {
        if (size == 1)
            *(uint8*)host = value;
        else if (size == 2) {
            uint16 word = SWAPUW_IFLE(value);
            memcpy(host, &word, sizeof word);
        }
        else {
            uint32 word = SWAPUL_IFLE(value);
            memcpy(host, &word, sizeof word);
        }
        return;
    }
    if (!span(context, address, size))
        return;
    for (unsigned i = 0; i < size; ++i)
        WRITEUB(address + i, value >> ((size - 1 - i) * 8));
}

void syn68k_ea_write_memory(Syn68kEAContext* context, uint32 address, unsigned size, uint32 value)
{
    syn68k_ea_write_memory_fc(context, address, size, value, cpu_state.sr & 0x2000 ? 5 : 1);
}

static uint32 fetch_ea(Syn68kEAContext* context, unsigned size)
{
    uint32 result = syn68k_ea_fetch_memory(context, context->pc, size);
    if (!context->error)
        context->pc += size;
    return result;
}

static uint32 indexed(Syn68kEAContext* context, uint32 base)
{
    unsigned extension = fetch_ea(context, 2);
    if (context->error)
        return 0;
    uint32 index = cpu_state.regs[extension >> 12].ul.n;
    if (!(extension & 0x0800))
        index = (int16)index;
    index <<= (extension >> 9) & 3;
    if (!(extension & 0x0100))
        return base + index + (int8)extension;
    if (!syn68k_indexed_extension_encoding_valid(extension)) {
        context->error = 4;
        return 0;
    }
    if (extension & 0x80)
        base = 0;
    if (extension & 0x40)
        index = 0;
    if ((extension & 0x30) == 0x20)
        base += (int16)fetch_ea(context, 2);
    else if ((extension & 0x30) == 0x30)
        base += fetch_ea(context, 4);
    unsigned indirect = extension & 7;
    if (!indirect)
        return base + index;
    uint32 outer = 0;
    if ((indirect & 3) == 2)
        outer = (int16)fetch_ea(context, 2);
    else if ((indirect & 3) == 3)
        outer = fetch_ea(context, 4);
    if (context->resume_ea)
        return *context->resume_ea;
    return syn68k_ea_read_memory(context, base + (indirect < 4 ? index : 0), 4) + outer + (indirect >= 5 ? index : 0);
}

Syn68kEA syn68k_effective_address(Syn68kEAContext* context, unsigned code, unsigned size, int writing, int registers)
{
    unsigned mode = (code >> 3) & 7, reg = code & 7;
    Syn68kEA result = {.reg = -1};
    if (context->error)
        return result;
    switch (mode) {
    case 0:
    case 1:
        if (size > 4 || !registers || (mode == 1 && registers != 2))
            context->error = 11;
        result.reg = mode * 8 + reg;
        break;
    case 2:
        result.address = EM_AREG(reg);
        break;
    case 3:
        result.address = EM_AREG(reg);
        EM_AREG(reg) += size == 1 && reg == 7 ? 2 : size;
        break;
    case 4:
        EM_AREG(reg) -= size == 1 && reg == 7 ? 2 : size;
        result.address = EM_AREG(reg);
        break;
    case 5:
        result.address = EM_AREG(reg) + (int16)fetch_ea(context, 2);
        break;
    case 6:
        result.address = indexed(context, EM_AREG(reg));
        break;
    case 7:
        switch (reg) {
        case 0:
            result.address = (int16)fetch_ea(context, 2);
            break;
        case 1:
            result.address = fetch_ea(context, 4);
            break;
        case 2:
            if (writing)
                context->error = 11;
            result.address = context->pc;
            result.address += (int16)fetch_ea(context, 2);
            break;
        case 3:
            if (writing)
                context->error = 11;
            result.address = indexed(context, context->pc);
            break;
        case 4:
            if (writing || !registers) {
                context->error = 11;
                break;
            }
            result.address = context->pc + (size == 1 ? 1 : 0);
            result.instruction = 1;
            context->pc += size == 1 ? 2 : size;
            break;
        default:
            context->error = 11;
        }
        break;
    }
    if (context->resume_ea && result.reg < 0 && !context->error)
        result.address = *context->resume_ea;
    return result;
}
