/* SPDX-License-Identifier: MIT */
/* Completed FPSP E1 frames retire without a zero-vector replay. */
#define main unused_mmu_fpu_main
#include "mmu040-fpu-test.c"
#undef main
#include "mapped-cpu.h"
#include "native040.h"
#include "jit-x64.h"
#include <stdlib.h>

static void completion_case(unsigned config, unsigned page, unsigned busy, unsigned post, unsigned precision,
                            unsigned rounding, unsigned mask, unsigned command_kind, unsigned flags_kind,
                            unsigned oldstate)
{
    memory.native = NULL;
    memory.cache = NULL;
    fixture(page, 1);
    unsigned size = busy ? 100 : 52, base = physical(0x20040);
    memset(ram + base, 0, size);
    put32(base, busy ? 0x41600000 : 0x41300000);
    ram[base + size - 40] = 0x60;
    static const unsigned commands[] = {0x040e, 0x0401, 0x5c00}; /* FSIN, FINT, FMOVECR. */
    put32(base + size - 36, commands[command_kind] << 16);
    ram[base + size - 28] = 4 | (flags_kind & 1); /* E1, optionally S. */
    ram[base + size - 27] = flags_kind & 2 ? 0x80 : 0; /* Optionally X. */
    put32(base + size - 12, 0x7fff0000);
    put32(base + size - 8, 0xc0000000);
    cpu_state.fpu.fpcr = precision * 0x40 + rounding * 0x10 + (mask & 1 ? 0x4000 : 0);
    cpu_state.fpu.fpsr = 0x08000088 | (mask & 2 ? 0x4000 : 0);
    cpu_state.fpu.fpiar = 0x12345678;
    for (unsigned r = 0; r < 8; ++r)
        cpu_state.fpu.regs[r] = (Syn68kFP80){0x4000 + r, UINT64_C(0x8000000000000000) + r};
    cpu_state.fpu.null_state = oldstate == 0;
    if (oldstate == 2) {
        cpu_state.fpu.frame_size = 52;
        cpu_state.fpu.pending_vector = 55;
        memset(cpu_state.fpu.frame, 0x69, sizeof cpu_state.fpu.frame);
    }
    Syn68kFPUState before = cpu_state.fpu;
    EM_A0 = 0x20040;
    EM_A1 = 0x20100;
    code_word(0x10000, post ? 0xf358 : 0xf350);
    code_word(0x10002, 0xf280); /* FNOP. */
    code_word(0x10004, 0);
    code_word(0x10006, 0xf311); /* FSAVE (A1). */
    memset(ram + physical(0x20100), 0xa5, 100);
    Syn68kMMU040Native native;
    if (config & 2)
        assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = config & 1};
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
    assert(EM_A0 == 0x20040 + post * size && EM_A1 == 0x20100);
    assert(!memcmp(before.regs, cpu_state.fpu.regs, sizeof before.regs));
    assert(before.fpcr == cpu_state.fpu.fpcr && before.fpsr == cpu_state.fpu.fpsr &&
           before.fpiar == cpu_state.fpu.fpiar && !cpu_state.fpu.null_state);
    unsigned expected_vector = flags_kind == 3 ? mask == 3 ? 54 : 0 : 11;
    if (cpu_state.fpu.pending_vector != expected_vector || cpu_state.fpu.frame_size != (expected_vector ? size : 0)) {
        fprintf(stderr, "FPSP completion config=%u page=%u busy=%u post=%u precision=%u rounding=%u mask=%u "
                        "command=%u flags=%u old=%u frame=%u pending=%u\n",
                config, page, busy, post, precision, rounding, mask, command_kind, flags_kind, oldstate,
                cpu_state.fpu.frame_size, cpu_state.fpu.pending_vector);
        abort();
    }
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    if (expected_vector) {
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == expected_vector && cpu.pc == 0x10002);
    }
    else {
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10006);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10008);
        unsigned address = physical(0x20100);
        assert(ram[address] == 0x41 && !ram[address + 1] && !ram[address + 2] && !ram[address + 3]);
        for (unsigned i = 4; i < 100; ++i)
            assert(ram[address + i] == 0xa5);
    }
    assert(!memcmp(before.regs, cpu_state.fpu.regs, sizeof before.regs));
    assert(before.fpcr == cpu_state.fpu.fpcr && before.fpsr == cpu_state.fpu.fpsr &&
           before.fpiar == cpu_state.fpu.fpiar);
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    unsigned count = 0;
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned busy = 0; busy < 2; ++busy)
                for (unsigned post = 0; post < 2; ++post)
                    for (unsigned precision = 0; precision < 3; ++precision)
                        for (unsigned rounding = 0; rounding < 4; ++rounding)
                            for (unsigned mask = 0; mask < 4; ++mask)
                                for (unsigned command = 0; command < 3; ++command)
                                    for (unsigned flags = 0; flags < 4; ++flags)
                                        for (unsigned oldstate = 0; oldstate < 3; ++oldstate) {
                                            completion_case(config, page, busy, post, precision, rounding,
                                                            mask, command, flags, oldstate);
                                            ++count;
                                        }
    uint64_t emitted = syn68k_native_compile_count() - compiled, loaded = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && loaded : emitted);
    printf("%u FPSP-completed E1/mask/flags/idle/next-instruction/save cases passed; emitted=%llu hits=%llu\n",
           count, (unsigned long long)emitted, (unsigned long long)loaded);
    return 0;
}
