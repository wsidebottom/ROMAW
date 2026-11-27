/*+===================================================================
  File:      mapped-sequential-prefetch-test.c

  Summary:   Verify retained FPU commands and split long displacements.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040 sequential instruction prefetch.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define main mapped_cache_main
#include "mapped-cache040-test.c"
#undef main
static unsigned reads;

static int fetch_record(void* o, uint32_t addr, unsigned char* bytes, unsigned size, uint32_t attrs, unsigned fc)
{
    if (fc == 6 && addr >= 0x8010 && addr < 0x8014)
        ++reads;
    return physical_read(o, addr, bytes, size, attrs, fc);
}

int main(int argc, char** argv)
{
    assert(argc <= 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    for (unsigned mode = 0; mode < 2; ++mode) {
        Syn68kMappedCPU cpu = setup(mode);
        cpu.pc = 0x800e;
        cpu_state.cacr = 0;
        memory.cache = NULL;
        memory.read = fetch_record;
        reads = 0;
        put(0x800e, 2, 0x4e71);
        put(0x8010, 2, 0xf200);
        put(0x8012, 2, 0x0018); /* FABS FP0,FP0. */
        cpu_state.fpu.regs[0].exponent = 0xbfff;
        cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && cpu.pc == 0x8010 && reads == 1);
        put(0x8012, 2, 0x007f);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && cpu.pc == 0x8014 && reads == 1);
        assert(cpu_state.fpu.regs[0].exponent == 0x3fff);
    }
    for (unsigned mode = 0; mode < 2; ++mode) {
        Syn68kMappedCPU cpu = setup(mode);
        cpu.pc = 0x800e;
        memory.cache = NULL;
        memory.read = fetch_record;
        reads = 0;
        put(0x800e, 2, 0x4e71);
        put(0x8010, 2, 0x66ff); /* BNE.L to $8030. */
        put(0x8012, 4, 0x0000001e);
        put(0x8030, 2, 0x7002);
        cpu_state.ccnz = 1;
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && cpu.pc == 0x8010 && reads == 1);
        put(0x8012, 2, 0x0100);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && cpu.pc == 0x8030 && reads == 1);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.completed == 1 && EM_D0 == 2);
    }
    printf("4 guest FPU and split branch prefetch cases passed; emitted=%llu hits=%llu\n",
           (unsigned long long)syn68k_native_compile_count(), (unsigned long long)syn68k_aot_hit_count());
}
