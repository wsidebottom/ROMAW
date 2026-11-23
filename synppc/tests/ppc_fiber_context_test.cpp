/*+===================================================================
  File:      ppc_fiber_context_test.cpp

  Summary:   Verify PPC runtime scopes belong to their Windows fiber, including workers outliving a launching CPU.

  Classes:   Fixture.

  Functions: main.

  Origin:    Original ROMAW regression for cooperative CPU context lifetime.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "synppc/ppc.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

extern ROMlib_ppc_environment* romaw_ppc_environment;

struct Fixture {
    unsigned char ram[4096] = {};
    void *main_fiber = nullptr, *worker = nullptr;
    ROMlib_ppc_translation_cache* cache = nullptr;
    uint32_t flags;
    unsigned phase = 0;

    explicit Fixture(uint32_t mode) : flags(mode)
    {
        main_fiber = ConvertThreadToFiber(nullptr);
        assert(main_fiber);
        assert(!flags || ROMlib_ppc_jit_available());
        cache = ROMlib_ppc_translation_cache_create();
        assert(cache);
        worker = CreateFiber(0, start, this);
        assert(worker);
        // Main and worker each yield through a host service, then finish.
        const uint32_t words[] = {0x18000001, 0x38601234, 0x18000000, 0x18000002, 0x38605678, 0x18000000};
        for (unsigned i = 0; i < sizeof words / sizeof *words; ++i)
            for (unsigned byte = 0; byte < 4; ++byte)
                ram[256 + i * 4 + byte] = words[i] >> (24 - byte * 8);
        if (flags)
            assert(ROMlib_ppc_translate_range(cache, ram + 256, 256, sizeof words));
    }

    ~Fixture()
    {
        DeleteFiber(worker);
        assert(ConvertFiberToThread());
        ROMlib_ppc_translation_cache_destroy(cache);
    }

    static void raw_switch(void* fiber)
    {
        SwitchToFiber(fiber);
    }

    static void* memory(void* opaque, uint32_t address, size_t size, int)
    {
        auto& f = *static_cast<Fixture*>(opaque);
        return address <= sizeof f.ram && size <= sizeof f.ram - address ? f.ram + address : nullptr;
    }

    static int service(void* opaque, uint32_t id, ROMlib_ppc_registers*)
    {
        auto& f = *static_cast<Fixture*>(opaque);
        assert(romaw_ppc_environment && romaw_ppc_environment->opaque == &f);
        assert((id == 1 && f.phase == 0) || (id == 2 && f.phase == 1));
        f.phase = id;
        ROMlib_ppc_environment* saved = romaw_ppc_environment;
        ROMlib_ppc_switch_host(raw_switch, id == 1 ? f.worker : f.main_fiber);
        assert(romaw_ppc_environment == saved);
        return 1;
    }

    void execute(uint32_t pc, uint32_t expected)
    {
        ROMlib_ppc_environment env = {};
        env.opaque = this;
        env.memory = memory;
        env.service = service;
        env.instruction_limit = 100;
        env.linear_memory = ram;
        env.linear_size = sizeof ram;
        env.execution_flags = flags;
        env.translation_cache = cache;
        ROMlib_ppc_registers r = {};
        r.pc = pc;
        r.lr = pc + 4;
        assert(ROMlib_ppc_execute(&r, &env));
        assert(r.gpr[3] == expected);
        assert(flags ? env.native_instructions > 0 : env.native_instructions == 0);
    }

    static VOID WINAPI start(void* opaque)
    {
        auto& f = *static_cast<Fixture*>(opaque);
        // A fresh fiber must not inherit a previous CPU or its host stack.
        assert(!romaw_ppc_environment);
        f.execute(268, 0x5678);
        assert(!romaw_ppc_environment);
        f.phase = 3;
        ROMlib_ppc_switch_host(raw_switch, f.main_fiber);
        std::abort();
    }

    void run()
    {
        execute(256, 0x1234);
        assert(phase == 2 && !romaw_ppc_environment);
        // The main CPU is now destroyed; finish the suspended worker anyway.
        ROMlib_ppc_switch_host(raw_switch, worker);
        assert(phase == 3 && !romaw_ppc_environment);
    }
};

int main()
{
    for (uint32_t flags : {uint32_t(0), uint32_t(ROMLIB_PPC_EXEC_JIT)}) {
        Fixture fixture(flags);
        fixture.run();
    }
    std::puts("PASS: interpreter/JIT fiber scopes survive launching CPU destruction");
    return 0;
}
