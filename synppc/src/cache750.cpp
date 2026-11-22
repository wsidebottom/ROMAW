/*+===================================================================
  File:      cache750.cpp

  Summary:   Implement 750 L1 bank fills, PLRU replacement, copyback, cache controls and MEI snoops.

  Classes:   cache_transaction, ROMlib_ppc_cache750_transaction; uses ROMlib_ppc_cache750,
             ROMlib_ppc_cache750_line, ROMlib_ppc_cache750_result, ROMlib_ppc_cache750_observer, ROMlib_ppc_cache750_layer
             and ROMlib_ppc_physical_bus.

  Functions: ROMlib_ppc_cache750_reset, ROMlib_ppc_cache750_transfer, ROMlib_ppc_cache750_block,
             ROMlib_ppc_cache750_invalidate_set, ROMlib_ppc_cache750_history, ROMlib_ppc_cache750_busy,
             ROMlib_ppc_cache750_transfer_observed, ROMlib_ppc_cache750_block_observed, ROMlib_ppc_cache750_transfer_layered,
             ROMlib_ppc_cache750_block_layered

  Origin:    Original functional model using MPC750UM 3.3 through 3.6 and table 8-2 burst ordering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/cache750.h"
#include "synppc/bus750.h"
#include <algorithm>
#include <cstring>
#include <cstddef>
#include "ppc-physical-bus.h"

static_assert(sizeof(ROMlib_ppc_cache750_line) == 40, "PPC cache line ABI must match ROMAW");
static_assert(offsetof(ROMlib_ppc_cache750, transactions) == 41088, "PPC cache bank ABI must match ROMAW");

struct ROMlib_ppc_cache750_transaction {
    uint32_t first, last;
    ROMlib_ppc_cache750_transaction* previous;
};

namespace {
// GCC 12+ does not connect the stack link to its restoring RAII destructor.
// The entry is reachable only during this scope, including nested bus callbacks.
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
class cache_transaction {
    ROMlib_ppc_cache750& bank;
    ROMlib_ppc_cache750_transaction entry;

  public:
    cache_transaction(ROMlib_ppc_cache750& cache, uint32_t address, unsigned size) : bank(cache)
    {
        entry = {address & ~31u, (address + size - 1) & ~31u, cache.transactions};
        bank.transactions = &entry;
    }

    ~cache_transaction()
    {
        bank.transactions = entry.previous;
    }

    cache_transaction(const cache_transaction&) = delete;
    cache_transaction& operator=(const cache_transaction&) = delete;
};

#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

unsigned set_index(uint32_t address)
{
    return (address >> 5) & 127;
}

void touch(ROMlib_ppc_cache750& cache, unsigned set, unsigned way)
{
    unsigned node = 0;
    for (unsigned level = 0; level < 3; ++level) {
        unsigned direction = (way >> (2 - level)) & 1;
        cache.plru[set] = (cache.plru[set] & ~(1u << node)) | ((direction ^ 1) << node);
        node = node * 2 + 1 + direction;
    }
}

unsigned victim(const ROMlib_ppc_cache750& cache, unsigned set, unsigned control)
{
    if (!(control & ROMLIB_PPC_CACHE750_FLUSH_ASSIST))
        for (unsigned way = 0; way < 8; ++way)
            if (!cache.lines[set][way].state)
                return way;
    unsigned node = 0, way = 0;
    for (unsigned level = 0; level < 3; ++level) {
        unsigned direction = (cache.plru[set] >> node) & 1;
        way = way * 2 + direction;
        node = node * 2 + 1 + direction;
    }
    return way;
}

unsigned lookup(const ROMlib_ppc_cache750& cache, unsigned set, uint32_t address)
{
    for (unsigned way = 0; way < 8; ++way)
        if (cache.lines[set][way].state && cache.lines[set][way].address == (address & ~31u))
            return way;
    return 8;
}

bool bus_transfer(const ROMlib_ppc_physical_bus* bus, uint32_t address, void* bytes, unsigned size, unsigned access,
                  unsigned wimg, ROMlib_ppc_cache750_result& result)
{
    result.physical = address;
    result.cause = 0x40000;
    bool ok = romaw_ppc_physical_transfer(bus, address, bytes, size, access, wimg, &result.cause);
    if (ok)
        result.cause = 0;
    else
        result.fault = romaw_ppc_physical_failure(bus, result.cause);
    return ok;
}

bool burst(const ROMlib_ppc_physical_bus* bus, uint32_t address, unsigned char* bytes, unsigned critical,
           unsigned access, unsigned wimg, ROMlib_ppc_cache750_result& result, const ROMlib_ppc_cache750_layer* layer)
{
    if (layer && layer->burst) {
        ROMlib_ppc_cache750_result lower = {};
        int ok = layer->burst(layer->opaque, address, bytes, critical, access, wimg, &lower);
        result.physical = lower.physical;
        result.cause = lower.cause;
        result.fault = lower.fault;
        return ok;
    }
    for (unsigned beat = 0; beat < 4; ++beat) {
        unsigned offset = (critical + beat * 8) & 31;
        if (!bus_transfer(bus, address + offset, bytes + offset, 8, access, wimg, result))
            return false;
    }
    return true;
}

bool push(ROMlib_ppc_cache750& bank, ROMlib_ppc_cache750_line& line, const ROMlib_ppc_physical_bus* bus,
          unsigned access, unsigned wimg, ROMlib_ppc_cache750_result& result, const ROMlib_ppc_cache750_layer* layer)
{
    if (line.state != 2)
        return true;
    cache_transaction pending(bank, line.address, 32);
    if (!burst(bus, line.address, line.data, 0, (access & ROMLIB_PPC_PHYSICAL_TABLE) | ROMLIB_PPC_MMU_WRITE, wimg,
               result, layer))
        return false;
    ++result.pushes;
    line.state = 1;
    return true;
}

void report(ROMlib_ppc_cache750_result& result, unsigned misses, unsigned castouts,
            const ROMlib_ppc_cache750_observer* observer)
{
    result.misses += misses;
    result.castouts += castouts;
    if (observer && observer->event)
        observer->event(observer->opaque, misses, castouts);
}

bool allocate(ROMlib_ppc_cache750& cache, const ROMlib_ppc_physical_bus* bus, uint32_t address, unsigned access,
              unsigned wimg, unsigned control, bool zero, unsigned& way, ROMlib_ppc_cache750_result& result,
              const ROMlib_ppc_cache750_observer* observer, const ROMlib_ppc_cache750_layer* layer)
{
    unsigned set = set_index(address);
    way = victim(cache, set, control);
    auto& line = cache.lines[set][way];
    cache_transaction pending(cache, address & ~31u, 32);
    bool dirty = line.state == 2;
    if (!push(cache, line, bus, access, 0, result, layer))
        return false;
    if (dirty)
        report(result, 0, 1, observer);
    ROMlib_ppc_cache750_line replacement = {};
    replacement.address = address & ~31u;
    replacement.state = zero ? 2 : 1;
    if (!zero && !burst(bus, replacement.address, replacement.data, address & 24, access & ~ROMLIB_PPC_MMU_WRITE, wimg,
                        result, layer))
        return false;
    line = replacement;
    touch(cache, set, way);
    return true;
}
}

extern "C" void ROMlib_ppc_cache750_reset(ROMlib_ppc_cache750* cache)
{
    if (cache)
        memset(cache, 0, sizeof *cache);
}

extern "C" int ROMlib_ppc_cache750_busy(const ROMlib_ppc_cache750* cache, uint32_t physical)
{
    if (cache)
        for (auto* pending = cache->transactions; pending; pending = pending->previous)
            if ((physical & ~31u) == pending->first || (physical & ~31u) == pending->last)
                return 1;
    return 0;
}

extern "C" void ROMlib_ppc_cache750_invalidate_set(ROMlib_ppc_cache750* cache, uint32_t effective)
{
    if (cache)
        for (auto& line : cache->lines[set_index(effective)])
            line.state = 0;
}

extern "C" int ROMlib_ppc_cache750_transfer(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                            uint32_t address, void* buffer, unsigned size, unsigned access,
                                            unsigned wimg, unsigned control, ROMlib_ppc_cache750_result* output)
{
    return ROMlib_ppc_cache750_transfer_observed(cache, bus, address, buffer, size, access, wimg, control, output,
                                                 nullptr);
}

extern "C" int ROMlib_ppc_cache750_block(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                         uint32_t address, unsigned operation, unsigned access, unsigned wimg,
                                         unsigned control, ROMlib_ppc_cache750_result* output)
{
    return ROMlib_ppc_cache750_block_observed(cache, bus, address, operation, access, wimg, control, output, nullptr);
}

extern "C" int ROMlib_ppc_cache750_transfer_observed(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                                     uint32_t address, void* buffer, unsigned size, unsigned access,
                                                     unsigned wimg, unsigned control,
                                                     ROMlib_ppc_cache750_result* output,
                                                     const ROMlib_ppc_cache750_observer* observer)
{
    return ROMlib_ppc_cache750_transfer_layered(cache, bus, address, buffer, size, access, wimg, control, output,
                                                observer, nullptr);
}

extern "C" int ROMlib_ppc_cache750_block_observed(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                                  uint32_t address, unsigned operation, unsigned access, unsigned wimg,
                                                  unsigned control, ROMlib_ppc_cache750_result* output,
                                                  const ROMlib_ppc_cache750_observer* observer)
{
    return ROMlib_ppc_cache750_block_layered(cache, bus, address, operation, access, wimg, control, output, observer,
                                             nullptr);
}

extern "C" int ROMlib_ppc_cache750_transfer_layered(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                                    uint32_t address, void* buffer, unsigned size, unsigned access,
                                                    unsigned wimg, unsigned control, ROMlib_ppc_cache750_result* output,
                                                    const ROMlib_ppc_cache750_observer* observer,
                                                    const ROMlib_ppc_cache750_layer* layer)
{
    if (!output)
        return 0;
    *output = {};
    output->physical = address;
    output->fault = ROMLIB_PPC_MMU_CONFIGURATION;
    if (!cache || !buffer || !size || size > 8 || uint64_t(address) + size > UINT64_C(0x100000000) ||
        (access & ~(ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE |
                    ROMLIB_PPC_PHYSICAL_ATOMIC)) ||
        ((access & ROMLIB_PPC_MMU_WRITE) && (access & ROMLIB_PPC_MMU_INSTRUCTION)) || wimg > 15 || (control & ~15u))
        return 0;
    auto& result = *output;
    result.fault = 0;
    bool write = access & ROMLIB_PPC_MMU_WRITE;
    unsigned char bytes[8] = {};
    if (write)
        memcpy(bytes, buffer, size);
    if (!(control & ROMLIB_PPC_CACHE750_ENABLE) || (wimg & 4)) {
        cache_transaction pending(*cache, address, size);
        if (!bus_transfer(bus, address, bytes, size, access, wimg, result))
            return 0;
        if (!write)
            memcpy(buffer, bytes, size);
        result.completed = size;
        return 1;
    }
    while (result.completed < size) {
        uint32_t current = address + result.completed;
        unsigned amount = std::min(size - result.completed, 32 - (current & 31));
        unsigned set = set_index(current), way = 8;
        bool enabled = (control & ROMLIB_PPC_CACHE750_ENABLE) && !(wimg & 4);
        bool through = (wimg & 8) || (control & ROMLIB_PPC_CACHE750_FORCE_THROUGH);
        if (enabled)
            way = lookup(*cache, set, current);
        if (enabled && way == 8)
            report(result, 1, 0, observer);
        if (enabled && way == 8 && !(control & ROMLIB_PPC_CACHE750_LOCK) && !(write && through)) {
            if (!allocate(*cache, bus, current, access, wimg, control, false, way, result, observer, layer))
                return 0;
        }
        if (way == 8 || (write && through)) {
            cache_transaction pending(*cache, current, amount);
            if (!bus_transfer(bus, current, bytes + result.completed, amount, access, wimg, result))
                return 0;
        }
        if (way != 8) {
            auto& line = cache->lines[set][way];
            if (write) {
                memcpy(line.data + (current & 31), bytes + result.completed, amount);
                if (!through)
                    line.state = 2;
            }
            else
                memcpy(bytes + result.completed, line.data + (current & 31), amount);
            touch(*cache, set, way);
        }
        result.completed += amount;
    }
    if (!write)
        memcpy(buffer, bytes, size);
    return 1;
}

extern "C" int ROMlib_ppc_cache750_block_layered(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus,
                                                 uint32_t address, unsigned operation, unsigned access, unsigned wimg,
                                                 unsigned control, ROMlib_ppc_cache750_result* output,
                                                 const ROMlib_ppc_cache750_observer* observer,
                                                 const ROMlib_ppc_cache750_layer* layer)
{
    if (!output)
        return 0;
    *output = {};
    output->physical = address;
    output->fault = ROMLIB_PPC_MMU_CONFIGURATION;
    if (!cache || operation < 1 || operation > 6 || wimg > 15 || (control & ~15u) ||
        (access & ~(ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_PHYSICAL_TABLE)))
        return 0;
    auto& result = *output;
    bool zero = operation == ROMLIB_PPC_CACHE750_ZERO;
    bool snoop = operation >= ROMLIB_PPC_CACHE750_SNOOP;
    if (zero && (!(control & ROMLIB_PPC_CACHE750_ENABLE) || (wimg & 12)))
        return 0;
    result.fault = 0;
    if (snoop && !(control & ROMLIB_PPC_CACHE750_ENABLE))
        return 1;
    if (ROMlib_ppc_cache750_busy(cache, address)) {
        result.fault = ROMLIB_PPC_MMU_RETRY;
        return 0;
    }
    unsigned set = set_index(address), way = lookup(*cache, set, address);
    if (zero) {
        if (way == 8) {
            report(result, 1, 0, observer);
            if (!allocate(*cache, bus, address, access, wimg, control, true, way, result, observer, layer))
                return 0;
        }
        else {
            auto& line = cache->lines[set][way];
            memset(line.data, 0, sizeof line.data);
            line.state = 2;
            touch(*cache, set, way);
        }
    }
    else if (way != 8) {
        auto& line = cache->lines[set][way];
        if (operation != ROMLIB_PPC_CACHE750_INVALIDATE && !push(*cache, line, bus, access, 8, result, layer))
            return 0;
        if (operation != ROMLIB_PPC_CACHE750_STORE && operation != ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ)
            line.state = 0;
    }
    return 1;
}

extern "C" int ROMlib_ppc_cache750_history(ROMlib_ppc_cache750* cache, const ROMlib_ppc_physical_bus* bus, uint32_t pte,
                                           uint32_t mask, unsigned control, ROMlib_ppc_cache750_result* output)
{
    if (!output)
        return 0;
    *output = {};
    output->physical = pte + 4;
    output->fault = ROMLIB_PPC_MMU_CONFIGURATION;
    if (!cache || (pte & 7) || (mask & ~0x180u) || (control & ~15u))
        return 0;
    auto& result = *output;
    unsigned set = set_index(pte), way = lookup(*cache, set, pte);
    if ((control & ROMLIB_PPC_CACHE750_ENABLE) && way != 8) {
        auto& line = cache->lines[set][way];
        auto* p = line.data + (pte & 31) + 4;
        uint32_t old = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        uint32_t value = old | mask;
        if (value != old) {
            for (unsigned i = 0; i < 4; ++i)
                p[i] = value >> (24 - 8 * i);
            line.state = 2;
        }
        touch(*cache, set, way);
        result.fault = 0;
    }
    else {
        cache_transaction pending(*cache, pte + 4, 4);
        result.cause = 0x40000;
        result.fault = ROMLIB_PPC_MMU_BUS;
        if (!romaw_ppc_physical_history(bus, pte, mask, &result.cause)) {
            result.fault = romaw_ppc_physical_failure(bus, result.cause);
            return 0;
        }
        result.fault = result.cause = 0;
    }
    result.completed = 4;
    return 1;
}
