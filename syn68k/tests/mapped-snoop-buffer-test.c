/*+===================================================================
  File:      mapped-snoop-buffer-test.c

  Summary:   Verify board snoops of buffered victims survive mapped FPU push faults and guest RTE.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW mapped CPU arbitration and format-7 recovery regression.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define main snoop_buffer_parent_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned grants, push_beats, armed, snoop_helpers;
static unsigned char expected_push[16];
static Syn68kCache040Bus physical_bus;

Syn68kCache040Snoop __real_syn68k_cache040_snoop(Syn68kCache040*, uint32_t, unsigned, unsigned, unsigned, int,
                                                 unsigned char[16]);

Syn68kCache040Snoop __wrap_syn68k_cache040_snoop(Syn68kCache040* c, uint32_t a, unsigned sc, unsigned tt, unsigned n,
                                                 int w, unsigned char d[16])
{
    ++snoop_helpers;
    return __real_syn68k_cache040_snoop(c, a, sc, tt, n, w, d);
}

static int bus_read(void* opaque, uint32_t a, unsigned char* d, unsigned n, const Syn68kCache040Cycle* cycle,
                    unsigned* inhibit)
{
    *inhibit = 0;
    return physical_read(opaque, a, d, n, cycle->attributes, cycle->fc);
}

static int bus_write(void* opaque, uint32_t a, const unsigned char* d, unsigned n, const Syn68kCache040Cycle* cycle)
{
    if (cycle->kind == CACHE040_PUSH) {
        assert(grants == 1 && !cache.snoop_window && !cache.victim_live && a == 0x1000 + push_beats * 4);
        ++push_beats;
        if (armed) {
            armed = 0;
            return 0;
        }
    }
    return physical_write(opaque, a, d, n, cycle->attributes, cycle->fc);
}

static void grant(void* opaque, Syn68kCache040* state)
{
    assert(opaque == &grants && state == &cache && !grants && !push_beats);
    ++grants;
    assert(cache.active && cache.snoop_window == 1 && cache.victim_live == 1 && cache.victim_way == 0);
    assert(cache.victim.valid && cache.victim.address == 0x1000 && cache.victim.dirty == 15);
    CPUState saved = cpu_state;
    Syn68kMMU040 saved_mmu = mmu;
    unsigned attempted = 0;
    unsigned* prior = memory.native ? native.write_attempted : NULL;
    if (memory.native)
        native.write_attempted = &attempted;
    unsigned char data[16] = {0};
    Syn68kCache040Snoop r = syn68k_mmu040_snoop(&memory, 0x1000, 1, 0, 16, 0, data);
    assert(!r.fault && r.hit == CACHE040_SNOOP_VICTIM && r.intervention == 1 && !r.invalidated && !r.way[0]);
    for (unsigned i = 0; i < 16; ++i)
        assert(data[i] == 0x40 + i);
    data[0] = 0x7a;
    r = syn68k_mmu040_snoop(&memory, 0x1005, 1, 0, 1, 1, data);
    assert(!r.fault && r.hit == CACHE040_SNOOP_VICTIM && r.intervention == 1 && !r.invalidated);
    assert(cache.victim.bytes[5] == 0x7a && get(0x1004, 4) == 0);
    if (memory.native)
        native.write_attempted = prior;
    assert(!attempted && !memcmp(&saved, &cpu_state, sizeof saved) && !memcmp(&saved_mmu, &mmu, sizeof mmu));
}

/* Save D2 below the frame. EA holds the CT trace PC and must survive cleanup.
   Four physical MOVES drain the captured PD; CPU retry never replays them. */
static void cleanup(void)
{
    unsigned pc = 0x8100;
    put(pc, 2, 0x2f02); /* MOVE.L D2,-(SP) */
    pc += 2;
    for (unsigned i = 0; i < 4; ++i) {
        put(pc, 2, 0x242f); /* MOVE.L 48+4*i(SP),D2 */
        put(pc + 2, 2, 48 + i * 4);
        pc += 4;
        put(pc, 2, 0x0eb9); /* MOVES.L D2,physical */
        put(pc + 2, 2, 0x2800);
        put(pc + 4, 4, 0x1000 + i * 4);
        pc += 8;
    }
    put(pc, 2, 0x241f); /* MOVE.L (SP)+,D2 */
    put(pc + 2, 2, 0x4e73);
}

