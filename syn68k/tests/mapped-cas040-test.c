/* SPDX-License-Identifier: MIT */
/* MC68040UM 7.4.5: mismatch writebacks and final-only CAS2 LOCKE. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

typedef struct {
    unsigned address, size, writing, value, fc, end, traffic;
} Cycle;

static Cycle observed[32];
static unsigned count, pushes, capturing, model_calls, failing, fault_address, protect, protected_pte, protected_value,
    operand_reads;
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

static void record(unsigned a, const unsigned char* bytes, unsigned n, unsigned w, const Syn68kCache040Cycle* c)
{
    if (!capturing || a < 0x9000 || a >= 0x9100)
        return;
    if (c->kind == CACHE040_PUSH) {
        assert(c->locked && w && !c->fc && !c->lock_end);
        ++pushes;
        return;
    }
    assert(c->locked && !c->burst && !c->beat);
    assert(c->kind == CACHE040_OPERAND && (c->attributes & 0x60) == 0x40 && count < 32);
    unsigned value = 0;
    for (unsigned i = 0; i < n; ++i)
        value = value << 8 | bytes[i];
    observed[count++] = (Cycle){a, n, w, value, c->fc, c->lock_end, c->traffic};
}

static int cycle_read(void* opaque, uint32_t a, unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* c,
                      unsigned* inhibit)
{
    *inhibit = 0;
    int ok = physical_read(opaque, a, bytes, n, c->attributes, c->fc);
    record(a, bytes, n, 0, c);
    if (c->kind == CACHE040_OPERAND && c->locked && a >= 0x9000 && a < 0x9100) {
        ++operand_reads;
        if (protect && operand_reads == protect) {
            put(protected_pte, 4, protected_value);
            syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
            protect = 0;
        }
    }
    return ok;
}

static int cycle_write(void* opaque, uint32_t a, const unsigned char* bytes, unsigned n, const Syn68kCache040Cycle* c)
{
    record(a, bytes, n, 1, c);
    if (capturing && failing && c->kind == CACHE040_OPERAND && a == fault_address) {
        failing = 0;
        return 0;
    }
    return physical_write(opaque, a, bytes, n, c->attributes, c->fc);
}

static unsigned mask(unsigned n)
{
    return n == 4 ? UINT32_MAX : (1u << (n * 8)) - 1;
}

static unsigned ccr(void)
{
    return !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static unsigned comparison(unsigned m, unsigned c, unsigned n, unsigned x)
{
    unsigned bits = mask(n), sign = 1u << (n * 8 - 1), result = (m - c) & bits;
    m &= bits;
    c &= bits;
    return (m < c) | (!!((m ^ c) & (m ^ result) & sign) << 1) | (!result << 2) | (!!(result & sign) << 3) | x;
}

static Syn68kMappedCPU fixture(unsigned mode, unsigned cm, unsigned enabled, unsigned hit, unsigned user,
                               unsigned first, unsigned second, unsigned n, unsigned m1, unsigned m2)
{
    Syn68kMappedCPU cpu = setup(mode);
    if (mode == 2)
        native.ram.size = 0x9000;
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = cycle_read, .write_cycle = cycle_write};
    binding.bus = &physical;
    mmu.tc = 0x8000;
    mmu.dtt[0] = 0xc000 | cm << 5;
    cpu_state.cacr = enabled ? 0x80008000 : 0;
    cpu_state.sr = user ? 0 : 0x2000;
    cpu_state.isp = 0xd000;
    EM_A7 = user ? 0xe000 : 0xd000;
    put(first, n, m1);
    put(second, n, m2);
    if (hit)
        for (unsigned operand = 0; operand < 2; ++operand) {
            unsigned address = operand ? second : first;
            for (unsigned line = address & ~15u; line <= ((address + n - 1) & ~15u); line += 16) {
                Syn68kCache040Line* entry = &cache.lines[0][(line >> 4) & 63][operand];
                *entry = (Syn68kCache040Line){.address = line, .valid = 1, .dirty = hit == 2 ? 15 : 0};
                memcpy(entry->bytes, ram + line, 16);
            }
        }
    count = pushes = failing = protect = operand_reads = 0;
    capturing = 1;
    return cpu;
}

static void expect(unsigned* at, unsigned a, unsigned n, unsigned w, unsigned v, unsigned fc, unsigned nonfinal)
{
    unsigned done = 0;
    while (done < n) {
        unsigned here = a + done, left = n - done;
        unsigned width = (here & 1) || left < 2 ? 1 : (here & 2) || left < 4 ? 2 : 4;
        assert(*at < count);
        Cycle* c = &observed[(*at)++];
        unsigned value = (v >> ((n - done - width) * 8)) & mask(width);
        unsigned traffic_ok = nonfinal ? c->traffic == CACHE040_LOCKED_CONTINUE
                              : w && done + width < n
                                  ? c->traffic == CACHE040_LOCKED || c->traffic == CACHE040_LOCKED_CONTINUE
                                  : c->traffic == CACHE040_LOCKED;
        if (c->address != here || c->size != width || c->writing != w || c->value != value || c->fc != fc ||
            c->end != (w && !nonfinal && done + width == n) || !traffic_ok)
            fprintf(stderr, "cycle at=%u addr=%x/%x size=%u/%u w=%u/%u v=%x/%x end=%u nf=%u traffic=%u\n", *at - 1,
                    c->address, here, c->size, width, c->writing, w, c->value, value, c->end, nonfinal, c->traffic);
        assert(c->address == here && c->size == width && c->writing == w && c->value == value && c->fc == fc);
        assert(c->end == (w && !nonfinal && done + width == n));
        /* Split physical chunks before the final chunk also suppress LOCKE. */
        if (w && !nonfinal && done + width < n)
            assert(c->traffic == CACHE040_LOCKED || c->traffic == CACHE040_LOCKED_CONTINUE);
        else
            assert(c->traffic == (nonfinal ? CACHE040_LOCKED_CONTINUE : CACHE040_LOCKED));
        done += width;
    }
}

