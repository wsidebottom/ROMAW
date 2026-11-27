/* SPDX-License-Identifier: MIT */
/* E3 writeback resolves before an E1 move-out stage can retire. */
#define main unused_mmu_fpu_main
#include "mmu040-fpu-test.c"
#undef main
#include "fpu-captured.h"

static int require_direct_restore;
int __real_syn68k_fpu_finish_frame(const Syn68kFPUFrame*, Syn68kMMU040Memory*, Syn68kFPUStep*);
int __wrap_syn68k_fpu_finish_frame(const Syn68kFPUFrame* frame, Syn68kMMU040Memory* memory, Syn68kFPUStep* result)
{
    assert(!require_direct_restore);
    return __real_syn68k_fpu_finish_frame(frame, memory, result);
}
#include "mapped-cpu.h"
#include "native040.h"
#include "jit-x64.h"
#include <stdlib.h>

static void resume_case(unsigned config, unsigned page, unsigned post, unsigned precision, unsigned rounding,
                        unsigned format, unsigned destination, unsigned stage, unsigned old_null, unsigned status_kind)
{
    memory.native = NULL;
    memory.cache = NULL;
    fixture(page, 1);
    unsigned base = physical(0x20040), stages = stage == 0 ? 2 : stage == 2 ? 6 : 4;
    unsigned size = stage == 3 ? 52 : 100;
    memset(ram + base, 0, size);
    put32(base, size == 52 ? 0x41300000 : 0x41600000);
    if (stages & 2) {
        put32(base + 0x18, 0x3fff0000);
        put32(base + 0x1c, old_null ? 0x80000040 : 0x80000000);
        ram[base + 0x3c] = old_null ? 2 : 0; /* A half extended ULP beyond 1 + 2^-25. */
        put32(base + 0x34, (0x12u | destination << 7) << 16); /* Older FADD. */
    }
    put32(base + size - 36, (0x6000u | format << 10) << 16);
    put32(base + size - 28, stages << 24);
    put32(base + size - 12, 0x3fff0000);
    put32(base + size - 8, 0x80000000);
    static const unsigned status[] = {0, 0x4000, 0x4000, 0x200, 0x1000};
    unsigned enables = status_kind == 1 ? 0x4000 : status_kind == 3 ? 0x200 : 0;
    cpu_state.fpu.fpcr = precision * 0x40 + rounding * 0x10 + enables;
    cpu_state.fpu.fpsr = 0x08000088 | status[status_kind];
    cpu_state.fpu.fpiar = 0x24681358;
    cpu_state.fpu.null_state = old_null;
    for (unsigned r = 0; r < 8; ++r)
        cpu_state.fpu.regs[r] = (Syn68kFP80){0x4000 + r, UINT64_C(0x8000000000000000) + r};
    Syn68kFPUState before = cpu_state.fpu;
    EM_A0 = 0x20040;
    EM_A1 = 0x20100;
    code_word(0x10000, post ? 0xf358 : 0xf350);
    code_word(0x10002, 0xf280);
    code_word(0x10004, 0);
    code_word(0x10006, 0xf311);
    unsigned output = physical(0x20100);
    memset(ram + output, 0xa5, 100);
    Syn68kMMU040Native native;
    if (config & 2)
        assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
    Syn68kMappedCPU cpu = {.pc=0x10000, .memory=&memory, .native=config & 1};
    Syn68kMappedRun run;
    require_direct_restore = config & 1;
    uint64 executions = syn68k_native_execution_count();
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    if (require_direct_restore)
        assert(syn68k_native_execution_count() > executions);
    require_direct_restore = 0;
    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002);
    assert(EM_A0 == 0x20040 + size * post && EM_A1 == 0x20100);
    unsigned packed = size == 100 && (format == 3 || format == 7);
    unsigned integer = format == 0 || format == 4 || format == 6;
    unsigned older_pending = (stages & 2) ? status_kind == 3 ? 49 : status_kind == 4 ? 53 : 0 : 0;
    unsigned conversion_pending = packed ? 55 : status_kind == 1 || (status_kind == 2 && integer) ? 54
                                               : status_kind == 3 ? 49 : status_kind == 4 ? 53 : 0;
    unsigned pending = older_pending ? older_pending : (stages & 4) ? conversion_pending : 0;
    if (cpu_state.fpu.frame_size != (pending ? size : 0) || cpu_state.fpu.pending_vector != pending) {
        fprintf(stderr, "Move-out resume config=%u page=%u post=%u precision=%u rounding=%u format=%u "
                        "destination=%u stages=%u null=%u status=%u frame=%u pending=%u\n",
                config,page,post,precision,rounding,format,destination,stages,old_null,status_kind,
                cpu_state.fpu.frame_size,cpu_state.fpu.pending_vector);
        abort();
    }
    if ((stages & 2) && !older_pending) {
        uint64_t significand = UINT64_C(0x8000000000000000);
        if (old_null) {
            if (precision != 1)
                significand |= UINT64_C(1) << 38;
            if (rounding == 3)
                significand += UINT64_C(1) << (precision == 1 ? 40 : precision == 2 ? 11 : 0);
        }
        before.regs[destination] = (Syn68kFP80){0x3fff, significand};
    }
    assert(!memcmp(before.regs, cpu_state.fpu.regs, sizeof before.regs));
    assert(cpu_state.fpu.fpcr == before.fpcr && cpu_state.fpu.fpsr == before.fpsr &&
           cpu_state.fpu.fpiar == before.fpiar && !cpu_state.fpu.null_state);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    if (pending)
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == pending && cpu.pc == 0x10002);
    else {
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10006);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10008);
        assert(ram[output] == 0x41 && !ram[output+1] && !ram[output+2] && !ram[output+3]);
        for (unsigned i = 4; i < 100; ++i)
            assert(ram[output+i] == 0xa5);
    }
    assert(!memcmp(before.regs, cpu_state.fpu.regs, sizeof before.regs));
    assert(cpu_state.fpu.fpcr == before.fpcr && cpu_state.fpu.fpsr == before.fpsr &&
           cpu_state.fpu.fpiar == before.fpiar);
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    unsigned count = 0;
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned post = 0; post < 2; ++post)
                for (unsigned precision = 0; precision < 3; ++precision)
                    for (unsigned rounding = 0; rounding < 4; ++rounding)
                        for (unsigned format = 0; format < 8; ++format)
                            for (unsigned destination = 0; destination < 8; ++destination)
                                for (unsigned stage = 0; stage < 4; ++stage)
                                    for (unsigned old_null = 0; old_null < 2; ++old_null)
                                        for (unsigned status = 0; status < 5; ++status) {
                                            resume_case(config,page,post,precision,rounding,format,destination,
                                                        stage,old_null,status);
                                            ++count;
                                        }
    uint64_t emitted = syn68k_native_compile_count() - compiled, loaded = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && loaded : emitted);
    printf("%u E3-writeback/E1-move-out/stage/format/register/priority/idle/save cases passed; emitted=%llu hits=%llu\n",
           count, (unsigned long long)emitted, (unsigned long long)loaded);
    return 0;
}
