/*+===================================================================
  File:      mmu040-memory.c

  Summary:   Implements mmu040 memory support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_mmu040_access_ssw, syn68k_mmu040_read, syn68k_mmu040_write, syn68k_mmu040_write_rmw

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Logical operand access over the separate physical operand/descriptor buses. */
#include "mmu040-memory.h"
#if defined(SYN68K_NATIVE040_TEMPLATE)
#include "native040-flat.h"
#endif

typedef struct {
    Syn68kMMU040Memory* memory;
    Syn68kCache040Access fault;
    unsigned physical_abort, raw_abort;
} CacheMemory;

static int cache_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                      const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    CacheMemory* binding = opaque;
    Syn68kMMU040Memory* memory = binding->memory;
    if (cycle->kind == CACHE040_OPERAND && cycle->traffic == CACHE040_DESCRIPTOR) {
        uint32_t value;
        int status =
            size == 4 && memory->tables.read32 ? memory->tables.read32(memory->tables.opaque, address, &value) : 0;
        if (status <= 0) {
            binding->raw_abort |= status < 0;
            return 0;
        }
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = value >> (24 - i * 8);
        *inhibit = 0;
        return 1;
    }
    const Syn68kCache040Bus* bus = memory->cache ? memory->cache->bus : 0;
    if (bus) {
        if (bus->read_cycle)
            return bus->read_cycle(bus->opaque, address, bytes, size, cycle, inhibit);
        return bus->read && bus->read(bus->opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst,
                                      cycle->beat, cycle->way, inhibit);
    }
    *inhibit = 0;
    return memory->read && memory->read(memory->opaque, address, bytes, size, cycle->attributes, cycle->fc);
}

static int cache_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    CacheMemory* binding = opaque;
    Syn68kMMU040Memory* memory = binding->memory;
    if (cycle->kind == CACHE040_OPERAND && cycle->traffic == CACHE040_DESCRIPTOR) {
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i)
            value = (value << 8) | bytes[i];
        int status = size == 4 && memory->tables.update32
                         ? memory->tables.update32(memory->tables.opaque, address, value, 0)
                         : 0;
        binding->raw_abort |= status < 0;
        return status > 0;
    }
    const Syn68kCache040Bus* bus = memory->cache ? memory->cache->bus : 0;
    if (bus) {
        if (bus->write_cycle)
            return bus->write_cycle(bus->opaque, address, bytes, size, cycle);
        return bus->write && bus->write(bus->opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst,
                                        cycle->beat, cycle->way);
    }
    return memory->write && memory->write(memory->opaque, address, bytes, size, cycle->attributes, cycle->fc);
}

static Syn68kCache040Bus cache_bus(CacheMemory* binding)
{
    return (Syn68kCache040Bus){.opaque = binding, .read_cycle = cache_read, .write_cycle = cache_write};
}

static int table_failure(CacheMemory* binding, Syn68kCache040Access access)
{
    unsigned physical = binding->raw_abort || access.fault != CACHE040_OPERAND_FAULT;
    /* A later physical push cannot be hidden by an earlier table diagnostic.
       Preserve its captured dirty data instead of publishing a negative ATC. */
    if (!binding->fault.fault || (physical && !binding->physical_abort))
        binding->fault = access;
    binding->physical_abort |= physical;
    return physical ? -1 : 0;
}

static int cache_table_read(void* opaque, uint32_t address, uint32_t* value)
{
    CacheMemory* binding = opaque;
    binding->raw_abort = 0;
    const Syn68kMMU040Cache* cache = binding->memory->cache;
    Syn68kCache040Bus bus = cache_bus(binding);
    Syn68kCache040Access access =
        syn68k_cache040_transfer(cache->state, &bus, address, 5, 4, 0, 0, 0, *cache->cacr, 1, CACHE040_DESCRIPTOR);
    if (access.fault)
        return table_failure(binding, access);
    *value = access.value;
    return 1;
}

