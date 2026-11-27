/* SPDX-License-Identifier: MIT */
/* MOVE16 line packets, physical aborts, captured PD cleanup and cache coherency. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned source_count, dest_count, push_count, fail_direction, fail_index, model_calls;
static unsigned retry_first, retry_fault, retry_split;
static unsigned observed[2][4], functions[2][4];
static Syn68kCache040Bus physical;

Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* m, const Syn68kMMU040Bus* b, uint32_t a, unsigned f,
                                                  int w)
{
    ++model_calls;
    return __real_syn68k_mmu040_translate(m, b, a, f, w);
}

Syn68kCache040Access __real_syn68k_cache040_move16(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned, int,
                                                   unsigned char[16], uint32_t, uint32_t);

Syn68kCache040Access __wrap_syn68k_cache040_move16(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                   unsigned f, int w, unsigned char d[16], uint32_t attrs,
                                                   uint32_t cacr)
{
    ++model_calls;
    return __real_syn68k_cache040_move16(c, b, a, f, w, d, attrs, cacr);
}

static int cycle_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, const Syn68kCache040Cycle* c,
                      unsigned* inhibit)
{
    *inhibit = 0;
    if (c->traffic == CACHE040_MOVE16 && c->kind == CACHE040_OPERAND) {
        assert(c->burst_inhibited == (retry_split && c->beat != 0));
        if (!c->beat && (retry_first & 1)) {
            retry_first &= ~1u;
            return CACHE040_CYCLE_RETRY;
        }
        assert(size == 4 && c->burst == 16 && c->beat == source_count && source_count < 4);
        observed[0][source_count] = address;
        functions[0][source_count++] = c->fc;
        if (fail_direction == 1 && c->beat == fail_index) {
            fail_direction = 0;
            return retry_fault ? CACHE040_CYCLE_RETRY : 0;
        }
    }
    int status = physical_read(opaque, address, bytes, size, c->attributes, c->fc);
    if (status && retry_split && c->traffic == CACHE040_MOVE16 && c->kind == CACHE040_OPERAND && !c->beat)
        return CACHE040_CYCLE_INHIBIT_BURST;
    return status;
}

static int cycle_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* c)
{
    if (c->kind == CACHE040_PUSH) {
        ++push_count;
        if (fail_direction == 3 && c->beat == fail_index) {
            fail_direction = 0;
            return 0;
        }
    }
    if (c->traffic == CACHE040_MOVE16 && c->kind == CACHE040_OPERAND) {
        assert(c->burst_inhibited == (retry_split && c->beat != 0));
        if (!c->beat && (retry_first & 2)) {
            retry_first &= ~2u;
            return CACHE040_CYCLE_RETRY;
        }
        assert(size == 4 && c->burst == 16 && c->beat == dest_count && dest_count < 4);
        observed[1][dest_count] = address;
        functions[1][dest_count++] = c->fc;
        if (fail_direction == 2 && c->beat == fail_index) {
            fail_direction = 0;
            return retry_fault ? CACHE040_CYCLE_RETRY : 0;
        }
    }
    int status = physical_write(opaque, address, bytes, size, c->attributes, c->fc);
    if (status && retry_split && c->traffic == CACHE040_MOVE16 && c->kind == CACHE040_OPERAND && !c->beat)
        return CACHE040_CYCLE_INHIBIT_BURST;
    return status;
}

static int table_read(void* opaque, uint32_t a, uint32_t* v)
{
    assert(opaque == ram);
    *v = get(a, 4);
    return 1;
}

static int table_update(void* opaque, uint32_t a, uint32_t v, int locked)
{
    assert(opaque == ram);
    put(a, 4, locked ? get(a, 4) | v : v);
    return 1;
}

static Syn68kMappedCPU fixture(unsigned mode, unsigned cm, unsigned enabled, unsigned hit, unsigned user)
{
    Syn68kMappedCPU cpu = setup(mode);
    if (mode == 2)
        native.ram.size = 0x9000;
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = cycle_read, .write_cycle = cycle_write};
    binding.bus = &physical;
    cpu_state.cacr = enabled ? 0x80008000 : 0;
    mmu.tc = 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    put(0x1008, 4, 0x200a);
    put(0x2000, 4, 0x300a);
    put(0x2100, 4, 0x340a);
    put(0x3000, 4, 0x9019 | cm << 5);
    put(0x3400, 4, 0xc019 | cm << 5);
    memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
    cpu_state.sr = user ? 0 : 0x2000;
    cpu_state.isp = 0xd000;
    EM_A7 = user ? 0xe000 : 0xd000;
    for (unsigned i = 0; i < 16; ++i) {
        ram[0xc000 + i] = i + 1;
        ram[0x9040 + i] = 0xaa;
    }
    if (hit) {
        Syn68kCache040Line* s = &cache.lines[0][0][2];
        *s = (Syn68kCache040Line){.valid = 1, .address = 0xc000, .dirty = hit == 2 ? 15 : 0};
        for (unsigned i = 0; i < 16; ++i)
            s->bytes[i] = i + 0x41;
        Syn68kCache040Line* d = &cache.lines[0][4][1];
        *d = (Syn68kCache040Line){.valid = 1, .address = 0x9040, .dirty = 15};
        memset(d->bytes, 0xbb, 16);
    }
    source_count = dest_count = push_count = fail_direction = 0;
    put(8, 4, 0x8100);
    put(36, 4, 0x8200);
    put(0x8200, 2, 0x4e73);
    return cpu;
}

static uint32_t visible(unsigned a, unsigned size)
{
    uint32_t v;
    Syn68kMMU040Access access = syn68k_mmu040_read(&memory, a, 5, size, 0, &v);
    assert(!access.translation.fault && !access.bus_error);
    return v;
}

static void cleanup(void)
{
    unsigned end = 0x8100;
    const unsigned prepare[] = {0x7605, 0x4e7b, 0x3001, 0x242f, 20, 0x0282, 0xffff, 0xfff0, 0x2442};
    for (unsigned i = 0; i < sizeof prepare / sizeof *prepare; ++i, end += 2)
        put(end, 2, prepare[i]);
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned words[] = {0x242f, 44 + i * 4, 0x0e9a, 0x2800};
        for (unsigned j = 0; j < 4; ++j, end += 2)
            put(end, 2, words[j]);
    }
    put(end, 2, 0x4e73);
}

static void exercise(unsigned mode, int signal_retry)
{
    for (unsigned cm = 0; cm < 4; ++cm)
        for (unsigned enabled = 0; enabled < 2; ++enabled)
            for (unsigned hit = 0; hit < 3; ++hit)
                for (unsigned form = 0; form < 5; ++form)
                    for (unsigned sw = 0; sw < 4; ++sw)
                        for (unsigned dw = 0; dw < 4; ++dw)
                            for (unsigned user = 0; user < 2; ++user)
                                for (unsigned fault = 0; fault < 9; ++fault) {
                                    if (signal_retry && (hit || form != 4 || fault == 1 || fault == 5))
                                        continue;
                                    unsigned cached = enabled && hit && cm < 2;
                                    if (cached && fault && fault < 5)
                                        continue;
                                    Syn68kMappedCPU cpu = fixture(mode, cm, enabled, hit, user);
                                    unsigned source = 0x05000000 + sw * 4 + (form & 3);
                                    unsigned dest = 0x04000040 + dw * 4 + ((form + 1) & 3);
                                    EM_A0 = form & 1 ? dest : source;
                                    EM_A1 = dest;
                                    unsigned a0 = EM_A0, a1 = EM_A1;
                                    unsigned trace = (sw + dw + form) & 3;
                                    cpu_state.sr |= trace << 14;
                                    unsigned sr = cpu_state.sr;
                                    put(0x8000, 2, 0xf600 | form << 3);
                                    if (form == 4)
                                        put(0x8002, 2, 0x9000);
                                    else
                                        put(0x8002, 4, form & 1 ? source : dest);
                                    unsigned next = form == 4 ? 0x8004 : 0x8006;
                                    fail_direction = fault ? fault < 5 ? 1 : 2 : 0;
                                    fail_index = (fault - 1) & 3;
                                    retry_first = signal_retry && !fault ? 3 : 0;
                                    retry_fault = signal_retry && fault;
                                    retry_split = signal_retry;
                                    Syn68kMappedRun run;
                                    unsigned called = model_calls;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(!retry_first);
                                    assert(mode ? model_calls == called : model_calls > called);
                                    assert(source_count == (cached ? 0 : fault && fault < 5 ? fault : 4));
                                    assert(dest_count == (fault && fault < 5 ? 0 : fault ? fault - 4 : 4));
                                    assert(push_count == (enabled && hit == 2 && cm >= 2 ? 4 : 0));
                                    for (unsigned i = 0; i < source_count; ++i)
                                        assert(observed[0][i] == 0xc000 + ((sw + i) & 3) * 4 &&
                                               functions[0][i] == (user ? 1 : 5));
                                    for (unsigned i = 0; i < dest_count; ++i)
                                        assert(observed[1][i] == 0x9040 + ((dw + i) & 3) * 4 &&
                                               functions[1][i] == (user ? 1 : 5));
                                    assert(!!resident(0, 0x9040) == !!(hit && (!enabled || (fault && fault < 5))));
                                    assert(!!resident(0, 0xc000) == !!(hit && (!enabled || cm < 2)));
                                    unsigned payload = enabled && hit && (cm < 2 || hit == 2) ? 0x41 : 1;
                                    if (fault) {
                                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                               !fail_direction);
                                        assert(run.step.move16.state == (fault < 5 ? 6u : trace & 2 ? 3u : 2u));
                                        assert(run.step.access.cache.fault == CACHE040_OPERAND_FAULT);
                                        assert(run.step.access.cache.completed == ((fault - 1) & 3) * 4);
                                        assert(cpu.pc == (fault < 5 ? 0x8000 : next));
                                        Syn68kMappedDelivery delivery;
                                        cleanup();
                                        if (user)
                                            put(0x8100, 2, 0x7601);
                                        if (fault < 5)
                                            put(0x8100, 2, 0x4e73);
                                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                                        unsigned sp = EM_A7;
                                        if (visible(sp + 12, 2) != ((fault < 5 ? 0x160 : 0x68) | (user ? 1 : 5) |
                                                                    (fault > 4 && (trace & 2) ? 0x2000 : 0)))
                                            fprintf(
                                                stderr,
                                                "mode=%u cm=%u en=%u hit=%u form=%u sw=%u dw=%u user=%u fault=%u ssw=%x\n",
                                                mode, cm, enabled, hit, form, sw, dw, user, fault, visible(sp + 12, 2));
                                        assert(visible(sp + 12, 2) == ((fault < 5 ? 0x160 : 0x68) | (user ? 1 : 5) |
                                                                       (fault > 4 && (trace & 2) ? 0x2000 : 0)));
                                        assert(!visible(sp + 14, 2) && !visible(sp + 16, 2) && !visible(sp + 18, 2));
                                        assert(visible(sp + 20, 4) == ((fault < 5 ? source : dest) & ~3u));
                                        for (unsigned i = 0; i < 4; ++i)
                                            assert(visible(sp + 44 + i * 4, 4) ==
                                                   (fault < 5 ? 0 : (payload + i * 4) * 0x01010101u + 0x00010203));
                                        unsigned reads = source_count;
                                        if (fault > 4)
                                            memset(ram + 0xc000, 0xee, 16);
                                        source_count = dest_count = 0;
                                        assert(syn68k_mapped_cpu_run(&cpu, fault < 5 ? 1 : 14, &run));
                                        if (fault < 5) {
                                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000);
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        }
                                        else
                                            assert(!source_count && reads == (cached ? 0 : 4));
                                    }
                                    unsigned traced = !!(trace & 2);
                                    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                                    assert(run.step.vector == (traced ? 9u : 0u) && cpu.pc == next);
                                    assert(EM_A0 == a0 + (form < 2 || form == 4 ? 16 : 0));
                                    assert(EM_A1 == a1 + (form == 4 ? 16 : 0));
                                    assert(cpu_state.sr == sr);
                                    for (unsigned i = 0; i < 16; ++i) {
                                        if (visible(0x04000040 + i, 1) != payload + i)
                                            fprintf(
                                                stderr,
                                                "data mode=%u cm=%u en=%u hit=%u form=%u sw=%u dw=%u user=%u fault=%u byte=%u actual=%x wanted=%x\n",
                                                mode, cm, enabled, hit, form, sw, dw, user, fault, i,
                                                visible(0x04000040 + i, 1), payload + i);
                                        assert(visible(0x04000040 + i, 1) == payload + i);
                                    }
                                    if (traced) {
                                        Syn68kMappedDelivery delivery;
                                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8200);
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                                    }
                                    ++cases;
                                }
    retry_first = retry_fault = retry_split = 0;
}

static void protection_and_erratum(unsigned mode)
{
    for (unsigned hit = 0; hit < 3; ++hit)
        for (unsigned same = 0; same < 2; ++same)
            for (unsigned user = 0; user < 2; ++user) {
                Syn68kMappedCPU cpu = fixture(mode, 1, 1, hit, user);
                unsigned source = 0x0500000f, dest = same ? source : 0x0400004b;
                put(same ? 0x3400 : 0x3000, 4, (same ? 0xc03d : 0x903d));
                put(0x8000, 2, 0xf620);
                put(0x8002, 2, same ? 0x8000 : 0x9000);
                EM_A0 = source;
                EM_A1 = dest;
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.move16.state == 4 && cpu.pc == 0x8000);
                assert(run.step.access.translation.fault == MMU040_WRITE_PROTECTED && !dest_count && !push_count);
                assert(EM_A0 == source && EM_A1 == dest);
                assert(!!resident(0, 0xc000) == !!(hit && !(same && hit == 2)));
                for (unsigned i = 0; i < 4; ++i)
                    assert(run.step.move16.data[i] == (hit ? 0x41424344u : 0x01020304u) + i * 0x04040404u);
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                unsigned sp = EM_A7;
                assert(visible(sp + 12, 2) == (0x468 | (user ? 1 : 5)));
                assert(visible(sp + 16, 2) == (0xe8 | (user ? 1 : 5)) && !visible(sp + 18, 2));
                assert(visible(sp + 20, 4) == (dest & ~3u) && visible(sp + 32, 4) == (dest & ~3u));
                ++cases;
            }
}

static void record_guards(unsigned mode)
{
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned bad = 0; bad < (writing ? 19u : 15u); ++bad) {
            Syn68kMappedCPU cpu = fixture(mode, 1, 1, 0, 0);
            EM_A0 = 0x0500000c;
            put(0x8000, 2, 0xf600);
            put(0x8002, 4, 0x04000048);
            fail_direction = writing ? 2 : 1;
            fail_index = 2;
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.step.vector == 2);
            Syn68kIntegerStep step = run.step;
            Syn68kMMU040Access* a = &step.access;
            switch (bad) {
            case 0:
                a->cache.completed = 16;
                break;
            case 1:
                a->cache.completed = 7;
                break;
            case 2:
                a->cache.cycle.traffic = CACHE040_NORMAL;
                break;
            case 3:
                a->cache.cycle.kind = CACHE040_PUSH;
                break;
            case 4:
                a->cache.cycle.burst = 4;
                break;
            case 5:
                a->cache.cycle.beat ^= 1;
                break;
            case 6:
                a->cache.cycle.way = 4;
                break;
            case 7:
                a->cache.cycle.locked = 1;
                break;
            case 8:
                a->cache.cycle.lock_end = 1;
                break;
            case 9:
                a->cache.cycle.fc = 1;
                break;
            case 10:
                a->cache.cycle.attributes ^= 0x20;
                break;
            case 11:
                a->cache.bus_address ^= 4;
                break;
            case 12:
                a->cache.interrupt_level = 1;
                break;
            case 13:
                a->cache_table = 1;
                break;
            case 14:
                a->cache.completed = 4;
                a->cache.cycle.beat = 1;
                break;
            case 15:
                a->cache.push_size = 4;
                break;
            case 16:
                a->cache.push_word ^= 1;
                break;
            case 17:
                a->cache.push_address ^= 4;
                break;
            case 18:
                a->cache.push_data[15] ^= 1;
                break;
            }
            Syn68kMappedCPU saved_cpu = cpu;
            CPUState state = cpu_state;
            Syn68kMMU040 saved_mmu = mmu;
            Syn68kCache040 saved_cache = cache;
            unsigned char saved_ram[sizeof ram];
            memcpy(saved_ram, ram, sizeof ram);
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED);
            assert(!memcmp(&cpu, &saved_cpu, sizeof cpu) && !memcmp(&cpu_state, &state, sizeof state));
            assert(!memcmp(&mmu, &saved_mmu, sizeof mmu) && !memcmp(&cache, &saved_cache, sizeof cache));
            assert(!memcmp(ram, saved_ram, sizeof ram));
            ++cases;
        }
}

static void source_push_recovery(unsigned mode)
{
    for (unsigned cm = 2; cm < 4; ++cm)
        for (unsigned user = 0; user < 2; ++user)
            for (unsigned trace = 0; trace < 4; ++trace)
                for (unsigned beat = 0; beat < 4; ++beat) {
                    Syn68kMappedCPU cpu = fixture(mode, cm, 1, 2, user);
                    cpu_state.sr |= trace << 14;
                    unsigned sr = cpu_state.sr;
                    cpu_state.dfc = 0;
                    EM_A0 = 0x0500000b;
                    EM_D2 = 0xaabbccdd;
                    put(0x8000, 2, 0xf600);
                    put(0x8002, 4, 0x04000045);
                    unsigned end = 0x8100;
                    put(end, 2, 0x2f42);
                    put(end + 2, 2, 8);
                    end += 4;
                    for (unsigned i = 0; i < 4; ++i) {
                        put(end, 2, 0x242f);
                        put(end + 2, 2, 44 + i * 4);
                        end += 4;
                        put(end, 2, 0x0eb9);
                        put(end + 2, 2, 0x2800);
                        put(end + 4, 4, 0xc000 + i * 4);
                        end += 8;
                    }
                    put(end, 2, 0x242f);
                    put(end + 2, 2, 8);
                    put(end + 4, 2, 0x4e73);
                    fail_direction = 3;
                    fail_index = beat;
                    Syn68kMappedRun run;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                           run.step.cache_restart == 1 && !run.step.move16.state && cpu.pc == 0x8000 &&
                           EM_A0 == 0x0500000b && !source_count && !dest_count && push_count == beat + 1);
                    assert(!resident(0, 0xc000));
                    Syn68kMappedDelivery delivery;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                    unsigned sp = EM_A7;
                    assert(visible(sp + 12, 2) == 0x60 && visible(sp + 20, 4) == 0xc000 &&
                           visible(sp + 40, 4) == 0xc000 && !visible(sp + 18, 2));
                    for (unsigned i = 0; i < 4; ++i)
                        assert(visible(sp + 44 + i * 4, 4) == 0x41424344u + i * 0x04040404u);
                    assert(syn68k_mapped_cpu_run(&cpu, 11, &run));
                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000 && cpu_state.sr == sr &&
                           EM_A0 == 0x0500000b && EM_D2 == 0xaabbccdd);
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                           run.step.vector == (trace & 2 ? 9u : 0u) && cpu.pc == 0x8006 && EM_A0 == 0x0500001b);
                    assert(source_count == 4 && dest_count == 4 && push_count == beat + 1);
                    for (unsigned i = 0; i < 16; ++i)
                        assert(ram[0x9040 + i] == 0x41 + i);
                    ++cases;
                }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        exercise(mode, 0);
        exercise(mode, 1);
        protection_and_erratum(mode);
        source_push_recovery(mode);
        record_guards(mode);
        instruction_view(mode); /* Persistent guest-code sentinel in the same mapped bus. */
    }
    printf("%u MOVE16 mapped/cache/line/frame/PD/guest-cleanup/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
