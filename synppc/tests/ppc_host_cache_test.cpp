/*+===================================================================
  File:      ppc_host_cache_test.cpp

  Summary:   Verify coherent host callbacks, nested CPU checkpoints and failed 750 data-cache boundaries.

  Classes:   fixture; uses ROMlib_ppc_environment, ROMlib_ppc_system750 and ROMlib_ppc_physical_bus.

  Functions: main

  Origin:    Original CPU/host-memory integration tests using original PowerPC instruction encodings.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/bus750.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

struct fixture {
    std::unique_ptr<ROMlib_ppc_system750> state{new ROMlib_ppc_system750{}};
    std::vector<unsigned char> ram = std::vector<unsigned char>(0x20000);
    ROMlib_ppc_registers r = {};
    ROMlib_ppc_environment env = {};
    ROMlib_ppc_physical_bus bus = {this, transfer, history};
    unsigned calls = 0, transfers = 0;
    uint32_t fail = UINT32_MAX, throw_bus = UINT32_MAX;
    bool nested = false, nested_throw = false, nested_private = false, throwing = false, fail_after = false,
         reenter_sync = false;
    unsigned retries = 0;

    fixture(unsigned flags, unsigned hid0)
    {
        state->hid0 = hid0;
        state->mmu.msr = 0x1000; // ME: failed copybacks enter a guest machine-check handler.
        env.opaque = this;
        env.system = state.get();
        env.physical_bus = &bus;
        env.execution_flags = flags;
        env.service = service;
        env.system_service_gate = [](void*, uint32_t pc, uint32_t op) -> int {
            return pc >= 0x1000 && pc < 0x1100 && (op == 0x18000000 || op == 0x18000001);
        };
        r.pc = 0x1000;
        r.gpr[3] = 0x11223344;
        r.gpr[4] = 0x2000;
        r.lr = 0x1008;
    }

    uint32_t word(uint32_t a) const
    {
        return uint32_t(ram[a]) << 24 | uint32_t(ram[a + 1]) << 16 | uint32_t(ram[a + 2]) << 8 | ram[a + 3];
    }

    void word(uint32_t a, uint32_t value)
    {
        for (unsigned i = 0; i < 4; ++i)
            ram[a + i] = value >> (24 - i * 8);
    }

    static int transfer(void* opaque, uint32_t a, void* bytes, unsigned size, unsigned access, unsigned,
                        uint32_t* cause)
    {
        auto& f = *static_cast<fixture*>(opaque);
        ++f.transfers;
        if (a == f.throw_bus && (access & ROMLIB_PPC_MMU_WRITE))
            throw std::runtime_error("physical callback fixture");
        if (f.reenter_sync) {
            unsigned before = f.transfers;
            ROMlib_ppc_bus_result result = {};
            assert(!ROMlib_ppc_750_host_sync(f.state.get(), &f.bus, &result));
            assert(result.mapping.fault == ROMLIB_PPC_MMU_RETRY && !result.mapping.vector && !result.mapping.cause);
            assert(!f.state->bus_check_pending && f.transfers == before);
            ++f.retries;
        }
        if ((a == f.fail && (access & ROMLIB_PPC_MMU_WRITE)) || uint64_t(a) + size > f.ram.size()) {
            *cause = 0x20000;
            return 0;
        }
        if (access & ROMLIB_PPC_MMU_WRITE)
            memcpy(f.ram.data() + a, bytes, size);
        else
            memcpy(bytes, f.ram.data() + a, size);
        return 1;
    }

    static int history(void*, uint32_t, uint32_t, uint32_t*)
    {
        return 1;
    }

    void empty() const
    {
        if (!(state->hid0 & 0x4000))
            return;
        assert(!state->cache[1].transactions);
        for (const auto& set : state->cache[1].lines)
            for (const auto& line : set)
                assert(!line.state);
    }

    void callback()
    {
        ++calls;
        empty();
        assert(word(0x2000) == 0x11223344);
        word(0x2004, 0xAABBCCDD);
        word(0x4000, 0x38600002); // Host code publication must not invalidate an old I-cache line.
        if (nested) {
            auto child = env;
            std::unique_ptr<ROMlib_ppc_system750> private_state;
            if (nested_private) {
                private_state.reset(new ROMlib_ppc_system750{});
                private_state->hid0 = 0x4000;
                private_state->mmu.msr = 0x1000;
                child.system = private_state.get();
            }
            ROMlib_ppc_registers registers = {};
            registers.pc = 0x3000;
            registers.gpr[4] = 0x2008;
            registers.gpr[9] = 0x55667788;
            child.instruction_limit = 1;
            word(0x3000, 0x91240000); // stw r9,0(r4)
            word(0x3004, 0x81440000); // lwz r10,0(r4), publish before the first I-cache fill.
            if (nested_throw)
                throw_bus = 0x2000; // Nested store shares the parent's now-invalidated data line.
            assert(!ROMlib_ppc_execute(&registers, &child));
            if (nested_throw) {
                assert(strstr(child.error, "host-memory callback failed"));
                throw_bus = UINT32_MAX;
                ROMlib_ppc_bus_result result = {};
                assert(ROMlib_ppc_750_host_sync(state.get(), &bus, &result));
            }
            else
                assert(strstr(child.error, "instruction limit"));
            assert(word(0x2008) == 0x55667788);
            empty();
            word(0x2008, 0x99887766);
            registers.pc = 0x3004;
            assert(!ROMlib_ppc_execute(&registers, &child));
            assert(registers.gpr[10] == 0x99887766);
            empty();
        }
        if (nested) {
            ROMlib_ppc_bus_result result = {};
            assert(ROMlib_ppc_750_write(state.get(), &bus, 0x200C, 4, 0x12345678, &result));
            ROMlib_ppc_switch_host(
                [](void* opaque) {
                    auto& f = *static_cast<fixture*>(opaque);
                    assert(f.word(0x200C) == 0x12345678);
                    f.word(0x200C, 0x98765432);
                },
                this);
            uint64_t value = 0;
            assert(ROMlib_ppc_750_read(state.get(), &bus, 0x200C, 4, 0, &value, &result) && value == 0x98765432);
        }
        if (fail_after) {
            ROMlib_ppc_bus_result result = {};
            assert(ROMlib_ppc_750_write(state.get(), &bus, 0x2040, 4, 0x12345678, &result));
            fail = 0x2040;
        }
        if (throwing)
            throw std::runtime_error("host callback fixture");
    }

    static int service(void* opaque, uint32_t id, ROMlib_ppc_registers*)
    {
        assert(id == 1);
        static_cast<fixture*>(opaque)->callback();
        return 1;
    }

    void checkpoint(unsigned limit)
    {
        env.instruction_limit = limit;
        assert(!ROMlib_ppc_execute(&r, &env));
        assert(strstr(env.error, "instruction limit"));
    }
};

int main(void)
{
    unsigned cases = 0;
    for (unsigned flags : {0u, unsigned(ROMLIB_PPC_EXEC_JIT)}) {
        for (unsigned hid0 : {0u, 0x4000u, 0xC000u, 0x5000u, 0xD040u})
            for (bool nested : {false, true}) {
                fixture f(flags, hid0);
                f.nested = nested;
                f.word(0x1000, 0x90640000); // stw r3,0(r4)
                f.word(0x1004, 0x18000001); // trusted service
                f.word(0x1008, 0x80A40004); // lwz r5,4(r4)
                f.word(0x100C, 0x18000000); // trusted host return
                f.word(0x4000, 0x38600001);
                uint64_t instruction = 0;
                ROMlib_ppc_bus_result result = {};
                assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, 0x4000, 4, ROMLIB_PPC_MMU_INSTRUCTION, &instruction,
                                           &result));
                f.env.instruction_limit = 16;
                assert(ROMlib_ppc_execute(&f.r, &f.env));
                assert(f.calls == 1 && f.r.gpr[5] == 0xAABBCCDD && f.r.pc == 0x1010);
                f.empty();
                assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, 0x4000, 4, ROMLIB_PPC_MMU_INSTRUCTION, &instruction,
                                           &result));
                assert(instruction == ((hid0 & 0x8000) ? 0x38600001u : 0x38600002u));
                ROMlib_ppc_cache750_invalidate_set(&f.state->cache[0], 0x4000);
                assert(ROMlib_ppc_750_read(f.state.get(), &f.bus, 0x4000, 4, ROMLIB_PPC_MMU_INSTRUCTION, &instruction,
                                           &result) &&
                       instruction == 0x38600002);
                ++cases;
            }
        {
            fixture f(flags, 0x4000);
            f.nested = f.nested_throw = true;
            f.word(0x1000, 0x90640000);
            f.word(0x1004, 0x18000001);
            f.word(0x1008, 0x18000000);
            f.env.instruction_limit = 8;
            assert(ROMlib_ppc_execute(&f.r, &f.env) && f.calls == 1);
            f.empty();
            ++cases;
        }
        {
            fixture f(flags, 0xC000);
            f.nested = f.nested_private = true;
            f.word(0x1000, 0x90640000);
            f.word(0x1004, 0x18000001);
            f.word(0x1008, 0x18000000);
            f.env.instruction_limit = 8;
            assert(ROMlib_ppc_execute(&f.r, &f.env) && f.calls == 1);
            f.empty();
            ++cases;
        }
        for (bool failed : {false, true}) {
            fixture f(flags, 0x4000);
            f.word(0x1000, 0x90640000);
            f.word(0x1004, 0x48000000); // loop until poll boundary
            f.env.poll = [](void* opaque) { static_cast<fixture*>(opaque)->callback(); };
            if (failed)
                f.fail = 0x2000;
            f.checkpoint(1024);
            if (failed) {
                assert(!f.calls && f.r.pc == 0x200 && f.state->srr0 == 0x1004 && !f.state->bus_check_pending);
            }
            else {
                assert(f.calls == 1 && f.word(0x2004) == 0xAABBCCDD);
                f.empty();
            }
            ++cases;
        }
        for (bool after : {false, true}) {
            fixture f(flags, 0x4000);
            f.word(0x1000, 0x90640000);
            f.word(0x1004, 0x18000001);
            f.word(0x1008, 0x80A40004);
            f.fail_after = after;
            if (!after)
                f.fail = 0x2000;
            f.checkpoint(2);
            assert(f.calls == unsigned(after) && f.r.pc == 0x200 && f.state->srr0 == 0x1004);
            assert((f.state->srr1 & 0x20000) && !f.state->bus_check_pending && !f.r.gpr[5]);
            ++cases;
        }
        {
            fixture f(flags, 0x4000);
            f.throwing = true;
            f.word(0x1000, 0x90640000);
            f.word(0x1004, 0x18000001);
            f.env.instruction_limit = 4;
            assert(!ROMlib_ppc_execute(&f.r, &f.env) && strstr(f.env.error, "host callback fixture"));
            assert(f.calls == 1 && f.word(0x2004) == 0xAABBCCDD);
            f.empty();
            ++cases;
        }
        {
            fixture f(flags, 0x4000);
            f.word(0x1000, 0x90640000);
            f.checkpoint(1);
            assert(!f.word(0x2000)); // A plain CPU checkpoint is not a host-memory boundary.
            ROMlib_ppc_bus_result result = {};
            f.state->reservation_valid = 1;
            assert(ROMlib_ppc_750_host_sync(f.state.get(), &f.bus, &result));
            assert(f.word(0x2000) == 0x11223344 && !f.state->reservation_valid);
            f.empty();
            ++cases;
        }
    }
    for (unsigned control : {0u, 0x4000u, 0x5000u}) {
        fixture f(0, control);
        f.reenter_sync = control != 0;
        for (unsigned set = 0; set < 128; ++set)
            for (unsigned way = 0; way < 8; ++way) {
                auto& line = f.state->cache[1].lines[set][way];
                line.address = (way << 12) | (set << 5);
                line.state = 2;
                memset(line.data, 0xA5, 32);
                f.state->cache[0].lines[set][way] = line;
            }
        f.state->reservation_valid = 1;
        ROMlib_ppc_bus_result result = {};
        assert(ROMlib_ppc_750_host_sync(f.state.get(), &f.bus, &result));
        assert(!f.state->reservation_valid && !result.mapping.fault);
        assert(f.transfers == (control ? 4096u : 0u) && f.retries == f.transfers);
        for (unsigned set = 0; set < 128; ++set)
            for (unsigned way = 0; way < 8; ++way) {
                const auto& line = f.state->cache[1].lines[set][way];
                assert(line.state == (control ? 0 : 2));
                assert(f.state->cache[0].lines[set][way].state == 2);
                for (unsigned byte = 0; byte < 32; ++byte)
                    assert(f.ram[line.address + byte] == (control ? 0xA5 : 0));
            }
        ++cases;
    }
    for (unsigned beat = 0; beat < 4; ++beat) {
        fixture f(0, 0x4000);
        ROMlib_ppc_bus_result result = {};
        assert(ROMlib_ppc_750_write(f.state.get(), &f.bus, 0x2000, 4, 0xAABBCCDD, &result));
        f.fail = 0x2000 + beat * 8;
        assert(!ROMlib_ppc_750_host_sync(f.state.get(), &f.bus, &result));
        assert(result.mapping.vector == 0x200 && result.mapping.cause == 0x20000 && result.mapping.physical == f.fail &&
               f.state->bus_check_pending);
        assert(!f.state->cache[1].transactions);
        f.fail = UINT32_MAX;
        assert(ROMlib_ppc_750_host_sync(f.state.get(), &f.bus, &result));
        assert(f.word(0x2000) == 0xAABBCCDD && f.state->bus_check_pending); // Retry cannot retract TEA.
        f.empty();
        ++cases;
    }
    printf("%u PowerPC 750 host-cache callback/nesting/poll/failure cases passed\n", cases);
}
