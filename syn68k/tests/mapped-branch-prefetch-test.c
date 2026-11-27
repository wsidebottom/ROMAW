/*+===================================================================
  File:      mapped-branch-prefetch-test.c

  Summary:   Verify conditional branch prefetch fault selection and guest recovery.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040 instruction prefetch and guest RTE.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define main mapped_cache_main
#include "mapped-cache040-test.c"
#undef main

static unsigned attempts, fail_address;

static int fail_fetch(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    if (address == fail_address && fc == 6 && !attempts++)
        return 0;
    return physical_read(opaque, address, bytes, size, attributes, fc);
}

int main(int argc, char** argv)
{
    assert(argc <= 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    for (unsigned width = 0; width <= 6; width += 2)
        for (unsigned mode = 0; mode < 3; ++mode)
            for (unsigned taken = 0; taken < 2; ++taken)
                for (unsigned failed_path = 0; failed_path < 2; ++failed_path) {
                    Syn68kMappedCPU cpu = setup(!!mode);
                    unsigned dbcc = width == 6, extra = dbcc ? 2 : width;
                    cpu.pc = 0x800e - extra;
                    memory.read = fail_fetch;
                    if (mode == 2)
                        assert(
                            syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){ram + 0x8000, 0x8000, 16}));
                    attempts = 0;
                    fail_address = failed_path ? 0x8030 : 0x8010;
                    put(cpu.pc, 2, dbcc ? 0x51c9 : width == 0 ? 0x6620 : width == 2 ? 0x6600 : 0x66ff);
                    if (extra)
                        put(cpu.pc + 2, extra, 0x20 + extra); /* All widths target $8030. */
                    put(0x8010, 2, 0x7001);
                    put(0x8030, 2, 0x7002);
                    put(0x8012, 2, 0xf200);
                    put(0x8014, 2, 0x0018);
                    put(0x8032, 2, 0xf200);
                    put(0x8034, 2, 0x0018);
                    cpu_state.ccnz = taken;
                    EM_D1 = 0xa5a50000u | taken;
                    Syn68kMappedRun run;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    unsigned selected = taken ? 0x8030 : 0x8010;
                    assert(run.completed == 1 && cpu.pc == selected && attempts == 1);
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(attempts == 1);
                    if (dbcc)
                        assert(EM_D1 == (taken ? 0xa5a50000u : 0xa5a5ffffu));
                    if (failed_path == taken) {
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.completed);
                        assert(run.step.vector == 2 && run.step.format == 7);
                        assert(run.step.fault_address == selected && run.step.exception_pc == selected);
                        put(8, 4, 0x8100);
                        put(0x8100, 2, 0x4e73); /* RTE */
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                        assert((get(EM_A7 + 6, 2) >> 12) == 7);
                        assert(get(EM_A7 + 2, 4) == selected);
                        assert(get(EM_A7 + 20, 4) == selected);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.completed == 1 && cpu.pc == selected && EM_A7 == 0xa000 && attempts == 1);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.completed == 1 && EM_D0 == taken + 1 && attempts == 2);
                        if (dbcc)
                            assert(EM_D1 == (taken ? 0xa5a50000u : 0xa5a5ffffu));
                    }
                    else {
                        assert(run.completed == 1 && EM_D0 == taken + 1);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.completed == 1 && cpu.pc == selected + 6);
                    }
                }
    for (unsigned mode = 0; mode < 3; ++mode) {
        Syn68kMappedCPU cpu = setup(!!mode);
        cpu.pc = 0x800e;
        memory.read = fail_fetch;
        if (mode == 2)
            assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){ram + 0x8000, 0x8000, 16}));
        attempts = 0;
        fail_address = 0x8010;
        put(0x800e, 2, 0x6620);
        put(0x8010, 2, 0x7001);
        put(25 * 4, 4, 0x8010);
        cpu_state.ccnz = 0;
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && cpu.pc == 0x8010 && attempts == 1);
        cpu_state.interrupt_pending[1] = 1;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_INTERRUPT && run.interrupt_level == 1 && attempts == 1);
        Syn68kMappedDelivery delivery;
        Syn68kIntegerStep irq = {.vector = 25, .exception_pc = cpu.pc};
        assert(syn68k_mapped_exception_enter(&cpu, &irq, 1, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8010);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && EM_D0 == 1 && attempts == 2);
    }
    printf("51 dual-path and IRQ cases passed; emitted=%llu hits=%llu\n",
           (unsigned long long)syn68k_native_compile_count(), (unsigned long long)syn68k_aot_hit_count());
}
