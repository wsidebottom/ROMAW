/*+===================================================================
  File:      thread_scheduler_test.cpp

  Summary:   Exercise real Windows fiber scheduling, guest results, critical sections and cancellation.

  Classes:   Declared: Fixture; Used: ROMlib_ppc_threads, ROMlib_ppc_thread_environment, ROMlib_ppc_registers.

  Functions: main

  Origin:    Original isolated cooperative-thread regression fixture; no external application is launched.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "carbon_threads.h"
#include <cassert>
#include <cfenv>
#include <cstdlib>
#include <cstring>
#include <cstdio>

struct Fixture {
    ROMlib_ppc_threads* scheduler = nullptr;
    ROMlib_ppc_thread_environment env;
    unsigned char bytes[4096] = {};
    unsigned blocks = 0, phase = 0, entries = 0;
    bool allocation_failure = false;

    Fixture() : env{this, memory, allocate, release, resolve, execute, switch_context, exiting, nullptr, nullptr}
    {
    }

    ~Fixture()
    {
        ROMlib_ppc_threads_destroy(scheduler);
        assert(!blocks);
    }

    static void* memory(void* opaque, uint32_t address, size_t length, int)
    {
        auto& fixture = *static_cast<Fixture*>(opaque);
        return address <= sizeof fixture.bytes && length <= sizeof fixture.bytes - address ? fixture.bytes + address
                                                                                           : nullptr;
    }

    static void* allocate(void* opaque, uint32_t size, uint32_t, uint32_t* address)
    {
        auto& fixture = *static_cast<Fixture*>(opaque);
        if (fixture.allocation_failure)
            return nullptr;
        void* data = std::malloc(size);
        if (data) {
            *address = 0x100000 + fixture.blocks * 0x20000;
            ++fixture.blocks;
        }
        return data;
    }

    static void release(void* opaque, void* memory)
    {
        auto& fixture = *static_cast<Fixture*>(opaque);
        assert(fixture.blocks);
        --fixture.blocks;
        std::free(memory);
    }

    static uint32_t resolve(void*, uint32_t address)
    {
        return address == 0x9000 ? address : 0;
    }

    static void switch_context(void*, void (*function)(void*), void* target)
    {
        function(target);
    }

    static int exiting(void*)
    {
        return 0;
    }

    uint32_t word(unsigned offset)
    {
        return uint32_t(bytes[offset]) << 24 | uint32_t(bytes[offset + 1]) << 16 | bytes[offset + 2] << 8 |
               bytes[offset + 3];
    }

    int call(const char* name, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
    {
        ROMlib_ppc_registers r = {};
        r.gpr[3] = a;
        r.gpr[4] = b;
        r.gpr[5] = c;
        int handled = ROMlib_ppc_threads_service(&scheduler, &env, name, &r, 1);
        assert(handled);
        return handled == 2 ? 1 : int32_t(r.gpr[3]);
    }

    int create(uint32_t mode, uint32_t options = 4, uint32_t style = 1)
    {
        ROMlib_ppc_registers r = {};
        r.gpr[3] = style;
        r.gpr[4] = 0x9000;
        r.gpr[5] = mode;
        r.gpr[6] = 65536;
        r.gpr[7] = options;
        r.gpr[8] = 0x104;
        r.gpr[9] = 0x108;
        assert(ROMlib_ppc_threads_service(&scheduler, &env, "NewThread", &r, 1) == 1);
        return int32_t(r.gpr[3]);
    }

    static int execute(void* opaque, uint32_t entry, uint32_t mode, uint32_t stack, uint32_t* result)
    {
        auto& f = *static_cast<Fixture*>(opaque);
        assert(entry == 0x9000 && stack >= 0x100000 && !(stack & 15));
        ++f.entries;
        assert(!f.call("GetCurrentThread", 0x110) && f.word(0x110) >= 3);
        f.phase = 1;
        std::fesetround(FE_UPWARD);
        if (mode == 1) {
            assert(f.call("SetThreadState", 1, 1, 2) == 1); // cancelled during stop
            return 1;
        }
        if (mode == 2) {
            assert(f.call("DisposeThread", 1) == 1);
            return 1;
        }
        int status = f.call("YieldToAnyThread");
        if (status == 1) // application cleanup cancels this paused entry
            return 1;
        assert(!status && std::fegetround() == FE_UPWARD);
        f.phase = 2;
        *result = 0x12345678;
        return 1;
    }
};

int main()
{
    assert(!IsThreadAFiber());
    int original_rounding = std::fegetround();
    {
        Fixture f;
        assert(!f.call("GetCurrentThread", 0x100) && f.word(0x100) == 2);
        f.allocation_failure = true;
        assert(f.create(0) == -108 && !f.word(0x108) && !f.blocks);
        f.allocation_failure = false;
        assert(f.create(0, 4, 2) == -50 && !f.word(0x108));
        assert(!f.create(0, 5));
        uint32_t id = f.word(0x108);
        assert(id >= 3 && !f.phase);
        assert(!f.call("GetThreadState", id, 0x100) && f.bytes[0x101] == 1);
        assert(!f.call("YieldToAnyThread") && !f.phase);
        assert(!f.call("SetThreadState", id, 0));
        std::fesetround(FE_DOWNWARD);
        assert(!f.call("YieldToAnyThread") && f.phase == 1 && std::fegetround() == FE_DOWNWARD);
        assert(!f.call("ThreadBeginCritical"));
        assert(f.call("YieldToThread", id) == -619 && f.phase == 1);
        assert(!f.call("ThreadEndCritical"));
        assert(!f.call("YieldToThread", id) && f.phase == 2 && f.word(0x104) == 0x12345678);
        assert(f.call("GetThreadState", id, 0x100) == -618 && !f.blocks);
        assert(f.call("ThreadEndCritical") == -619);
        assert(f.call("DisposeThread", 2) == -619);
        f.phase = 0;
        assert(!f.create(1));
        id = f.word(0x108);
        assert(!f.call("YieldToAnyThread") && f.phase == 1);
        assert(!f.call("GetThreadState", id, 0x100) && f.bytes[0x101] == 1);
        assert(!f.call("DisposeThread", id, 0xabcdef) && f.word(0x104) == 0xabcdef && f.phase == 1 && !f.blocks);
        assert(!f.create(2));
        assert(!f.call("YieldToAnyThread") && !f.blocks);
        assert(!f.create(0, 5)); // dispose a never-started fiber
        assert(!f.call("DisposeThread", f.word(0x108)) && !f.blocks);
        assert(!f.create(0));
        assert(!f.call("YieldToAnyThread") && f.phase == 1); // destructor unwinds paused thread
    }
    assert(!IsThreadAFiber());
    void* external = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    assert(external);
    {
        Fixture f;
        assert(!f.create(0, 5));
    }
    assert(IsThreadAFiber() && GetCurrentFiber() == external);
    assert(ConvertFiberToThread());
    std::fesetround(original_rounding);
    std::puts(
        "Cooperative threads: independent stacks/results, ready/stopped states, critical sections, FP contexts, cancellation and borrowed fibers passed");
}
