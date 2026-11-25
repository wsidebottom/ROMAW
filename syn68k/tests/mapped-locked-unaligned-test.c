/* SPDX-License-Identifier: MIT */
/* Full scalar writebacks survive unaligned locked bus and second-page faults. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned model_calls, layout, nbytes, pair, first, second, capture, rmw_reads, expected_reads, deny_reads;
static unsigned fail_bus, fail_skip, fail_at, protect_at, protected_pte, protected_value, repair_value;
static Syn68kCache040Bus physical;
Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* m, const Syn68kMMU040Bus* b, uint32_t a, unsigned f,
                                                  int w)
{
    ++model_calls;
    return __real_syn68k_mmu040_translate(m, b, a, f, w);
}

Syn68kCache040Access __real_syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                     unsigned, int, uint32_t, uint32_t, uint32_t, int,
                                                     enum Syn68kCache040Traffic);

Syn68kCache040Access __wrap_syn68k_cache040_transfer(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                     unsigned f, unsigned n, int w, uint32_t v, uint32_t attrs,
                                                     uint32_t cacr, int noalloc, enum Syn68kCache040Traffic t)
{
    ++model_calls;
    return __real_syn68k_cache040_transfer(c, b, a, f, n, w, v, attrs, cacr, noalloc, t);
}

static unsigned mask(unsigned n)
{
    return n == 4 ? UINT32_MAX : (1u << (n * 8)) - 1;
}

static unsigned flags(void)
{
    return !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static unsigned compare_flags(unsigned a, unsigned b, unsigned n)
{
    unsigned bits = mask(n), sign = 1u << (n * 8 - 1), r = (a - b) & bits;
    a &= bits;
    b &= bits;
    return (a < b) | (!!((a ^ b) & (a ^ r) & sign) << 1) | (!r << 2) | (!!(r & sign) << 3) | 16;
}

/* Literal mappings are independent of the production translator. */
static unsigned physical_address(unsigned a)
{
    if (layout == 1) {
        if ((a & 0xfffff000) == 0x04000000)
            return 0x9000 + (a & 4095);
        if ((a & 0xfffff000) == 0x04001000)
            return 0xb000 + (a & 4095);
        if ((a & 0xfffff000) == 0x05000000)
            return 0xc000 + (a & 4095);
        if ((a & 0xfffff000) == 0x05001000)
            return 0xe000 + (a & 4095);
    }
    if (layout == 2 && (a & 0xfffff000) == 0x04000000)
        return 0xc000 + (a & 4095);
    return a;
}

static unsigned backing(unsigned a)
{
    return a >= 0xfffff000 ? 0x9000 + (a & 4095) : a;
}

static int operand_address(unsigned a)
{
    for (unsigned op = 0; op < (pair ? 2u : 1u); ++op)
        for (unsigned i = 0; i < nbytes; ++i)
            if (a == physical_address((op ? second : first) + i))
                return 1;
    return 0;
}

static unsigned width(unsigned a, unsigned left)
{
    return (a & 1) || left < 2 ? 1 : (a & 2) || left < 4 ? 2 : 4;
}

static unsigned beats(unsigned a)
{
    unsigned total = 0;
    for (unsigned pos = 0; pos < nbytes; pos += width(a + pos, nbytes - pos))
        ++total;
    return total;
}

static unsigned beat_offset(unsigned a, unsigned beat)
{
    unsigned pos = 0;
    while (beat--)
        pos += width(a + pos, nbytes - pos);
    return pos;
}

/* This fixture uses 4-KiB pages. A three-byte first page uses two
   natural beats; the one- and two-byte prefixes use one each. */
static unsigned page_replay_reads(unsigned a)
{
    unsigned prefix = 4096 - (a & 4095);
    return prefix >= nbytes ? 0 : prefix == 3 ? 2 : 1;
}

static unsigned replayed_beat(unsigned a, unsigned beat)
{
    return page_replay_reads(a) && beat_offset(a, beat) < 4096 - (a & 4095);
}

static int cycle_read(void* opaque, uint32_t a, unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* c,
                      unsigned* inhibit)
{
    *inhibit = 0;
    if (c->kind == CACHE040_OPERAND && c->locked && operand_address(a)) {
        ++rmw_reads;
        if (deny_reads)
            return 0;
    }
    int ok = physical_read(opaque, backing(a), bytes, n, c->attributes, c->fc);
    if (capture && protect_at && rmw_reads == protect_at) {
        put(protected_pte, 4, protected_value);
        syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
        protect_at = 0;
    }
    return ok;
}