static int cache_table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    CacheMemory* binding = opaque;
    if (binding->physical_abort)
        return -1;
    binding->raw_abort = 0;
    Syn68kMMU040Memory* memory = binding->memory;
    const Syn68kMMU040Cache* cache = memory->cache;
    Syn68kCache040Bus bus = cache_bus(binding);
    if (!locked) {
        Syn68kCache040Access access = syn68k_cache040_transfer(cache->state, &bus, address, 5, 4, 1, value, 0,
                                                               *cache->cacr, 1, CACHE040_DESCRIPTOR);
        return access.fault ? table_failure(binding, access) : 1;
    }
    /* U-bit updates retain the embedding's indivisible read/OR/write after
       any cached line has been pushed/invalidated. A push must never be
       mistaken for a descriptor update merely because a walk triggered it. */
    Syn68kCache040Access access =
        syn68k_cache040_maintain_traffic(cache->state, &bus, 1, 1, address, 12, 1, *cache->cacr, CACHE040_LOCKED);
    if (access.fault)
        return table_failure(binding, access);
    int status = memory->tables.update32 ? memory->tables.update32(memory->tables.opaque, address, value, 1) : 0;
    if (status > 0)
        return 1;
    binding->raw_abort = status < 0;
    return table_failure(binding, (Syn68kCache040Access){.fault = CACHE040_OPERAND_FAULT,
                                                         .bus_address = address,
                                                         .cycle = {.attributes = 0x40,
                                                                   .fc = 5,
                                                                   .kind = CACHE040_OPERAND,
                                                                   .traffic = CACHE040_LOCKED,
                                                                   .locked = 1,
                                                                   .lock_end = 1}});
}

static int valid_cache(const Syn68kMMU040Memory* memory)
{
    const Syn68kMMU040Cache* cache = memory->cache;
    return !cache || (cache->state && cache->cacr && (unsigned)cache->traffic <= CACHE040_LOCKED_CONTINUE &&
                      cache->traffic != CACHE040_DESCRIPTOR && !(memory->native && memory->native->flat));
}

Syn68kMMU040Access syn68k_mmu040_memory_translate(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc,
                                                  int writing, int ptest)
{
    Syn68kMMU040Access result = {.logical = logical, .request = logical, .fc = fc, .writing = !!writing};
    result.translation.fault = MMU040_BAD_REQUEST;
    if (!memory || !memory->mmu || !valid_cache(memory) || (ptest != 0 && ptest != 1))
        return result;
    CacheMemory binding = {.memory = memory};
    Syn68kMMU040Bus tables =
        memory->cache ? (Syn68kMMU040Bus){&binding, cache_table_read, cache_table_write} : memory->tables;
    result.translation = ptest ? syn68k_mmu040_ptest(memory->mmu, &tables, logical, fc, writing)
                               : syn68k_mmu040_translate(memory->mmu, &tables, logical, fc, writing);
    result.cache = binding.fault;
    result.cache_table = !!binding.fault.fault;
    result.cache_exception = binding.physical_abort || result.translation.fault == MMU040_PHYSICAL_ABORT;
    return result;
}

Syn68kMMU040Access syn68k_mmu040_cache_maintain(Syn68kMMU040Memory* memory, unsigned caches, unsigned scope,
                                                uint32_t physical, int push)
{
    Syn68kMMU040Access result = {.logical = physical, .request = physical, .writing = !!push};
    result.translation.fault = MMU040_BAD_REQUEST;
    if (!memory || !memory->mmu || !memory->cache || !valid_cache(memory))
        return result;
    CacheMemory binding = {.memory = memory};
    Syn68kCache040Bus bus = cache_bus(&binding);
    result.cache =
        syn68k_cache040_maintain_interruptible(memory->cache->state, &bus, caches, scope, physical,
                                               memory->mmu->tc & 0x4000 ? 13 : 12, push, &memory->cache->interrupt);
    if (result.cache.fault == CACHE040_BAD_REQUEST)
        return result;
    result.translation.fault = result.cache.fault ? MMU040_PHYSICAL_ABORT : MMU040_OK;
    result.bus_error = result.cache_exception = !!result.cache.fault;
    return result;
}

