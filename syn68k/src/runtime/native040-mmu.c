/*+===================================================================
  File:      native040-mmu.c

  Summary:   Instantiate closed native translation, operand and MMU-control roots.

  Classes:   None.

  Functions: Build-time native040_bind_memory, packet read/write/write_rmw/span/translate/ptest/flush/control/reset roots
             and typed native040_bus_read/write/write_rmw roots.

  Origin:    Original ARDI Executor direct 68040 lowering; uses maintained MMU and floating arithmetic.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "native040-memory.h"
#include "native040-flat.h"

static unsigned char* ordinary_ram(const NativeMemory* memory, uint32_t address, unsigned size)
{
    const Syn68kNative040RAM* ram = memory->ram;
    if (!ram->bytes || !ram->size || ram->size > UINT64_C(0x100000000) - ram->address || address < ram->address)
        return 0;
    uint64_t delta = (uint64_t)address - ram->address;
    return delta <= ram->size && size <= ram->size - delta ? ram->bytes + delta : 0;
}

static int read_descriptor(void* opaque, uint32_t address, uint32_t* value)
{
    NativeMemory* memory = opaque;
    const unsigned char* bytes = ordinary_ram(memory, address, 4);
    if (bytes) {
        *value = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
        return 1;
    }
    const Syn68kMMU040Bus* bus = &memory->original->tables;
    return bus->read32 ? bus->read32(bus->opaque, address, value) : 0;
}

static int write_descriptor(void* opaque, uint32_t address, uint32_t value, int locked)
{
    NativeMemory* memory = opaque;
    unsigned char* bytes = ordinary_ram(memory, address, 4);
    if (bytes && (!locked || !((uintptr_t)bytes & 3))) {
        if (locked)
            __atomic_fetch_or((uint32_t*)bytes, __builtin_bswap32(value), __ATOMIC_SEQ_CST);
        else
            for (unsigned i = 0; i < 4; ++i)
                bytes[i] = value >> (24 - i * 8);
        return 1;
    }
    const Syn68kMMU040Bus* bus = &memory->original->tables;
    return bus->update32 ? bus->update32(bus->opaque, address, value, locked) : 0;
}

static int read_operand(void* opaque, uint32_t address, unsigned char* value, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    NativeMemory* memory = opaque;
    const unsigned char* bytes = fc == 1 || fc == 2 || fc == 5 || fc == 6 ? ordinary_ram(memory, address, size) : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            value[i] = bytes[i];
        return 1;
    }
    return memory->original->read &&
           memory->original->read(memory->original->opaque, address, value, size, attributes, fc);
}

static int write_operand(void* opaque, uint32_t address, const unsigned char* value, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    NativeMemory* memory = opaque;
    unsigned char* bytes = fc == 1 || fc == 2 || fc == 5 || fc == 6 ? ordinary_ram(memory, address, size) : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = value[i];
        return 1;
    }
    return memory->original->write &&
           memory->original->write(memory->original->opaque, address, value, size, attributes, fc);
}

static int read_cache_cycle(void* opaque, uint32_t address, unsigned char* value, unsigned size,
                            const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    NativeMemory* memory = opaque;
    unsigned fc = cycle->fc;
    const unsigned char* bytes = fc == 1 || fc == 2 || fc == 5 || fc == 6 ? ordinary_ram(memory, address, size) : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            value[i] = bytes[i];
        *inhibit = 0;
        return 1;
    }
    const Syn68kCache040Bus* bus = memory->original->cache->bus;
    if (bus) {
        if (bus->read_cycle)
            return bus->read_cycle(bus->opaque, address, value, size, cycle, inhibit);
        return bus->read && bus->read(bus->opaque, address, value, size, cycle->attributes, fc, cycle->burst,
                                      cycle->beat, cycle->way, inhibit);
    }
    *inhibit = 0;
    return memory->original->read &&
           memory->original->read(memory->original->opaque, address, value, size, cycle->attributes, fc);
}

static int write_cache_cycle(void* opaque, uint32_t address, const unsigned char* value, unsigned size,
                             const Syn68kCache040Cycle* cycle)
{
    NativeMemory* memory = opaque;
    unsigned fc = cycle->fc;
    unsigned char* bytes = fc == 1 || fc == 2 || fc == 5 || fc == 6 || cycle->kind == CACHE040_PUSH
                               ? ordinary_ram(memory, address, size)
                               : 0;
    if (bytes) {
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = value[i];
        return 1;
    }
    const Syn68kCache040Bus* bus = memory->original->cache->bus;
    if (bus) {
        if (bus->write_cycle)
            return bus->write_cycle(bus->opaque, address, value, size, cycle);
        return bus->write && bus->write(bus->opaque, address, value, size, cycle->attributes, fc, cycle->burst,
                                        cycle->beat, cycle->way);
    }
    return memory->original->write &&
           memory->original->write(memory->original->opaque, address, value, size, cycle->attributes, fc);
}

Syn68kMMU040Memory* native040_bind_memory(Syn68kNative040MMU* frame, NativeMemory* memory)
{
    if (!frame->memory || !frame->ram.bytes || !frame->ram.size ||
        frame->ram.size > UINT64_C(0x100000000) - frame->ram.address)
        return frame->memory;
    memory->memory = *frame->memory;
    memory->original = frame->memory;
    memory->ram = &frame->ram;
    memory->memory.tables = (Syn68kMMU040Bus){memory, read_descriptor, write_descriptor};
    memory->memory.opaque = memory;
    memory->memory.read = read_operand;
    memory->memory.write = write_operand;
    if (frame->memory->cache) {
        memory->cache = *frame->memory->cache;
        memory->cache_bus =
            (Syn68kCache040Bus){.opaque = memory, .read_cycle = read_cache_cycle, .write_cycle = write_cache_cycle};
        memory->cache.bus = &memory->cache_bus;
        memory->memory.cache = &memory->cache;
    }
    return &memory->memory;
}

#define ROOT(name) __attribute__((used, noinline)) void native040_##name(Syn68kNative040MMU* f)

ROOT(read)
{
    NativeMemory bound;
    f->access = syn68k_mmu040_read(native040_bind_memory(f, &bound), f->address, f->fc, f->size, f->rmw, &f->value);
}

ROOT(write)
{
    if (f->memory && f->memory->native && f->memory->native->write_attempted)
        *f->memory->native->write_attempted = 1;
    NativeMemory bound;
    f->access = syn68k_mmu040_write(native040_bind_memory(f, &bound), f->address, f->fc, f->size, f->value);
}

/* A mapped bus validates each actual transfer; flat multiword stores must
   validate their complete span before publishing the first scalar. */