static int cycle_write(void* opaque, uint32_t a, const unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* c)
{
    if (capture && fail_bus && c->kind == CACHE040_OPERAND && c->locked && a == fail_at) {
        if (fail_skip)
            --fail_skip;
        else {
            fail_bus = 0;
            return 0;
        }
    }
    return physical_write(opaque, backing(a), bytes, n, c->attributes, c->fc);
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

static void set_operand(unsigned a, unsigned v)
{
    for (unsigned i = 0; i < nbytes; ++i)
        ram[backing(physical_address(a + i))] = v >> ((nbytes - i - 1) * 8);
}

static unsigned visible(unsigned a, unsigned n)
{
    uint32_t v = 0;
    Syn68kMMU040Access r = syn68k_mmu040_read(&memory, a, 5, n, 0, &v);
    assert(!r.translation.fault && !r.bus_error);
    return v;
}

static Syn68kMappedCPU fixture(unsigned mode, unsigned enabled, unsigned hit, unsigned user, unsigned m1, unsigned m2)
{
    Syn68kMappedCPU cpu = setup(mode);
    if (mode == 2)
        native.ram = (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = cycle_read, .write_cycle = cycle_write};
    binding.bus = &physical;
    cpu_state.cacr = enabled ? 0x80008000 : 0;
    cpu_state.sr = user ? 0 : 0x2000;
    cpu_state.isp = 0x1d000;
    EM_A7 = user ? 0x1e000 : 0x1d000;
    mmu.tc = 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    put(0x1008, 4, 0x200a);
    put(0x2000, 4, 0x300a);
    put(0x2100, 4, 0x340a);
    put(0x3000, 4, layout == 2 ? 0xc039 : 0x9039);
    put(0x3004, 4, 0xb039);
    put(0x3400, 4, 0xc039);
    put(0x3404, 4, 0xe039);
    mmu.dtt[1] = 0xff00c020;
    memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
    set_operand(first, m1);
    if (pair)
        set_operand(second, m2);
    if (hit)
        for (unsigned op = 0; op < (pair ? 2u : 1u); ++op)
            for (unsigned i = 0; i < nbytes; ++i) {
                unsigned a = physical_address((op ? second : first) + i) & ~15u;
                Syn68kCache040Line* line = &cache.lines[0][(a >> 4) & 63][op];
                *line = (Syn68kCache040Line){.address = a, .valid = 1, .dirty = hit == 2 ? 15 : 0};
                memcpy(line->bytes, ram + backing(a), 16);
            }
    capture = 1;
    rmw_reads = deny_reads = fail_bus = fail_skip = protect_at = 0;
    expected_reads = beats(first) + page_replay_reads(first) + (pair ? beats(second) + page_replay_reads(second) : 0);
    cpu_state.vbr = 0x7000;
    put(0x7008, 4, 0x8100);
    put(0x7024, 4, 0x8200);
    put(0x8200, 2, 0x4e73);
    return cpu;
}

static unsigned handler, steps;

static void word(unsigned x)
{
    put(handler, 2, x);
    handler += 2;
}

static void longword(unsigned x)
{
    put(handler, 4, x);
    handler += 4;
}

static unsigned cleanup(unsigned fc, unsigned bus_error, unsigned tail)
{
    handler = 0x8100;
    word(0x2f04);
    word(0x2f0a);
    word(0x4e7a);
    word(0x4001);
    word(0x2f04);
    word(0x7800 | fc);
    word(0x4e7b);
    word(0x4001);
    steps = 6;
    if (!bus_error) {
        word(0x23fc);
        longword(repair_value);
        longword(protected_pte);
        word(0xf518);
        steps += 2;
    }
    for (unsigned slot = 0; slot < 1 + tail; ++slot) {
        unsigned addr = slot ? (bus_error ? 32 : 24) : (bus_error ? 40 : 32);
        unsigned data = slot ? (bus_error ? 36 : 28) + 4 - nbytes : bus_error ? 44 : 36 + 4 - nbytes;
        word(0x246f);
        word(addr + 12);
        if (!slot && bus_error) {
            word(0x282f);
            word(data + 12);
            ++steps;
            /* WB1D follows bus lanes, including a wrapped word at offset 3. */
            unsigned original = visible(EM_A7 + 40, 4);
            for (unsigned i = 0; i < (original & 3); ++i) {
                word(0xe19c);
                ++steps;
            } /* ROL.L #8,D4 */
            for (unsigned i = nbytes; i < 4; ++i) {
                word(0xe08c);
                ++steps;
            } /* LSR.L #8,D4 */
        }
        else {
            word(nbytes == 2 ? 0x382f : 0x282f);
            word(data + 12);
            ++steps;
        }
        word(nbytes == 2 ? 0x0e52 : 0x0e92);
        word(0x4800);
        steps += 2;
    }
    word(0x281f);
    word(0x4e7b);
    word(0x4001);
    word(0x245f);
    word(0x281f);
    word(0x4e73);
    return steps + 5;
}

/* Malformed fault snapshots must be rejected before any architectural traffic. */
static void malformed(Syn68kMappedCPU* cpu, const Syn68kIntegerStep* valid)
{
    static unsigned char saved_ram[sizeof ram];
    static Syn68kCache040 saved_cache;
    CPUState saved_cpu = cpu_state;
    Syn68kMMU040 saved_mmu = mmu;
    Syn68kMappedCPU saved_run = *cpu;
    saved_cache = cache;
    memcpy(saved_ram, ram, sizeof ram);
    for (unsigned bad = 0; bad < 22; ++bad) {
        Syn68kIntegerStep record = *valid;
        switch (bad) {
        case 0:
            record.access.writing = 2;
            break;
        case 1:
            record.access.completed = 2;
            record.access.logical = record.access.request + 2;
            break;
        case 2:
            record.access.cache.cycle.locked = 2;
            break;
        case 3:
            record.access.cache.cycle.lock_end ^= 1;
            break;
        case 4:
            record.access.cache.cycle.traffic = CACHE040_NORMAL;
            break;
        case 5:
            record.access.cache.cycle.fc ^= 4;
            break;
        case 6:
            record.access.cache.cycle.attributes ^= 0x20;
            break;
        case 7:
            record.access.cache.cycle.attributes ^= 0x100;
            break;
        case 8:
            record.access.cache.cycle.burst = 1;
            break;
        case 9:
            record.access.cache.cycle.beat = 1;
            break;
        case 10:
            record.access.cache.cycle.way = 4;
            break;
        case 11:
            record.access.cache.interrupt_level = 1;
            break;
        case 12:
            record.access.cache.bus_address ^= 1;
            break;
        case 13:
            record.access.cache.completed = record.access.completed + 1;
            break;
        case 14:
            record.access.cache_table = 1;
            break;
        case 15:
            record.access.logical ^= 1;
            break;
        case 16:
            record.access.rmw = 0;
            break;
        case 17:
            record.store_locked = 2;
            break;
        case 18:
            record.access.size = 3;
            break;
        case 19:
            record.store_tail_valid = 2;
            record.store_tail_fc = 2;
            break;
        case 20:
            record.store_next |= 1;
            break;
        case 21:
            record.access.cache.cycle.kind = CACHE040_PUSH;
            break;
        }
        unsigned calls = model_calls, reads = rmw_reads;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED);
        assert(calls == model_calls && reads == rmw_reads && !memcmp(&saved_run, cpu, sizeof saved_run));
        assert(!memcmp(&saved_cpu, &cpu_state, sizeof saved_cpu) && !memcmp(&saved_mmu, &mmu, sizeof mmu));
        assert(!memcmp(&saved_cache, &cache, sizeof cache) && !memcmp(saved_ram, ram, sizeof ram));
        ++cases;
    }
}