static Syn68kMMU040Access transfer(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                   int writing, int rmw, uint32_t* value)
{
    Syn68kMMU040Access result = {0};
    result.logical = logical;
    result.request = logical;
    result.size = size;
    result.fc = fc;
    result.writing = !!writing;
    result.rmw = !!rmw;
    result.translation.fault = MMU040_BAD_REQUEST;
    if (!memory || !memory->mmu || !value || memory->aligned_transfers > 1 || fc > 7 ||
        (size != 1 && size != 2 && size != 4) || !valid_cache(memory))
        return result;
#if defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory->native && memory->native->flat) {
        Syn68kNative040Flat flat = {
            .state = memory->native->flat, .address = logical, .size = size, .fc = fc, .value = writing ? *value : 0};
        if (writing)
            native040_flat_write(&flat);
        else
            native040_flat_read(&flat);
        if (flat.error == 11)
            return result;
        result.translation.fault = MMU040_OK;
        if (writing)
            result.value = *value & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
        if (!flat.ok) {
            result.logical = flat.fault_address;
            result.bus_error = 1;
            return result;
        }
        result.completed = size;
        if (!writing)
            *value = result.value = flat.value;
        else if (memory->native->flat_code_pages && memory->native->flat_write &&
                 memory->native->flat_page_shift < 32) {
            uint32_t mask = *memory->native->flat->mask;
            for (unsigned i = 0; i < size; ++i)
                if (memory->native->flat_code_pages[((logical + i) & mask) >> memory->native->flat_page_shift]) {
                    memory->native->flat_write(logical, size);
                    break;
                }
        }
        return result;
    }
#endif
    if (!memory->cache && (writing ? !memory->write : !memory->read))
        return result;
    if (writing && memory->native && memory->native->write_attempted)
        *memory->native->write_attempted = 1;

    unsigned char bytes[4];
    if (writing) {
        result.value = *value & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
        for (unsigned i = 0; i < size; ++i)
            bytes[i] = result.value >> ((size - i - 1) * 8);
    }
    /* MC68040UM 4.1: a misaligned data operand spanning two pages accesses its
       first-page portion twice, including serialized pages. The first pass is
       internal; only the replayed data belongs to a completed read. Latch the
       initial page extent so callback changes cannot create repeated replays. */
    unsigned initial_mask = memory->mmu->tc & 0x4000 ? 8191 : 4095;
    unsigned repeat_prefix = initial_mask + 1 - (logical & initial_mask);
    /* Instruction-space reads belong to the aligned prefetch stream, not
       the data unit's misaligned-operand replay (MC68040UM 7.3/7.4.2). */
    if (repeat_prefix >= size || (fc & 3) == 2)
        repeat_prefix = 0;
    while (result.completed < size) {
        result.logical = logical + result.completed;
        Syn68kMMU040Access translated = syn68k_mmu040_memory_translate(memory, result.logical, fc, writing || rmw, 0);
        result.translation = translated.translation;
        result.cache = translated.cache;
        result.cache_table = translated.cache_table;
        result.cache_exception = translated.cache_exception;
        if (result.translation.fault != MMU040_OK)
            return result;
        /* TTR boundaries are coarser than either page size. This also splits
         the 32-bit address wrap when translation is disabled or bypassed. */
        unsigned mask = memory->mmu->tc & 0x4000 ? 8191 : 4095;
        unsigned chunk = mask + 1 - (result.logical & mask);
        if (chunk > size - result.completed)
            chunk = size - result.completed;
        if (repeat_prefix && chunk > repeat_prefix - result.completed)
            chunk = repeat_prefix - result.completed;
        /* The uncached data memory unit issues only naturally aligned bus
           transfers (MC68040UM 7.3), including byte/word/byte for odd longs.
           Cache/device operand bindings may instead own this decomposition. */
        if (memory->aligned_transfers && !memory->cache) {
            unsigned aligned = (result.translation.physical & 1) || chunk < 2   ? 1
                               : (result.translation.physical & 2) || chunk < 4 ? 2
                                                                                : 4;
            if (chunk > aligned)
                chunk = aligned;
        }
        if (memory->cache) {
            const Syn68kMMU040Cache* cache = memory->cache;
            CacheMemory binding = {.memory = memory};
            Syn68kCache040Bus bus = cache_bus(&binding);
            uint32_t operand = 0;
            if (writing)
                for (unsigned i = 0; i < chunk; ++i)
                    operand = (operand << 8) | bytes[result.completed + i];
            enum Syn68kCache040Traffic traffic =
                rmw && cache->traffic != CACHE040_LOCKED_CONTINUE ? CACHE040_LOCKED : cache->traffic;
            if (writing && traffic == CACHE040_LOCKED && result.completed + chunk < size)
                traffic = CACHE040_LOCKED_CONTINUE;
            Syn68kCache040Access access =
                syn68k_cache040_transfer(cache->state, &bus, result.translation.physical, fc, chunk, writing, operand,
                                         result.translation.attributes, *cache->cacr, 0, traffic);
            if (access.fault) {
                result.cache = access;
                result.cache_exception = 1;
                if (!writing)
                    for (unsigned i = 0; i < access.completed; ++i)
                        result.value = (result.value << 8) | ((access.value >> ((access.completed - i - 1) * 8)) & 255);
                result.completed += access.completed;
                result.logical = logical + result.completed;
                result.bus_error = access.fault != CACHE040_BAD_REQUEST;
                if (!result.bus_error)
                    result.translation.fault = MMU040_BAD_REQUEST;
                return result;
            }
            if (!writing)
                for (unsigned i = 0; i < chunk; ++i)
                    result.value = (result.value << 8) | ((access.value >> ((chunk - i - 1) * 8)) & 255);
        }
        else {
            int ok = writing ? memory->write(memory->opaque, result.translation.physical, bytes + result.completed,
                                             chunk, result.translation.attributes, fc)
                             : memory->read(memory->opaque, result.translation.physical, bytes + result.completed,
                                            chunk, result.translation.attributes, fc);
            if (!ok) {
                result.bus_error = 1;
                return result;
            }
            if (!writing)
                for (unsigned i = result.completed; i < result.completed + chunk; ++i)
                    result.value = (result.value << 8) | bytes[i];
        }
        result.completed += chunk;
        if (repeat_prefix && result.completed == repeat_prefix) {
            repeat_prefix = 0;
            result.completed = 0;
            if (!writing)
                result.value = 0;
        }
    }
    if (!writing)
        *value = result.value;
    return result;
}

