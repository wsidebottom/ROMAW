/* SPDX-License-Identifier: MIT */
/* Cached-source MOVE16, destination descriptor pushes and guest PD cleanup. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned source_count, dest_count, push_count, fail_direction, fail_index, model_calls;
static unsigned inject_push, fault_ordinal, mutate_push, locked_updates;
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
        assert(size == 4 && c->burst == 16 && c->beat == source_count && source_count < 4);
        observed[0][source_count] = address;
        functions[0][source_count++] = c->fc;
        if (fail_direction == 1 && c->beat == fail_index) {
            fail_direction = 0;
            return 0;
        }
    }
    return physical_read(opaque, address, bytes, size, c->attributes, c->fc);
}

static int cycle_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    if (cycle->kind == CACHE040_PUSH) {
        unsigned ordinal = push_count++;
        if (inject_push && ordinal == fault_ordinal) {
            inject_push = 0;
            if (mutate_push) {
                EM_A2 = 0x12345678;
                EM_D5 = 0x55667788;
                cpu_state.fpu.null_state = 0;
                cpu_state.ccnz = 0;
                cpu_state.ccn = cpu_state.ccv = cpu_state.ccc = cpu_state.ccx = 1;
            }
            return 0;
        }
    }
    if (cycle->traffic == CACHE040_MOVE16 && cycle->kind == CACHE040_OPERAND) {
        assert(size == 4 && cycle->burst == 16 && cycle->beat == dest_count && dest_count < 4);
        observed[1][dest_count] = address;
        functions[1][dest_count++] = cycle->fc;
    }
    return physical_write(opaque, address, bytes, size, cycle->attributes, cycle->fc);
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
    locked_updates += !!locked;
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


static unsigned live_sr(void)
{
    return (cpu_state.sr & ~31u) | (cpu_state.ccx ? 16 : 0) | (cpu_state.ccn ? 8 : 0) |
           (!cpu_state.ccnz ? 4 : 0) | (cpu_state.ccv ? 2 : 0) | (cpu_state.ccc ? 1 : 0);
}

static void install_cleanup(void)
{
    unsigned pc = 0x8100;
    put(pc, 2, 0x2f42); /* Save D2 in the unused EA field. */
    put(pc + 2, 2, 8);
    pc += 4;
    for (unsigned i = 0; i < 4; ++i) {
        put(pc, 2, 0x242f); /* MOVE.L PDn(SP),D2 */
        put(pc + 2, 2, 44 + i * 4);
        pc += 4;
        if (!i) {
            /* Guest repair: clear WP after recovering PD0. The initial U=0,
               M=0, WP=1 descriptor is valid (UM Table 3-1). */
            put(pc, 2, 0x0282); /* ANDI.L #~4,D2 */
            put(pc + 2, 4, 0xfffffffbu);
            pc += 6;
        }
        put(pc, 2, 0x0eb9); /* MOVES.L D2,physical descriptor line, DFC=0 */
        put(pc + 2, 2, 0x2800);
        put(pc + 4, 4, 0x3000 + i * 4);
        pc += 8;
    }
    put(pc, 2, 0x242f);
    put(pc + 2, 2, 8);
    put(pc + 4, 2, 0x4e73);
}

