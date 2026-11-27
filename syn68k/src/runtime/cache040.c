/*+===================================================================
  File:      cache040.c

  Summary:   Implement physical MC68040 cache, snoop and maintenance transactions.

  Classes:   None.

  Functions: syn68k_cache040_access, syn68k_cache040_snoop, syn68k_cache040_move16,
             syn68k_cache040_maintain

  Origin:    Existing repository cache implementation, extended with physical snoop support.

  Copyright and Legal notices: Applicable license: syn68k/COPYING. Original SPDX notice is retained below.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Functional cache storage and physical transactions. No CPU globals. */
#include "cache040.h"

void syn68k_cache040_init(Syn68kCache040* cache)
{
    if (cache)
        *cache = (Syn68kCache040){0};
}

static Syn68kCache040Line* lookup(Syn68kCache040* cache, unsigned bank, uint32_t address)
{
    Syn68kCache040Line* set = cache->lines[bank][(address >> 4) & 63];
    for (unsigned i = 0; i < 4; ++i)
        if (set[i].valid && set[i].address == (address & ~15u))
            return &set[i];
    return 0;
}

static unsigned width(uint32_t address, unsigned remaining)
{
    return (address & 1) || remaining < 2 ? 1 : (address & 2) || remaining < 4 ? 2 : 4;
}

static Syn68kCache040Cycle cycle_for(const Syn68kCache040* cache, uint32_t attributes, unsigned fc, unsigned burst,
                                     unsigned beat, unsigned way, enum Syn68kCache040CycleKind kind, unsigned lock_end)
{
    unsigned locked = cache->traffic == CACHE040_LOCKED || cache->traffic == CACHE040_LOCKED_CONTINUE;
    return (Syn68kCache040Cycle){.attributes = attributes,
                                 .fc = fc,
                                 .burst = burst,
                                 .beat = beat,
                                 .way = way,
                                 .kind = kind,
                                 .traffic = cache->traffic,
                                 .locked = locked,
                                 .lock_end = cache->traffic == CACHE040_LOCKED && lock_end};
}

static int read_bus(const Syn68kCache040Bus* bus, uint32_t address, unsigned char* bytes, unsigned size,
                    const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    if (!bus->read_cycle)
        return bus->read(bus->opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst,
                         cycle->beat, cycle->way, inhibit);
    int status;
    do {
        *inhibit = 0;
        status = bus->read_cycle(bus->opaque, address, bytes, size, cycle, inhibit);
    } while (status == CACHE040_CYCLE_RETRY && !(cycle->burst && cycle->beat));
    return status != CACHE040_CYCLE_RETRY && status != 0;
}

static int write_bus(const Syn68kCache040Bus* bus, uint32_t address, const unsigned char* bytes, unsigned size,
                     const Syn68kCache040Cycle* cycle)
{
    if (!bus->write_cycle)
        return bus->write(bus->opaque, address, bytes, size, cycle->attributes, cycle->fc, cycle->burst,
                          cycle->beat, cycle->way);
    int status;
    do
        status = bus->write_cycle(bus->opaque, address, bytes, size, cycle);
    while (status == CACHE040_CYCLE_RETRY && !(cycle->burst && cycle->beat));
    return status != CACHE040_CYCLE_RETRY && status != 0;
}

static int push_line(Syn68kCache040* cache, Syn68kCache040Line* line, const Syn68kCache040Bus* bus, unsigned way,
                     Syn68kCache040Access* result)
{
    unsigned dirty = line->dirty & 15;
    if (!line->valid || !dirty)
        return 1;
    unsigned size = (dirty & (dirty - 1)) ? 16 : 4;
    unsigned first = 0;
    if (size == 4)
        while (!(dirty & (1u << first)))
            ++first;
    for (unsigned i = 0; i < size / 4; ++i) {
        unsigned word = first + i;
        uint32_t address = line->address + word * 4;
        Syn68kCache040Cycle cycle = cycle_for(cache, 0, 0, size == 16 ? 16 : 0, i, way, CACHE040_PUSH, 0);
        if (!write_bus(bus, address, line->bytes + word * 4, 4, &cycle)) {
            result->fault = CACHE040_PUSH_FAULT;
            result->bus_address = address;
            result->push_address = line->address + first * 4;
            result->push_size = size;
            result->push_word = word;
            result->cycle = cycle;
            for (unsigned j = 0; j < 16; ++j)
                result->push_data[j] = line->bytes[j];
            return 0;
        }
    }
    line->dirty = 0;
    return 1;
}

