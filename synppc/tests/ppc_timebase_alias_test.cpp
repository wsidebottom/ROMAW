/*+===================================================================
  File:      ppc_timebase_alias_test.cpp

  Summary:   Verify legacy MFSPR/MFTB time-base aliases with a deterministic clock and retained native kernels.

  Classes:   fixture; uses ROMlib_ppc_registers, ROMlib_ppc_environment and std::unique_ptr.

  Functions: __wrap__Z13GetTicks_usecv, fixture::fixture, fixture::memory, main

  Origin:    Original PowerPC instruction tests; GNU ld wraps the interpreter's GetTicks_usec clock symbol.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/
#include "synppc/ppc.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <initializer_list>

static uint64_t microseconds;
static unsigned clock_reads;

extern "C" uint64_t __wrap__Z13GetTicks_usecv(void)
{
    ++clock_reads;
    return microseconds;
}

struct fixture {
    unsigned char code[20];

    fixture()
    {
        auto read = [](unsigned spr, unsigned r) {
            return 0x7C0002A6 | r << 21 | (spr & 31) << 16 | (spr & 0x3E0) << 6;
        };
        uint32_t words[] = {read(268, 3), read(268, 4) | 0x40, read(269, 5), read(269, 6) | 0x40, 0x18000000};
        for (unsigned w = 0; w < 5; ++w)
            for (unsigned b = 0; b < 4; ++b)
                code[w * 4 + b] = words[w] >> (24 - b * 8);
    }

    static void* memory(void* opaque, uint32_t address, size_t length, int writing)
    {
        auto& self = *static_cast<fixture*>(opaque);
        uint32_t relative = address - 0x1000;
        return !writing && relative <= sizeof self.code && length <= sizeof self.code - relative ? self.code + relative
                                                                                                 : nullptr;
    }
};

int main(void)
{
    unsigned cases = 0;
    for (unsigned flags : {0u, unsigned(ROMLIB_PPC_EXEC_JIT)}) {
        fixture f;
        std::unique_ptr<ROMlib_ppc_translation_cache, decltype(&ROMlib_ppc_translation_cache_destroy)> cache(
            ROMlib_ppc_translation_cache_create(), ROMlib_ppc_translation_cache_destroy);
        assert(cache || !ROMlib_ppc_jit_available());
        if (flags && ROMlib_ppc_jit_available())
            assert(ROMlib_ppc_translate_range(cache.get(), f.code, 0x1000, 16) == 4);
        for (uint64_t sample :
             {UINT64_C(0), UINT64_C(1), UINT64_C(0x123456789), UINT64_C(0x123456789ABC), UINT64_MAX / 25}) {
            ROMlib_ppc_registers r = {};
            r.pc = 0x1000;
            ROMlib_ppc_environment env = {};
            env.opaque = &f;
            env.memory = fixture::memory;
            env.linear_memory = f.code;
            env.linear_address = 0x1000;
            env.linear_size = sizeof f.code;
            env.execution_flags = flags;
            env.translation_cache = cache.get();
            env.instruction_limit = 100;
            microseconds = sample;
            clock_reads = 0;
            assert(ROMlib_ppc_execute(&r, &env));
            uint64_t ticks = sample * 25;
            assert(r.gpr[3] == uint32_t(ticks) && r.gpr[4] == uint32_t(ticks));
            assert(r.gpr[5] == uint32_t(ticks >> 32) && r.gpr[6] == uint32_t(ticks >> 32));
            assert(clock_reads == 4 && env.instructions == 5 && r.pc == 0x1014);
            if (flags && ROMlib_ppc_jit_available()) {
                assert(env.native_kernel_instructions == 4);
                assert(!env.native_compilations && env.native_cache_hits == 4);
            }
            ++cases;
        }
    }
    printf("%u legacy time-base alias programs passed with fresh clock values and retained kernels\n", cases);
}