ROOT(span)
{
    f->ok = 0;
    f->access =
        (Syn68kMMU040Access){.logical = f->address, .request = f->address, .size = f->size, .fc = f->fc, .writing = 1};
    f->access.translation.fault = MMU040_BAD_REQUEST;
    if (!f->memory || !f->memory->mmu || !f->size || f->size > 100 || f->fc > 7)
        return;
    const Syn68kNative040FlatState* state = f->memory->native ? f->memory->native->flat : 0;
    if (!state) {
        f->access.translation.fault = MMU040_OK;
        f->ok = 1;
        return;
    }
    Syn68kNative040Flat flat = {.state = state, .address = f->address, .size = f->size};
    native040_flat_span(&flat);
    if (flat.error == 11)
        return;
    f->access.translation.fault = MMU040_OK;
    f->ok = flat.ok;
    if (!flat.ok) {
        f->access.logical = flat.fault_address;
        f->access.bus_error = 1;
    }
}

ROOT(write_rmw)
{
    if (f->memory && f->memory->native && f->memory->native->write_attempted)
        *f->memory->native->write_attempted = 1;
    NativeMemory bound;
    f->access = syn68k_mmu040_write_rmw(native040_bind_memory(f, &bound), f->address, f->fc, f->size, f->value);
}

ROOT(translate)
{
    NativeMemory bound;
    Syn68kMMU040Memory* memory = native040_bind_memory(f, &bound);
    f->access = syn68k_mmu040_memory_translate(memory, f->address, f->fc, f->writing, 0);
    f->translation = f->access.translation;
}

ROOT(ptest)
{
    NativeMemory bound;
    Syn68kMMU040Memory* memory = native040_bind_memory(f, &bound);
    f->access = syn68k_mmu040_memory_translate(memory, f->address, f->fc, f->writing, 1);
    f->translation = f->access.translation;
}

/* Reuse all/size/writing as cache selector/scope/push in this packet. */
ROOT(memory_cache_maintain)
{
    NativeMemory bound;
    Syn68kMMU040Memory* memory = native040_bind_memory(f, &bound);
    f->access = syn68k_mmu040_cache_maintain(memory, f->all, f->size, f->address, f->writing);
}

ROOT(flush)
{
    syn68k_mmu040_flush(f->memory->mmu, f->all, f->address, f->supervisor, f->global);
}

ROOT(control)
{
    f->ok = syn68k_mmu040_control(f->memory->mmu, f->control, f->writing, &f->value);
}

ROOT(reset)
{
    syn68k_mmu040_reset(f->memory->mmu);
}

/* These typed entries match the public operand bus ABI, including its
   structure return. Their internal walker is linked into the closed image. */
__attribute__((used, noinline)) Syn68kMMU040Access native040_bus_read(Syn68kMMU040Memory* memory, uint32_t address,
                                                                      unsigned fc, unsigned size, int rmw,
                                                                      uint32_t* value)
{
    Syn68kNative040MMU frame = {.memory = memory};
    if (memory && memory->native)
        frame.ram = memory->native->ram;
    NativeMemory bound;
    return syn68k_mmu040_read(native040_bind_memory(&frame, &bound), address, fc, size, rmw, value);
}

__attribute__((used, noinline)) Syn68kMMU040Access native040_bus_write(Syn68kMMU040Memory* memory, uint32_t address,
                                                                       unsigned fc, unsigned size, uint32_t value)
{
    Syn68kNative040MMU frame = {.memory = memory};
    if (memory && memory->native) {
        frame.ram = memory->native->ram;
        if (memory->native->write_attempted)
            *memory->native->write_attempted = 1;
    }
    NativeMemory bound;
    return syn68k_mmu040_write(native040_bind_memory(&frame, &bound), address, fc, size, value);
}

__attribute__((used, noinline)) Syn68kMMU040Access native040_bus_write_rmw(Syn68kMMU040Memory* memory, uint32_t address,
                                                                           unsigned fc, unsigned size, uint32_t value)
{
    Syn68kNative040MMU frame = {.memory = memory};
    if (memory && memory->native) {
        frame.ram = memory->native->ram;
        if (memory->native->write_attempted)
            *memory->native->write_attempted = 1;
    }
    NativeMemory bound;
    return syn68k_mmu040_write_rmw(native040_bind_memory(&frame, &bound), address, fc, size, value);
}