static void rejection_guards(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* original)
{
    Syn68kMappedCPU saved_cpu = *cpu;
    CPUState state = cpu_state;
    Syn68kMMU040 saved_mmu = mmu;
    Syn68kCache040 saved_cache = cache;
    unsigned char saved_ram[sizeof ram];
    memcpy(saved_ram, ram, sizeof ram);
    unsigned saved_source = source_count, saved_dest = dest_count, saved_push = push_count;
    unsigned saved_updates = locked_updates, saved_models = model_calls;
    for (unsigned bad = 0; bad < 58; ++bad) {
        Syn68kIntegerStep step = *original;
        Syn68kMMU040Access* a = &step.access;
        switch (bad) {
        case 0: step.vector = 3; break;
        case 1: step.format = 0; break;
        case 2: step.next += 2; break;
        case 3: step.exception_pc |= 1; break;
        case 4: step.fault_address ^= 4; break;
        case 5: step.move16.state = 7; break;
        case 6: step.move16.state = 9; break;
        case 7: step.move16.address |= 1; break;
        case 8: step.move16.next += 8; break;
        case 9: step.move16.trace_pc = 2; break;
        case 10: step.move16.source = 1; break;
        case 11: step.move16.destination = 1; break;
        case 12: step.move16.command = 1; break;
        case 13: step.move16.data[(a->request >> 2) & 3] ^= 1; break;
        case 14: a->writing = 0; break;
        case 15: a->rmw = 1; break;
        case 16: a->size = 2; break;
        case 17: a->completed = 1; break;
        case 18: a->bus_error = 1; break;
        case 19: a->cache_table = 0; break;
        case 20: a->cache_exception = 0; break;
        case 21: a->translation.fault = MMU040_WRITE_PROTECTED; break;
        case 22: a->cache.fault = CACHE040_OK; break;
        case 23: a->cache.completed = 1; break;
        case 24: a->cache.cycle.traffic = CACHE040_NORMAL; break;
        case 25: a->cache.cycle.way = 4; break;
        case 26: a->cache.interrupt_level = 1; break;
        case 27: a->cache.push_size = 8; break;
        case 28: a->cache.push_address |= 1; break;
        case 29: a->cache.push_word = 4; break;
        case 30: a->cache.bus_address ^= 4; break;
        case 31: a->cache.cycle.burst = 0; break;
        case 32: a->cache.cycle.beat = 4; break;
        case 33: step.cache_restart = 1; break;
        case 34: step.restartable_read = 1; break;
        case 35: step.movem_state = 1; break;
        case 36: step.movem_ea = 1; break;
        case 37: step.store_state = 2; break;
        case 38: step.store_next = 2; break;
        case 39: step.store_trace_pc = 2; break;
        case 40: step.store_ccr = 1; break;
        case 41: step.store_tail = 1; break;
        case 42: step.store_tail_valid = 1; break;
        case 43: step.store_tail_fc = 1; break;
        case 44: step.store_tail_address = 1; break;
        case 45: step.store_locked = 1; break;
        case 46: step.store_link_displacement = 1; break;
        case 47: step.fpu_vector = 49; break;
        case 48: step.fpu_ea = 1; break;
        case 49: step.stopped = 1; break;
        case 50: step.prefetch_address = 2; break;
        case 51: step.flow_changed = 1; break;
        case 52: step.forced_trace = 1; break;
        case 53: step.forced_trace_pc = 2; break;
        case 54: step.interrupt_level = 1; break;
        case 55: a->logical ^= 4; break;
        case 56: a->request ^= 4; break;
        case 57: a->fc = 0; break;
        }
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(cpu, &step, 0, &delivery));
        if (delivery.reason != SYN68K_DELIVERY_UNSUPPORTED) {
            fprintf(stderr, "descriptor guard %u accepted\n", bad);
            abort();
        }
        assert(!memcmp(cpu, &saved_cpu, sizeof saved_cpu) && !memcmp(&cpu_state, &state, sizeof state));
        assert(!memcmp(&mmu, &saved_mmu, sizeof mmu) && !memcmp(&cache, &saved_cache, sizeof cache));
        assert(!memcmp(ram, saved_ram, sizeof ram));
        assert(source_count == saved_source && dest_count == saved_dest && push_count == saved_push &&
               locked_updates == saved_updates && model_calls == saved_models);
        ++cases;
    }
}