static void normal(unsigned mode)
{
    const unsigned opcodes[] = {0x0ad0, 0x0cd0, 0x0ed0, 0x0cfc, 0x0efc};
    const unsigned sizes[] = {1, 2, 4, 2, 4};
    const unsigned samples[] = {0, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned kind = 0; kind < 5; ++kind)
        for (unsigned lane = 0; lane < 16; ++lane)
            for (unsigned cm = 0; cm < 4; ++cm)
                for (unsigned enabled = 0; enabled < 2; ++enabled)
                    for (unsigned hit = 0; hit < 3; ++hit)
                        for (unsigned user = 0; user < 2; ++user)
                            for (unsigned condition = 0; condition < (kind < 3 ? 2u : 3u); ++condition) {
                                unsigned n = sizes[kind], pair = kind >= 3;
                                unsigned a = 0x9000 + lane, b = 0x9080 + ((lane + 5) & 15);
                                unsigned m1 = samples[lane & 3] & mask(n), m2 = samples[(lane + 1) & 3] & mask(n);
                                Syn68kMappedCPU cpu = fixture(mode, cm, enabled, hit, user, a, b, n, m1, m2);
                                unsigned trace = (lane + condition) & 3, x = (lane & 1) ? 16 : 0;
                                cpu_state.sr |= trace << 14;
                                cpu_state.ccx = !!x;
                                unsigned c1 = m1 ^ (condition == 1 ? 1u << (n * 8 - 1) : 0),
                                         c2 = m2 ^ (condition == 2 ? mask(n) : 0);
                                EM_D0 = c1;
                                EM_D1 = c2;
                                EM_D2 = 0xa1b2c3d4;
                                EM_D3 = 0x55667788;
                                unsigned form = 2 + lane % 3;
                                EM_A0 = a + (!pair && form == 4 ? n : 0);
                                EM_A1 = b;
                                unsigned opcode = pair ? opcodes[kind] : (opcodes[kind] & ~63u) | (form << 3);
                                put(0x8000, 2, opcode);
                                put(0x8002, 2, pair ? 0x8080 : 0x0080);
                                if (pair)
                                    put(0x8004, 2, 0x90c1);
                                unsigned expected_flags = comparison(condition == 1 ? m1
                                                                     : pair         ? m2
                                                                                    : m1,
                                                                     condition == 1 ? c1
                                                                     : pair         ? c2
                                                                                    : c1,
                                                                     n, x);
                                Syn68kMappedRun run;
                                unsigned called = model_calls;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                capturing = 0;
                                assert(mode ? model_calls == called : model_calls > called);
                                assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                                assert(run.step.vector == (trace ? 9u : 0u) && cpu.pc == (pair ? 0x8006 : 0x8004));
                                assert(ccr() == expected_flags && EM_A0 == a + (!pair && form == 3 ? n : 0) &&
                                       EM_A1 == b);
                                assert(EM_D0 == (condition ? m1 : c1) && EM_D1 == (condition && pair ? m2 : c2));
                                assert(EM_D2 == 0xa1b2c3d4 && EM_D3 == 0x55667788);
                                unsigned at = 0, fc = user ? 1 : 5;
                                expect(&at, a, n, 0, m1, fc, 0);
                                if (pair)
                                    expect(&at, b, n, 0, m2, fc, 0);
                                if (!condition) {
                                    expect(&at, a, n, 1, EM_D2 & mask(n), fc, pair);
                                    if (pair)
                                        expect(&at, b, n, 1, EM_D3 & mask(n), fc, 0);
                                }
                                else
                                    expect(&at, pair ? b : a, n, 1, pair ? m2 : m1, fc, 0);
                                assert(at == count && binding.traffic == CACHE040_NORMAL && memory.cache == &binding &&
                                       !cache.active);
                                assert(get(a, n) == (condition ? m1 : EM_D2 & mask(n)));
                                assert(get(b, n) == (condition || !pair ? m2 : EM_D3 & mask(n)));
                                for (unsigned operand = 0; operand < (pair ? 2u : 1u); ++operand) {
                                    unsigned addr = operand ? b : a;
                                    assert(!resident(0, addr) && !resident(0, addr + n - 1));
                                }
                                if (hit == 2)
                                    assert(pushes);
                                else
                                    assert(!pushes);
                                ++cases;
                            }
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(address, 4);
    return 1;
}

