/* SPDX-License-Identifier: MIT */
/* Copyback allocation failures retain the original logical store in WB2. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned armed, fail_line, fail_beat, source_reads, pushes, callbacks;
static Syn68kCache040Cycle failed_cycle;
static unsigned model_calls;

Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* mmu, const Syn68kMMU040Bus* bus, uint32_t logical,
                                                  unsigned fc, int writing)
{
    ++model_calls;
    return __real_syn68k_mmu040_translate(mmu, bus, logical, fc, writing);
}

Syn68kCache040Access __real_syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                     unsigned, int, uint32_t, uint32_t, uint32_t, int,
                                                     enum Syn68kCache040Traffic);

Syn68kCache040Access __wrap_syn68k_cache040_transfer(Syn68kCache040* cache, const Syn68kCache040Bus* bus,
                                                     uint32_t address, unsigned fc, unsigned size, int writing,
                                                     uint32_t value, uint32_t attributes, uint32_t cacr,
                                                     int no_allocate, enum Syn68kCache040Traffic traffic)
{
    ++model_calls;
    return __real_syn68k_cache040_transfer(cache, bus, address, fc, size, writing, value, attributes, cacr, no_allocate,
                                           traffic);
}

static int read_cycle(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                      const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    ++callbacks;
    *inhibit = address >= 0xc000 && address < 0xc010 && cycle->burst == 16 && !cycle->beat;
    if (address >= 0xc000 && address < 0xc010)
        ++source_reads;
    if (armed && (address & ~15u) == fail_line && cycle->burst == 16 && cycle->beat == fail_beat) {
        armed = 0;
        failed_cycle = *cycle;
        return 0;
    }
    return physical_read(opaque, address >= 0xfffff000 ? 0x9000 + (address & 0xfff) : address, bytes, size,
                         cycle->attributes, cycle->fc);
}

static int write_cycle(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    ++callbacks;
    if (cycle->kind == CACHE040_PUSH && (address & 0x3f0) == (fail_line & 0x3f0))
        ++pushes;
    return physical_write(opaque, address >= 0xfffff000 ? 0x9000 + (address & 0xfff) : address, bytes, size,
                          cycle->attributes, cycle->fc);
}

static int read_table(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(address, 4);
    return 1;
}

static int update_table(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == ram);
    put(address, 4, locked ? get(address, 4) | value : value);
    return 1;
}

static Syn68kCache040Bus physical;

static void bind(unsigned mode, unsigned alias, unsigned victim, unsigned line_address)
{
    if (mode == 2)
        native.ram.size = 0x9000;
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};
    binding.bus = &physical;
    mmu.tc = 0x8000;
    if (alias) {
        mmu.srp = mmu.urp = 0x1000;
        put(0x1008, 4, 0x200a);
        put(0x2000, 4, 0x300a);
        put(0x3000, 4, 0x9039);
        memory.tables = (Syn68kMMU040Bus){ram, read_table, update_table};
    }
    for (unsigned line = 0; line < 2; ++line)
        for (unsigned way = 0; way < 4; ++way) {
            unsigned address = line_address + line * 16;
            Syn68kCache040Line* entry = &cache.lines[0][(address >> 4) & 63][way];
            *entry = (Syn68kCache040Line){
                .address = 0xa000 + (address & 0x3f0) + way * 0x400, .valid = !!victim, .dirty = victim == 2 ? 15 : 0};
            memset(entry->bytes, 0x5a + way, 16);
        }
    armed = 1;
    source_reads = pushes = callbacks = 0;
    put(8, 4, 0x8100);
    put(36, 4, 0x8200);
    put(0x8200, 2, 0x4e73);
}

static unsigned handler_pc, handler_count;

static void word(unsigned value)
{
    put(handler_pc, 2, value);
    handler_pc += 2;
}

static void longword(unsigned value)
{
    put(handler_pc, 4, value);
    handler_pc += 4;
}

static unsigned cleanup(unsigned pending, unsigned fc, unsigned size)
{
    handler_pc = 0x8100;
    handler_count = 4;
    word(0x2f00);
    word(0x2f08);
    word(0x4e7a);
    word(1);
    word(0x2f00);
    for (unsigned slot = 0; slot < pending; ++slot) {
        word(0x7000 | fc);
        word(0x4e7b);
        word(1);
        word(0x202f);
        word((slot ? 28 : 36) + 12);
        word(0x206f);
        word((slot ? 24 : 32) + 12);
        word(0x0e10 | (size == 4 ? 2 : size == 2) << 6);
        word(0x0800);
        handler_count += 5;
    }
    word(0x201f);
    word(0x4e7b);
    word(1);
    word(0x205f);
    word(0x201f);
    word(0x4e73);
    return handler_count + 5;
}

static uint32_t visible(unsigned address, unsigned size)
{
    uint32_t value = 0;
    Syn68kMMU040Access access = syn68k_mmu040_read(&memory, address, 5, size, 0, &value);
    assert(!access.translation.fault && !access.bus_error);
    return value;
}

static void scalar(unsigned mode)
{
    for (unsigned offset = 0; offset < 16; ++offset)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned alias = 0; alias < 2; ++alias)
                for (unsigned victim = 0; victim < 3; ++victim)
                    for (unsigned line = 0; line < 1 + (offset + size > 16); ++line)
                        for (unsigned beat = 0; beat < 4; ++beat)
                            for (unsigned trace = 0; trace < 3; ++trace)
                                for (unsigned user = 0; user < 2; ++user)
                                    for (unsigned negative = 0; negative < 2; ++negative) {
                                        Syn68kMappedCPU cpu = setup(mode);
                                        bind(mode, alias, victim, 0x9040);
                                        unsigned target = (alias ? 0x04000040 : 0x9040) + offset;
                                        unsigned fc = user ? 1 : 5;
                                        cpu_state.sr = (user ? 0 : 0x2000) | trace << 14;
                                        cpu_state.ccx = cpu_state.ccc = cpu_state.ccv = 1;
                                        cpu_state.isp = 0xd000;
                                        EM_A7 = user ? 0xe000 : 0xd000;
                                        EM_A0 = target;
                                        EM_A1 = 0xc000;
                                        put(0xc000, 4, negative ? 0x80ffffff : 0);
                                        unsigned value = get(0xc000, size);
                                        put(0x8000, 2, (size == 1 ? 0x1000 : size == 2 ? 0x3000 : 0x2000) | 0xd9);
                                        fail_line = 0x9040 + line * 16;
                                        fail_beat = beat;
                                        unsigned current = line ? 0 : offset;
                                        unsigned chunk = line ? size - (16 - offset) : size;
                                        if (chunk > 16 - current)
                                            chunk = 16 - current;
                                        unsigned fault = !beat || (beat == 1 && (current & 3) + chunk > 4);
                                        unsigned steps = cleanup(1, fc, size);
                                        Syn68kMappedRun run;
                                        unsigned called = model_calls;
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        assert(mode ? model_calls == called : model_calls > called);
                                        assert(!armed && source_reads == 4 && !pushes);
                                        unsigned ccr = 16 | (!value ? 4 : 0) | (value & (1u << (size * 8 - 1)) ? 8 : 0);
                                        if (fault) {
                                            if (run.step.store_state != (trace == 2 ? 3u : 2u))
                                                fprintf(
                                                    stderr,
                                                    "allocate mode=%u off=%u size=%u alias=%u line=%u beat=%u state=%u reason=%u\n",
                                                    mode, offset, size, alias, line, beat, run.step.store_state,
                                                    run.reason);
                                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                                   run.step.store_state == (trace == 2 ? 3u : 2u) && cpu.pc == 0x8002);
                                            assert(run.step.access.cache.fault == CACHE040_FILL_FAULT &&
                                                   run.step.access.completed == (line ? 16 - offset : 0));
                                            Syn68kCache040Line* old =
                                                &cache.lines[0][(fail_line >> 4) & 63][failed_cycle.way];
                                            assert(old->valid == (victim == 2));
                                            if (victim == 2) {
                                                assert(old->dirty == 15 &&
                                                       old->address ==
                                                           0xa000 + (fail_line & 0x3f0) + failed_cycle.way * 0x400);
                                                for (unsigned byte = 0; byte < 16; ++byte)
                                                    assert(old->bytes[byte] == 0x5a + failed_cycle.way);
                                            }
                                            Syn68kMappedDelivery delivery;
                                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                                            unsigned sp = EM_A7;
                                            unsigned status = fc | (size == 1 ? 0x20 : size == 2 ? 0x40 : 0);
                                            assert(visible(sp + 12, 2) == (status | (trace == 2 ? 0x2000 : 0)));
                                            assert(visible(sp + 8, 4) == (trace == 2 ? 0x8000 : 0));
                                            assert(!visible(sp + 14, 2) && !visible(sp + 18, 2));
                                            assert(visible(sp + 16, 2) == (status | 0x80));
                                            assert(visible(sp + 20, 4) == target && visible(sp + 32, 4) == target &&
                                                   visible(sp + 36, 4) == value && !visible(sp + 40, 4));
                                            for (unsigned index = 0; index < 4; ++index)
                                                assert(!visible(sp + 44 + index * 4, 4));
                                            assert(visible(sp, 2) == ((user ? 0 : 0x2000) | trace << 14 | ccr));
                                            assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                                            assert(run.reason ==
                                                   (trace == 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                                        }
                                        else
                                            assert(run.reason ==
                                                   (trace == 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                                        if (trace == 2)
                                            assert(run.step.vector == 9 && run.step.fault_address == 0x8000);
                                        assert(cpu.pc == 0x8002 && !cpu.halted && EM_A0 == target + size &&
                                               EM_A1 == 0xc000 + size);
                                        assert(EM_A7 == (user ? 0xe000 : 0xd000) && visible(target, size) == value);
                                        assert(source_reads == 4 && (COMPUTE_SR_FROM_CPU_STATE() & 31) == ccr);
                                        ++cases;
                                    }
}

static void compound(unsigned mode)
{
    for (unsigned family = 0; family < 10; ++family)
        for (unsigned stage = 0; stage < 3; ++stage)
            for (unsigned victim = 0; victim < 3; ++victim)
                for (unsigned trace = 0; trace < 3; ++trace) {
                    if (family == 1)
                        continue;
                    unsigned words = family < 2                   ? 2
                                     : family == 2 || family == 7 ? 1
                                     : family == 3                ? 2
                                     : family == 8                ? 13
                                     : family == 9                ? 25
                                                                  : 3;
                    if ((words == 1 && stage != 2) || (words == 2 && !stage))
                        continue;
                    unsigned index = !stage ? 0 : stage == 1 ? words - 2 : words - 1;
                    unsigned start = 0x9040 - index * 4;
                    Syn68kMappedCPU cpu = setup(mode);
                    bind(mode, 0, victim, 0x9040);
                    mmu.dtt[0] = 0xc020;
                    cpu_state.sr = 0x2000 | trace << 14;
                    EM_A0 = start;
                    EM_D0 = 0x12345678;
                    EM_D1 = 0x87654321;
                    cpu_state.fpu.null_state = 0;
                    cpu_state.fpu.fpiar = 0x8000;
                    cpu_state.fpu.regs[0].exponent = 0x3fff;
                    cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
                    unsigned output[25] = {0x3fff0000, 0x80000000, 0};
                    unsigned next = family >= 7 ? 0x8002 : 0x8004;
                    unsigned auto_update = family == 1 || (family >= 2 && family <= 4) || family == 6;
                    if (family < 2) {
                        put(0x8000, 2, family ? 0x4cd8 : 0x48d0);
                        put(0x8002, 2, 3);
                        output[0] = EM_D0;
                        output[1] = EM_D1;
                        if (family)
                            for (unsigned word = 0; word < 2; ++word)
                                put(start + word * 4, 4, output[word]);
                    }
                    else if (family <= 6) {
                        put(0x8000, 2, family == 5 ? 0xf210 : 0xf218);
                        unsigned command = family == 2   ? 0x6400
                                           : family == 3 ? 0x7400
                                           : family == 4 ? 0x6800
                                           : family == 5 ? 0xf080
                                                         : 0xbc00;
                        put(0x8002, 2, command);
                        if (family == 2)
                            output[0] = 0x3f800000;
                        if (family == 3) {
                            output[0] = 0x3ff00000;
                            output[1] = 0;
                        }
                        if (family == 6) {
                            output[0] = 0;
                            output[1] = 0;
                            output[2] = 0x8000;
                        }
                    }
                    else {
                        put(0x8000, 2, 0xf310);
                        if (family == 7) {
                            cpu_state.fpu.null_state = 1;
                            output[0] = 0;
                        }
                        else {
                            unsigned size = words * 4;
                            cpu_state.fpu.frame_size = size;
                            cpu_state.fpu.pending_vector = 55;
                            for (unsigned byte = 0; byte < size; ++byte)
                                cpu_state.fpu.frame[byte] = byte ^ 0x5a;
                            cpu_state.fpu.frame[0] = 0x41;
                            cpu_state.fpu.frame[1] = size - 4;
                            cpu_state.fpu.frame[2] = cpu_state.fpu.frame[3] = 0;
                            for (unsigned word = 0; word < words; ++word) {
                                output[word] = 0;
                                for (unsigned byte = 0; byte < 4; ++byte)
                                    output[word] = output[word] << 8 | cpu_state.fpu.frame[word * 4 + byte];
                            }
                        }
                    }
                    fail_line = 0x9040;
                    fail_beat = 0;
                    unsigned cm = family < 2, abort = !cm && index + 2 < words;
                    unsigned tail = !cm && !abort && index + 2 == words;
                    unsigned current_size = (family == 4 || family == 5) && !index ? 2 : 4;
                    unsigned current_value = current_size == 2 ? output[index] >> 16 : output[index];
                    unsigned serial = family == 5 || family == 6 || family >= 7;
                    unsigned traced = trace == 2 || (trace == 1 && serial);
                    Syn68kMappedRun run;
                    unsigned called = model_calls;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(mode ? model_calls == called : model_calls > called);
                    if (run.reason != SYN68K_MAPPED_EXCEPTION || run.step.vector != 2 ||
                        run.step.store_state != (cm       ? 0u
                                                 : abort  ? 4u
                                                 : traced ? 3u
                                                          : 2u))
                        fprintf(stderr,
                                "compound allocation mode=%u family=%u stage=%u state=%u vector=%u pc=%x done=%u\n",
                                mode, family, stage, run.step.store_state, run.step.vector, cpu.pc,
                                run.step.access.completed);
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                           run.step.movem_state == cm &&
                           run.step.store_state == (cm       ? 0u
                                                    : abort  ? 4u
                                                    : traced ? 3u
                                                             : 2u));
                    assert(cpu.pc == (cm || abort ? 0x8000 : next));
                    if (family >= 8)
                        assert(cpu_state.fpu.frame_size == (abort ? words * 4 : 0));
                    unsigned steps = cleanup(1 + tail, 5, current_size);
                    Syn68kMappedDelivery delivery;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                    unsigned sp = EM_A7;
                    assert(visible(sp + 2, 4) == (cm || abort ? 0x8000 : next));
                    assert(visible(sp + 8, 4) == (cm ? start : !abort && traced ? 0x8000 : 0));
                    unsigned status = 5 | (current_size == 2 ? 0x40 : 0);
                    assert(visible(sp + 12, 2) == (status | (cm ? 0x1000 : !abort && traced ? 0x2000 : 0)));
                    assert(visible(sp + 14, 2) == (tail ? 0x85 : 0) && visible(sp + 16, 2) == (status | 0x80) &&
                           !visible(sp + 18, 2));
                    assert(visible(sp + 20, 4) == 0x9040 && !visible(sp + 40, 4));
                    assert(visible(sp + 32, 4) == 0x9040 && visible(sp + 36, 4) == current_value);
                    assert(visible(sp + 24, 4) == (tail ? 0x9044 : 0) &&
                           visible(sp + 28, 4) == (tail ? output[index + 1] : 0));
                    for (unsigned word = 0; word < 4; ++word)
                        assert(!visible(sp + 44 + word * 4, 4));
                    assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                    if (cm || abort) {
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    }
                    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) && cpu.pc == next);
                    if (traced) {
                        assert(run.step.vector == 9 && run.step.fault_address == 0x8000);
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                    }
                    for (unsigned word = 0; word < words; ++word)
                        assert(visible(start + word * 4, 4) == output[word]);
                    assert(EM_A0 == start + (auto_update ? words * 4 : 0) && EM_A7 == 0xa000);
                    if (family >= 8)
                        assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
                    ++cases;
                }
}

static void numeric(unsigned mode)
{
    for (unsigned alias = 0; alias < 2; ++alias)
        for (unsigned offset = 0; offset < 2; ++offset)
            for (unsigned trace = 0; trace < 3; ++trace) {
                Syn68kMappedCPU cpu = setup(mode);
                bind(mode, alias, 2, 0x9040);
                unsigned target = (alias ? 0x04000040 : 0x9040) + (offset ? 15 : 0);
                cpu_state.sr = 0x2000 | trace << 14;
                cpu_state.fpu.null_state = 0;
                cpu_state.fpu.fpcr = 0x200;
                cpu_state.fpu.fpiar = 0x8000;
                cpu_state.fpu.regs[0].exponent = 0x3fff;
                cpu_state.fpu.regs[0].significand = UINT64_C(0xc000000000000000);
                EM_A0 = target;
                put(0x8000, 2, 0xf218);
                put(0x8002, 2, 0x7000);
                put(49 * 4, 4, 0x8300);
                fail_line = offset ? 0x9050 : 0x9040;
                fail_beat = 0;
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.store_state == 2 &&
                       run.step.fpu_vector == 49 && cpu.pc == 0x8004 && EM_A0 == target + 2);
                assert(run.step.access.completed == offset);
                unsigned frame_size = cpu_state.fpu.frame_size;
                unsigned char frame[sizeof cpu_state.fpu.frame];
                memcpy(frame, cpu_state.fpu.frame, sizeof frame);
                unsigned steps = cleanup(1, 5, 2);
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                unsigned sp = EM_A7;
                assert(visible(sp + 8, 4) == target && visible(sp + 12, 2) == 0x8045);
                assert(!visible(sp + 14, 2) && visible(sp + 16, 2) == 0xc5 && !visible(sp + 18, 2));
                assert(visible(sp + 20, 4) == target && visible(sp + 32, 4) == target && visible(sp + 36, 4) == 2);
                assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 49 && run.step.format == 3 &&
                       run.step.fault_address == target && cpu.pc == 0x8004);
                assert(visible(target, 2) == 2 && cpu_state.fpu.frame_size == frame_size &&
                       cpu_state.fpu.pending_vector == 49 && !memcmp(frame, cpu_state.fpu.frame, sizeof frame));
                ++cases;
            }
}

static void page_prefix(unsigned mode)
{
    for (unsigned wrap = 0; wrap < 2; ++wrap)
        for (unsigned offset = 4093; offset < 4096; ++offset)
            for (unsigned size = 2; size <= 4; size *= 2)
                for (unsigned trace = 0; trace < 3; ++trace)
                    for (unsigned user = 0; user < 2; ++user) {
                        if (offset + size <= 4096)
                            continue;
                        Syn68kMappedCPU cpu = setup(mode);
                        bind(mode, !wrap, 2, wrap ? 0xfffffff0u : 0x9ff0);
                        if (wrap) {
                            mmu.tc = 0;
                            mmu.dtt[1] = 0xff00c020;
                        }
                        else
                            put(0x3004, 4, 0xb039);
                        if (mode == 2)
                            native.ram = (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
                        uint32_t target = (wrap ? 0xfffff000u : 0x04000000u) + offset;
                        unsigned fc = user ? 1 : 5;
                        cpu_state.sr = (user ? 0 : 0x2000) | trace << 14;
                        cpu_state.isp = 0xd000;
                        EM_A7 = user ? 0xe000 : 0xd000;
                        EM_A0 = target;
                        EM_A1 = 0xc000;
                        put(0xc000, 4, 0x80abcdef);
                        unsigned value = get(0xc000, size);
                        put(0x8000, 2, (size == 2 ? 0x3000 : 0x2000) | 0xd9);
                        fail_line = wrap ? 0 : 0xb000;
                        fail_beat = 0;
                        Syn68kMappedRun run;
                        unsigned called = model_calls;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(mode ? model_calls == called : model_calls > called);
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                               run.step.access.completed == 4096 - offset && cpu.pc == 0x8002);
                        unsigned steps = cleanup(1, fc, size);
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                        unsigned status = fc | (size == 2 ? 0x40 : 0);
                        assert(visible(EM_A7 + 12, 2) == (status | (trace == 2 ? 0x2000 : 0)));
                        assert(visible(EM_A7 + 16, 2) == (status | 0x80) && !visible(EM_A7 + 18, 2));
                        assert(visible(EM_A7 + 20, 4) == target && visible(EM_A7 + 32, 4) == target &&
                               visible(EM_A7 + 36, 4) == value);
                        assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                        assert(run.reason == (trace == 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                        assert(cpu.pc == 0x8002 && EM_A0 == (uint32_t)(target + size) && EM_A1 == 0xc000 + size);
                        assert(visible(target, size) == value && source_reads == 4);
                        ++cases;
                    }
}

static void invalid_metadata(unsigned mode)
{
    Syn68kMappedCPU cpu = setup(mode);
    bind(mode, 0, 2, 0x9040);
    EM_A0 = 0x9043;
    EM_A1 = 0xc000;
    put(0x8000, 2, 0x20d9);
    fail_line = 0x9040;
    fail_beat = 0;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.step.store_state == 2);
    const Syn68kIntegerStep original = run.step;
    assert(!syn68k_mmu040_access_ssw(&original.access, NULL));
    CPUState state = cpu_state;
    Syn68kMMU040 saved_mmu = mmu;
    Syn68kCache040 saved_cache = cache;
    Syn68kMappedCPU saved_cpu = cpu;
    unsigned count = callbacks;
    for (unsigned bad = 0; bad < 25; ++bad) {
        Syn68kIntegerStep record = original;
        Syn68kMMU040Access* a = &record.access;
        switch (bad) {
        case 0:
            a->writing = 2;
            break;
        case 1:
            a->rmw = 1;
            break;
        case 2:
            a->bus_error = 0;
            break;
        case 3:
            a->translation.fault = MMU040_INVALID;
            break;
        case 4:
            a->cache_exception = 0;
            break;
        case 5:
            a->cache_table = 1;
            break;
        case 6:
            a->fc = a->cache.cycle.fc = 2;
            break;
        case 7:
            a->fc = a->cache.cycle.fc = 3;
            break;
        case 8:
            a->fc = 8;
            break;
        case 9:
            a->cache.cycle.kind = CACHE040_PUSH;
            break;
        case 10:
            a->cache.cycle.burst = 0;
            break;
        case 11:
            a->cache.cycle.beat = 4;
            break;
        case 12:
            a->cache.cycle.way = 4;
            break;
        case 13:
            a->cache.cycle.locked = 1;
            break;
        case 14:
            a->cache.cycle.lock_end = 1;
            break;
        case 15:
            a->cache.interrupt_level = 1;
            break;
        case 16:
            a->cache.cycle.traffic = CACHE040_EXCEPTION;
            break;
        case 17:
            a->cache.cycle.attributes = a->translation.attributes = 0;
            break;
        case 18:
            a->cache.cycle.attributes ^= 0x100;
            break;
        case 19:
            a->cache.completed = a->completed + 1;
            break;
        case 20:
            a->logical ^= 1;
            break;
        case 21:
            a->cache.bus_address ^= 4;
            break;
        case 22:
            a->cache.cycle.beat = 2;
            a->cache.bus_address = 0x9048;
            break;
        case 23:
            a->size = 3;
            break;
        case 24:
            a->completed = a->size;
            break;
        }
        uint16_t status = 0xaaaa;
        assert(!syn68k_mmu040_access_ssw(a, &status) && status == 0xaaaa);
        assert(!syn68k_mmu040_operand_status(a, &status) && status == 0xaaaa);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && callbacks == count);
        assert(!memcmp(&cpu, &saved_cpu, sizeof cpu) && !memcmp(&cpu_state, &state, sizeof state));
        assert(!memcmp(&mmu, &saved_mmu, sizeof mmu) && !memcmp(&cache, &saved_cache, sizeof cache));
        ++cases;
    }
}

static void move16_guard(unsigned mode)
{
    Syn68kMappedCPU cpu = setup(mode);
    bind(mode, 0, 2, 0x9040);
    EM_A0 = 0xc000;
    put(0x8000, 2, 0xf600);
    put(0x8002, 4, 0x9040);
    fail_line = 0x9040;
    fail_beat = 0;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && cpu.pc == 0x8000);
    assert(run.step.access.cache.fault == CACHE040_FILL_FAULT && !run.step.move16.state);
    CPUState state = cpu_state;
    unsigned count = callbacks;
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && callbacks == count &&
           !memcmp(&state, &cpu_state, sizeof state) && EM_A0 == 0xc000);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        scalar(mode);
        compound(mode);
        numeric(mode);
        page_prefix(mode);
        invalid_metadata(mode);
        move16_guard(mode);
    }
    printf("%u copyback allocation/frame/WB2/guest-cleanup/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
}