static void store_line(Syn68kCache040Line* line, uint32_t address, const unsigned char* bytes, unsigned size, int dirty)
{
    for (unsigned i = 0; i < size; ++i) {
        unsigned offset = (address & 15) + i;
        line->bytes[offset] = bytes[i];
        if (dirty)
            line->dirty |= 1u << (offset >> 2);
    }
}

static Syn68kCache040Line* fill_line(Syn68kCache040* cache, const Syn68kCache040Bus* bus, unsigned bank,
                                     uint32_t address, unsigned size, uint32_t attributes, unsigned fc, unsigned way,
                                     unsigned char* requested, const unsigned char* store, Syn68kCache040Access* result)
{
    Syn68kCache040Line* line = &cache->lines[bank][(address >> 4) & 63][way];
    Syn68kCache040Line old = *line, fresh = {.address = address & ~15u, .valid = 1};
    unsigned received = 0, inhibit = 0;
    unsigned needed = ((1u << size) - 1) << (address & 15);
    line->valid = 0;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned word = (((address >> 2) & 3) + i) & 3;
        unsigned beat_inhibit = 0;
        uint32_t physical = fresh.address + word * 4;
        Syn68kCache040Cycle cycle = cycle_for(cache, attributes, fc, 16, i, way, CACHE040_OPERAND, 0);
        if (!read_bus(bus, physical, fresh.bytes + word * 4, 4, &cycle, &beat_inhibit)) {
            /* An unrequested trailing fill fault aborts allocation but not an
               already satisfied operand. Dirty victims return from the buffer;
               clean victims stay invalid even when allocation fails. */
            if (old.valid && old.dirty)
                *line = old;
            if ((received & needed) != needed) {
                result->fault = CACHE040_FILL_FAULT;
                result->bus_address = physical;
                result->cycle = cycle;
            }
            for (unsigned j = 0; j < size; ++j)
                requested[j] = fresh.bytes[(address & 15) + j];
            return 0;
        }
        if (!i)
            inhibit = !!beat_inhibit;
        received |= 15u << (word * 4);
    }
    for (unsigned j = 0; j < size; ++j)
        requested[j] = fresh.bytes[(address & 15) + j];
    if (inhibit) {
        if (old.valid && old.dirty)
            *line = old;
        return 0;
    }
    /* Table 4-4 D3: a copyback write miss updates the incoming line before
       pushing its victim. The new dirty data survives a failed old push. */
    if (store)
        store_line(&fresh, address, store, size, 1);
    *line = fresh;
    /* The requested fill precedes the victim push. An external bus grant at
       this boundary must see both the incoming line and the buffered victim
       (UM 4.7.2). Snoop changes determine the subsequent push, including its
       dirty-word selection or cancellation after ownership is relinquished. */
    if (!bank && old.valid && (old.dirty & 15) && cache->arbitrate) {
        cache->victim = old;
        cache->victim_live = 1;
        cache->victim_way = way;
        cache->snoop_window = 1;
        cache->arbitrate(cache->arbitrate_opaque, cache);
        cache->snoop_window = 0;
        old = cache->victim;
        cache->victim = (Syn68kCache040Line){0};
        cache->victim_live = cache->victim_way = 0;
    }
    /* The fresh line remains installed if the old push faults; the returned
       fault owns the complete, possibly snoop-modified victim data. */
    if (!push_line(cache, &old, bus, way, result))
        return 0;
    return line;
}