static int table_update(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == ram);
    put(address, 4, locked ? get(address, 4) | value : value);
    return 1;
}

static unsigned handler, handler_steps;

static void word(unsigned value)
{
    put(handler, 2, value);
    handler += 2;
}

static void longword(unsigned value)
{
    put(handler, 4, value);
    handler += 4;
}

static unsigned cleanup(unsigned n, unsigned fc, unsigned physical_fault, unsigned tail)
{
    handler = 0x8100;
    word(0x2f04); /* Save D4 and A2 below the frame, retaining its CT address. */
    word(0x2f0a);
    word(0x4e7a);
    word(0x4001);
    word(0x2f04);
    word(0x7800 | fc);
    word(0x4e7b);
    word(0x4001);
    handler_steps = 6;
    if (!physical_fault) {
        word(0x23fc);
        longword(0x9039);
        longword(protected_pte);
        word(0xf518);
        handler_steps += 2;
    }
    for (unsigned slot = 0; slot < 1 + tail; ++slot) {
        unsigned address = slot ? (physical_fault ? 32 : 24) : (physical_fault ? 40 : 32);
        unsigned data = slot ? (physical_fault ? 36 : 28) + 4 - n : physical_fault ? 44 : 36 + 4 - n;
        word(0x246f);
        word(address + 12); /* MOVEA.L WBnA,A2 */
        word((n == 1 ? 0x182f : n == 2 ? 0x382f : 0x282f));
        word(data + 12);
        word(n == 1 ? 0x0e12 : n == 2 ? 0x0e52 : 0x0e92);
        word(0x4800); /* MOVES D4,(A2) */
        handler_steps += 3;
    }
    word(0x281f);
    word(0x4e7b);
    word(0x4001);
    word(0x245f);
    word(0x281f);
    word(0x4e73);
    return handler_steps + 5;
}

