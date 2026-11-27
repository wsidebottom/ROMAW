/*+===================================================================
  File:      mapped-exception-prefetch-test.c

  Summary:   Verify nonresident boundary prefetch precedes forced exceptions and new FPU frames.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040UMAD item 15 and guest format-7 repair/RTE.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define main exception_prefetch_parent_unused
#include "mapped-cache040-test.c"
#undef main
#include "fpu.h"

static unsigned table_effect, effect_seen, effect_address, next_page_reads;

static unsigned translation_helpers;
Syn68kMMU040Access __real_syn68k_mmu040_memory_translate(Syn68kMMU040Memory*, uint32_t, unsigned, int, int);

Syn68kMMU040Access __wrap_syn68k_mmu040_memory_translate(Syn68kMMU040Memory* m, uint32_t a, unsigned fc, int w, int p)
{
    ++translation_helpers;
    return __real_syn68k_mmu040_memory_translate(m, a, fc, w, p);
}

static int table_read(void* opaque, uint32_t a, uint32_t* value)
{
    assert(opaque == ram && a <= sizeof ram - 4);
    *value = get(a, 4);
    next_page_reads += a == 0x3004;
    if (table_effect && a == effect_address) {
        ++effect_seen;
        if (table_effect == 1) {
            EM_A2 = 0x12345678;
            EM_D3 = 0x11223344;
            cpu_state.ccn = 1;
        }
        else {
            cpu_state.fpu.null_state = 1;
            cpu_state.fpu.fpcr = 0x50;
            cpu_state.fpu.fpiar = 0xfeedbeef;
        }
        table_effect = 0;
    }
    return 1;
}

static int table_write(void* opaque, uint32_t a, uint32_t value, int locked)
{
    assert(opaque == ram && a <= sizeof ram - 4);
    put(a, 4, locked ? get(a, 4) | value : value);
    return 1;
}

static unsigned live_sr(void)
{
    return (cpu_state.sr & ~31u) | (cpu_state.ccx ? 16 : 0) | (cpu_state.ccn ? 8 : 0) | (!cpu_state.ccnz ? 4 : 0) |
           (cpu_state.ccv ? 2 : 0) | (cpu_state.ccc ? 1 : 0);
}

/* Save D2 below the frame; install the repaired root/pointer/page path in
   physical space, flush cached invalid translations, then RTE to the opcode. */
static void repair(const unsigned address[3], const unsigned value[3])
{
    unsigned pc = 0x8100;
    put(pc, 2, 0x2f02);
    pc += 2;
    for (unsigned i = 0; i < 3; ++i) {
        put(pc, 2, 0x243c);
        put(pc + 2, 4, value[i]);
        pc += 6;
        put(pc, 2, 0x0eb9);
        put(pc + 2, 2, 0x2800);
        put(pc + 4, 4, address[i]);
        pc += 8;
    }
    put(pc, 2, 0xf518);
    pc += 2; /* PFLUSHA */
    put(pc, 2, 0x241f);
    put(pc + 2, 2, 0x4e73);
}