Syn68kMMU040Access syn68k_mmu040_read(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size, int rmw,
                                      uint32_t* value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->read)
        return memory->native->read(memory, logical, fc, size, rmw, value);
#endif
    return transfer(memory, logical, fc, size, 0, rmw, value);
}

Syn68kMMU040Access syn68k_mmu040_write(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                       uint32_t value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->write)
        return memory->native->write(memory, logical, fc, size, value);
#endif
    return transfer(memory, logical, fc, size, 1, 0, &value);
}

Syn68kMMU040Access syn68k_mmu040_write_rmw(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, unsigned size,
                                           uint32_t value)
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->write_rmw)
        return memory->native->write_rmw(memory, logical, fc, size, value);
#endif
    return transfer(memory, logical, fc, size, 1, 1, &value);
}

Syn68kMMU040Access syn68k_mmu040_move16(Syn68kMMU040Memory* memory, uint32_t logical, unsigned fc, int writing,
                                        unsigned char bytes[16])
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->move16)
        return memory->native->move16(memory, logical, fc, writing, bytes);
#endif
    uint32_t first = logical & ~3u;
    Syn68kMMU040Access result = {.logical = first, .request = first, .fc = fc, .size = 4, .writing = !!writing};
    result.translation.fault = MMU040_BAD_REQUEST;
    if (!memory || !memory->mmu || !bytes || (fc != 1 && fc != 5) || (writing != 0 && writing != 1) ||
        memory->aligned_transfers > 1 || !valid_cache(memory) ||
        (memory->cache && (memory->cache->state->active || memory->cache->traffic != CACHE040_NORMAL)) ||
        (memory->native && memory->native->flat) || (!memory->cache && (writing ? !memory->write : !memory->read)))
        return result;
    unsigned char held[16];
    if (writing) {
        for (unsigned i = 0; i < 16; ++i)
            held[i] = bytes[i];
        result.value = (uint32_t)held[(first & 15)] << 24 | (uint32_t)held[(first & 15) + 1] << 16 |
                       (uint32_t)held[(first & 15) + 2] << 8 | held[(first & 15) + 3];
        if (memory->native && memory->native->write_attempted)
            *memory->native->write_attempted = 1;
    }
    Syn68kMMU040Access translated = syn68k_mmu040_memory_translate(memory, first, fc, writing, 0);
    result.translation = translated.translation;
    result.cache = translated.cache;
    result.cache_table = translated.cache_table;
    result.cache_exception = translated.cache_exception;
    if (result.translation.fault != MMU040_OK)
        return result;
    CacheMemory binding = {.memory = memory};
    Syn68kCache040Bus bus = cache_bus(&binding);
    result.cache =
        syn68k_cache040_move16(memory->cache ? memory->cache->state : 0, &bus, result.translation.physical, fc, writing,
                               held, result.translation.attributes, memory->cache ? *memory->cache->cacr : 0);
    if (result.cache.fault) {
        result.cache_exception = 1;
        if (result.cache.fault == CACHE040_BAD_REQUEST) {
            result.translation.fault = MMU040_BAD_REQUEST;
            return result;
        }
        result.bus_error = 1;
        if (result.cache.fault == CACHE040_OPERAND_FAULT) {
            result.logical = result.request = (first & ~15u) | (result.cache.bus_address & 15);
            result.translation.physical = result.cache.bus_address;
            if (writing) {
                unsigned word = result.cache.bus_address & 15;
                result.value = (uint32_t)held[word] << 24 | (uint32_t)held[word + 1] << 16 |
                               (uint32_t)held[word + 2] << 8 | held[word + 3];
            }
        }
        return result;
    }
    if (!writing)
        for (unsigned i = 0; i < 16; ++i)
            bytes[i] = held[i];
    result.size = result.completed = 16;
    return result;
}