static unsigned visible(unsigned address, unsigned n)
{
    uint32_t value = 0;
    Syn68kMMU040Access access = syn68k_mmu040_read(&memory, address, 5, n, 0, &value);
    assert(!access.translation.fault && !access.bus_error);
    return value;
}

static void write_faults(unsigned mode)
{
    const unsigned opcodes[] = {0x0ad0, 0x0cd0, 0x0ed0, 0x0cfc, 0x0efc};
    const unsigned sizes[] = {1, 2, 4, 2, 4};
    for (unsigned kind = 0; kind < 5; ++kind)
        for (unsigned condition = 0; condition < (kind < 3 ? 2u : 3u); ++condition)
            for (unsigned which = 0; which < ((kind >= 3 && !condition) ? 2u : 1u); ++which)
                for (unsigned fault = 0; fault < 3; ++fault)
                    for (unsigned user = 0; user < 2; ++user)
                        for (unsigned trace = 0; trace < 4; ++trace)
                            for (unsigned hit = 0; hit < 3; ++hit) {
                                unsigned n = sizes[kind], pair = kind >= 3;
                                unsigned a = 0x9040, b = 0x9080;
                                unsigned m1 = 0x80000081 & mask(n), m2 = 0x7fffff42 & mask(n);
                                Syn68kMappedCPU cpu = fixture(mode, 1, 1, hit, user, a, b, n, m1, m2);
                                unsigned la = 0x04000040, lb = 0x04001080;
                                mmu.urp = mmu.srp = 0x1000;
                                put(0x1008, 4, 0x200a);
                                put(0x2000, 4, 0x300a);
                                put(0x3000, 4, 0x9039);
                                put(0x3004, 4, 0x9039);
                                memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
                                cpu_state.sr |= trace << 14;
                                cpu_state.ccx = 1;
                                unsigned initial_sr = cpu_state.sr;
                                EM_A0 = la;
                                EM_A1 = lb;
                                EM_A2 = 0x12345678;
                                EM_D4 = 0xdeadbeef;
                                cpu_state.dfc = 7;
                                unsigned c1 = m1 ^ (condition == 1 ? mask(n) : 0),
                                         c2 = m2 ^ (condition == 2 ? mask(n) : 0);
                                EM_D0 = c1;
                                EM_D1 = c2;
                                EM_D2 = 0xa1b2c3d4;
                                EM_D3 = 0x55667788;
                                put(0x8000, 2, opcodes[kind]);
                                put(0x8002, 2, pair ? 0x8080 : 0x0080);
                                if (pair)
                                    put(0x8004, 2, 0x90c1);
                                unsigned tail = pair && !condition && !which;
                                unsigned target_second = pair && (condition || which);
                                fault_address = target_second ? b : a;
                                unsigned logical_fault = target_second ? lb : la;
                                unsigned value =
                                    condition ? (pair ? m2 : m1) : (target_second ? EM_D3 : EM_D2) & mask(n);
                                unsigned next = pair ? 0x8006 : 0x8004;
                                unsigned expected_flags = comparison(condition == 1 ? m1
                                                                     : pair         ? m2
                                                                                    : m1,
                                                                     condition == 1 ? c1
                                                                     : pair         ? c2
                                                                                    : c1,
                                                                     n, 16);
                                if (!fault)
                                    failing = 1;
                                else {
                                    protect = pair ? 2 : 1;
                                    protected_pte = target_second ? 0x3004 : 0x3000;
                                    protected_value = fault == 1 ? 0x903d : 0;
                                }
                                put(8, 4, 0x8100);
                                put(36, 4, 0x8200);
                                put(0x8200, 2, 0x4e73);
                                unsigned steps = cleanup(n, user ? 1 : 5, !fault, tail);
                                Syn68kFPUState before_fpu = cpu_state.fpu;
                                Syn68kMappedRun run;
                                unsigned called = model_calls;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                capturing = 0;
                                assert(mode ? model_calls == called : model_calls > called);
                                assert(!failing && !protect && operand_reads == (pair ? 2u : 1u));
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.store_state == (trace ? 3u : 2u) && cpu.pc == next &&
                                       run.step.store_locked && run.step.access.writing && run.step.access.rmw &&
                                       run.step.access.request == logical_fault && run.step.access.value == value);
                                assert(run.step.access.bus_error == !fault &&
                                       run.step.store_tail_valid == (tail ? 2u : 0u));
                                assert(ccr() == expected_flags && EM_D0 == (condition ? m1 : c1) &&
                                       EM_D1 == (condition && pair ? m2 : c2));
                                if (tail)
                                    assert(run.step.store_tail == (0x55667788u & mask(n)) &&
                                           run.step.store_tail_address == lb);
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                                unsigned frame = EM_A7, fc = user ? 1 : 5, sz = n == 1 ? 0x20 : n == 2 ? 0x40 : 0;
                                assert(visible(frame + 12, 2) ==
                                       (0x200 | sz | fc | (fault ? 0x400 : 0) | (trace ? 0x2000 : 0)));
                                assert(visible(frame + (!fault ? 18 : 16), 2) == (0x80 | sz | fc));
                                assert(visible(frame + (!fault ? 40 : 32), 4) == logical_fault);
                                if (tail)
                                    assert(visible(frame + (!fault ? 16 : 14), 2) == (0x80 | sz | fc));
                                unsigned reads_before = operand_reads;
                                assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                                assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                       run.step.vector == (trace ? 9u : 0u) && cpu.pc == next &&
                                       operand_reads == reads_before);
                                assert(EM_A0 == la && EM_A1 == lb && EM_A2 == 0x12345678 && EM_D4 == 0xdeadbeef &&
                                       cpu_state.dfc == 7);
                                assert(cpu_state.sr == initial_sr && ccr() == expected_flags &&
                                       !memcmp(&before_fpu, &cpu_state.fpu, sizeof before_fpu));
                                assert(visible(la, n) == (condition ? m1 : 0xa1b2c3d4 & mask(n)));
                                assert(visible(lb, n) == (condition || !pair ? m2 : 0x55667788 & mask(n)));
                                if (trace) {
                                    assert(run.step.fault_address == 0x8000);
                                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8200);
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                                }
                                ++cases;
                            }
}