static void exercise(unsigned config, unsigned page, unsigned kind, unsigned user, unsigned trace, unsigned caching,
                     unsigned scenario, unsigned effect)
{
    Syn68kMappedCPU cpu = setup(config & 2 ? 2 : config & 1);
    cpu.native = config & 1;
    memory.tables = (Syn68kMMU040Bus){ram, table_read, table_write};
    if (!caching)
        memory.cache = NULL;
    if (caching == 1)
        cpu_state.cacr = 0;
    mmu.dtt[0] = 0xc000; /* All table storage is WT. */
    mmu.tc = page ? 0xc000 : 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    unsigned size = page ? 0x2000 : 0x1000;
    unsigned shift = page ? 13 : 12, mask = page ? 31 : 63;
    unsigned base = scenario == 1 ? 0x04040000 - size : scenario == 2 ? 0x06000000 - size : 0x04000000;
    unsigned end = base + size - (scenario == 4 ? 2 : 0);
    unsigned length = kind >= 4 ? 4 : 2;
    unsigned physical = page ? 0xa000 : 0x9000;
    if (scenario == 5) {
        mmu.tc = page ? 0x4000 : 0;
        base = physical;
        end = physical + size;
    }
    unsigned pc = end - length;
    unsigned first_root = 0x1000 + (base >> 25) * 4;
    unsigned first_pointer = 0x2000 + ((base >> 18) & 127) * 4;
    unsigned first_leaf = 0x3000 + ((base >> shift) & mask) * 4;
    put(first_root, 4, 0x200a);
    put(first_pointer, 4, 0x300a);
    put(first_leaf, 4, physical | 9);
    unsigned next = base + size;
    unsigned pointer_base = scenario == 2 ? 0x2400 : 0x2000;
    unsigned leaf_base = scenario == 1 || scenario == 2 ? 0x3800 : 0x3000;
    unsigned address[3] = {0x1000 + (next >> 25) * 4, pointer_base + ((next >> 18) & 127) * 4,
                           leaf_base + ((next >> shift) & mask) * 4};
    unsigned value[3] = {pointer_base | 10, leaf_base | 10, 0x10009};
    if (scenario == 3)
        for (unsigned i = 0; i < 3; ++i)
            put(address[i], 4, value[i]);
    const unsigned opcodes[] = {0x4afc, 0x4380, 0x4380, 0xa123, 0xf200, 0xf218, 0x02d0};
    put(physical + (pc - base), 2, opcodes[kind]);
    if (kind >= 4)
        put(physical + (pc - base) + 2, 2, kind == 4 ? 0x5c00 : kind == 5 ? 0x440e : 0x1800);
    put(0x7000, 4, kind == 6 ? 0x000a0014 : 0x3f800000);
    put(8, 4, 0x8100);
    repair(address, value);
    cpu.pc = pc;
    cpu_state.sr = (user ? 0 : 0x2000) | trace << 14;
    cpu_state.isp = 0xd000;
    cpu_state.dfc = 0;
    cpu_state.ccx = cpu_state.ccv = 1;
    cpu_state.ccn = cpu_state.ccc = 0;
    cpu_state.ccnz = 1;
    EM_A7 = user ? 0xe000 : 0xd000;
    EM_A0 = 0x7000;
    EM_D0 = 10;
    EM_D1 = kind == 2 ? UINT32_MAX : kind == 6 ? 30 : 20;
    EM_D2 = 0xdeadbeef;
    unsigned sr = live_sr();
    unsigned helpers = translation_helpers;
    table_effect = effect;
    effect_seen = 0;
    effect_address = address[2];
    if (effect && memory.native)
        native.ram = (Syn68kNative040RAM){ram + physical, physical, size};
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    if (scenario < 3) {
        if (run.step.vector != 2 || run.step.exception_pc != pc || run.step.fault_address != end) {
            fprintf(
                stderr,
                "prefetch config=%u page=%u kind=%u user=%u trace=%u caching=%u scenario=%u vector=%u pc=%x fault=%x expected=%x/%x\n",
                config, page, kind, user, trace, caching, scenario, run.step.vector, cpu.pc, run.step.fault_address, pc,
                end);
            abort();
        }
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.format == 7 && !run.completed && cpu.pc == pc);
        assert(run.step.access.size == 2 && run.step.access.fc == (user ? 2u : 6u) && run.step.access.request == end);
        assert(run.step.access.translation.fault == MMU040_INVALID && !run.step.forced_trace);
        if (effect) {
            assert(effect_seen == 1 && !run.step.restartable_read);
            assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
            assert(EM_A0 == (kind == 5 ? 0x7004u : 0x7000u));
            if (effect == 1)
                assert(EM_A2 == 0x12345678 && EM_D3 == 0x11223344 && cpu_state.ccn);
            else
                assert(cpu_state.fpu.null_state == 1 && cpu_state.fpu.fpcr == 0x50 &&
                       cpu_state.fpu.fpiar == 0xfeedbeef);
            if (config)
                assert(translation_helpers == helpers);
            else
                assert(translation_helpers > helpers);
            ++cases;
            return;
        }
        assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector && cpu_state.fpu.fpiar == 0);
        assert(live_sr() == sr && EM_A0 == 0x7000 && EM_D2 == 0xdeadbeef);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
        unsigned sp = EM_A7;
        assert(get(sp, 2) == sr && get(sp + 2, 4) == pc && get(sp + 6, 2) == 0x7008);
        assert(get(sp + 12, 2) == (0x540u | (user ? 2 : 6)) && get(sp + 20, 4) == end);
        assert(!get(sp + 14, 2) && !get(sp + 16, 2) && !get(sp + 18, 2));
        assert(syn68k_mapped_cpu_run(&cpu, 10, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc && live_sr() == sr && EM_A0 == 0x7000 &&
               EM_D2 == 0xdeadbeef && EM_A7 == (user ? 0xe000u : 0xd000u));
        assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    }
    unsigned vector = kind == 0 ? 4 : kind == 3 ? 10 : kind >= 4 && kind <= 5 ? 11 : 6;
    unsigned format = vector == 4 || vector == 10 ? 0 : 2;
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == vector && run.step.format == format);
    assert(cpu.pc == (format ? end : pc) && !run.completed);
    assert(run.step.forced_trace == ((vector == 6 && (trace & 2)) ? 1u : 0u));
    assert(EM_A0 == (kind == 5 ? 0x7004u : 0x7000u) && EM_D2 == 0xdeadbeef);
    if (vector == 11)
        assert(cpu_state.fpu.frame_size == 52 && cpu_state.fpu.pending_vector == 11 && cpu_state.fpu.fpiar == pc);
    else
        assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
    if (config)
        assert(translation_helpers == helpers);
    else if (scenario < 4)
        assert(translation_helpers > helpers);
    ++cases;
}

