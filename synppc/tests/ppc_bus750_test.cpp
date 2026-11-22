/*+===================================================================
  File:      ppc_bus750_test.cpp

  Summary:   Verify scalar/external transfers, endian lanes, wrapping and partial bus faults.

  Classes:   fixture, ram_fixture; ROMlib_ppc_physical_bus and ROMlib_ppc_bus_result.

  Functions: bus750_checks

  Origin:    Original scalar/page/physical-bus tests from MPC750UM and MPCFPE endian definitions.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "synppc/bus750.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <initializer_list>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {

struct fixture {
    struct event {
        uint32_t address;
        unsigned size, access, wimg;
    };

    std::map<uint32_t, unsigned char> memory;
    std::vector<event> events;
    unsigned data_count = 0, fail_data = 0;
    bool fail_table = false, fail_history = false;
    uint32_t cause = 0x40000;

    unsigned char byte(uint32_t address, bool table = false) const
    {
        auto found = memory.find(address);
        return found != memory.end() ? found->second : table ? 0 : (address * 37 + 0x53) & 255;
    }

    void word(uint32_t address, uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            memory[address + i] = value >> (24 - i * 8);
    }

    static int transfer(void* opaque, uint32_t address, void* buffer, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& self = *static_cast<fixture*>(opaque);
        assert(size && uint64_t(address) + size <= UINT64_C(0x100000000));
        self.events.push_back({address, size, access, wimg});
        bool table = access & ROMLIB_PPC_PHYSICAL_TABLE;
        if ((table && self.fail_table) || (!table && ++self.data_count == self.fail_data)) {
            *cause = self.cause;
            return 0;
        }
        auto* bytes = static_cast<unsigned char*>(buffer);
        for (unsigned i = 0; i < size; ++i) {
            if (access & ROMLIB_PPC_MMU_WRITE)
                self.memory[address + i] = bytes[i];
            else
                bytes[i] = self.byte(address + i, table);
        }
        if (table)
            assert(size == 8 && wimg == 2 && !(access & ROMLIB_PPC_MMU_INSTRUCTION));
        return 1;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask, uint32_t* cause)
    {
        auto& self = *static_cast<fixture*>(opaque);
        if (self.fail_history) {
            *cause = self.cause;
            return 0;
        }
        uint32_t low = 0;
        for (unsigned i = 0; i < 4; ++i)
            low = low << 8 | self.byte(address + 4 + i, true);
        self.word(address + 4, low | mask);
        return 1;
    }

    ROMlib_ppc_physical_bus bus()
    {
        return {this, transfer, history};
    }
};

static unsigned cases;

struct ram_fixture {
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x20000);
    unsigned transfers = 0, histories = 0, controls = 0, external = 0, aborted = 0;
    bool throwing = false, checked_abort = false, copy_before_throw = false;
    bool throwing_history = false, checked_history_abort = false;
    ROMlib_ppc_system750* clock = nullptr;

    ram_fixture()
    {
        for (unsigned i = 0; i < memory.size(); ++i)
            memory[i] = i * 13 + 7;
    }

    void word(uint32_t address, uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            memory[address + i] = value >> (24 - 8 * i);
    }

    uint32_t word(uint32_t address) const
    {
        return uint32_t(memory[address]) << 24 | uint32_t(memory[address + 1]) << 16 |
               uint32_t(memory[address + 2]) << 8 | memory[address + 3];
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned,
                        uint32_t*)
    {
        auto& f = *static_cast<ram_fixture*>(opaque);
        if (f.clock)
            ROMlib_ppc_750_advance_core(f.clock, 5);
        ++f.transfers;
        if (access & ROMLIB_PPC_PHYSICAL_CONTROL) {
            assert(!size && !bytes);
            ++f.controls;
            return 1;
        }
        if (access & ROMLIB_PPC_PHYSICAL_EXTERNAL) {
            assert(size == 4);
            ++f.external;
            if (!(access & ROMLIB_PPC_MMU_WRITE)) {
                const unsigned char value[] = {0xca, 0xfe, 0x12, 0x34};
                memcpy(bytes, value, 4);
            }
            return 1;
        }
        bool fits = uint64_t(address) + size <= f.memory.size();
        if (fits && ((!f.throwing && !f.checked_abort) || f.copy_before_throw)) {
            if (access & ROMLIB_PPC_MMU_WRITE)
                memcpy(f.memory.data() + address, bytes, size);
            else
                memcpy(bytes, f.memory.data() + address, size);
        }
        if (f.throwing)
            throw std::runtime_error("physical RAM boundary fixture");
        if (f.checked_abort) {
            f.aborted = 1;
            return 0;
        }
        return fits;
    }

    static int history(void* opaque, uint32_t address, uint32_t mask, uint32_t*)
    {
        auto& f = *static_cast<ram_fixture*>(opaque);
        if (f.clock)
            ROMlib_ppc_750_advance_core(f.clock, 5);
        ++f.histories;
        if (f.throwing_history)
            throw std::runtime_error("physical history boundary fixture");
        if (f.checked_history_abort) {
            f.aborted = 1;
            return 0;
        }
        if (uint64_t(address) + 8 > f.memory.size())
            return 0;
        f.word(address + 4, f.word(address + 4) | mask);
        return 1;
    }

    ROMlib_ppc_physical_bus bus(bool direct, bool boundary = false)
    {
        return {this,
                transfer,
                history,
                direct ? memory.data() : nullptr,
                0,
                unsigned(memory.size()),
                boundary ? &aborted : nullptr};
    }
};

static void direct_ram()
{
    static_assert(std::is_standard_layout<ROMlib_ppc_physical_bus>::value, "physical bus keeps its C layout");
    static_assert(std::is_trivially_copyable<ROMlib_ppc_physical_bus>::value, "physical bus remains copyable by C");
    for (bool cached : {false, true})
        for (bool little : {false, true})
            for (bool write : {false, true})
                for (unsigned size : {1u, 2u, 4u, 8u})
                    for (unsigned offset = 0; offset < 8; ++offset) {
                        ram_fixture checked, direct;
                        auto checked_bus = checked.bus(false), direct_bus = direct.bus(true);
                        std::unique_ptr<ROMlib_ppc_system750> expected(new ROMlib_ppc_system750{}),
                            actual(new ROMlib_ppc_system750{});
                        expected->mmu.msr = actual->mmu.msr = little;
                        expected->hid0 = actual->hid0 = cached ? 0x4000 : 0;
                        ROMlib_ppc_bus_result expected_result = {}, actual_result = {};
                        uint64_t first = UINT64_C(0x123456789abcdef0), second = first;
                        bool ok = write ? ROMlib_ppc_750_write(expected.get(), &checked_bus, 0x103 + offset, size,
                                                               first, &expected_result)
                                        : ROMlib_ppc_750_read(expected.get(), &checked_bus, 0x103 + offset, size, 0,
                                                              &first, &expected_result);
                        assert(ok);
                        assert(write ? ROMlib_ppc_750_write(actual.get(), &direct_bus, 0x103 + offset, size, second,
                                                            &actual_result)
                                     : ROMlib_ppc_750_read(actual.get(), &direct_bus, 0x103 + offset, size, 0, &second,
                                                           &actual_result));
                        assert(first == second && !memcmp(expected.get(), actual.get(), sizeof *actual) &&
                               !memcmp(&expected_result, &actual_result, sizeof actual_result) &&
                               checked.memory == direct.memory && !direct.transfers && !direct.histories);
                        ++cases;
                    }

    for (bool cached : {false, true}) {
        ram_fixture checked, direct;
        auto checked_bus = checked.bus(false), direct_bus = direct.bus(true);
        std::unique_ptr<ROMlib_ppc_system750> expected(new ROMlib_ppc_system750{}), actual(new ROMlib_ppc_system750{});
        expected->mmu.msr = actual->mmu.msr = 0x10;
        expected->mmu.sdr1 = actual->mmu.sdr1 = 0x10000;
        expected->hid0 = actual->hid0 = cached ? 0x4000 : 0;
        checked.word(0x10000, 0x80000000);
        direct.word(0x10000, 0x80000000);
        checked.word(0x10004, 0x5012);
        direct.word(0x10004, 0x5012);
        for (bool write : {false, true, false}) {
            ROMlib_ppc_bus_result expected_result = {}, actual_result = {};
            uint64_t first = 0xdeadbeef, second = first;
            assert(write
                       ? ROMlib_ppc_750_write(expected.get(), &checked_bus, 0x20000004, 4, first, &expected_result)
                       : ROMlib_ppc_750_read(expected.get(), &checked_bus, 0x20000004, 4, 0, &first, &expected_result));
            assert(write ? ROMlib_ppc_750_write(actual.get(), &direct_bus, 0x20000004, 4, second, &actual_result)
                         : ROMlib_ppc_750_read(actual.get(), &direct_bus, 0x20000004, 4, 0, &second, &actual_result));
            assert(first == second && !memcmp(expected.get(), actual.get(), sizeof *actual) &&
                   !memcmp(&expected_result, &actual_result, sizeof actual_result) && checked.memory == direct.memory &&
                   !direct.transfers && !direct.histories);
            ++cases;
        }
    }

    ram_fixture f;
    auto bus = f.bus(true);
    std::unique_ptr<ROMlib_ppc_system750> state(new ROMlib_ppc_system750{});
    state->hid0 = 0x4000 | ROMLIB_PPC_750_HID0_ABE;
    ROMlib_ppc_bus_result result = {};
    for (unsigned i = 0; i < 16; ++i) {
        assert(ROMlib_ppc_750_write(state.get(), &bus, 0x100 + i * 4096, 4, 0xa5000000 | i, &result));
        assert(!f.transfers && !f.histories);
        ++cases;
    }
    for (unsigned i = 0; i < 16; ++i) {
        assert(ROMlib_ppc_750_cache_control(state.get(), &bus, 0x100 + i * 4096, 86, &result));
        assert(f.word(0x100 + i * 4096) == (0xa5000000 | i));
        ++cases;
    }
    assert(f.transfers == f.controls && f.controls);
    state->ear = 0x80000005;
    uint32_t value = 0;
    assert(ROMlib_ppc_750_external(state.get(), &bus, 0x100, 0, &value, &result));
    assert(value == 0xcafe1234 && f.external == 1);
    assert(ROMlib_ppc_750_cache_control(state.get(), &bus, 0, 598, &result));
    assert(f.transfers == f.controls + f.external);
    *state = {};
    bus.linear_memory = f.memory.data() + 0x100;
    bus.linear_address = 0x100;
    bus.linear_size = 0x100;
    for (unsigned size : {4u, 8u})
        for (uint32_t address : {0xfcu, 0x100u, 0x1fcu, 0x1ffu, 0x200u, 0xfffffffcu}) {
            unsigned calls = f.transfers;
            uint64_t scalar = UINT64_MAX;
            bool ok = ROMlib_ppc_750_read(state.get(), &bus, address, size, 0, &scalar, &result);
            assert(ok == (uint64_t(address) + size <= f.memory.size()));
            bool within = address >= 0x100 && uint64_t(address) + size <= 0x200;
            assert(f.transfers == calls + !within);
            if (!ok)
                assert(scalar == UINT64_MAX);
            ++cases;
        }
    bus.linear_memory = f.memory.data();
    bus.linear_address = 0xfffffff0;
    bus.linear_size = 0x100; // An invalid wrapping declaration must not expose host RAM.
    unsigned calls = f.transfers;
    uint64_t scalar = UINT64_MAX;
    assert(!ROMlib_ppc_750_read(state.get(), &bus, 0xfffffffc, 4, 0, &scalar, &result));
    assert(f.transfers == calls + 1 && scalar == UINT64_MAX);
    puts(
        "Direct physical RAM matches checked LE/BE scalars, MMU R/C reloads and L1 dirty castouts; devices stay checked");
}

static void checked_callback_abort()
{
    for (bool reserve : {false, true}) {
        ram_fixture throwing, checked;
        auto throwing_bus = throwing.bus(false), checked_bus = checked.bus(false, true);
        std::unique_ptr<ROMlib_ppc_system750> expected(new ROMlib_ppc_system750{}), actual(new ROMlib_ppc_system750{});
        expected->hid0 = actual->hid0 = reserve ? 0 : 0x4000;
        ROMlib_ppc_bus_result result = {};
        if (!reserve) {
            assert(ROMlib_ppc_750_write(expected.get(), &throwing_bus, 0x100, 4, 0x12345678, &result));
            assert(ROMlib_ppc_750_write(actual.get(), &checked_bus, 0x100, 4, 0x12345678, &result));
        }
        throwing.throwing = checked.checked_abort = true;
        throwing.copy_before_throw = checked.copy_before_throw = true;
        uint32_t value = 0xdeadbeef;
        try {
            if (reserve)
                ROMlib_ppc_750_load_reserve(expected.get(), &throwing_bus, 0x100, &value, &result);
            else
                ROMlib_ppc_750_cache_control(expected.get(), &throwing_bus, 0x100, 86, &result);
            assert(false && "throwing callback returned");
        }
        catch (const std::runtime_error&) {
        }
        assert(reserve ? !ROMlib_ppc_750_load_reserve(actual.get(), &checked_bus, 0x100, &value, &result)
                       : !ROMlib_ppc_750_cache_control(actual.get(), &checked_bus, 0x100, 86, &result));
        assert(value == 0xdeadbeef && checked.aborted && !actual->bus_check_pending &&
               result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION && !result.mapping.vector &&
               !memcmp(expected.get(), actual.get(), sizeof *actual) && throwing.memory == checked.memory);
        ++cases;
    }
    for (bool instruction : {false, true})
        for (bool history : {false, true}) {
            ram_fixture throwing, checked;
            auto throwing_bus = throwing.bus(false), checked_bus = checked.bus(false, true);
            std::unique_ptr<ROMlib_ppc_system750> expected(new ROMlib_ppc_system750{}),
                actual(new ROMlib_ppc_system750{});
            throwing.clock = expected.get();
            checked.clock = actual.get();
            throwing.throwing = checked.checked_abort = !history;
            throwing.throwing_history = checked.checked_history_abort = history;
            expected->mmu.msr = actual->mmu.msr = 0x30;
            expected->mmu.sdr1 = actual->mmu.sdr1 = 0x10000;
            // PMC1 selector 6 counts ITLB search cycles; PMC4 selector 6 counts
            // DTLB search cycles. Neither observes an unwound table transaction.
            expected->mmcr0 = actual->mmcr0 = 6 << 6;
            expected->mmcr1 = actual->mmcr1 = 6 << 22;
            throwing.word(0x10000, 0x80000000);
            throwing.word(0x10004, 0x5002);
            checked.memory = throwing.memory;
            uint64_t value = 0xdeadbeef;
            ROMlib_ppc_bus_result result = {};
            unsigned access = instruction ? ROMLIB_PPC_MMU_INSTRUCTION : 0;
            try {
                ROMlib_ppc_750_read(expected.get(), &throwing_bus, 0x20000000, 4, access, &value, &result);
                assert(false && "throwing table callback returned");
            }
            catch (const std::runtime_error&) {
            }
            assert(!ROMlib_ppc_750_read(actual.get(), &checked_bus, 0x20000000, 4, access, &value, &result));
            assert(value == 0xdeadbeef && actual->core_cycles == unsigned(history ? 10 : 5));
            assert(!actual->pmc[0] && !actual->pmc[3] && !memcmp(expected.get(), actual.get(), sizeof *actual));
            assert(throwing.memory == checked.memory && checked.aborted && !actual->bus_check_pending);
            ++cases;
        }
    puts("Checked callback aborts retain native transaction/reservation state without false machine checks");
}

static void byte_ranges()
{
    for (bool little : {false, true})
        for (unsigned access : {0u, unsigned(ROMLIB_PPC_MMU_WRITE)}) {
            uint32_t lane = little ? 7 : 0;
            for (uint32_t address : {0x100u, 0x105u, 0xFFDu, 0xFFFFFFF9u})
                for (uint32_t size : {0u, 1u, 7u, 8u, 9u, 16u, 29u}) {
                    ROMlib_ppc_system750 state = {};
                    state.mmu.msr = little;
                    fixture f;
                    auto bus = f.bus();
                    unsigned char bytes[31];
                    for (unsigned i = 0; i < sizeof bytes; ++i)
                        bytes[i] = 0x80 + 3 * i;
                    ROMlib_ppc_bus_result result;
                    assert(ROMlib_ppc_750_transfer_bytes(&state, &bus, address, size ? bytes + 1 : nullptr, size,
                                                         access, &result));
                    assert(result.address == address && result.size == size && result.completed == size);
                    assert(bytes[0] == 0x80 && bytes[size + 1] == 0x80 + 3 * (size + 1));
                    for (uint32_t i = 0; i < size; ++i)
                        assert(bytes[i + 1] == f.byte((address + i) ^ lane));
                    if (access)
                        assert(f.memory.size() == size);
                    if (!size)
                        assert(f.events.empty());
                    ++cases;
                }
            ROMlib_ppc_system750 state = {};
            state.mmu.msr = little;
            fixture f;
            auto bus = f.bus();
            f.fail_data = 2;
            unsigned char bytes[16];
            for (unsigned i = 0; i < sizeof bytes; ++i)
                bytes[i] = 0xA0 + i;
            ROMlib_ppc_bus_result result;
            assert(!ROMlib_ppc_750_transfer_bytes(&state, &bus, 0xFF8, bytes, sizeof bytes, access, &result));
            assert(result.address == 0xFF8 && result.size == 16 && result.completed == 8);
            assert(result.mapping.address == 0x1000 && result.mapping.vector == 0x200);
            for (unsigned i = 0; i < 8; ++i)
                assert(bytes[i] == f.byte((0xFF8 + i) ^ lane) && bytes[8 + i] == 0xA8 + i);
            if (access)
                assert(f.memory.size() == 8);
            ++cases;
        }
}

static void scalar_lanes()
{
    for (bool little : {false, true})
        for (unsigned size : {1u, 2u, 4u, 8u})
            for (uint32_t address :
                 {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 0xFFDu, 0xFFFu, 0xFFFFFFF9u, 0xFFFFFFFDu, 0xFFFFFFFFu}) {
                ROMlib_ppc_system750 state = {};
                state.mmu.msr = little;
                fixture f;
                auto bus = f.bus();
                uint64_t expected = 0;
                for (unsigned i = 0; i < size; ++i) {
                    unsigned char byte = f.byte((address + i) ^ (little ? 7 : 0));
                    if (little)
                        expected |= uint64_t(byte) << (i * 8);
                    else
                        expected = expected << 8 | byte;
                }
                uint64_t value = 0xDEADBEEFCAFEBABE;
                ROMlib_ppc_bus_result result;
                assert(ROMlib_ppc_750_read(&state, &bus, address, size, 0, &value, &result));
                assert(value == expected && result.completed == size && !result.mapping.fault);
                for (const auto& event : f.events)
                    assert(event.wimg == 3 && !event.access);
                const uint64_t stored = UINT64_C(0x80112233445566FF);
                assert(ROMlib_ppc_750_write(&state, &bus, address, size, stored, &result));
                assert(f.memory.size() == size);
                for (unsigned i = 0; i < size; ++i) {
                    unsigned shift = (little ? i : size - 1 - i) * 8;
                    assert(f.byte((address + i) ^ (little ? 7 : 0)) == ((stored >> shift) & 255));
                }
                value = 0;
                assert(ROMlib_ppc_750_read(&state, &bus, address, size, 0, &value, &result));
                assert(value == (stored & (UINT64_MAX >> (64 - size * 8))));
                ++cases;
            }
    // Explicit architectural lane examples independent of the transfer loop.
    fixture f;
    for (unsigned i = 0; i < 16; ++i)
        f.memory[i] = i;
    auto bus = f.bus();
    ROMlib_ppc_system750 state = {};
    ROMlib_ppc_bus_result result;
    uint64_t value;
    assert(ROMlib_ppc_750_read(&state, &bus, 0, 4, 0, &value, &result) && value == 0x00010203);
    state.mmu.msr = 1;
    assert(ROMlib_ppc_750_read(&state, &bus, 0, 1, 0, &value, &result) && value == 7);
    assert(ROMlib_ppc_750_read(&state, &bus, 0, 4, 0, &value, &result) && value == 0x04050607);
    assert(ROMlib_ppc_750_read(&state, &bus, 1, 4, 0, &value, &result) && value == 0x03040506);
    assert(ROMlib_ppc_750_read(&state, &bus, 0, 8, 0, &value, &result) && value == UINT64_C(0x0001020304050607));
}

static void page(fixture& f, uint32_t logical, uint32_t physical, unsigned wimg)
{
    // VSID=0, SDR1=10000; low logical page indices select these fixed PTEGs.
    uint32_t location = 0x10000 | (((logical >> 12) & 1023) << 6);
    f.word(location, 0x80000000 | (logical >> 22 & 63));
    f.word(location + 4, physical | (wimg << 3) | 2);
}

static void mapped_pages()
{
    for (bool little : {false, true})
        for (bool missing : {false, true})
            for (bool writing : {false, true}) {
                ROMlib_ppc_system750 state = {};
                state.mmu.msr = 0x30 | little;
                state.mmu.sdr1 = 0x10000;
                fixture f;
                page(f, 0x20000000, 0x5000, 2);
                if (!missing)
                    page(f, 0x20001000, 0xA000, 0);
                auto bus = f.bus();
                ROMlib_ppc_bus_result result;
                const uint64_t input = UINT64_C(0x0123456789ABCDEF);
                uint64_t value = input, expected = 0;
                for (unsigned i = 0; i < 8; ++i) {
                    uint32_t logical = 0xFFD + i;
                    uint32_t physical = (logical < 0x1000 ? 0x5000 : 0x9000) + logical;
                    physical ^= little ? 7 : 0;
                    if (little)
                        expected |= uint64_t(f.byte(physical)) << (8 * i);
                    else
                        expected = expected << 8 | f.byte(physical);
                }
                bool ok = writing ? ROMlib_ppc_750_write(&state, &bus, 0x20000FFD, 8, input, &result)
                                  : ROMlib_ppc_750_read(&state, &bus, 0x20000FFD, 8, 0, &value, &result);
                assert(ok == !missing);
                assert(result.completed == (missing ? 3u : 8u));
                if (!writing)
                    assert(value == (missing ? input : expected));
                if (missing) {
                    assert(result.mapping.address == 0x20001000 && result.address == 0x20000FFD);
                    assert(result.mapping.vector == 0x300 && result.mapping.fault == ROMLIB_PPC_MMU_MISSING);
                    assert(result.mapping.cause == (writing ? 0x42000000u : 0x40000000u));
                }
                for (const auto& event : f.events)
                    if (!(event.access & ROMLIB_PPC_PHYSICAL_TABLE))
                        assert(event.wimg == (event.address < 0xA000 ? 2u : 0u));
                if (writing)
                    for (unsigned i = 0; i < (missing ? 3u : 8u); ++i) {
                        uint32_t logical = 0xFFD + i;
                        uint32_t physical = ((logical < 0x1000 ? 0x5000 : 0x9000) + logical) ^ (little ? 7 : 0);
                        unsigned shift = (little ? i : 7 - i) * 8;
                        assert(f.byte(physical) == ((input >> shift) & 255));
                    }
                ++cases;
            }
}

static void bus_faults()
{
    for (unsigned stage = 0; stage < 3; ++stage) {
        fixture f;
        ROMlib_ppc_system750 state = {};
        state.mmu.msr = 0x30;
        state.mmu.sdr1 = 0x10000;
        page(f, 0, 0x5000, 2);
        f.fail_table = stage == 0;
        f.fail_history = stage == 1;
        f.fail_data = stage == 2 ? 1 : 0;
        f.cause = 0x80020000;
        auto bus = f.bus();
        ROMlib_ppc_bus_result result;
        uint64_t value = 0xDEADBEEF;
        assert(!ROMlib_ppc_750_read(&state, &bus, 0, 4, 0, &value, &result));
        assert(value == 0xDEADBEEF && result.completed == 0 && result.mapping.vector == 0x200);
        assert(result.mapping.cause == 0x20000 && result.table == unsigned(stage != 2));
        assert(result.mapping.physical == (stage == 0 ? 0x10000u : stage == 1 ? 0x10004u : 0x5000u));
        ++cases;
    }
    fixture f;
    auto bus = f.bus();
    ROMlib_ppc_system750 state = {};
    ROMlib_ppc_bus_result result;
    uint64_t value = 0x12345678;
    assert(!ROMlib_ppc_750_read(&state, &bus, 0, 3, 0, &value, &result));
    assert(!ROMlib_ppc_750_read(&state, &bus, 0, 4, ROMLIB_PPC_MMU_WRITE, &value, &result));
    assert(!ROMlib_ppc_750_read(&state, &bus, 1, 4, ROMLIB_PPC_MMU_INSTRUCTION, &value, &result));
    assert(!ROMlib_ppc_750_read(&state, &bus, 0, 4, 0, nullptr, &result));
    assert(result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION && f.events.empty() && value == 0x12345678);
}

static void external_controls()
{
    for (bool little : {false, true}) {
        fixture f;
        auto bus = f.bus();
        ROMlib_ppc_system750 state = {};
        state.mmu.msr = little;
        state.ear = 0x80000005;
        ROMlib_ppc_bus_result result;
        uint32_t value = 0;
        uint32_t expected = 0;
        for (unsigned i = 0; i < 4; ++i)
            expected = expected << 8 | f.byte(0x80 + i);
        assert(ROMlib_ppc_750_external(&state, &bus, 0x80, 0, &value, &result));
        assert(value == expected && result.size == 4 && result.completed == 4 && result.mapping.wimg == 5);
        assert(f.events.back().address == 0x80 && f.events.back().access == (ROMLIB_PPC_PHYSICAL_EXTERNAL | 5 << 8));
        ++cases;
        value = 0x12345678;
        assert(ROMlib_ppc_750_external(&state, &bus, 0x80, 1, &value, &result));
        assert(f.byte(0x80) == 0x12 && f.byte(0x81) == 0x34 && f.byte(0x82) == 0x56 && f.byte(0x83) == 0x78);
        ++cases;
        f.events.clear();
        state.ear = 0;
        assert(!ROMlib_ppc_750_external(&state, &bus, 0x81, 1, &value, &result));
        assert(result.mapping.vector == 0x300 && result.mapping.cause == 0x02100000 && f.events.empty());
        ++cases;
        state.ear = 0x80000005;
        assert(!ROMlib_ppc_750_external(&state, &bus, 0x81, 0, &value, &result));
        assert(result.mapping.vector == 0x600 && result.mapping.address == 0x81 && f.events.empty());
        ++cases;
        for (unsigned bad = 0; bad < 3; ++bad) {
            assert(!ROMlib_ppc_750_external(&state, &bus, 0x80, bad == 0 ? 2 : 0, bad == 1 ? nullptr : &value,
                                            bad == 2 ? nullptr : &result));
            assert(value == 0x12345678 && f.events.empty());
            if (bad != 2)
                assert(result.mapping.fault == ROMLIB_PPC_MMU_CONFIGURATION);
            ++cases;
        }
        f.fail_data = f.data_count + 1;
        f.cause = 0x20000;
        assert(!ROMlib_ppc_750_external(&state, &bus, 0x80, 0, &value, &result));
        assert(value == 0x12345678 && result.mapping.vector == 0x200 && result.mapping.cause == 0x20000);
        assert(result.size == 4 && !result.completed && result.mapping.physical == 0x80 && !result.table);
        ++cases;
        f.fail_data = 0;
        expected = 0;
        for (unsigned i = 0; i < 4; ++i)
            expected = expected << 8 | f.byte(0xFFFFFFFC + i);
        assert(ROMlib_ppc_750_external(&state, &bus, 0xFFFFFFFC, 0, &value, &result));
        assert(value == expected && result.mapping.physical == 0xFFFFFFFC);
        ++cases;
    }
}

} // namespace

void bus750_checks()
{
    scalar_lanes();
    byte_ranges();
    mapped_pages();
    bus_faults();
    external_controls();
    direct_ram();
    checked_callback_abort();
    printf("%u PowerPC 750 scalar/byte-range/external endian/page/wrap/bus-fault cases passed\n", cases);
}
