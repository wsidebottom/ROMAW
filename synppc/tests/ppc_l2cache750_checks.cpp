/*+===================================================================
  File:      ppc_l2cache750_checks.cpp

  Summary:   Check 750 sector geometry, L2 transfer policies, copyback failures and re-entrant snoop retries.

  Classes:   l2_fixture; uses ROMlib_ppc_l2cache750 and ROMlib_ppc_physical_bus.

  Functions: l2cache_checks

  Origin:    Original deterministic tests from MPC750UM 9.1.1/9.1.2; linked into the existing cache suite.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/l2cache750.h"
#include "synppc/bus750.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
struct l2_fixture {
    struct event {
        uint32_t address;
        unsigned size, access, wimg;
    };

    std::unique_ptr<ROMlib_ppc_l2cache750> bank{new ROMlib_ppc_l2cache750{}};
    std::vector<unsigned char> memory = std::vector<unsigned char>(2097152);
    std::vector<event> events;
    ROMlib_ppc_physical_bus bus = {this, transfer, nullptr};
    ROMlib_ppc_l2cache750_result result = {};
    unsigned capacity = 262144, fail = 0, throw_at = 0, retry_checks = 0;
    uint32_t cause = 0x20000, incoming = 0;
    bool query = false, high_mirror = false;

    l2_fixture()
    {
        for (unsigned i = 0; i < memory.size(); ++i)
            memory[i] = i * 37 + (i >> 8) + (i >> 16);
    }

    void clear(unsigned size)
    {
        assert(!bank->transactions && ROMlib_ppc_l2cache750_invalidate(bank.get()));
        capacity = size;
        events.clear();
        fail = throw_at = 0;
        cause = 0x20000;
        query = high_mirror = false;
    }

    static int transfer(void* opaque, uint32_t address, void* bytes, unsigned size, unsigned access, unsigned wimg,
                        uint32_t* cause)
    {
        auto& f = *static_cast<l2_fixture*>(opaque);
        f.events.push_back({address, size, access, wimg});
        uint32_t mapped = f.high_mirror ? address & 0x1FFFFF : address;
        assert(size && size <= 8 && uint64_t(address) + size <= UINT64_C(0x100000000));
        assert(uint64_t(mapped) + size <= f.memory.size());
        assert(!(access & (ROMLIB_PPC_L2CACHE750_EXPLICIT_PUSH | ROMLIB_PPC_L2CACHE750_L1_HIT)));
        if (f.query) {
            assert(ROMlib_ppc_l2cache750_busy(f.bank.get(), address));
            assert(ROMlib_ppc_l2cache750_busy(f.bank.get(), f.incoming));
            assert(!ROMlib_ppc_l2cache750_invalidate(f.bank.get()));
            ROMlib_ppc_l2cache750_result observed = {};
            assert(!ROMlib_ppc_l2cache750_block(f.bank.get(), &f.bus, f.capacity, address, 5, 1, &observed));
            assert(observed.fault == ROMLIB_PPC_MMU_RETRY && !observed.cause);
            ++f.retry_checks;
        }
        if (f.events.size() == f.throw_at)
            throw std::runtime_error("injected L2 bus exception");
        if (f.events.size() == f.fail) {
            *cause = f.cause;
            return 0;
        }
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(f.memory.data() + mapped, bytes, size);
        else
            memcpy(bytes, f.memory.data() + mapped, size);
        return 1;
    }

    int transfer(uint32_t address, void* bytes, unsigned size, unsigned access = 0, unsigned wimg = 0,
                 unsigned control = 1, unsigned critical = 0)
    {
        incoming = address;
        return ROMlib_ppc_l2cache750_transfer(bank.get(), &bus, capacity, address, bytes, size, critical, access, wimg,
                                              control, &result);
    }

    int block(uint32_t address, unsigned operation, unsigned control = 1)
    {
        incoming = address;
        return ROMlib_ppc_l2cache750_block(bank.get(), &bus, capacity, address, operation, control, &result);
    }

    unsigned state(uint32_t address) const
    {
        unsigned tag_bytes = capacity == 1048576 ? 128 : 64;
        unsigned set = (address / tag_bytes) % (capacity / (2 * tag_bytes));
        for (const auto& tag : bank->tags[set])
            if (tag.address == address - address % tag_bytes)
                return tag.state[(address % tag_bytes) / 32];
        return 0;
    }

    void seed(uint32_t address, unsigned state)
    {
        memset(memory.data() + address, 0x11, 32);
        unsigned char bytes[32];
        memset(bytes, 0xAA, sizeof bytes);
        if (state)
            assert(transfer(address, bytes, 32, state == 2 ? 1 : 0));
        events.clear();
    }
};
}

unsigned l2cache_checks(void)
{
    l2_fixture f;
    unsigned cases = 0;
    // Fill and read both ways of every set and every sector at all three sizes.
    // A second pass detects aliasing that immediate read-after-fill would miss.
    for (unsigned capacity : {262144u, 524288u, 1048576u}) {
        f.clear(capacity);
        unsigned tag_bytes = capacity == 1048576 ? 128 : 64, stride = capacity / 2;
        for (unsigned base = 0; base < stride; base += tag_bytes)
            for (unsigned way = 0; way < 2; ++way)
                for (unsigned sector = 0; sector < tag_bytes; sector += 32) {
                    uint32_t address = base + way * stride + sector;
                    unsigned critical = ((base / tag_bytes + sector / 32) & 3) * 8;
                    f.events.clear();
                    unsigned char bytes[32];
                    memset(bytes, 0xEE, sizeof bytes);
                    f.query = true;
                    assert(f.transfer(address, bytes, 32, 0, 0, 1, critical));
                    assert(!memcmp(bytes, f.memory.data() + address, 32));
                    assert(f.result.misses == 1 && !f.result.hits && !f.result.castouts && f.events.size() == 4);
                    for (unsigned beat = 0; beat < 4; ++beat)
                        assert(f.events[beat].address == address + ((critical + beat * 8) & 31));
                    ++cases;
                }
        f.query = false;
        f.events.clear();
        for (uint32_t address = 0; address < capacity; address += 32) {
            unsigned char bytes[32];
            assert(f.transfer(address, bytes, 32));
            assert(f.result.hits == 1 && !f.result.misses && !memcmp(bytes, f.memory.data() + address, 32));
        }
        assert(f.events.empty());
    }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (unsigned control = 0; control < 16; ++control)
            for (unsigned wimg = 0; wimg < 16; ++wimg)
                for (unsigned kind = 0; kind < 3; ++kind)
                    for (unsigned state = 0; state < 3; ++state) {
                        f.clear(capacity);
                        f.seed(0x2000, state);
                        unsigned access = kind == 0 ? 0 : kind == 1 ? 1 : 2;
                        bool active = (control & 1) && !(wimg & 4) && !(kind == 2 && (control & 2));
                        bool hit = active && state;
                        bool through = (wimg & 8) || (control & 4);
                        bool external = !hit || (kind == 1 && through);
                        if (active && !hit && kind == 1 && (control & 8))
                            external = false;
                        unsigned char bytes[4] = {0xCC, 0xCC, 0xCC, 0xCC};
                        assert(f.transfer(0x2004, bytes, 4, access, wimg, control));
                        assert(f.events.size() == unsigned(external));
                        assert(f.result.hits == unsigned(hit) && f.result.misses == unsigned(active && !hit));
                        unsigned after = hit && kind == 1 && !through ? 2 : state;
                        assert(f.state(0x2000) == after);
                        for (unsigned i = 0; i < 4; ++i) {
                            if (kind == 1)
                                assert(f.memory[0x2004 + i] == (external ? 0xCC : 0x11));
                            else
                                assert(bytes[i] == (hit && state == 2 ? 0xAA : 0x11));
                        }
                        ++cases;
                    }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (unsigned control = 0; control < 16; ++control)
            for (unsigned wimg = 0; wimg < 16; ++wimg)
                for (bool explicit_push : {false, true})
                    for (unsigned state = 0; state < 3; ++state) {
                        f.clear(capacity);
                        f.seed(0x2000, state);
                        bool active = (control & 1) && !(wimg & 4), hit = active && state;
                        bool tested = explicit_push && (control & 8);
                        bool external = !active || (explicit_push ? !tested : (wimg & 8) || (control & 4));
                        unsigned char bytes[32];
                        memset(bytes, 0xCC, sizeof bytes);
                        assert(f.transfer(0x2000, bytes, 32, 1 | (explicit_push ? 65536 : 0), wimg, control));
                        assert(f.events.size() == (external ? 4u : 0u));
                        assert(f.result.hits == unsigned(hit) && f.result.misses == unsigned(active && !hit));
                        unsigned after = !active ? state : explicit_push && !tested ? 0 : external ? 1 : 2;
                        assert(f.state(0x2000) == after);
                        for (unsigned i = 0; i < 32; ++i)
                            assert(f.memory[0x2000 + i] == (external ? 0xCC : 0x11));
                        ++cases;
                    }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (unsigned state = 0; state < 3; ++state)
            for (bool l1_hit : {false, true})
                for (bool page_wt : {false, true})
                    for (bool l2_wt : {false, true}) {
                        f.clear(capacity);
                        f.seed(0x2000, state);
                        bool through = page_wt || l2_wt;
                        bool local = state == 2 || (state && !through && l1_hit);
                        unsigned char bytes[4] = {0xCC, 0xCC, 0xCC, 0xCC};
                        assert(
                            f.transfer(0x2004, bytes, 4, 129 | (l1_hit ? 131072 : 0), page_wt ? 8 : 0, l2_wt ? 5 : 1));
                        assert(f.events.size() == unsigned(!local));
                        assert(f.state(0x2000) == (local ? 2u : 0u));
                        assert(f.memory[0x2004] == (local ? 0x11 : 0xCC));
                        ++cases;
                    }
    for (unsigned capacity : {262144u, 524288u, 1048576u}) {
        unsigned sectors = capacity == 1048576 ? 4 : 2, stride = capacity / 2;
        for (unsigned bitmap = 0; bitmap < (1u << sectors); ++bitmap) {
            unsigned dirty = 0;
            for (unsigned i = 0; i < sectors; ++i)
                dirty += (bitmap >> i) & 1;
            for (unsigned failure = 0; failure <= dirty * 4 + 4; ++failure) {
                f.clear(capacity);
                unsigned char bytes[32];
                for (unsigned sector = 0; sector < sectors; ++sector)
                    f.seed(0x2000 + sector * 32, (bitmap & (1u << sector)) ? 2 : 1);
                f.seed(0x2000 + stride, 1); // Way one was touched last, so evict way zero.
                f.events.clear();
                f.fail = failure;
                f.query = true;
                memset(bytes, 0xEE, sizeof bytes);
                assert(bool(f.transfer(0x2000 + stride * 2, bytes, 32)) == !failure);
                assert(!f.bank->transactions);
                unsigned writes = failure ? std::min(failure - 1, dirty * 4) : dirty * 4;
                assert(f.result.castouts == writes / 4 && f.result.pushes == writes / 4);
                assert(f.state(0x2000 + stride) == 1);
                if (failure) {
                    for (auto byte : bytes)
                        assert(byte == 0xEE);
                    assert(f.result.fault == ROMLIB_PPC_MMU_BUS && f.result.cause == 0x20000);
                    assert(f.state(0x2000 + stride * 2) == 0);
                    f.fail = 0;
                    assert(f.transfer(0x2000 + stride * 2, bytes, 32));
                }
                assert(!memcmp(bytes, f.memory.data() + 0x2000 + stride * 2, 32));
                ++cases;
            }
        }
        for (bool touch_first : {false, true}) {
            f.clear(capacity);
            unsigned char bytes[32];
            assert(f.transfer(0x2000, bytes, 32));
            assert(f.transfer(0x2000 + stride, bytes, 32));
            assert(f.transfer(0x2000 + (touch_first ? 0 : stride), bytes, 1));
            assert(f.transfer(0x2000 + stride * 2, bytes, 32));
            assert(f.state(0x2000) == unsigned(touch_first));
            assert(f.state(0x2000 + stride) == unsigned(!touch_first));
            ++cases;
        }
    }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (unsigned operation : {1u, 2u, 3u, 5u, 6u})
            for (unsigned state = 0; state < 3; ++state)
                for (bool enabled : {false, true})
                    for (unsigned failure = 0; failure <= 4; ++failure) {
                        f.clear(capacity);
                        f.seed(0x2000, state);
                        bool pushes = enabled && state == 2 && operation != 3;
                        f.fail = failure;
                        f.query = enabled;
                        assert(bool(f.block(0x2000, operation, enabled ? 1 : 0)) == !(pushes && failure));
                        unsigned after = !enabled || (pushes && failure) ? state : operation == 6 && state ? 1 : 0;
                        assert(f.state(0x2000) == after);
                        assert(f.result.pushes == unsigned(pushes && !failure));
                        if (pushes && failure)
                            assert(f.result.fault == ROMLIB_PPC_MMU_BUS && !f.bank->transactions);
                        ++cases;
                    }
    {
        f.clear(262144);
        f.seed(0x2000, 2);
        auto snapshot = std::vector<unsigned char>(f.bank->data, f.bank->data + sizeof f.bank->data);
        assert(ROMlib_ppc_l2cache750_invalidate(f.bank.get()));
        assert(!f.state(0x2000) && !memcmp(snapshot.data(), f.bank->data, snapshot.size()));
        ROMlib_ppc_l2cache750_reset(f.bank.get());
        for (auto byte : f.bank->data)
            assert(!byte);
        ++cases;
    }
    for (unsigned capacity : {262144u, 524288u, 1048576u}) {
        f.clear(capacity);
        f.seed(0x2000, 2);
        unsigned char bytes[32];
        assert(f.transfer(0x2020, bytes, 32)); // Fill another sector of an already dirty tag.
        assert(!f.result.castouts && f.state(0x2000) == 2 && f.state(0x2020) == 1);
        assert(f.transfer(0x2000, bytes, 32));
        for (auto byte : bytes)
            assert(byte == 0xAA);
        ++cases;
    }
    for (unsigned state = 0; state < 3; ++state)
        for (bool writing : {false, true})
            for (bool fail : {false, true}) {
                f.clear(262144);
                f.seed(0x2000, state);
                // Cross a sector boundary. First byte may hit; the second part
                // misses and must not allocate, even though it shares the tag.
                unsigned char bytes[8];
                memset(bytes, 0xCC, sizeof bytes);
                f.fail = fail ? (state ? 1 : 2) : 0;
                bool ok = f.transfer(0x201F, bytes, 8, writing ? 1 : 0);
                assert(ok == !fail && f.result.completed == (fail ? 1u : 8u));
                assert(!f.state(0x2020));
                if (!writing) {
                    if (fail)
                        for (auto byte : bytes)
                            assert(byte == 0xCC);
                    else {
                        assert(bytes[0] == (state == 2 ? 0xAA : 0x11));
                        assert(!memcmp(bytes + 1, f.memory.data() + 0x2020, 7));
                    }
                }
                ++cases;
            }
    {
        f.clear(262144);
        f.seed(0x2000, 2);
        const auto snapshot =
            std::vector<unsigned char>(reinterpret_cast<unsigned char*>(f.bank.get()),
                                       reinterpret_cast<unsigned char*>(f.bank.get()) + sizeof *f.bank);
        for (unsigned bad = 0; bad < 19; ++bad) {
            unsigned capacity = bad == 0 ? 65536 : 262144;
            uint32_t address = bad == 1 ? 0x2001 : bad == 2 ? 0xFFFFFFFCu : 0x2000;
            unsigned size = bad == 1   ? 32
                            : bad == 2 ? 8
                            : bad == 3 ? 0
                            : bad == 4 ? 9
                            : bad == 5 ? 31
                            : bad == 6 ? 33
                                       : 4;
            unsigned critical = bad == 7 ? 1 : bad == 8 ? 8 : 0;
            unsigned access = bad == 9    ? 3
                              : bad == 10 ? 65536
                              : bad == 11 ? 65537
                              : bad == 12 ? 131073
                              : bad == 13 ? 128
                              : bad == 14 ? 129
                              : bad == 15 ? 128 | 16
                                          : 0;
            if (bad == 13 || bad == 14)
                size = 8;
            unsigned char bytes[32];
            memset(bytes, 0xEE, sizeof bytes);
            assert(!ROMlib_ppc_l2cache750_transfer(bad == 16 ? nullptr : f.bank.get(), &f.bus, capacity, address,
                                                   bad == 17 ? nullptr : bytes, size, critical, access,
                                                   bad == 18 ? 16 : 0, 1, &f.result));
            assert(f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION && !f.result.hits && !f.result.misses);
            assert(f.events.empty() && !memcmp(snapshot.data(), f.bank.get(), snapshot.size()));
            for (auto byte : bytes)
                assert(byte == 0xEE);
            ++cases;
        }
        unsigned char bytes[32] = {};
        assert(!ROMlib_ppc_l2cache750_transfer(f.bank.get(), &f.bus, 262144, 0x2000, bytes, 4, 0, 0, 0, 32, &f.result));
        assert(!ROMlib_ppc_l2cache750_transfer(f.bank.get(), &f.bus, 262144, 0x2000, bytes, 4, 0, 0, 0, 1, nullptr));
        for (unsigned operation : {0u, 7u})
            assert(!f.block(0x2000, operation) && f.result.fault == ROMLIB_PPC_MMU_CONFIGURATION);
        assert(f.block(0x2000, 4) && f.result.hits == 1 && f.state(0x2000) == 2);
        assert(!memcmp(snapshot.data(), f.bank.get(), snapshot.size()) && f.events.empty());
        ++cases;
    }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (bool dirty : {false, true}) {
            f.clear(capacity);
            f.seed(0x2000, dirty ? 2 : 1);
            f.seed(0x2000 + capacity / 2, 1);
            f.events.clear();
            f.throw_at = 1;
            f.query = true;
            unsigned char bytes[32];
            memset(bytes, 0xEE, sizeof bytes);
            bool threw = false;
            try {
                f.transfer(0x2000 + capacity, bytes, 32);
            }
            catch (const std::runtime_error&) {
                threw = true;
            }
            assert(threw && !f.bank->transactions);
            assert(!ROMlib_ppc_l2cache750_busy(f.bank.get(), 0x2000) &&
                   !ROMlib_ppc_l2cache750_busy(f.bank.get(), 0x2000 + capacity));
            for (auto byte : bytes)
                assert(byte == 0xEE);
            assert(f.state(0x2000) == (dirty ? 2u : 1u));
            f.throw_at = 0;
            assert(f.transfer(0x2000 + capacity, bytes, 32));
            ++cases;
        }
    for (unsigned capacity : {262144u, 524288u, 1048576u})
        for (uint32_t cause : {0u, 0x20000u}) {
            f.clear(capacity);
            f.fail = 2;
            f.cause = cause;
            unsigned char bytes[32];
            memset(bytes, 0xEE, sizeof bytes);
            assert(!f.transfer(0x2000, bytes, 32));
            assert(f.result.fault == ROMLIB_PPC_MMU_BUS && f.result.cause == cause && !f.state(0x2000));
            for (auto byte : bytes)
                assert(byte == 0xEE);
            assert(!f.bank->transactions);
            ++cases;
        }
    for (unsigned capacity : {262144u, 524288u, 1048576u}) {
        f.clear(capacity);
        f.high_mirror = true;
        unsigned char bytes[32];
        assert(f.transfer(0xFFFFFFE0, bytes, 32, 0, 0, 1, 24));
        assert(!memcmp(bytes, f.memory.data() + 0x1FFFE0, 32));
        assert(f.events[0].address == 0xFFFFFFF8 && f.events[3].address == 0xFFFFFFF0);
        assert(f.transfer(0xFFFFFFE0 - capacity / 2, bytes, 32));
        f.events.clear();
        assert(f.transfer(0xFFFFFFE0, bytes, 32) && f.result.hits == 1 && f.events.empty());
        assert(!memcmp(bytes, f.memory.data() + 0x1FFFE0, 32));
        ++cases;
    }
    {
        f.clear(0); // Unconfigured, disabled L2 is a transparent external path.
        unsigned char bytes[32];
        assert(f.transfer(0x2000, bytes, 32, 0, 0, 0, 16));
        assert(f.events.size() == 4 && !f.result.hits && !f.result.misses);
        assert(!memcmp(bytes, f.memory.data() + 0x2000, 32));
        assert(f.block(0x2000, 1, 0));
        ++cases;
    }
    f.clear(262144);
    unsigned char parity_bytes[32] = {};
    assert(f.transfer(0, parity_bytes, 32, ROMLIB_PPC_MMU_WRITE, 0, 17));
    for (unsigned i = 0; i < 32; ++i) {
        assert(f.bank->parity[i >> 3] == 255);
        f.bank->data[i] ^= 1;
        assert(!f.transfer(0, parity_bytes, 32, 0, 0, 17));
        assert(f.result.fault == ROMLIB_PPC_MMU_BUS && f.result.cause == 0x100000 && f.result.physical == i);
        assert(!f.block(0, ROMLIB_PPC_CACHE750_FLUSH, 17) && f.state(0) == 2);
        f.bank->data[i] ^= 1;
        assert(f.transfer(0, parity_bytes, 32, 0, 0, 17));
        ++cases;
    }
    for (bool hit : {false, true}) {
        f.clear(262144);
        if (hit)
            f.seed(0x2000, 1);
        ROMlib_ppc_l2cache750_observer observer = {
            &f, [](void* opaque, unsigned event, unsigned count) {
                auto& fixture = *static_cast<l2_fixture*>(opaque);
                assert(count == 1 && (event == ROMLIB_PPC_L2CACHE750_HIT || event == ROMLIB_PPC_L2CACHE750_MISS));
                assert(!ROMlib_ppc_l2cache750_invalidate(fixture.bank.get()));
                ROMlib_ppc_l2cache750_result snoop = {};
                assert(!ROMlib_ppc_l2cache750_block(fixture.bank.get(), &fixture.bus, fixture.capacity, 0x2004,
                                                    ROMLIB_PPC_CACHE750_SNOOP, 1, &snoop));
                assert(snoop.fault == ROMLIB_PPC_MMU_RETRY);
                ++fixture.retry_checks;
            }};
        unsigned observed = f.retry_checks;
        assert(bool(ROMlib_ppc_l2cache750_history(f.bank.get(), &f.bus, f.capacity, 0x2000, 0x100, 1, &f.result,
                                                  &observer)) == hit);
        assert(f.retry_checks == observed + 1 && !f.bank->transactions);
        ++cases;
    }
    return cases;
}