/* An already posted unimplemented frame is a preinstruction pending
   exception. It must not be mistaken for a newly completed boundary command. */
static void pending_frame(unsigned config, unsigned page)
{
    Syn68kMappedCPU cpu = setup(config & 2 ? 2 : config & 1);
    cpu.native = config & 1;
    mmu.dtt[0] = 0xc000;
    mmu.tc = page ? 0xc000 : 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    memory.tables = (Syn68kMMU040Bus){ram, table_read, table_write};
    unsigned size = page ? 0x2000 : 0x1000, physical = page ? 0xa000 : 0x9000;
    if (memory.native)
        native.ram = (Syn68kNative040RAM){ram + physical, physical, size};
    put(0x1008, 4, 0x200a);
    put(0x2000, 4, 0x300a);
    put(0x3000, 4, physical | 9);
    put(0x3004, 4, 0);
    unsigned pc = 0x04000000 + size - 4;
    put(physical + size - 4, 2, 0xf200);
    put(physical + size - 2, 2, 0x5c00);
    table_effect = next_page_reads = 0;
    Syn68kFPUStep fp;
    /* Component stepping intentionally does not opt into the CPU boundary. */
    assert(syn68k_fpu_step_decoded(pc, 0xf200, &memory, &fp));
    assert(fp.vector == 11 && fp.format == 2 && cpu_state.fpu.pending_vector == 11 && cpu_state.fpu.frame_size == 52 &&
           !next_page_reads);
    Syn68kFPUState saved = cpu_state.fpu;
    cpu.pc = pc;
    unsigned helpers = translation_helpers;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 11 && !next_page_reads);
    assert(!memcmp(&saved, &cpu_state.fpu, sizeof saved));
    assert(translation_helpers == helpers);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t emitted = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned kind = 0; kind < 7; ++kind)
                for (unsigned user = 0; user < 2; ++user)
                    for (unsigned trace = 0; trace < 4; ++trace)
                        for (unsigned caching = 0; caching < 3; ++caching)
                            for (unsigned scenario = 0; scenario < 6; ++scenario)
                                exercise(config, page, kind, user, trace, caching, scenario, 0);
    /* Callback changes to registers, flags and FPU state must survive the
       prefetch fault. The runner must deny unsafe speculative-EA rollback. */
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned kind = 0; kind < 7; ++kind)
                for (unsigned effect = 1; effect <= 2; ++effect)
                    exercise(config, page, kind, 0, 3, 2, 0, effect);
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            pending_frame(config, page);
    for (unsigned mode = 0; mode < 3; ++mode)
        instruction_view(mode);
    emitted = syn68k_native_compile_count() - emitted;
    hits = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && hits : emitted);
    printf(
        "%u nonresident-boundary/forced-exception/FPU-posting/guest-repair/RTE cases passed; emitted=%llu hits=%llu\n",
        cases, (unsigned long long)emitted, (unsigned long long)hits);
    return 0;
}