Syn68kCache040Snoop syn68k_mmu040_snoop(Syn68kMMU040Memory* memory, uint32_t physical, unsigned control,
                                        unsigned transfer_type, unsigned size, int writing, unsigned char bytes[16])
{
#if !defined(SYN68K_NATIVE040_TEMPLATE)
    if (memory && memory->native && memory->native->snoop)
        return memory->native->snoop(memory, physical, control, transfer_type, size, writing, bytes);
#endif
    if (!memory)
        return (Syn68kCache040Snoop){.fault = CACHE040_BAD_REQUEST};
    return syn68k_cache040_snoop(memory->cache ? memory->cache->state : 0, physical, control, transfer_type, size,
                                 writing, bytes);
}

int syn68k_mmu040_cache_push_ssw(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    if (!access || !ssw || !access->cache_exception || access->cache.fault != CACHE040_PUSH_FAULT ||
        (access->translation.fault != MMU040_OK && access->translation.fault != MMU040_PHYSICAL_ABORT) ||
        (!access->bus_error && access->translation.fault != MMU040_PHYSICAL_ABORT))
        return 0;
    const Syn68kCache040Access* push = &access->cache;
    const Syn68kCache040Cycle* cycle = &push->cycle;
    if ((push->push_size != 4 && push->push_size != 16) || (push->push_address & (push->push_size - 1)) ||
        push->interrupt_level || cycle->kind != CACHE040_PUSH || cycle->fc || cycle->attributes || cycle->way > 3 ||
        (unsigned)cycle->traffic > CACHE040_MOVE16 || cycle->lock_end ||
        cycle->locked != (cycle->traffic == CACHE040_LOCKED || cycle->traffic == CACHE040_LOCKED_CONTINUE))
        return 0;
    if (push->push_size == 16) {
        if (cycle->burst != 16 || cycle->beat > 3 || push->push_word != cycle->beat ||
            push->bus_address != (uint32_t)(push->push_address + cycle->beat * 4))
            return 0;
    }
    else if (cycle->burst || cycle->beat || push->push_word != ((push->push_address >> 2) & 3) ||
             push->bus_address != push->push_address)
        return 0;
    *ssw = (push->push_size == 16 ? 0x60 : 0) | (cycle->locked ? 0x200 : 0);
    return 1;
}