static void exercise(unsigned mode)
{
    for (nbytes = 2; nbytes <= 4; nbytes *= 2)
        for (pair = 0; pair < 2; ++pair)
            for (layout = 0; layout < 3; ++layout)
                for (unsigned lane = 0; lane < (layout ? nbytes - 1 : 16u); ++lane) {
                    if (!layout && !(lane & (nbytes - 1)))
                        continue;
                    first = !layout ? 0x9040 + lane : layout == 1 ? 0x04001000 - (lane + 1) : 0u - (lane + 1);
                    second = !layout       ? 0x9080 + ((lane + 5) & 15)
                             : layout == 1 ? 0x05001000 - ((lane + 1) % (nbytes - 1) + 1)
                                           : 0x04000081 + lane;
                    for (unsigned condition = 0; condition < (pair ? 3u : 2u); ++condition)
                        for (unsigned which = 0; which < ((pair && !condition) ? 2u : 1u); ++which) {
                            unsigned target = pair && (condition || which) ? second : first;
                            unsigned tail = pair && !condition && !which;
                            for (unsigned fault = 0; fault < 3; ++fault) {
                                if (fault && layout != 1)
                                    continue;
                                for (unsigned at = 0; at < (fault ? 2u : beats(target)); ++at)
                                    for (unsigned occurrence = 0;
                                         occurrence < 1u + (!fault && replayed_beat(target, at)); ++occurrence)
                                        for (unsigned user = 0; user < 2; ++user)
                                            for (unsigned trace = 0; trace < 4; ++trace)
                                                for (unsigned hit = 0; hit < 3; ++hit) {
                                                    unsigned m1 = 0x80008081 & mask(nbytes),
                                                             m2 = 0x7fff7f42 & mask(nbytes);
                                                    Syn68kMappedCPU cpu = fixture(mode, 1, hit, user, m1, m2);
                                                    unsigned form = 2 + lane % 3,
                                                             initial = first + (!pair && form == 4 ? nbytes : 0);
                                                    EM_A0 = initial;
                                                    EM_A1 = second;
                                                    EM_A2 = 0x12345678;
                                                    cpu_state.sr |= trace << 14;
                                                    cpu_state.ccx = 1;
                                                    cpu_state.dfc = 7;
                                                    unsigned sr = cpu_state.sr;
                                                    unsigned c1 = m1 ^ (condition == 1 ? mask(nbytes) : 0),
                                                             c2 = m2 ^ (condition == 2 ? mask(nbytes) : 0);
                                                    EM_D0 = c1;
                                                    EM_D1 = c2;
                                                    EM_D2 = 0xa1b2c3d4;
                                                    EM_D3 = 0x55667788;
                                                    EM_D4 = 0xdeadbeef;
                                                    unsigned opcode = pair
                                                                          ? (nbytes == 2 ? 0x0cfc : 0x0efc)
                                                                          : (nbytes == 2 ? 0x0cc0 : 0x0ec0) | form << 3;
                                                    put(0x8000, 2, opcode);
                                                    put(0x8002, 2, pair ? 0x8080 : 0x0080);
                                                    if (pair)
                                                        put(0x8004, 2, 0x90c1);
                                                    unsigned prefix = fault ? (at ? 4096 - (target & 4095) : 0)
                                                                            : beat_offset(target, at);
                                                    if (!fault) {
                                                        fail_bus = 1;
                                                        fail_skip = occurrence;
                                                        fail_at = physical_address(target + prefix);
                                                    }
                                                    else {
                                                        unsigned base =
                                                            (target & 0xff000000) == 0x04000000 ? 0x3000 : 0x3400;
                                                        protected_pte = base + (at ? 4 : 0);
                                                        repair_value = get(protected_pte, 4);
                                                        protected_value = fault == 1 ? repair_value | 4 : 0;
                                                        protect_at = expected_reads;
                                                    }
                                                    unsigned value =
                                                        condition ? (pair ? m2 : m1)
                                                                  : (pair && (condition || which) ? EM_D3 : EM_D2) &
                                                                        mask(nbytes);
                                                    unsigned expected = compare_flags(condition == 1 ? m1
                                                                                      : pair         ? m2
                                                                                                     : m1,
                                                                                      condition == 1 ? c1
                                                                                      : pair         ? c2
                                                                                                     : c1,
                                                                                      nbytes);
                                                    Syn68kMappedRun run;
                                                    unsigned called = model_calls;
                                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                    capture = 0;
                                                    assert(mode ? model_calls == called : model_calls > called);
                                                    assert(!fail_bus && !fail_skip && !protect_at &&
                                                           rmw_reads == expected_reads);
                                                    if (run.step.store_state < 2)
                                                        fprintf(
                                                            stderr,
                                                            "locked mode=%u n=%u pair=%u layout=%u lane=%u cond=%u which=%u fault=%u at=%u state=%u prefix=%u/%u\n",
                                                            mode, nbytes, pair, layout, lane, condition, which, fault,
                                                            at, run.step.store_state, run.step.access.completed,
                                                            prefix);
                                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION &&
                                                           run.step.vector == 2 &&
                                                           run.step.store_state == (trace ? 3u : 2u));
                                                    assert(run.step.access.completed == prefix &&
                                                           run.step.access.request == target &&
                                                           run.step.access.logical == (uint32_t)(target + prefix));
                                                    assert(run.step.access.value == value && run.step.access.rmw &&
                                                           run.step.store_tail_valid == (tail ? 2u : 0u));
                                                    unsigned next = pair ? 0x8006 : 0x8004;
                                                    assert(cpu.pc == next && flags() == expected &&
                                                           EM_A0 == first + (!pair && form == 3 ? nbytes : 0));
                                                    assert(EM_D0 == (condition ? m1 : c1) &&
                                                           EM_D1 == (condition && pair ? m2 : c2));
                                                    if (!mode && nbytes == 4 && !pair && !layout && lane == 1 &&
                                                        !condition && !fault && at == 1 && !user && !trace && !hit)
                                                        malformed(&cpu, &run.step);
                                                    if (occurrence)
                                                        for (unsigned i = 0; i < 4096 - (target & 4095); ++i)
                                                            assert(ram[backing(physical_address(target + i))] ==
                                                                   ((value >> ((nbytes - i - 1) * 8)) & 255));
                                                    Syn68kFPUState fpu = cpu_state.fpu;
                                                    deny_reads = 1;
                                                    Syn68kMappedDelivery delivery;
                                                    assert(
                                                        syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                    if (delivery.reason != SYN68K_DELIVERY_COMPLETE || cpu.pc != 0x8100)
                                                        fprintf(
                                                            stderr,
                                                            "entry mode=%u layout=%u lane=%u n=%u pair=%u fault=%u at=%u user=%u trace=%u hit=%u reason=%u pc=%x sp=%x\n",
                                                            mode, layout, lane, nbytes, pair, fault, at, user, trace,
                                                            hit, delivery.reason, cpu.pc, EM_A7);
                                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE &&
                                                           cpu.pc == 0x8100);
                                                    unsigned frame = EM_A7, fc = user ? 1 : 5,
                                                             sz = nbytes == 2 ? 0x40 : 0;
                                                    assert(visible(frame + 12, 2) ==
                                                           (0x200 | sz | fc | (fault ? 0x400 : 0) |
                                                            (fault && prefix ? 0x800 : 0) | (trace ? 0x2000 : 0)));
                                                    assert(visible(frame + 20, 4) == target &&
                                                           visible(frame + (fault ? 32 : 40), 4) == target);
                                                    assert(visible(frame + (fault ? 16 : 18), 2) == (0x80 | sz | fc));
                                                    unsigned count = cleanup(fc, !fault, tail);
                                                    unsigned reads = rmw_reads;
                                                    assert(syn68k_mapped_cpu_run(&cpu, count, &run));
                                                    assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION
                                                                                : SYN68K_MAPPED_BUDGET) &&
                                                           run.step.vector == (trace ? 9u : 0u) && cpu.pc == next);
                                                    assert(rmw_reads == reads && EM_A2 == 0x12345678 &&
                                                           EM_D4 == 0xdeadbeef && cpu_state.dfc == 7 &&
                                                           cpu_state.sr == sr && flags() == expected);
                                                    assert(!memcmp(&fpu, &cpu_state.fpu, sizeof fpu));
                                                    assert(visible(first, nbytes) ==
                                                           (condition ? m1 : 0xa1b2c3d4 & mask(nbytes)));
                                                    if (pair)
                                                        assert(visible(second, nbytes) ==
                                                               (condition ? m2 : 0x55667788 & mask(nbytes)));
                                                    if (trace) {
                                                        assert(run.step.fault_address == 0x8000);
                                                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0,
                                                                                             &delivery));
                                                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                                                    }
                                                    ++cases;
                                                }
                            }
                        }
                }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        exercise(mode);
        instruction_view(mode);
    }
    printf("%u unaligned locked/partial/page/wrap/frame/cleanup/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