static void descriptor_case(unsigned mode, unsigned page, unsigned cm, unsigned hit, unsigned form,
                            unsigned sw, unsigned dw, unsigned user, unsigned trace, unsigned dirty,
                            unsigned fault, unsigned mutate, unsigned guards, unsigned normal)
{
    Syn68kMappedCPU cpu = fixture(mode, cm, 1, 0, user);
    if (mode == 2)
        native.ram.size = 0;
    mmu.tc = page ? 0xc000 : 0x8000;
    cpu_state.sr |= trace << 14;
    cpu_state.dfc = 0;
    EM_D2 = 0xaabbccdd;
    unsigned source = 0x05000000 + sw * 4 + (form & 3);
    unsigned destination = 0x04000040 + dw * 4 + ((form + 1) & 3);
    EM_A0 = form & 1 ? destination : source;
    EM_A1 = destination;
    unsigned initial_a0 = EM_A0;
    unsigned destination_physical = page ? 0xa040 : 0x9040;
    memset(ram + destination_physical, 0xaa, 16);
    /* U=0,M=1 is prohibited (UM 3.2.1). The fault matrix uses the
       valid U=0,M=0,WP=1 locked-U transition in Table 3-1. */
    const unsigned status = normal ? normal == 1 ? 1 : normal == 2 ? 9 : 25 : 5;
    const unsigned descriptor[] = {(page ? 0xa000u : 0x9000u) | status | cm << 5,
                                   0x11223344, 0x55667788, 0x99aabbcc};
    for (unsigned i = 0; i < 4; ++i)
        put(0x3000 + i * 4, 4, descriptor[i]);
    const unsigned addresses[] = {0xc000, 0x1000, 0x3400, 0x3000};
    for (unsigned way = 0; way < 4; ++way) {
        Syn68kCache040Line* line = &cache.lines[0][0][way];
        *line = (Syn68kCache040Line){.valid = !!(way || hit), .dirty = way == 3 ? dirty : way ? 15 : hit == 2 ? 15 : 0,
                                    .address = addresses[way]};
        memcpy(line->bytes, ram + addresses[way], 16);
        if (!way)
            for (unsigned i = 0; i < 16; ++i)
                line->bytes[i] = 0x41 + i;
    }
    Syn68kCache040Line* pointer = &cache.lines[0][16][0];
    *pointer = (Syn68kCache040Line){.valid = 1, .dirty = 15, .address = 0x2100};
    memcpy(pointer->bytes, ram + 0x2100, 16);
    put(0x8000, 2, 0xf600 | form << 3);
    if (form == 4)
        put(0x8002, 2, 0x9000);
    else
        put(0x8002, 4, form & 1 ? source : destination);
    install_cleanup();
    inject_push = !normal;
    fault_ordinal = fault;
    mutate_push = mutate;
    locked_updates = 0;
    unsigned model_before = model_calls;
    unsigned expected_sr = (cpu_state.sr & ~31u) | (mutate ? 31 : 0);
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    if (normal) {
        assert(run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
               run.step.vector == (trace & 2 ? 9u : 0u));
        assert(cpu.pc == 0x8000 + (form == 4 ? 4 : 6) && live_sr() == expected_sr);
        assert(EM_A0 == (form == 4 ? source + 16 : form < 2 ? initial_a0 + 16 : initial_a0));
        assert(EM_A1 == (form == 4 ? destination + 16 : destination) && EM_D2 == 0xaabbccdd);
        assert(!push_count && !locked_updates && source_count == (hit ? 0u : 4u) && dest_count == 4);
        assert(get(0x3000, 4) == (descriptor[0] | 0x18));
        Syn68kCache040Line* leaf = resident(0, 0x3000);
        assert(leaf && leaf->dirty == 15);
        assert(leaf->bytes[2] == ((descriptor[0] | 0x18) >> 8 & 255) &&
               leaf->bytes[3] == ((descriptor[0] | 0x18) & 255));
        for (unsigned i = 0; i < 16; ++i)
            assert(ram[destination_physical + i] == (hit ? 0x41u : 1u) + i);
        for (unsigned i = 0; i < 4; ++i) {
            unsigned word = ((destination >> 2) + i) & 3;
            assert(observed[1][i] == destination_physical + word * 4 && functions[1][i] == (user ? 1u : 5u));
            if (!hit) {
                word = ((source >> 2) + i) & 3;
                assert(observed[0][i] == 0xc000 + word * 4 && functions[0][i] == (user ? 1u : 5u));
            }
        }
        assert(!hit || (resident(0, 0xc000) && resident(0, 0xc000)->dirty == (hit == 2 ? 15u : 0u)));
        if (mode == 2)
            assert(model_calls == model_before);
        ++cases;
        return;
    }
    if (run.reason != SYN68K_MAPPED_EXCEPTION || run.step.vector != 2 || run.step.move16.state != 8) {
        fprintf(stderr, "descriptor push mode=%u page=%u cm=%u hit=%u form=%u sw=%u dw=%u user=%u "
                        "trace=%u dirty=%u fault=%u mutate=%u vector=%u state=%u\n",
                mode,page,cm,hit,form,sw,dw,user,trace,dirty,fault,mutate,run.step.vector,run.step.move16.state);
        abort();
    }
    assert(!run.step.cache_restart && !source_count && !dest_count && push_count == fault + 1 && !locked_updates);
    assert(cpu.pc == 0x8000 && EM_A0 == initial_a0 && EM_A1 == destination && live_sr() == expected_sr);
    assert(EM_A2 == (mutate ? 0x12345678u : 0) && EM_D5 == (mutate ? 0x55667788u : 0));
    assert(cpu_state.fpu.null_state == !mutate);
    unsigned push_size = run.step.access.cache.push_size;
    unsigned push_address = run.step.access.cache.push_address;
    assert(push_size == ((dirty & (dirty - 1)) ? 16u : 4u));
    if (guards)
        rejection_guards(&cpu, &run.step);
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
    unsigned sp = EM_A7;
    assert(visible(sp, 2) == expected_sr && visible(sp + 2, 4) == 0x8000 && visible(sp + 6, 2) == 0x7008);
    assert(visible(sp + 12, 2) == (0x200 | (push_size == 16 ? 0x60 : 0)) && !visible(sp + 14, 2));
    assert(visible(sp + 16, 2) == (0xe8 | (user ? 1 : 5)) && !visible(sp + 18, 2));
    assert(visible(sp + 20, 4) == push_address && visible(sp + 32, 4) == (destination & ~3u) &&
           visible(sp + 40, 4) == push_address);
    for (unsigned i = 0; i < 4; ++i)
        assert(visible(sp + 44 + i * 4, 4) == descriptor[i]);
    assert(syn68k_mapped_cpu_run(&cpu, 12, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000 && live_sr() == expected_sr &&
           EM_A0 == initial_a0 && EM_A1 == destination && EM_D2 == 0xaabbccdd && !locked_updates);
    for (unsigned i = 0; i < 4; ++i)
        assert(get(0x3000 + i * 4, 4) == (i ? descriptor[i] : descriptor[i] & ~4u));
    for (unsigned i = 0; i < 16; ++i)
        assert(ram[destination_physical + i] == 0xaa);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
           run.step.vector == (trace & 2 ? 9u : 0u));
    assert(cpu.pc == 0x8000 + (form == 4 ? 4 : 6));
    assert(EM_A0 == (form == 4 ? source + 16 : form < 2 ? initial_a0 + 16 : initial_a0));
    assert(EM_A1 == (form == 4 ? destination + 16 : destination) && EM_D2 == 0xaabbccdd);
    assert(EM_A2 == (mutate ? 0x12345678u : 0) && EM_D5 == (mutate ? 0x55667788u : 0));
    assert(live_sr() == expected_sr && cpu_state.fpu.null_state == !mutate);
    assert(!source_count && dest_count == 4 && push_count == fault + 1 && !locked_updates);
    assert(get(0x3000, 4) == ((descriptor[0] & ~4u) | 0x18));
    for (unsigned i = 0; i < 16; ++i)
        assert(ram[destination_physical + i] == 0x41 + i);
    for (unsigned i = 0; i < 4; ++i) {
        unsigned word = ((destination >> 2) + i) & 3;
        assert(observed[1][i] == destination_physical + word * 4 && functions[1][i] == (user ? 1u : 5u));
    }
    assert(resident(0, 0xc000) && resident(0, 0xc000)->dirty == (hit == 2 ? 15u : 0u));
    if (mode == 2)
        assert(model_calls == model_before);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned cm = 0; cm < 2; ++cm)
                for (unsigned hit = 1; hit < 3; ++hit)
                    for (unsigned form = 0; form < 5; ++form)
                        for (unsigned sw = 0; sw < 4; ++sw)
                            for (unsigned dw = 0; dw < 4; ++dw)
                                for (unsigned user = 0; user < 2; ++user)
                                    for (unsigned trace = 0; trace < 4; ++trace)
                                        for (unsigned fault = 0; fault < 4; ++fault)
                                            descriptor_case(mode,page,cm,hit,form,sw,dw,user,trace,15,fault,0,0,0);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned user = 0; user < 2; ++user)
                for (unsigned trace = 0; trace < 4; trace += 2)
                    for (unsigned form = 0; form < 5; form += 4)
                        for (unsigned dirty = 1; dirty < 15; ++dirty)
                            for (unsigned fault = 0; fault < ((dirty & (dirty - 1)) ? 4u : 1u); ++fault)
                                descriptor_case(mode,page,0,2,form,2,1,user,trace,dirty,fault,0,0,0);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned user = 0; user < 2; ++user)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned form = 0; form < 5; ++form)
                        for (unsigned fault = 0; fault < 4; ++fault)
                            descriptor_case(mode,page,0,2,form,2,1,user,trace,15,fault,1,0,0);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned user = 0; user < 2; ++user)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned form = 0; form < 5; form += 4)
                        descriptor_case(mode,page,0,2,form,2,1,user,trace,15,0,0,1,0);
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned cm = 0; cm < 2; ++cm)
                for (unsigned hit = 0; hit < 3; ++hit)
                    for (unsigned form = 0; form < 5; ++form)
                        for (unsigned user = 0; user < 2; ++user)
                            for (unsigned trace = 0; trace < 4; ++trace)
                                for (unsigned state = 1; state < 4; ++state)
                                    descriptor_case(mode,page,cm,hit,form,2,1,user,trace,15,0,0,0,state);
    /* MOVE16 continuation uses the closed memory image, rather than a persisted
       instruction body. Also exercise the shared mapped-code cache in this bus. */
    unsigned matrix_cases = cases;
    for (unsigned mode = 0; mode < 3; ++mode)
        instruction_view(mode);
    assert(cases == matrix_cases + 3);
    cases = matrix_cases;
    puts("3 persistent cache-backed instruction-view sentinel cases passed");
    uint64_t emitted = syn68k_native_compile_count() - compiled, loaded = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && loaded : emitted);
    printf("%u MOVE16 descriptor-push/PD/WB2/guest-RTE/retry/trace/callback cases passed; emitted=%llu hits=%llu\n",
           cases, (unsigned long long)emitted, (unsigned long long)loaded);
    return 0;
}
