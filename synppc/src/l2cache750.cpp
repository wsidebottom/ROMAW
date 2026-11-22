/*+===================================================================
  File:      l2cache750.cpp

  Summary:   Implement 750 two-way sector tags, L2 SRAM transfers, copybacks and coherency controls.

  Classes:   geometry, transaction, ROMlib_ppc_l2cache750_transaction; uses ROMlib_ppc_l2cache750,
             ROMlib_ppc_l2cache750_tag, ROMlib_ppc_l2cache750_result, ROMlib_ppc_l2cache750_observer and ROMlib_ppc_physical_bus.

  Functions: ROMlib_ppc_l2cache750_reset, ROMlib_ppc_l2cache750_invalidate, ROMlib_ppc_l2cache750_busy,
             ROMlib_ppc_l2cache750_transfer, ROMlib_ppc_l2cache750_block, ROMlib_ppc_l2cache750_transfer_observed,
             ROMlib_ppc_l2cache750_block_observed, ROMlib_ppc_l2cache750_history

  Origin:    Original functional implementation from MPC750UM 9.1.1 and 9.1.2.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/l2cache750.h"
#include "synppc/bus750.h"
#include <algorithm>
#include <cstring>
#include "ppc-physical-bus.h"

struct ROMlib_ppc_l2cache750_transaction {
    uint32_t first, last;
    ROMlib_ppc_l2cache750_transaction* previous;
};

namespace {
struct geometry {
    unsigned sectors, sets, shift;

    explicit geometry(unsigned capacity)
        : sectors(capacity == 1048576 ? 4 : 2), sets(capacity == 262144 ? 2048 : 4096),
          shift(capacity == 1048576 ? 7 : 6)
    {
    }

    unsigned set(uint32_t address) const
    {
        return (address >> shift) & (sets - 1);
    }

    unsigned sector(uint32_t address) const
    {
        return (address >> 5) & (sectors - 1);
    }

    uint32_t tag(uint32_t address) const
    {
        return address & ~((1u << shift) - 1);
    }

    unsigned offset(unsigned set, unsigned way, unsigned sector) const
    {
        return ((set * 2 + way) * sectors + sector) * 32;
    }
};

// Stack links exist only while their restoring scope is active.
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
class transaction {
    ROMlib_ppc_l2cache750& bank;
    ROMlib_ppc_l2cache750_transaction entry;

  public:
    transaction(ROMlib_ppc_l2cache750& cache, uint32_t address, unsigned size) : bank(cache)
    {
        entry = {address & ~31u, (address + size - 1) & ~31u, cache.transactions};
        bank.transactions = &entry;
    }

    ~transaction()
    {
        bank.transactions = entry.previous;
    }

    transaction(const transaction&) = delete;
    transaction& operator=(const transaction&) = delete;
};
#if defined(__GNUC__) && __GNUC__ >= 12 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

bool capacity_valid(unsigned capacity, unsigned control)
{
    return capacity == 262144 || capacity == 524288 || capacity == 1048576 ||
           (!capacity && !(control & ROMLIB_PPC_L2CACHE750_ENABLE));
}

bool valid(const ROMlib_ppc_l2cache750_tag& tag, unsigned sectors)
{
    for (unsigned i = 0; i < sectors; ++i)
        if (tag.state[i])
            return true;
    return false;
}

unsigned lookup(const ROMlib_ppc_l2cache750& bank, const geometry& g, uint32_t address)
{
    unsigned set = g.set(address);
    for (unsigned way = 0; way < 2; ++way)
        if (bank.tags[set][way].address == g.tag(address) && valid(bank.tags[set][way], g.sectors))
            return way;
    return 2;
}

bool physical(const ROMlib_ppc_physical_bus* bus, uint32_t address, void* bytes, unsigned size, unsigned access,
              unsigned wimg, ROMlib_ppc_l2cache750_result& result)
{
    result.physical = address;
    result.cause = 0x40000;
    // L2-private qualifiers never reach the external system bus.
    access &=
        ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE | ROMLIB_PPC_PHYSICAL_ATOMIC;
    if (!romaw_ppc_physical_transfer(bus, address, bytes, size, access, wimg, &result.cause)) {
        result.fault = romaw_ppc_physical_failure(bus, result.cause);
        return false;
    }
    result.cause = 0;
    return true;
}

bool burst(const ROMlib_ppc_physical_bus* bus, uint32_t address, unsigned char* bytes, unsigned critical,
           unsigned access, unsigned wimg, ROMlib_ppc_l2cache750_result& result)
{
    for (unsigned beat = 0; beat < 4; ++beat) {
        unsigned offset = (critical + beat * 8) & 31;
        if (!physical(bus, address + offset, bytes + offset, 8, access, wimg, result))
            return false;
    }
    return true;
}

void report(ROMlib_ppc_l2cache750_result& result, unsigned event, const ROMlib_ppc_l2cache750_observer* observer)
{
    if (event == ROMLIB_PPC_L2CACHE750_HIT)
        ++result.hits;
    else if (event == ROMLIB_PPC_L2CACHE750_MISS)
        ++result.misses;
    else if (event == ROMLIB_PPC_L2CACHE750_CASTOUT)
        ++result.castouts;
    else if (event == ROMLIB_PPC_L2CACHE750_PUSH)
        ++result.pushes;
    if (observer && observer->event)
        observer->event(observer->opaque, event, 1);
}

unsigned odd_parity(unsigned byte)
{
    byte ^= byte >> 4;
    byte ^= byte >> 2;
    byte ^= byte >> 1;
    return (~byte) & 1;
}

void store_sram(ROMlib_ppc_l2cache750& bank, unsigned offset, const unsigned char* bytes, unsigned size,
                unsigned control)
{
    memcpy(bank.data + offset, bytes, size);
    for (unsigned i = 0; i < size; ++i) {
        unsigned index = offset + i, bit = 1u << (index & 7);
        auto& parity = bank.parity[index >> 3];
        if ((control & ROMLIB_PPC_L2CACHE750_PARITY) && odd_parity(bytes[i]))
            parity |= bit;
        else
            parity &= ~bit;
    }
}

bool check_sram(const ROMlib_ppc_l2cache750& bank, unsigned offset, unsigned size, unsigned control, uint32_t address,
                ROMlib_ppc_l2cache750_result& result)
{
    if (control & ROMLIB_PPC_L2CACHE750_PARITY) {
        for (unsigned i = 0; i < size; ++i) {
            unsigned index = offset + i;
            if (((bank.parity[index >> 3] >> (index & 7)) & 1u) != odd_parity(bank.data[index])) {
                result.fault = ROMLIB_PPC_MMU_BUS;
                result.cause = 0x00100000; // SRR1[11], L2 data parity.
                result.physical = address + i;
                return false;
            }
        }
    }
    return true;
}

bool push(ROMlib_ppc_l2cache750& bank, const geometry& g, const ROMlib_ppc_physical_bus* bus, unsigned set,
          unsigned way, unsigned sector, bool castout, unsigned control, ROMlib_ppc_l2cache750_result& result,
          const ROMlib_ppc_l2cache750_observer* observer)
{
    auto& tag = bank.tags[set][way];
    if (tag.state[sector] != 2)
        return true;
    uint32_t address = tag.address + sector * 32;
    transaction pending(bank, address, 32);
    if (!check_sram(bank, g.offset(set, way, sector), 32, control, address, result))
        return false;
    if (!burst(bus, address, bank.data + g.offset(set, way, sector), 0, ROMLIB_PPC_MMU_WRITE, castout ? 0 : 8, result))
        return false;
    tag.state[sector] = 1;
    report(result, ROMLIB_PPC_L2CACHE750_PUSH, observer);
    if (castout)
        report(result, ROMLIB_PPC_L2CACHE750_CASTOUT, observer);
    return true;
}

bool select(ROMlib_ppc_l2cache750& bank, const geometry& g, const ROMlib_ppc_physical_bus* bus, uint32_t address,
            unsigned& way, unsigned control, ROMlib_ppc_l2cache750_result& result,
            const ROMlib_ppc_l2cache750_observer* observer)
{
    unsigned set = g.set(address);
    way = lookup(bank, g, address);
    if (way != 2)
        return true; // Same tag, another sector: no replacement.
    way = !valid(bank.tags[set][0], g.sectors) ? 0 : !valid(bank.tags[set][1], g.sectors) ? 1 : bank.lru[set] & 1;
    const auto& old = bank.tags[set][way];
    transaction victim(bank, old.address, 1u << g.shift);
    for (unsigned sector = 0; sector < g.sectors; ++sector)
        if (!push(bank, g, bus, set, way, sector, true, control, result, observer))
            return false;
    return true;
}
}

extern "C" void ROMlib_ppc_l2cache750_reset(ROMlib_ppc_l2cache750* bank)
{
    if (bank)
        memset(bank, 0, sizeof *bank);
}

extern "C" int ROMlib_ppc_l2cache750_invalidate(ROMlib_ppc_l2cache750* bank)
{
    if (!bank || bank->transactions)
        return 0;
    memset(bank->tags, 0, sizeof bank->tags);
    memset(bank->lru, 0, sizeof bank->lru);
    return 1;
}

extern "C" int ROMlib_ppc_l2cache750_busy(const ROMlib_ppc_l2cache750* bank, uint32_t physical)
{
    if (bank)
        for (auto* p = bank->transactions; p; p = p->previous)
            if ((physical & ~31u) >= p->first && (physical & ~31u) <= p->last)
                return 1;
    return 0;
}

extern "C" int ROMlib_ppc_l2cache750_transfer(ROMlib_ppc_l2cache750* bank, const ROMlib_ppc_physical_bus* bus,
                                              unsigned capacity, uint32_t address, void* buffer, unsigned size,
                                              unsigned critical, unsigned access, unsigned wimg, unsigned control,
                                              ROMlib_ppc_l2cache750_result* output)
{
    return ROMlib_ppc_l2cache750_transfer_observed(bank, bus, capacity, address, buffer, size, critical, access, wimg,
                                                   control, output, nullptr);
}

extern "C" int ROMlib_ppc_l2cache750_block(ROMlib_ppc_l2cache750* bank, const ROMlib_ppc_physical_bus* bus,
                                           unsigned capacity, uint32_t address, unsigned operation, unsigned control,
                                           ROMlib_ppc_l2cache750_result* output)
{
    return ROMlib_ppc_l2cache750_block_observed(bank, bus, capacity, address, operation, control, output, nullptr);
}

extern "C" int ROMlib_ppc_l2cache750_transfer_observed(ROMlib_ppc_l2cache750* bank, const ROMlib_ppc_physical_bus* bus,
                                                       unsigned capacity, uint32_t address, void* buffer, unsigned size,
                                                       unsigned critical, unsigned access, unsigned wimg,
                                                       unsigned control, ROMlib_ppc_l2cache750_result* output,
                                                       const ROMlib_ppc_l2cache750_observer* observer)
{
    if (!output)
        return 0;
    *output = {};
    auto& result = *output;
    result.physical = address;
    result.fault = ROMLIB_PPC_MMU_CONFIGURATION;
    bool writing = access & ROMLIB_PPC_MMU_WRITE, instruction = access & ROMLIB_PPC_MMU_INSTRUCTION;
    bool full = size == 32, snoop_push = access & ROMLIB_PPC_L2CACHE750_SNOOP_PUSH;
    bool explicit_push = access & (ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH | ROMLIB_PPC_L2CACHE750_SNOOP_PUSH);
    unsigned allowed = ROMLIB_PPC_MMU_WRITE | ROMLIB_PPC_MMU_INSTRUCTION | ROMLIB_PPC_PHYSICAL_TABLE |
                       ROMLIB_PPC_PHYSICAL_ATOMIC | ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH | ROMLIB_PPC_L2CACHE750_L1_HIT |
                       ROMLIB_PPC_L2CACHE750_SNOOP_PUSH | ROMLIB_PPC_L2CACHE750_RETAIN;
    if (!bank || !buffer || !capacity_valid(capacity, control) || (!full && (!size || size > 8)) ||
        uint64_t(address) + size > UINT64_C(0x100000000) || (full && (address & 31)) || (critical & ~24u) ||
        (!full && critical) || (writing && critical) || wimg > 15 || (control & ~31u) || (access & ~allowed) ||
        (writing && instruction) || (explicit_push && (!full || !writing)) ||
        ((access & ROMLIB_PPC_L2CACHE750_RETAIN) && !snoop_push) ||
        (snoop_push && (access & ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH)) ||
        ((access & ROMLIB_PPC_L2CACHE750_L1_HIT) && (!(access & ROMLIB_PPC_PHYSICAL_ATOMIC) || !writing || full)) ||
        ((access & ROMLIB_PPC_PHYSICAL_ATOMIC) &&
         (instruction ||
          (access &
           (ROMLIB_PPC_PHYSICAL_TABLE | ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH | ROMLIB_PPC_L2CACHE750_SNOOP_PUSH)) ||
          (writing ? size != 4 || (address & 3) : !full && (size != 4 || (address & 3))))))
        return 0;
    result.fault = 0;
    unsigned char bytes[32] = {};
    if (writing)
        memcpy(bytes, buffer, size);
    transaction pending(*bank, address, size);
    bool bypass = !(control & ROMLIB_PPC_L2CACHE750_ENABLE) || (wimg & 4) ||
                  ((control & ROMLIB_PPC_L2CACHE750_DATA_ONLY) && instruction);
    if (bypass) {
        bool ok = full ? burst(bus, address, bytes, critical, access, wimg, result)
                       : physical(bus, address, bytes, size, access, wimg, result);
        if (!ok)
            return 0;
        result.completed = size;
    }
    else {
        geometry g(capacity);
        bool test = control & ROMLIB_PPC_L2CACHE750_TEST;
        bool through = (wimg & 8) || (control & ROMLIB_PPC_L2CACHE750_WRITE_THROUGH);
        while (result.completed < size) {
            uint32_t current = address + result.completed;
            unsigned amount = full ? 32 : std::min(size - result.completed, 32 - (current & 31));
            unsigned set = g.set(current), sector = g.sector(current), way = lookup(*bank, g, current);
            bool hit = way != 2 && bank->tags[set][way].state[sector];
            report(result, hit ? ROMLIB_PPC_L2CACHE750_HIT : ROMLIB_PPC_L2CACHE750_MISS, observer);
            if (hit && !writing &&
                !check_sram(*bank, g.offset(set, way, sector) + (current & 31), amount, control, current, result))
                return 0;
            if (explicit_push && (!test || snoop_push)) {
                if (!burst(bus, current, bytes, 0, access, wimg, result))
                    return 0;
                if (hit) {
                    if (access & ROMLIB_PPC_L2CACHE750_RETAIN) {
                        store_sram(*bank, g.offset(set, way, sector), bytes, 32, control);
                        bank->tags[set][way].state[sector] = 1;
                    }
                    else
                        bank->tags[set][way].state[sector] = 0;
                }
            }
            else if (full) {
                if (!hit && !select(*bank, g, bus, current, way, control, result, observer))
                    return 0;
                auto& tag = bank->tags[set][way];
                unsigned char replacement[32];
                if (writing) {
                    memcpy(replacement, bytes, 32);
                    if (through && !(explicit_push && test) &&
                        !burst(bus, current, replacement, 0, access, wimg, result))
                        return 0;
                }
                else if (!hit && !burst(bus, current, replacement, critical, access, wimg, result))
                    return 0;
                if (!hit) {
                    if (tag.address != g.tag(current)) {
                        memset(&tag, 0, sizeof tag);
                        tag.address = g.tag(current);
                    }
                    if (!writing)
                        store_sram(*bank, g.offset(set, way, sector), replacement, 32, control);
                    tag.state[sector] = 1;
                }
                if (writing) {
                    store_sram(*bank, g.offset(set, way, sector), replacement, 32, control);
                    tag.state[sector] = (through && !(explicit_push && test)) ? 1 : 2;
                }
                else
                    memcpy(bytes, bank->data + g.offset(set, way, sector), 32);
                bank->lru[set] = way ^ 1;
            }
            else if (writing) {
                bool conditional = access & ROMLIB_PPC_PHYSICAL_ATOMIC;
                bool local = hit && (conditional ? bank->tags[set][way].state[sector] == 2 ||
                                                       (!through && (access & ROMLIB_PPC_L2CACHE750_L1_HIT))
                                                 : !through);
                // A clean conditional hit in WT mode must broadcast and kill;
                // an ordinary WT hit retains its previous modified status.
                bool kill = hit && conditional && !local;
                if (!local && !(test && !hit) &&
                    !physical(bus, current, bytes + result.completed, amount, access, wimg, result))
                    return 0;
                if (hit) {
                    store_sram(*bank, g.offset(set, way, sector) + (current & 31), bytes + result.completed, amount,
                               control);
                    if (kill)
                        bank->tags[set][way].state[sector] = 0;
                    else if (local)
                        bank->tags[set][way].state[sector] = 2;
                    bank->lru[set] = way ^ 1;
                }
            }
            else if (hit) {
                memcpy(bytes + result.completed, bank->data + g.offset(set, way, sector) + (current & 31), amount);
                bank->lru[set] = way ^ 1;
            }
            else if (!physical(bus, current, bytes + result.completed, amount, access, wimg, result))
                return 0;
            result.completed += amount;
        }
    }
    if (!writing)
        memcpy(buffer, bytes, size);
    return 1;
}

extern "C" int ROMlib_ppc_l2cache750_block_observed(ROMlib_ppc_l2cache750* bank, const ROMlib_ppc_physical_bus* bus,
                                                    unsigned capacity, uint32_t address, unsigned operation,
                                                    unsigned control, ROMlib_ppc_l2cache750_result* output,
                                                    const ROMlib_ppc_l2cache750_observer* observer)
{
    if (!output)
        return 0;
    *output = {};
    auto& result = *output;
    result.physical = address;
    result.fault = ROMLIB_PPC_MMU_CONFIGURATION;
    if (!bank || !capacity_valid(capacity, control) || (control & ~31u) || operation < 1 || operation > 6)
        return 0;
    result.fault = 0;
    if (!(control & ROMLIB_PPC_L2CACHE750_ENABLE))
        return 1;
    if (ROMlib_ppc_l2cache750_busy(bank, address)) {
        result.fault = ROMLIB_PPC_MMU_RETRY;
        return 0;
    }
    transaction pending(*bank, address, 1);
    geometry g(capacity);
    unsigned set = g.set(address), sector = g.sector(address), way = lookup(*bank, g, address);
    bool hit = way != 2 && bank->tags[set][way].state[sector];
    report(result, hit ? ROMLIB_PPC_L2CACHE750_HIT : ROMLIB_PPC_L2CACHE750_MISS, observer);
    if (hit && operation != ROMLIB_PPC_CACHE750_ZERO) {
        if (operation != ROMLIB_PPC_CACHE750_INVALIDATE &&
            !push(*bank, g, bus, set, way, sector, false, control, result, observer))
            return 0;
        if (operation != ROMLIB_PPC_CACHE750_SNOOP_UNCACHED_READ)
            bank->tags[set][way].state[sector] = 0;
    }
    return 1;
}

extern "C" int ROMlib_ppc_l2cache750_history(ROMlib_ppc_l2cache750* bank, const ROMlib_ppc_physical_bus* bus,
                                             unsigned capacity, uint32_t pte, uint32_t mask, unsigned control,
                                             ROMlib_ppc_l2cache750_result* output,
                                             const ROMlib_ppc_l2cache750_observer* observer)
{
    if (!output)
        return 0;
    *output = {};
    auto& result = *output;
    result.physical = pte + 4;
    result.fault = ROMLIB_PPC_MMU_CONFIGURATION;
    if (!bank || !capacity_valid(capacity, control) || (pte & 7) || (mask & ~0x180u) || (control & ~31u))
        return 0;
    result.fault = 0;
    geometry g(capacity);
    unsigned set = g.set(pte), sector = g.sector(pte), way = lookup(*bank, g, pte);
    bool hit = (control & ROMLIB_PPC_L2CACHE750_ENABLE) && way != 2 && bank->tags[set][way].state[sector];
    transaction pending(*bank, pte, 8);
    if (control & ROMLIB_PPC_L2CACHE750_ENABLE)
        report(result, hit ? ROMLIB_PPC_L2CACHE750_HIT : ROMLIB_PPC_L2CACHE750_MISS, observer);
    if (hit && !check_sram(*bank, g.offset(set, way, sector) + (pte & 31) + 4, 4, control, pte + 4, result))
        return 0;
    if (!hit || (control & ROMLIB_PPC_L2CACHE750_WRITE_THROUGH)) {
        result.cause = 0x40000;
        if (!romaw_ppc_physical_history(bus, pte, mask, &result.cause)) {
            result.fault = romaw_ppc_physical_failure(bus, result.cause);
            return 0;
        }
        result.cause = 0;
    }
    if (hit) {
        auto* bytes = bank->data + g.offset(set, way, sector) + (pte & 31) + 4;
        uint32_t old = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
        uint32_t value = old | mask;
        if (value != old) {
            unsigned char updated[4];
            for (unsigned i = 0; i < 4; ++i)
                updated[i] = value >> (24 - i * 8);
            store_sram(*bank, unsigned(bytes - bank->data), updated, 4, control);
        }
        if (value != old && !(control & ROMLIB_PPC_L2CACHE750_WRITE_THROUGH))
            bank->tags[set][way].state[sector] = 2;
        bank->lru[set] = way ^ 1;
    }
    result.completed = 4;
    return 1;
}
