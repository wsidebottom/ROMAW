/*+===================================================================
  File:      mapped-snoop-test.c

  Summary:   Verify physical snoops share data with mapped CPU execution and preserve instruction-cache
             rules.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW mapped CPU and external-master coherence regression.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* External-master snoops share physical data tags with mapped CPU execution. */
#define main snoop_parent_unused
#include "mapped-cache040-test.c"
#undef main
#include <stdlib.h>

static unsigned snoop_helpers;

Syn68kCache040Snoop __real_syn68k_cache040_snoop(Syn68kCache040*, uint32_t, unsigned, unsigned, unsigned, int,
                                                 unsigned char[16]);

Syn68kCache040Snoop __wrap_syn68k_cache040_snoop(Syn68kCache040* c, uint32_t a, unsigned sc, unsigned tt, unsigned n,
                                                 int w, unsigned char d[16])
{
    ++snoop_helpers;
    return __real_syn68k_cache040_snoop(c, a, sc, tt, n, w, d);
}

static Syn68kMappedCPU cpu_setup(unsigned config)
{
    Syn68kMappedCPU cpu = setup(config & 2 ? 2 : config & 1);
    cpu.native = config & 1;
    return cpu;
}

static Syn68kCache040Snoop board(unsigned address, unsigned sc, unsigned n, int writing, unsigned char bytes[16])
{
    CPUState saved = cpu_state;
    Syn68kMMU040 saved_mmu = mmu;
    unsigned attempted = 0;
    unsigned* original_attempted = memory.native ? memory.native->write_attempted : NULL;
    if (memory.native)
        native.write_attempted = &attempted;
    Syn68kCache040Snoop result = syn68k_mmu040_snoop(&memory, address, sc, 0, n, writing, bytes);
    if (memory.native)
        native.write_attempted = original_attempted;
    assert(!memcmp(&saved, &cpu_state, sizeof saved) && !memcmp(&mmu, &saved_mmu, sizeof mmu));
    assert(!attempted);
    return result;
}

static void data_coherence(unsigned config, unsigned disabled)
{
    Syn68kMappedCPU cpu = cpu_setup(config);
    unsigned helpers = snoop_helpers;
    put(0x9000, 4, 0x11223344);
    put(0x8000, 2, 0x2080); /* MOVE.L D0,(A0) */
    put(0x8002, 2, 0x2210); /* MOVE.L (A0),D1 */
    EM_A0 = 0x9000;
    EM_D0 = 0xaabbccdd;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(get(0x9000, 4) == 0x11223344 && resident(0, 0x9000)->dirty == 1);
    uint32_t enabled = cpu_state.cacr;
    if (disabled)
        cpu_state.cacr = 0;
    unsigned char bytes[16] = {0x5e, 0x6f};
    Syn68kCache040Snoop result = board(0x9001, 1, 2, 1, bytes);
    assert(!result.fault && result.hit && result.intervention && !result.invalidated);
    assert(get(0x9000, 4) == 0x11223344 && resident(0, 0x9000)->dirty == 1);
    cpu_state.cacr = enabled;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D1 == 0xaa5e6fdd);
    memset(bytes, 0, 16);
    result = board(0x9000, 1, 4, 0, bytes);
    assert(!result.fault && result.hit && result.intervention && !result.invalidated);
    assert(bytes[0] == 0xaa && bytes[1] == 0x5e && bytes[2] == 0x6f && bytes[3] == 0xdd);
    result = board(0x9004, 2, 16, 0, bytes);
    assert(!result.fault && result.hit && result.intervention && result.invalidated && !resident(0, 0x9000));
    assert(get(0x9000, 4) == 0x11223344);
    /* The board memory controller observes this intervening line transfer
       and updates memory before another caching master can own the line. */
    memcpy(ram + 0x9000, bytes, 16);
    cpu.pc = 0x8002;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D1 == 0xaa5e6fdd && !resident(0, 0x9000)->dirty);
    bytes[0] = 0x77;
    result = board(0x9002, 1, 1, 1, bytes);
    assert(!result.fault && result.hit && !result.intervention && result.invalidated);
    ram[0x9002] = bytes[0]; /* No intervention: memory responds to the write. */
    cpu.pc = 0x8002;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D1 == 0xaa5e77dd && !resident(0, 0x9000)->dirty);
    assert(snoop_helpers - helpers == (config & 2 ? 0u : 4u));
    ++cases;
}

static void instruction_coherence(unsigned config, unsigned control)
{
    Syn68kMappedCPU cpu = cpu_setup(config);
    unsigned helpers = snoop_helpers;
    uint64_t entries = syn68k_native_execution_count();
    put(0x8000, 2, 0x7001); /* MOVEQ #1,D0 */
    put(0x8002, 2, 0x0018); /* Future FABS command word. */
    cpu_state.fpu.null_state = 0;
    cpu_state.fpu.regs[0] = (Syn68kFP80){0xbfff, UINT64_C(0xc000000000000000)};
    put(0x8100, 2, 0xf489); /* CINVL IC,(A1) */
    put(0x8102, 2, 0x4ed1); /* JMP (A1) */
    EM_A1 = 0x8000;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D0 == 1 && resident(1, 0x8000));
    Syn68kCache040Line instruction = *resident(1, 0x8000);
    unsigned char bytes[16] = {0xf2, 0x00, 0x00, 0x18}; /* FABS FP0,FP0 */
    Syn68kCache040Snoop result = board(0x8000, control, 4, 1, bytes);
    unsigned snooped = control == 1 || control == 2;
    assert(!result.fault && result.hit == (snooped ? 2u : 0u) && !result.intervention &&
           result.invalidated == (snooped ? 2u : 0u));
    memcpy(ram + 0x8000, bytes, 4);
    if (snooped)
        assert(!resident(1, 0x8000));
    else
        assert(!memcmp(&instruction, resident(1, 0x8000), sizeof instruction));
    cpu.pc = 0x8000;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D0 == 1 && cpu.pc == (snooped ? 0x8004u : 0x8002u));
    assert(cpu_state.fpu.regs[0].exponent == (snooped ? 0x3fffu : 0xbfffu) &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xc000000000000000));
    cpu.pc = 0x8100;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(cpu.pc == 0x8102 && !resident(1, 0x8000));
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(cpu.pc == 0x8000);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.reason == SYN68K_MAPPED_BUDGET);
    assert(EM_D0 == 1 && cpu.pc == 0x8004 && resident(1, 0x8000));
    assert(cpu_state.fpu.regs[0].exponent == 0x3fff &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xc000000000000000));
    assert(snoop_helpers - helpers == (config & 2 ? 0u : 1u));
    if (config & 1)
        assert(syn68k_native_execution_count() > entries);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t emitted = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned config = 0; config < 4; ++config) {
        for (unsigned disabled = 0; disabled < 2; ++disabled)
            data_coherence(config, disabled);
        for (unsigned control = 0; control < 4; ++control)
            instruction_coherence(config, control);
    }
    emitted = syn68k_native_compile_count() - emitted;
    hits = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && hits : emitted);
    printf("%u mapped snoop/CPU/dirty-data/disabled-cache/instruction-coherence cases passed; emitted=%llu hits=%llu\n",
           cases, (unsigned long long)emitted, (unsigned long long)hits);
    return 0;
}