static Syn68kCache040Access access_inner(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t address,
                                         unsigned fc, unsigned size, int writing, uint32_t value, uint32_t attributes,
                                         uint32_t cacr, int no_allocate)
{
    Syn68kCache040Access result = {0};
    if (writing)
        result.value = value & (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
    unsigned char bytes[4];
    for (unsigned i = 0; i < size; ++i)
        bytes[i] = value >> ((size - i - 1) * 8);
    unsigned bank = !writing && (fc == 2 || fc == 6);
    unsigned enabled = !!(cacr & (bank ? 0x8000u : 0x80000000u));
    unsigned locked = cache->traffic == CACHE040_LOCKED || cache->traffic == CACHE040_LOCKED_CONTINUE;
    unsigned descriptor = cache->traffic == CACHE040_DESCRIPTOR;
    if (cache->traffic != CACHE040_NORMAL)
        no_allocate = 1;
    if (descriptor)
        attributes &= ~0x60u;
    if (locked)
        attributes = (attributes & ~0x60u) | 0x40;
    unsigned mode = (attributes >> 5) & 3;
    unsigned ordinary = fc == 1 || fc == 2 || fc == 5 || fc == 6;
    while (result.completed < size) {
        uint32_t physical = address + result.completed;
        unsigned chunk = 16 - (physical & 15);
        if (chunk > size - result.completed)
            chunk = size - result.completed;
        Syn68kCache040Line* line = ordinary && (enabled || locked || descriptor) ? lookup(cache, bank, physical) : 0;
        unsigned allocate = !no_allocate;
        if (descriptor && !enabled && line) {
            result.fault = CACHE040_DESCRIPTOR_LOCKUP;
            result.bus_address = physical;
            goto complete;
        }
        if (locked && !enabled && line)
            cache->bypass_data_miss = 1;
        if (enabled && ordinary && !bank && !locked && !line && cache->bypass_data_miss) {
            cache->bypass_data_miss = 0;
            allocate = 0;
        }
        unsigned way = line ? (unsigned)(line - cache->lines[bank][(physical >> 4) & 63]) : 0;
        if (ordinary && mode >= 2 && line && !bank) {
            if (!push_line(cache, line, bus, way, &result)) {
                line->valid = 0;
                goto complete;
            }
            line->valid = 0;
            line = 0;
        }
        if (enabled && ordinary && mode < 2) {
            unsigned count = writing ? 1 : 1 + (((physical & 7) + chunk - 1) >> 3);
            if (!line && allocate && (!writing || mode == 1)) {
                Syn68kCache040Line* set = cache->lines[bank][(physical >> 4) & 63];
                for (way = 0; way < 4 && set[way].valid; ++way) {
                }
                if (way == 4)
                    way = cache->replacement[bank] & 3;
                unsigned char read_data[4];
                line = fill_line(cache, bus, bank, physical, chunk, attributes, fc, way,
                                 writing ? read_data : bytes + result.completed, writing ? bytes + result.completed : 0,
                                 &result);
                cache->replacement[bank] = (cache->replacement[bank] + count) & 3;
                if (result.fault) {
                    if (writing && result.fault == CACHE040_PUSH_FAULT)
                        result.completed += chunk;
                    goto complete;
                }
                if (line && writing) {
                    result.completed += chunk;
                    continue;
                }
                if (!line && !writing) {
                    result.completed += chunk;
                    continue;
                }
            }
            else if (!writing || mode == 1)
                cache->replacement[bank] = (cache->replacement[bank] + count) & 3;
        }
        if (line && !writing && mode < 2) {
            for (unsigned i = 0; i < chunk; ++i)
                bytes[result.completed + i] = line->bytes[(physical & 15) + i];
        }
        else if (line && writing && mode == 1) {
            store_line(line, physical, bytes + result.completed, chunk, 1);
        }
        else {
            /* V6/D6 update all requested cache bytes before the WT bus
               transfers. A failed transfer retains that cache update. */
            if (line && writing)
                store_line(line, physical, bytes + result.completed, chunk, 0);
            unsigned done = 0;
            while (done < chunk) {
                unsigned beat = width(physical + done, chunk - done), ignored = 0;
                Syn68kCache040Cycle cycle = cycle_for(cache, attributes, fc, 0, 0, way, CACHE040_OPERAND,
                                                      writing && result.completed + beat == size);
                int ok = writing ? write_bus(bus, physical + done, bytes + result.completed, beat, &cycle)
                                 : read_bus(bus, physical + done, bytes + result.completed, beat, &cycle, &ignored);
                if (enabled && ordinary && mode == 0 && writing)
                    cache->replacement[bank] = (cache->replacement[bank] + 1) & 3;
                if (!ok) {
                    result.fault = CACHE040_OPERAND_FAULT;
                    result.bus_address = physical + done;
                    result.cycle = cycle;
                    goto complete;
                }
                done += beat;
                result.completed += beat;
            }
            continue;
        }
        result.completed += chunk;
    }
complete:
    if (!writing)
        for (unsigned i = 0; i < result.completed; ++i)
            result.value = (result.value << 8) | bytes[i];
    return result;
}

Syn68kCache040Access syn68k_cache040_access(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t address,
                                            unsigned fc, unsigned size, int writing, uint32_t value,
                                            uint32_t attributes, uint32_t cacr, int no_allocate)
{
    return syn68k_cache040_transfer(cache, bus, address, fc, size, writing, value, attributes, cacr, no_allocate,
                                    CACHE040_NORMAL);
}

Syn68kCache040Access syn68k_cache040_transfer(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t address,
                                              unsigned fc, unsigned size, int writing, uint32_t value,
                                              uint32_t attributes, uint32_t cacr, int no_allocate,
                                              enum Syn68kCache040Traffic traffic)
{
    if (!cache || !bus || (!bus->read && !bus->read_cycle) || (!bus->write && !bus->write_cycle) || cache->active ||
        fc > 7 || !size || size > 4 || (writing != 0 && writing != 1) || (no_allocate != 0 && no_allocate != 1) ||
        (unsigned)traffic > CACHE040_LOCKED_CONTINUE || (traffic != CACHE040_NORMAL && fc != 1 && fc != 5) ||
        (traffic == CACHE040_DESCRIPTOR && (size != 4 || (address & 3))))
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    cache->active = 1;
    cache->traffic = traffic;
    Syn68kCache040Access result =
        access_inner(cache, bus, address, fc, size, writing, value, attributes, cacr, no_allocate);
    cache->traffic = CACHE040_NORMAL;
    cache->active = 0;
    return result;
}

Syn68kCache040Access syn68k_cache040_move16(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t address,
                                            unsigned fc, int writing, unsigned char bytes[16], uint32_t attributes,
                                            uint32_t cacr)
{
    Syn68kCache040Access result = {0};
    if (!bus || (!bus->read && !bus->read_cycle) || (!bus->write && !bus->write_cycle) || !bytes ||
        (cache && cache->active) || (fc != 1 && fc != 5) || (writing != 0 && writing != 1))
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    unsigned char held[16];
    if (writing)
        for (unsigned i = 0; i < 16; ++i)
            held[i] = bytes[i];
    if (cache) {
        cache->active = 1;
        cache->traffic = CACHE040_MOVE16;
    }
    unsigned enabled = cache && (cacr & 0x80000000u), mode = (attributes >> 5) & 3;
    Syn68kCache040Line* line = enabled ? lookup(cache, 0, address) : 0;
    if (enabled && !line)
        cache->bypass_data_miss = 0;
    if (enabled && mode < 2)
        cache->replacement[0] = (cache->replacement[0] + (!writing ? 2 : mode == 1)) & 3;
    if (line && writing)
        line->valid = 0;
    else if (line && mode >= 2) {
        unsigned way = (unsigned)(line - cache->lines[0][(address >> 4) & 63]);
        int pushed = push_line(cache, line, bus, way, &result);
        line->valid = 0;
        if (!pushed)
            goto done;
    }
    else if (line) {
        for (unsigned i = 0; i < 16; ++i)
            held[i] = line->bytes[i];
        result.completed = 16;
        goto publish;
    }
    for (unsigned i = 0; i < 4; ++i) {
        unsigned word = (((address >> 2) & 3) + i) & 3, inhibit = 0;
        uint32_t physical = (address & ~15u) + word * 4;
        Syn68kCache040Cycle cycle = {.attributes = attributes,
                                     .fc = fc,
                                     .burst = 16,
                                     .beat = i,
                                     .kind = CACHE040_OPERAND,
                                     .traffic = CACHE040_MOVE16};
        result.cycle = cycle;
        if (enabled && !mode && writing)
            cache->replacement[0] = (cache->replacement[0] + 1) & 3;
        int ok = writing ? write_bus(bus, physical, held + word * 4, 4, &cycle)
                         : read_bus(bus, physical, held + word * 4, 4, &cycle, &inhibit);
        if (!ok) {
            result.fault = CACHE040_OPERAND_FAULT;
            result.bus_address = physical;
            if (writing) {
                result.push_address = address & ~3u;
                result.push_size = 16;
                result.push_word = word;
                for (unsigned byte = 0; byte < 16; ++byte)
                    result.push_data[byte] = held[byte];
            }
            goto done;
        }
        result.completed += 4;
    }
publish:
    if (!writing)
        for (unsigned i = 0; i < 16; ++i)
            bytes[i] = held[i];
done:
    if (cache) {
        cache->traffic = CACHE040_NORMAL;
        cache->active = 0;
    }
    return result;
}

Syn68kCache040Snoop syn68k_cache040_snoop(Syn68kCache040* cache, uint32_t address, unsigned control,
                                          unsigned transfer_type, unsigned size, int writing, unsigned char bytes[16])
{
    Syn68kCache040Snoop result = {.fault = CACHE040_BAD_REQUEST};
    if (!bytes || control > 3 || transfer_type > 3 || (writing != 0 && writing != 1) ||
        (size != 1 && size != 2 && size != 4 && size != 16) || (size != 16 && size > 16 - (address & 15)))
        return result;
    result.fault = CACHE040_OK;
    if (!cache || !control || control == 3 || transfer_type >= 2)
        return result;
    if (cache->active && cache->snoop_window != 1)
        return (Syn68kCache040Snoop){.fault = CACHE040_BAD_REQUEST};
    unsigned char input[4];
    if (writing && size != 16)
        for (unsigned i = 0; i < size; ++i)
            input[i] = bytes[i];
    if (writing || control == 2) {
        Syn68kCache040Line* instruction = lookup(cache, 1, address);
        if (instruction) {
            result.hit = result.invalidated = 2;
            result.way[1] = (unsigned)(instruction - cache->lines[1][(address >> 4) & 63]);
            instruction->valid = 0;
        }
    }
    Syn68kCache040Line* line = lookup(cache, 0, address);
    unsigned selected = CACHE040_SNOOP_DATA;
    unsigned way = line ? (unsigned)(line - cache->lines[0][(address >> 4) & 63]) : 0;
    if (!line && cache->active && cache->snoop_window == 1 && cache->victim_live == 1 && cache->victim.valid &&
        cache->victim.address == (address & ~15u)) {
        line = &cache->victim;
        selected = CACHE040_SNOOP_VICTIM;
        way = cache->victim_way;
    }
    if (!line)
        return result;
    result.hit |= selected;
    result.way[0] = way;
    unsigned dirty = line->dirty & 15;
    if (!writing) {
        if (dirty) {
            unsigned char held[16];
            unsigned offset = size == 16 ? 0 : address & 15;
            for (unsigned i = 0; i < size; ++i)
                held[i] = line->bytes[offset + i];
            for (unsigned i = 0; i < size; ++i)
                bytes[i] = held[i];
            result.intervention = 1;
        }
        if (control == 2) {
            line->valid = 0;
            result.invalidated |= selected;
        }
    }
    else if (control == 1 && dirty && size != 16) {
        for (unsigned i = 0; i < size; ++i) {
            unsigned offset = (address & 15) + i;
            line->bytes[offset] = input[i];
            line->dirty |= 1u << (offset >> 2);
        }
        result.intervention = 1;
    }
    else {
        line->valid = 0;
        result.invalidated |= selected;
    }
    return result;
}

Syn68kCache040Access syn68k_cache040_maintain(Syn68kCache040* cache, const Syn68kCache040Bus* bus, unsigned caches,
                                              unsigned scope, uint32_t address, unsigned page_shift, int push)
{
    return syn68k_cache040_maintain_traffic(cache, bus, caches, scope, address, page_shift, push, 0, CACHE040_NORMAL);
}

static unsigned interrupt_level(const Syn68kCache040Interrupt* interrupt)
{
    if (!interrupt || !interrupt->pending || !interrupt->sr)
        return 0;
    if (interrupt->pending[7])
        return 7;
    unsigned mask = (*interrupt->sr >> 8) & 7;
    for (unsigned level = 6; level > mask; --level)
        if (interrupt->pending[level])
            return level;
    return 0;
}

static Syn68kCache040Access maintain_inner(Syn68kCache040* cache, const Syn68kCache040Bus* bus, unsigned caches,
                                           unsigned scope, uint32_t address, unsigned page_shift, int push,
                                           uint32_t cacr, enum Syn68kCache040Traffic traffic,
                                           const Syn68kCache040Interrupt* interrupt)
{
    Syn68kCache040Access result = {0};
    if (!cache || !bus || (!bus->write && !bus->write_cycle) || cache->active || caches > 3 || !scope || scope > 3 ||
        (page_shift != 12 && page_shift != 13) || (push != 0 && push != 1) ||
        (traffic != CACHE040_NORMAL && traffic != CACHE040_LOCKED) ||
        (traffic == CACHE040_LOCKED && (caches != 1 || scope != 1 || !push)) ||
        (interrupt && (!!interrupt->pending != !!interrupt->sr)))
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    cache->active = 1;
    cache->traffic = traffic;
    if (traffic == CACHE040_LOCKED && !(cacr & 0x80000000u) && lookup(cache, 0, address))
        cache->bypass_data_miss = 1;
    result.interrupt_level = interrupt_level(interrupt);
    if (result.interrupt_level)
        goto maintenance_done;
    uint32_t mask = scope == 1 ? ~15u : scope == 2 ? UINT32_MAX << page_shift : 0;
    for (unsigned bank = 0; bank < 2; ++bank)
        if (caches & (1u << bank))
            for (unsigned set = 0; set < 64; ++set)
                for (unsigned way = 0; way < 4; ++way) {
                    Syn68kCache040Line* line = &cache->lines[bank][set][way];
                    if (!line->valid || (line->address & mask) != (address & mask))
                        continue;
                    if (push && !bank && !push_line(cache, line, bus, way, &result)) {
                        /* Fault data belongs to the exception recipient. */
                        line->valid = 0;
                        cache->traffic = CACHE040_NORMAL;
                        cache->active = 0;
                        return result;
                    }
                    line->valid = 0;
                    result.interrupt_level = interrupt_level(interrupt);
                    if (result.interrupt_level)
                        goto maintenance_done;
                }
maintenance_done:
    cache->traffic = CACHE040_NORMAL;
    cache->active = 0;
    return result;
}

Syn68kCache040Access syn68k_cache040_maintain_traffic(Syn68kCache040* cache, const Syn68kCache040Bus* bus,
                                                      unsigned caches, unsigned scope, uint32_t address,
                                                      unsigned page_shift, int push, uint32_t cacr,
                                                      enum Syn68kCache040Traffic traffic)
{
    return maintain_inner(cache, bus, caches, scope, address, page_shift, push, cacr, traffic, 0);
}

Syn68kCache040Access syn68k_cache040_maintain_interruptible(Syn68kCache040* cache, const Syn68kCache040Bus* bus,
                                                            unsigned caches, unsigned scope, uint32_t address,
                                                            unsigned page_shift, int push,
                                                            const Syn68kCache040Interrupt* interrupt)
{
    return maintain_inner(cache, bus, caches, scope, address, page_shift, push, 0, CACHE040_NORMAL, interrupt);
}
