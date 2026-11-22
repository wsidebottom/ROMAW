/*+===================================================================
  File:      ppc_cache750_test.cpp

  Summary:   Check 750 L1/L2 bank replacement, bus ordering, storage attributes, controls and fault recovery.

  Classes:   fixture, transaction; uses ROMlib_ppc_cache750 and ROMlib_ppc_physical_bus.

  Functions: cache750_checks; imports l2cache_checks.

  Origin:    Original deterministic tests using MPC750UM tables 3-2, 3-3 and 8-2 and section 3.3.3.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/cache750.h"
#include "synppc/bus750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>

unsigned l2cache_checks(void);

namespace {

struct transaction {
    uint32_t address;
    unsigned size, access, wimg;
};

struct fixture {
    std::unique_ptr<ROMlib_ppc_cache750> cache{new ROMlib_ppc_cache750{}};
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x20000);
    std::vector<transaction> transactions;
    ROMlib_ppc_physical_bus bus = {this, transfer, history};
    ROMlib_ppc_cache750_result result = {};
    unsigned fail = 0;
    unsigned histories = 0;
    bool fail_history = false;
    uint32_t failure_cause = 0x20000;

    fixture()
    {
        for (unsigned i = 0; i < memory.size(); ++i)
            memory[i] = i * 37 + (i >> 8);
    }

    void clear()
    {
        ROMlib_ppc_cache750_reset(cache.get());
        transactions.clear();
        fail = 0;
        histories = 0;
        fail_history = false;
        failure_cause = 0x20000;
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        f.transactions.push_back({address, size, access, wimg});
        if (f.transactions.size() == f.fail || uint64_t(address) + size > f.memory.size()) {
            *cause = f.failure_cause;
            return 0;
        }
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(f.memory.data() + address, bytes, size);
        else
            memcpy(bytes, f.memory.data() + address, size);
        return 1;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask, uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        ++f.histories;
        if (f.fail_history || uint64_t(address) + 8 > f.memory.size()) {
            *cause = f.failure_cause;
            return 0;
        }
        for (unsigned i = 0; i < 4; ++i)
            f.memory[address + 4 + i] |= mask >> (24 - i * 8);
        return 1;
    }

    int transfer(uint32_t address, void* bytes, unsigned size, unsigned access = 0, unsigned wimg = 0,
                 unsigned control = ROMLIB_PPC_CACHE750_ENABLE)
    {
        return ROMlib_ppc_cache750_transfer(cache.get(), &bus, address, bytes, size, access, wimg, control, &result);
    }

    int block(uint32_t address, unsigned op, unsigned control = ROMLIB_PPC_CACHE750_ENABLE, unsigned wimg = 0)
    {
        return ROMlib_ppc_cache750_block(cache.get(), &bus, address, op, 0, wimg, control, &result);
    }

    void resident(unsigned set, unsigned way, uint32_t address, unsigned state = 1)
    {
        auto& line = cache->lines[set][way];
        line.address = address;
        line.state = state;
        memset(line.data, 0xAA, sizeof line.data);
    }
};

} // namespace

void cache750_checks()
{
    unsigned cases = l2cache_checks();
    fixture f;
    const unsigned masks[] = {0x0B, 0x0B, 0x13, 0x13, 0x25, 0x25, 0x45, 0x45};
    const unsigned updated[] = {0x0B, 0x03, 0x11, 0x01, 0x24, 0x04, 0x40, 0x00};
    const unsigned selected[] = {0x00, 0x08, 0x02, 0x12, 0x01, 0x21, 0x05, 0x45};
    for (unsigned bits = 0; bits < 128; ++bits)
        for (unsigned hit = 0; hit < 9; ++hit) {
            f.clear();
            for (unsigned way = 0; way < 8; ++way)
                f.resident(17, way, way * 4096 + 17 * 32);
            f.cache->plru[17] = bits;
            unsigned expected = bits;
            unsigned char byte = 0;
            if (hit < 8) {
                assert(f.transfer(hit * 4096 + 17 * 32, &byte, 1));
                expected = (bits & ~masks[hit]) | updated[hit];
                assert(byte == 0xAA && f.cache->plru[17] == expected && f.transactions.empty());
            }
            unsigned replaced = 8;
            for (unsigned way = 0; way < 8; ++way)
                if ((expected & masks[way]) == selected[way])
                    replaced = way;
            assert(replaced < 8);
            assert(f.transfer(8 * 4096 + 17 * 32, &byte, 1));
            for (unsigned way = 0; way < 8; ++way)
                assert(f.cache->lines[17][way].address == (way == replaced ? 8 : way) * 4096 + 17 * 32);
            assert(f.transactions.size() == 4);
            ++cases;
        }
    for (unsigned bitmap = 0; bitmap < 256; ++bitmap)
        for (bool assist : {false, true}) {
            f.clear();
            for (unsigned way = 0; way < 8; ++way)
                f.resident(17, way, way * 4096 + 17 * 32, (bitmap >> way) & 1);
            f.cache->plru[17] = 127;
            unsigned target = 7;
            if (!assist)
                for (unsigned way = 0; way < 8; ++way)
                    if (!(bitmap & (1u << way))) {
                        target = way;
                        break;
                    }
            unsigned char byte = 0;
            assert(f.transfer(8 * 4096 + 17 * 32, &byte, 1, 0, 0, assist ? 5 : 1));
            assert(f.cache->lines[17][target].address == 8 * 4096 + 17 * 32);
            ++cases;
        }
    for (unsigned set = 0; set < 128; ++set)
        for (unsigned critical : {0u, 8u, 16u, 24u}) {
            f.clear();
            unsigned char bytes[8] = {};
            assert(f.transfer(set * 32 + critical, bytes, 8, ROMLIB_PPC_MMU_INSTRUCTION, 3));
            assert(f.cache->lines[set][0].state == 1);
            for (unsigned beat = 0; beat < 4; ++beat) {
                auto t = f.transactions[beat];
                assert(t.address == set * 32 + ((critical + beat * 8) & 31) && t.size == 8 && t.access == 2 &&
                       t.wimg == 3);
            }
            ++cases;
        }
    for (unsigned wimg = 0; wimg < 16; ++wimg)
        for (unsigned control : {0u, 1u, 3u, 9u, 11u})
            for (bool write : {false, true})
                for (unsigned state = 0; state < 3; ++state) {
                    f.clear();
                    f.resident(17, 0, 0x1220, state);
                    f.cache->plru[17] = 0x55;
                    unsigned char bytes[8];
                    memset(bytes, 0x55, sizeof bytes);
                    unsigned char original[8];
                    memcpy(original, f.memory.data() + 0x1234, 8);
                    bool enabled = (control & 1) && !(wimg & 4);
                    bool hit = enabled && state;
                    bool through = (wimg & 8) || (control & 8);
                    bool allocate = enabled && !state && !(control & 2) && !(write && through);
                    assert(f.transfer(0x1234, bytes, 8, write ? 1 : 0, wimg, control));
                    assert(f.result.completed == 8 && f.result.misses == unsigned(enabled && !state) &&
                           !f.result.castouts);
                    assert(f.transactions.size() == (allocate ? 4u : (!hit || (write && through)) ? 1u : 0u));
                    bool cached = hit || allocate;
                    if (write) {
                        bool memory_write = !cached || through;
                        for (unsigned i = 0; i < 8; ++i)
                            assert(f.memory[0x1234 + i] == (memory_write ? 0x55 : original[i]));
                        if (cached) {
                            for (unsigned i = 0; i < 8; ++i)
                                assert(f.cache->lines[17][0].data[20 + i] == 0x55);
                            assert(f.cache->lines[17][0].state == (through ? state : 2));
                        }
                    }
                    else
                        for (unsigned i = 0; i < 8; ++i)
                            assert(bytes[i] == (hit ? 0xAA : original[i]));
                    if (!cached) {
                        assert(f.cache->lines[17][0].state == state && f.cache->plru[17] == 0x55);
                        for (unsigned i = 0; i < 32; ++i)
                            assert(f.cache->lines[17][0].data[i] == 0xAA);
                    }
                    ++cases;
                }
    for (unsigned beat = 1; beat <= 8; ++beat) {
        f.clear();
        for (unsigned way = 0; way < 8; ++way)
            f.resident(17, way, way * 4096 + 17 * 32, 2);
        f.fail = beat;
        unsigned char byte = 0x55;
        assert(!f.transfer(8 * 4096 + 17 * 32 + 24, &byte, 1));
        assert(byte == 0x55 && !f.result.completed && f.result.cause == 0x20000);
        assert(f.result.misses == 1 && f.result.castouts == unsigned(beat > 4));
        auto& line = f.cache->lines[17][0];
        assert(line.address == 17 * 32 && line.state == (beat <= 4 ? 2 : 1));
        for (unsigned i = 0; i < std::min(beat - 1, 4u); ++i) {
            assert(f.transactions[i].address == 17 * 32 + i * 8);
            assert(f.transactions[i].access == ROMLIB_PPC_MMU_WRITE && !f.transactions[i].wimg);
        }
        for (unsigned i = 0; i < 32; ++i)
            assert(line.data[i] == 0xAA);
        assert(f.result.physical == f.transactions.back().address);
        f.fail = 0;
        assert(f.transfer(8 * 4096 + 17 * 32 + 24, &byte, 1));
        assert(f.cache->lines[17][0].address == 8 * 4096 + 17 * 32);
        ++cases;
    }
    for (unsigned operation : {1u, 2u, 3u, 5u, 6u})
        for (unsigned control : {0u, 1u, 3u})
            for (unsigned state = 0; state < 3; ++state) {
                f.clear();
                f.resident(17, 0, 0x1220, state);
                bool ignored = operation >= 5 && !(control & 1);
                bool push = !ignored && operation != 3 && state == 2;
                assert(f.block(0x1234, operation, control));
                assert(f.transactions.size() == (push ? 4u : 0u));
                assert(!f.result.misses && !f.result.castouts);
                for (auto t : f.transactions)
                    assert(t.access == ROMLIB_PPC_MMU_WRITE && t.wimg == 8);
                unsigned next = ignored ? state : (operation == 1 || operation == 6) ? (state ? 1 : 0) : 0;
                assert(f.cache->lines[17][0].state == next);
                for (unsigned i = 0; i < 32; ++i)
                    if (push)
                        assert(f.memory[0x1220 + i] == 0xAA);
                ++cases;
            }
    for (unsigned control : {1u, 3u, 5u, 7u})
        for (bool hit : {false, true}) {
            f.clear();
            if (hit)
                f.resident(17, 0, 0x1220, 2);
            assert(f.block(0x1234, 4, control));
            assert(f.result.misses == unsigned(!hit) && !f.result.castouts);
            auto& line = f.cache->lines[17][0];
            assert(line.address == 0x1220 && line.state == 2 && f.transactions.empty());
            for (auto byte : line.data)
                assert(!byte);
            ++cases;
        }
    for (unsigned beat = 1; beat <= 4; ++beat) {
        f.clear();
        f.resident(17, 0, 0x1220, 2);
        f.fail = beat;
        assert(!f.block(0x1234, 2));
        assert(f.cache->lines[17][0].state == 2 && f.result.cause == 0x20000);
        f.fail = 0;
        assert(f.block(0x1234, 2) && !f.cache->lines[17][0].state);
        ++cases;
    }
    for (unsigned beat = 1; beat <= 4; ++beat) {
        f.clear();
        unsigned char bytes[8];
        memset(bytes, 0x55, sizeof bytes);
        f.resident(0, 0, 0, 1);
        f.fail = beat;
        assert(!f.transfer(31, bytes, 8));
        assert(f.result.completed == 1 && f.result.cause == 0x20000);
        assert(f.result.misses == 1 && !f.result.castouts);
        for (auto byte : bytes)
            assert(byte == 0x55);
        assert(!f.cache->lines[1][0].state);
        ++cases;
    }
    {
        f.clear();
        f.resident(17, 0, 0x1220, 2);
        f.fail = 1;
        unsigned char byte = 0x55;
        assert(!f.transfer(0x1234, &byte, 1, 1, 8));
        assert(f.cache->lines[17][0].state == 2 && f.cache->lines[17][0].data[20] == 0xAA);
        assert(f.result.cause == 0x20000 && !f.result.completed);
        ++cases;
    }
    {
        f.clear();
        for (unsigned set = 0; set < 128; ++set)
            for (unsigned way = 0; way < 8; ++way)
                f.resident(set, way, way * 4096 + set * 32);
        f.cache->plru[17] = 0x7F;
        ROMlib_ppc_cache750_invalidate_set(f.cache.get(), 0xFFFFF234);
        for (unsigned set = 0; set < 128; ++set)
            for (unsigned way = 0; way < 8; ++way)
                assert(f.cache->lines[set][way].state == (set != 17));
        assert(f.cache->plru[17] == 0x7F && f.transactions.empty());
        ROMlib_ppc_cache750_reset(f.cache.get());
        const auto* bytes = reinterpret_cast<unsigned char*>(f.cache.get());
        for (unsigned i = 0; i < sizeof *f.cache; ++i)
            assert(!bytes[i]);
        ++cases;
    }
    for (unsigned invalid = 0; invalid < 10; ++invalid) {
        f.clear();
        unsigned char bytes[16] = {};
        assert(!ROMlib_ppc_cache750_transfer(invalid == 0 ? nullptr : f.cache.get(), &f.bus,
                                             invalid == 1 ? 0xFFFFFFFC : 0x1234, invalid == 2 ? nullptr : bytes,
                                             invalid == 3   ? 0
                                             : invalid == 4 ? 9
                                                            : 8,
                                             invalid == 5   ? 3
                                             : invalid == 6 ? 32
                                                            : 0,
                                             invalid == 7 ? 16 : 0, invalid == 8 ? 16 : 1,
                                             invalid == 9 ? nullptr : &f.result));
        assert(f.transactions.empty() && !f.result.cause);
        assert(f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION);
        ++cases;
    }
    {
        f.clear();
        std::unique_ptr<ROMlib_ppc_cache750> instructions(new ROMlib_ppc_cache750{});
        unsigned char byte = 0;
        f.memory[0x1234] = 0x11;
        assert(ROMlib_ppc_cache750_transfer(instructions.get(), &f.bus, 0x1234, &byte, 1, 2, 2, 1, &f.result));
        assert(byte == 0x11);
        byte = 0x22;
        assert(f.transfer(0x1234, &byte, 1, 1));
        assert(f.memory[0x1234] == 0x11);
        assert(f.block(0x1234, ROMLIB_PPC_CACHE750_FLUSH));
        assert(f.memory[0x1234] == 0x22);
        assert(ROMlib_ppc_cache750_transfer(instructions.get(), &f.bus, 0x1234, &byte, 1, 2, 2, 1, &f.result));
        assert(byte == 0x11); // Data copyback does not snoop the instruction cache.
        ROMlib_ppc_cache750_invalidate_set(instructions.get(), 0x1234);
        assert(ROMlib_ppc_cache750_transfer(instructions.get(), &f.bus, 0x1234, &byte, 1, 2, 2, 1, &f.result));
        assert(byte == 0x22);
        ++cases;
    }
    for (unsigned invalid = 0; invalid < 7; ++invalid) {
        f.clear();
        assert(!ROMlib_ppc_cache750_block(f.cache.get(), &f.bus, 0x1234,
                                          invalid == 0   ? 0
                                          : invalid == 1 ? 7
                                                         : 4,
                                          invalid == 2 ? 2 : 0,
                                          invalid == 3   ? 16
                                          : invalid == 4 ? 4
                                          : invalid == 5 ? 8
                                                         : 0,
                                          invalid == 6 ? 0 : 1, &f.result));
        assert(f.transactions.empty() && !f.result.cause);
        assert(f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION);
        ++cases;
    }
    for (unsigned control : {0u, 1u, 3u})
        for (unsigned state = 0; state < 3; ++state)
            for (uint32_t mask : {0x100u, 0x80u, 0x180u}) {
                f.clear();
                f.memory[0x1234] = 0;
                f.memory[0x1235] = 3;
                f.memory[0x1236] = 0;
                f.memory[0x1237] = 2;
                f.resident(17, 0, 0x1220, state);
                auto& line = f.cache->lines[17][0];
                line.data[20] = 0;
                line.data[21] = 5;
                line.data[22] = 0;
                line.data[23] = 2;
                bool hit = (control & 1) && state;
                assert(ROMlib_ppc_cache750_history(f.cache.get(), &f.bus, 0x1230, mask, control, &f.result));
                assert(!f.result.fault && f.result.completed == 4 && f.result.physical == 0x1234);
                assert(f.histories == (hit ? 0u : 1u) && f.transactions.empty());
                if (hit) {
                    assert(line.state == 2 && line.data[21] == 5);
                    assert(line.data[22] == (mask >> 8) && line.data[23] == ((mask & 0xFF) | 2));
                    assert(f.memory[0x1235] == 3 && !f.memory[0x1236] && f.memory[0x1237] == 2);
                    assert(f.block(0x1230, 1));
                    assert(f.memory[0x1235] == 5 && f.memory[0x1236] == (mask >> 8));
                    assert(f.memory[0x1237] == ((mask & 0xFF) | 2));
                }
                else {
                    assert(line.state == state && line.data[21] == 5 && !line.data[22] && line.data[23] == 2);
                    assert(f.memory[0x1236] == (mask >> 8) && f.memory[0x1237] == ((mask & 0xFF) | 2));
                }
                ++cases;
            }
    for (uint32_t cause : {0u, 0x20000u}) {
        f.clear();
        f.failure_cause = cause;
        f.fail = 1;
        unsigned char byte = 0x55;
        assert(!f.transfer(0x1234, &byte, 1));
        assert(f.result.fault == ROMLIB_PPC_MMU_BUS && f.result.cause == cause && byte == 0x55);
        f.fail_history = true;
        assert(!ROMlib_ppc_cache750_history(f.cache.get(), &f.bus, 0x1230, 0x180, 3, &f.result));
        assert(f.result.fault == ROMLIB_PPC_MMU_BUS && f.result.cause == cause && !f.result.completed);
        ++cases;
    }
    {
        f.clear();
        assert(!ROMlib_ppc_cache750_history(f.cache.get(), &f.bus, 0x1231, 0x180, 1, &f.result));
        assert(f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION);
        assert(!ROMlib_ppc_cache750_history(f.cache.get(), &f.bus, 0x1230, 1, 1, &f.result));
        assert(f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION && !f.histories);
        ++cases;
    }
    for (unsigned beat = 0; beat <= 8; ++beat) {
        f.clear();
        unsigned char bytes[8] = {};
        f.fail = beat;
        assert(bool(f.transfer(31, bytes, 8)) == (beat == 0));
        assert(f.result.misses == (beat && beat <= 4 ? 1u : 2u) && !f.result.castouts);
        ++cases;
    }
    printf("%u PowerPC 750 L1/L2 replacement/attribute/control/bus-fault cases passed\n", cases);
}