static void register_aliases(unsigned mode)
{
    for (unsigned n = 2; n <= 4; n *= 2)
        for (unsigned general = 0; general < 2; ++general)
            for (unsigned same_compare = 0; same_compare < 2; ++same_compare)
                for (unsigned update_alias = 0; update_alias < 2; ++update_alias)
                    for (unsigned condition = 0; condition < 3; ++condition)
                        for (unsigned user = 0; user < 2; ++user) {
                            unsigned a = 0x9000, b = 0x9080, first = a, second = same_compare ? a : b;
                            unsigned m1 = first ^ (condition == 1), m2 = second ^ (condition == 2);
                            Syn68kMappedCPU cpu = fixture(mode, 1, 1, 2, user, a, b, n, m1, m2);
                            unsigned trace = condition, compare2 = same_compare ? 0 : 1;
                            cpu_state.sr |= trace << 14;
                            EM_D0 = a;
                            EM_D1 = b;
                            EM_D2 = 0x11223344;
                            EM_D3 = 0x55667788;
                            EM_A0 = a;
                            EM_A1 = b;
                            unsigned before[16], expected[16];
                            for (unsigned i = 0; i < 16; ++i)
                                before[i] = expected[i] = EM_DREG(i);
                            unsigned u1 = before[update_alias ? 0 : 2] & mask(n),
                                     u2 = before[update_alias ? 1 : 3] & mask(n);
                            unsigned ea1 = general ? 0 : 8, ea2 = general ? 1 : 9;
                            put(0x8000, 2, n == 2 ? 0x0cfc : 0x0efc);
                            put(0x8002, 2, ea1 << 12 | (update_alias ? 0 : 2) << 6);
                            put(0x8004, 2, ea2 << 12 | (update_alias ? 1 : 3) << 6 | compare2);
                            if (condition) {
                                expected[compare2] = (expected[compare2] & ~mask(n)) | (m2 & mask(n));
                                expected[0] = (expected[0] & ~mask(n)) | (m1 & mask(n));
                            }
                            unsigned expected_flags =
                                comparison(condition == 1 ? m1 : m2, condition == 1 ? first : second, n, 0);
                            Syn68kMappedRun run;
                            unsigned called = model_calls;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            capturing = 0;
                            assert(mode ? model_calls == called : model_calls > called);
                            assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                   run.step.vector == (trace ? 9u : 0u) && cpu.pc == 0x8006 && ccr() == expected_flags);
                            for (unsigned i = 0; i < 16; ++i)
                                assert(EM_DREG(i) == expected[i]);
                            unsigned at = 0, fc = user ? 1 : 5;
                            expect(&at, a, n, 0, m1, fc, 0);
                            expect(&at, b, n, 0, m2, fc, 0);
                            if (!condition) {
                                expect(&at, a, n, 1, u1, fc, 1);
                                expect(&at, b, n, 1, u2, fc, 0);
                            }
                            else
                                expect(&at, b, n, 1, m2, fc, 0);
                            assert(at == count && get(a, n) == (condition ? m1 : u1) &&
                                   get(b, n) == (condition ? m2 : u2));
                            ++cases;
                        }
    for (unsigned n = 2; n <= 4; n *= 2)
        for (unsigned condition = 0; condition < 3; ++condition)
            for (unsigned trace = 0; trace < 4; ++trace) {
                unsigned a = 0x9000, m = 0x12345678 & mask(n);
                Syn68kMappedCPU cpu = fixture(mode, 1, 1, 0, 0, a, a, n, m, m);
                cpu_state.sr |= trace << 14;
                EM_A0 = EM_A1 = a;
                EM_D0 = m ^ (condition == 1);
                EM_D1 = m ^ (condition == 2);
                EM_D2 = 0xaabbccdd;
                EM_D3 = 0x56789abc;
                put(0x8000, 2, n == 2 ? 0x0cfc : 0x0efc);
                put(0x8002, 2, 0x8080);
                put(0x8004, 2, 0x90c1);
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                capturing = 0;
                assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) && cpu.pc == 0x8006);
                unsigned at = 0;
                expect(&at, a, n, 0, m, 5, 0);
                expect(&at, a, n, 0, m, 5, 0);
                if (!condition) {
                    expect(&at, a, n, 1, EM_D2 & mask(n), 5, 1);
                    expect(&at, a, n, 1, EM_D3 & mask(n), 5, 0);
                }
                else
                    expect(&at, a, n, 1, m, 5, 0);
                assert(at == count && get(a, n) == (condition ? m : EM_D3 & mask(n)));
                ++cases;
            }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        normal(mode);
        register_aliases(mode);
        write_faults(mode);
        instruction_view(mode);
    }
    printf("%u CAS/CAS2 mismatch/lock/cache/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
