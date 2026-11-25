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

static int push_line(Syn68kCache040Line* line, const Syn68kCache040Bus* bus, unsigned way, Syn68kCache040Access* result)
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
        if (!bus->write(bus->opaque, address, line->bytes + word * 4, 4, 0, 0, size == 16 ? 16 : 0, i, way)) {
            result->fault = CACHE040_PUSH_FAULT;
            result->bus_address = address;
            result->push_address = line->address + first * 4;
            result->push_size = size;
            result->push_word = word;
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
        if (!bus->read(bus->opaque, physical, fresh.bytes + word * 4, 4, attributes, fc, 16, i, way, &beat_inhibit)) {
            /* An unrequested trailing fill fault aborts allocation but not an
               already satisfied operand. Dirty victims return from the buffer;
               clean victims stay invalid even when allocation fails. */
            if (old.valid && old.dirty)
                *line = old;
            if ((received & needed) != needed) {
                result->fault = CACHE040_FILL_FAULT;
                result->bus_address = physical;
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
    /* The requested fill precedes the victim push. The fresh line remains
       installed if the old push faults; the result owns the captured victim. */
    if (!push_line(&old, bus, way, result))
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
    unsigned mode = (attributes >> 5) & 3;
    unsigned ordinary = fc == 1 || fc == 2 || fc == 5 || fc == 6;
    while (result.completed < size) {
        uint32_t physical = address + result.completed;
        unsigned chunk = 16 - (physical & 15);
        if (chunk > size - result.completed)
            chunk = size - result.completed;
        Syn68kCache040Line* line = enabled && ordinary ? lookup(cache, bank, physical) : 0;
        unsigned way = line ? (unsigned)(line - cache->lines[bank][(physical >> 4) & 63]) : 0;
        if (enabled && ordinary && mode >= 2 && line && !bank) {
            if (!push_line(line, bus, way, &result)) {
                line->valid = 0;
                return result;
            }
            line->valid = 0;
            line = 0;
        }
        if (enabled && ordinary && mode < 2) {
            unsigned count = writing ? 1 : 1 + (((physical & 7) + chunk - 1) >> 3);
            if (!line && !no_allocate && (!writing || mode == 1)) {
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
                    return result;
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
                int ok = writing ? bus->write(bus->opaque, physical + done, bytes + result.completed, beat, attributes,
                                              fc, 0, 0, way)
                                 : bus->read(bus->opaque, physical + done, bytes + result.completed, beat, attributes,
                                             fc, 0, 0, way, &ignored);
                if (enabled && ordinary && mode == 0 && writing)
                    cache->replacement[bank] = (cache->replacement[bank] + 1) & 3;
                if (!ok) {
                    result.fault = CACHE040_OPERAND_FAULT;
                    result.bus_address = physical + done;
                    return result;
                }
                done += beat;
                result.completed += beat;
            }
            continue;
        }
        result.completed += chunk;
    }
    if (!writing)
        for (unsigned i = 0; i < size; ++i)
            result.value = (result.value << 8) | bytes[i];
    return result;
}

Syn68kCache040Access syn68k_cache040_access(Syn68kCache040* cache, const Syn68kCache040Bus* bus, uint32_t address,
                                            unsigned fc, unsigned size, int writing, uint32_t value,
                                            uint32_t attributes, uint32_t cacr, int no_allocate)
{
    if (!cache || !bus || !bus->read || !bus->write || cache->active || fc > 7 ||
        (size != 1 && size != 2 && size != 4) || (writing != 0 && writing != 1) ||
        (no_allocate != 0 && no_allocate != 1))
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    cache->active = 1;
    Syn68kCache040Access result =
        access_inner(cache, bus, address, fc, size, writing, value, attributes, cacr, no_allocate);
    cache->active = 0;
    return result;
}

Syn68kCache040Access syn68k_cache040_maintain(Syn68kCache040* cache, const Syn68kCache040Bus* bus, unsigned caches,
                                              unsigned scope, uint32_t address, unsigned page_shift, int push)
{
    Syn68kCache040Access result = {0};
    if (!cache || !bus || !bus->write || cache->active || caches > 3 || !scope || scope > 3 ||
        (page_shift != 12 && page_shift != 13) || (push != 0 && push != 1))
        return (Syn68kCache040Access){.fault = CACHE040_BAD_REQUEST};
    cache->active = 1;
    uint32_t mask = scope == 1 ? ~15u : scope == 2 ? UINT32_MAX << page_shift : 0;
    for (unsigned bank = 0; bank < 2; ++bank)
        if (caches & (1u << bank))
            for (unsigned set = 0; set < 64; ++set)
                for (unsigned way = 0; way < 4; ++way) {
                    Syn68kCache040Line* line = &cache->lines[bank][set][way];
                    if (!line->valid || (line->address & mask) != (address & mask))
                        continue;
                    if (push && !bank && !push_line(line, bus, way, &result)) {
                        /* Fault data belongs to the exception recipient. */
                        line->valid = 0;
                        cache->active = 0;
                        return result;
                    }
                    line->valid = 0;
                }
    cache->active = 0;
    return result;
}