static void exercise(unsigned config, unsigned store, unsigned fault, unsigned trace)
{
    Syn68kMappedCPU cpu = setup(config & 2 ? 2 : config & 1);
    cpu.native = config & 1;
    grants = push_beats = 0;
    armed = fault;
    unsigned helpers = snoop_helpers;
    cpu_state.sr = 0x2000 | trace << 14;
    cpu_state.dfc = 0;
    EM_D2 = 0xdeadbeef;
    EM_A0 = 0x2400;
    if (store) {
        cpu_state.fpu.null_state = 0;
        cpu_state.fpu.regs[0].exponent = 0x3fff;
        cpu_state.fpu.regs[0].significand = UINT64_C(0xc000000000000000);
    }
    put(0x8000, 2, 0xf210);
    put(0x8002, 2, store ? 0x6400 : 0x4400); /* FMOVE.S FP0,(A0) / (A0),FP0 */
    put(0x2400, 4, store ? 0x11223344 : 0x3fc00000);
    put(8, 4, 0x8100);
    put(36, 4, 0x8200);
    put(0x8200, 2, 0x4e73);
    cleanup();
    for (unsigned way = 0; way < 4; ++way) {
        Syn68kCache040Line* line = &cache.lines[0][0][way];
        *line = (Syn68kCache040Line){.address = 0x1000 + way * 0x400, .valid = 1, .dirty = way ? 0 : 15};
        for (unsigned i = 0; i < 16; ++i)
            line->bytes[i] = 0x40 + i;
    }
    for (unsigned i = 0; i < 16; ++i)
        expected_push[i] = i == 5 ? 0x7a : 0x40 + i;
    cache.arbitrate = grant;
    cache.arbitrate_opaque = &grants;
    physical_bus = (Syn68kCache040Bus){.opaque = ram, .read_cycle = bus_read, .write_cycle = bus_write};
    binding.bus = &physical_bus;
    if (memory.native && fault)
        native.ram = (Syn68kNative040RAM){ram + 0x2000, 0x2000, sizeof ram - 0x2000};
    uint64_t entries = syn68k_native_execution_count();
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(grants == 1 && !cache.active && !cache.snoop_window && !cache.victim_live && !cache.victim_way);
    Syn68kCache040Line empty = {0};
    assert(!memcmp(&cache.victim, &empty, sizeof empty) && cache.arbitrate == grant &&
           cache.arbitrate_opaque == &grants);
    assert(snoop_helpers - helpers == (config & 2 ? 0u : 2u));
    assert(resident(0, 0x2400) && resident(0, 0x2400)->dirty == store);
    assert(get(0x2400, 4) == (store ? 0x11223344u : 0x3fc00000u));
    if (fault) {
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && push_beats == 1);
        assert(run.step.cache_restart == !store && cpu.pc == (store ? 0x8004u : 0x8000u));
        assert(run.step.access.cache.push_size == 16 && !memcmp(run.step.access.cache.push_data, expected_push, 16));
        /* Class-0 FPU command entry leaves reset/null state before the
           operand fault; the uncommitted destination register is unchanged. */
        assert(!cpu_state.fpu.null_state);
        if (!store)
            assert(!cpu_state.fpu.regs[0].exponent && !cpu_state.fpu.regs[0].significand);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100 && EM_A7 == 0x9fc4);
        unsigned sp = EM_A7;
        assert(get(sp + 6, 2) == 0x7008 && get(sp + 20, 4) == 0x1000 && get(sp + 40, 4) == 0x1000);
        assert(get(sp + 12, 2) == (0x60u | (store && (trace & 2) ? 0x2000 : 0)));
        assert(get(sp + 8, 4) == (store && (trace & 2) ? 0x8000u : 0));
        assert(!memcmp(ram + sp + 44, expected_push, 16));
        assert(syn68k_mapped_cpu_run(&cpu, 11, &run));
        assert(EM_D2 == 0xdeadbeef && EM_A7 == 0xa000 && !memcmp(ram + 0x1000, expected_push, 16));
        if (!store) {
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000);
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        }
    }
    else
        assert(push_beats == (config & 2 ? 0u : 4u) && !memcmp(ram + 0x1000, expected_push, 16));
    assert(cpu.pc == 0x8004 && run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
    assert(!cpu_state.fpu.null_state && cpu_state.fpu.regs[0].exponent == 0x3fff &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0xc000000000000000));
    if (trace & 2) {
        assert(run.step.vector == 9 && run.step.format == 2 && run.step.fault_address == 0x8000);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8200);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8004);
    }
    assert(grants == 1 && EM_A0 == 0x2400 && EM_D2 == 0xdeadbeef && EM_A7 == 0xa000);
    if (config & 1)
        assert(syn68k_native_execution_count() > entries);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t emitted = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned store = 0; store < 2; ++store)
            for (unsigned fault = 0; fault < 2; ++fault)
                for (unsigned trace = 0; trace < 4; ++trace)
                    exercise(config, store, fault, trace);
    emitted = syn68k_native_compile_count() - emitted;
    hits = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && hits : emitted);
    printf("%u mapped victim-snoop/FPU/push-fault/PD/guest-RTE/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)emitted, (unsigned long long)hits);
    return 0;
}