int syn68k_mmu040_access_ssw(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    if (!access || !ssw || access->fc > 7 || (access->size != 1 && access->size != 2 && access->size != 4) ||
        access->completed >= access->size)
        return 0;
    if (access->cache.fault && access->cache.fault != CACHE040_OPERAND_FAULT) {
        const Syn68kCache040Cycle* cycle = &access->cache.cycle;
        /* UM 2.2/8.4.6: preserve the execution unit's original request.
           A copyback allocation retains its logical store in WB2 until the
           line can accept it; an internal line read is not a WB1 write. */
        if (access->cache.fault != CACHE040_FILL_FAULT || access->cache_table || !access->cache_exception ||
            access->rmw || !access->bus_error || access->translation.fault != MMU040_OK ||
            cycle->kind != CACHE040_OPERAND || cycle->fc != access->fc || cycle->burst != 16 || cycle->beat > 3 ||
            cycle->way > 3 || cycle->locked || cycle->lock_end || access->cache.interrupt_level ||
            (access->cache.bus_address & 3) ||
            (access->fc != 1 && access->fc != 2 && access->fc != 5 && access->fc != 6))
            return 0;
        if (access->writing) {
            uint32_t physical = access->translation.physical + access->cache.completed;
            uint32_t beat = (physical & ~15u) + ((((physical >> 2) & 3) + cycle->beat) & 3) * 4;
            unsigned chunk = access->size - access->completed;
            if (chunk > 16 - (physical & 15))
                chunk = 16 - (physical & 15);
            if (access->writing != 1 || (access->fc != 1 && access->fc != 5) || cycle->traffic != CACHE040_NORMAL ||
                (cycle->attributes & 0x60) != 0x20 || cycle->attributes != access->translation.attributes ||
                access->cache.completed > access->completed ||
                access->logical != (uint32_t)(access->request + access->completed) ||
                access->cache.bus_address != beat || cycle->beat > ((physical & 3) + chunk > 4))
                return 0;
        }
    }
    int atc;
    switch (access->translation.fault) {
    case MMU040_OK:
        if (!access->bus_error)
            return 0;
        atc = 0;
        break;
    case MMU040_INVALID:
    case MMU040_TABLE_BUS_ERROR:
    case MMU040_WRITE_PROTECTED:
    case MMU040_SUPERVISOR_ONLY:
        atc = 1;
        break;
    default:
        return 0;
    }
    /* MC68040UM table 3-2: alternate spaces bypass translation and use
       TT=2. The integer MOVES decoder already converts FC2/6 to FC1/5. */
    unsigned alternate = (access->fc & 3) == 0 || (access->fc & 3) == 3;
    if (alternate && atc)
        return 0;
    *ssw = access->fc | (alternate ? 0x10 : 0) |
           (access->size == 1   ? 0x20
            : access->size == 2 ? 0x40
                                : 0) |
           (!access->writing ? 0x100 : 0) | (access->rmw ? 0x200 : 0) | (atc ? 0x400 : 0) |
           (atc && access->completed ? 0x800 : 0);
    return 1;
}

/* Pending operand slots describe the logical transfer, independently of a
   failed physical cache push. The push itself keeps its own SSW/PD snapshot. */
int syn68k_mmu040_operand_status(const Syn68kMMU040Access* access, uint16_t* ssw)
{
    if (!ssw)
        return 0;
    if (syn68k_mmu040_access_ssw(access, ssw))
        return 1;
    uint16_t push_ssw;
    if (!syn68k_mmu040_cache_push_ssw(access, &push_ssw) || access->writing > 1 || access->rmw > 1 ||
        (access->size != 1 && access->size != 2 && access->size != 4) || access->completed > access->size ||
        (!access->writing && access->completed == access->size) || access->cache.completed > access->completed ||
        access->logical != (uint32_t)(access->request + access->completed) ||
        (access->fc != 1 && access->fc != 2 && access->fc != 5 && access->fc != 6) ||
        (access->writing && access->fc != 1 && access->fc != 5) ||
        (access->rmw && (access->completed || (access->fc != 1 && access->fc != 5))))
        return 0;
    *ssw = access->fc |
           (access->size == 1   ? 0x20
            : access->size == 2 ? 0x40
                                : 0) |
           (access->writing ? 0 : 0x100) | (access->rmw ? 0x200 : 0);
    return 1;
}
