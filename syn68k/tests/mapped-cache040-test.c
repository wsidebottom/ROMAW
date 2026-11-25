/* SPDX-License-Identifier: MIT */
/* Cached instruction view, copyback operands and no-allocate exception traffic. */
#include "syn68k_public.h"
#include "mapped-cpu.h"
#include "native040.h"
#include "jit-x64.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char ram[0x20000];
static Syn68kMMU040 mmu;
static Syn68kCache040 cache;
static Syn68kMMU040Cache binding;
static Syn68kMMU040Memory memory;
static Syn68kMMU040Native native;
static unsigned cases;

static void put(unsigned address, unsigned size, uint32_t value)
{
    for (unsigned i = 0; i < size; ++i)
        ram[address + i] = value >> ((size - i - 1) * 8);
}

static uint32_t get(unsigned address, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[address + i];
    return value;
}

static int physical_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    assert(opaque == ram && address <= sizeof ram - size);
    (void)attributes;
    (void)fc;
    memcpy(bytes, ram + address, size);
    return 1;
}

static int physical_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                          uint32_t attributes, unsigned fc)
{
    assert(opaque == ram && address <= sizeof ram - size);
    (void)attributes;
    (void)fc;
    memcpy(ram + address, bytes, size);
    return 1;
}

static Syn68kCache040Line* resident(unsigned bank, unsigned address)
{
    for (unsigned way = 0; way < 4; ++way) {
        Syn68kCache040Line* line = &cache.lines[bank][(address >> 4) & 63][way];
        if (line->valid && line->address == (address & ~15u))
            return line;
    }
    return NULL;
}

static Syn68kMappedCPU setup(unsigned mode)
{
    memset(ram, 0, sizeof ram);
    memset(&cpu_state, 0, sizeof cpu_state);
    cpu_state.sr = 0x2000;
    cpu_state.ccnz = 1;
    cpu_state.dfc = 5;
    cpu_state.cacr = 0x80008000;
    cpu_state.fpu.null_state = 1;
    EM_A7 = 0xa000;
    syn68k_mmu040_init(&mmu);
    syn68k_cache040_init(&cache);
    mmu.itt[0] = 0xc000;
    mmu.dtt[0] = 0xc020;
    binding = (Syn68kMMU040Cache){.state = &cache, .cacr = &cpu_state.cacr};
    memory = (Syn68kMMU040Memory){
        .mmu = &mmu, .opaque = ram, .read = physical_read, .write = physical_write, .cache = &binding};
    if (mode == 2)
        assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){ram, 0, sizeof ram}));
    return (Syn68kMappedCPU){.pc = 0x8000, .memory = &memory, .native = !!mode};
}

static void instruction_view(unsigned mode)
{
    Syn68kMappedCPU cpu = setup(mode);
    put(0x8000, 2, 0x2080); /* MOVE.L D0,(A0) */
    put(0x8002, 2, 0x2210); /* MOVE.L (A0),D1 */
    put(0x8004, 2, 0xf568); /* PTESTR (A0) */
    put(0x8006, 2, 0xf210); /* FMOVE.S (A0),FP0 */
    put(0x8008, 2, 0x4400);
    put(0x9000, 4, 0x11223344);
    EM_A0 = 0x9000;
    EM_D0 = 0x3f800000;
    Syn68kMappedRun run;
    uint64_t before = syn68k_native_execution_count();
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu.pc == 0x8002);
    assert(get(0x9000, 4) == 0x11223344 && resident(0, 0x9000) && resident(0, 0x9000)->dirty);
    assert(resident(1, 0x8000));
    /* The stale physical words must not override either cached data or the
       cached instruction view used by native source-word validation. */
    put(0x9000, 4, 0x55667788);
    put(0x8002, 2, 0x4afc);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu.pc == 0x8004 && EM_D1 == 0x3f800000);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu.pc == 0x8006 && mmu.mmusr == 3);
    put(0x8006, 2, 0x4afc);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu.pc == 0x800a);
    assert(cpu_state.fpu.regs[0].exponent == 0x3fff &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
    if (mode)
        assert(syn68k_native_execution_count() >= before + 2);
    /* Board-side invalidation, independent of the unfinished CPU cache-op
       decoder, changes the instruction view on the very next fetch. */
    resident(1, 0x8000)->valid = 0;
    cpu.pc = 0x8006;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 4 && !run.completed);
    ++cases;
}

static void exception_traffic(unsigned mode, unsigned hit)
{
    Syn68kMappedCPU cpu = setup(mode);
    put(4 * 4, 4, 0x8100);
    if (hit) {
        Syn68kCache040Line* line = &cache.lines[0][(0x9ff0 >> 4) & 63][0];
        *line = (Syn68kCache040Line){.address = 0x9ff0, .valid = 1};
    }
    Syn68kIntegerStep record = {.vector = 4, .format = 0, .exception_pc = 0x8000};
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100 && EM_A7 == 0x9ff8);
    assert(!resident(0, 4 * 4));
    if (hit) {
        Syn68kCache040Line* line = resident(0, 0x9ff0);
        assert(line && line->dirty && get(0x9ff8, 4) == 0);
        assert(line->bytes[8] == 0x20 && line->bytes[9] == 0 && line->bytes[12] == 0x80 && line->bytes[13] == 0 &&
               line->bytes[15] == 0x10);
    }
    else {
        assert(!resident(0, 0x9ff0));
        assert(get(0x9ff8, 2) == 0x2000 && get(0x9ffa, 4) == 0x8000 && get(0x9ffe, 2) == 0x10);
    }
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        instruction_view(mode);
        exception_traffic(mode, 0);
        exception_traffic(mode, 1);
    }
    printf("%u cached mapped CPU instruction/data/exception-stack cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
