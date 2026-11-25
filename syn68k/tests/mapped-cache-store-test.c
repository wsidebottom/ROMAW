/* SPDX-License-Identifier: MIT */
/* Logical store retirement remains separate from physical cache-push cleanup. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static Syn68kCache040Bus physical;
static unsigned fail_address, armed, source_reads, operand_writes, target, width;
static unsigned any_victim, fail_beat, failed_push;

static int read_cycle(void* opaque, uint32_t address, unsigned char* bytes, unsigned size,
                      const Syn68kCache040Cycle* cycle, unsigned* inhibit)
{
    if (address >= 0xb000 && address < 0xb010) {
        ++source_reads;
        *inhibit = cycle->burst == 16 && !cycle->beat; /* Side-effect read is not retained. */
    }
    else
        *inhibit = 0;
    return physical_read(opaque, address, bytes, size, cycle->attributes, cycle->fc);
}

static int write_cycle(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                       const Syn68kCache040Cycle* cycle)
{
    if (armed && cycle->kind == CACHE040_PUSH &&
        ((any_victim && cycle->beat == fail_beat && (address & 0x3f0) == 0x40) ||
         (!any_victim && address == fail_address))) {
        armed = 0;
        failed_push = address - cycle->beat * 4;
        return 0;
    }
    if (cycle->kind == CACHE040_OPERAND && cycle->fc && address >= target && address < target + width)
        operand_writes += size;
    return physical_write(opaque, address, bytes, size, cycle->attributes, cycle->fc);
}

static void seed(unsigned address, unsigned dirty, unsigned way)
{
    Syn68kCache040Line* line = &cache.lines[0][(address >> 4) & 63][way];
    *line = (Syn68kCache040Line){.address = address, .valid = 1, .dirty = dirty};
    for (unsigned word = 0; word < 4; ++word) {
        uint32_t value = dirty & (1u << word) ? 0x11223344 + word * 0x11223344 : 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            line->bytes[word * 4 + byte] = value >> (24 - byte * 8);
    }
}

static void bind(unsigned mode)
{
    if (mode == 2)
        native.ram.size = 0x9000;
    physical = (Syn68kCache040Bus){.opaque = ram, .read_cycle = read_cycle, .write_cycle = write_cycle};
    binding.bus = &physical;
    armed = 1;
    source_reads = operand_writes = any_victim = failed_push = 0;
    cpu_state.dfc = 0;
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

static void move_frame(unsigned offset)
{
    word(0x202f);
    word(offset + 12);
    ++handler_count;
}

static void dfc(unsigned fc)
{
    word(0x7000 | fc);
    word(0x4e7b);
    word(1);
    handler_count += 2;
}

/* Save working registers below the frame: EA is live for CM/CT/CP. Drain PD
   with physical MOVES, then WB2/WB3 with their logical FC/size, and RTE. */
static unsigned cleanup(unsigned push_address, unsigned pending, unsigned first_fc, unsigned first_size,
                        unsigned tail_fc, unsigned tail_size)
{
    handler_pc = 0x8100;
    handler_count = 0;
    word(0x2f00);
    word(0x2f08); /* Save D0 and A0. */
    word(0x4e7a);
    word(1);
    word(0x2f00);
    handler_count += 4; /* Save DFC. */
    dfc(0);
    for (unsigned index = 0; index < 4; ++index) {
        move_frame(44 + index * 4);
        word(0x0eb9);
        word(0x0800);
        longword(push_address + index * 4);
        ++handler_count;
    }
    for (unsigned slot = 0; slot < pending; ++slot) {
        unsigned fc = slot ? tail_fc : first_fc, size = slot ? tail_size : first_size;
        dfc(fc);
        move_frame(slot ? 28 : 36);
        word(0x206f);
        word((slot ? 24 : 32) + 12);
        ++handler_count;
        word(0x0e10 | (size == 4 ? 2 : size == 2) << 6);
        word(0x0800);
        ++handler_count;
    }
    word(0x201f);
    word(0x4e7b);
    word(1);
    word(0x205f);
    word(0x201f);
    word(0x4e73);
    handler_count += 5;
    return handler_count;
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
    const unsigned values[] = {0, 0x7fffffff, 0x80000000};
    for (unsigned offset = 0; offset < 16; ++offset)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned copyback = 0; copyback < 2; ++copyback)
                for (unsigned dirty_kind = 0; dirty_kind < 2; ++dirty_kind)
                    for (unsigned beat = 0; beat < (dirty_kind ? 4u : 1u); ++beat)
                        for (unsigned trace = 0; trace < 3; ++trace)
                            for (unsigned user = 0; user < 2; ++user)
                                for (unsigned v = 0; v < 3; ++v) {
                                    Syn68kMappedCPU cpu = setup(mode);
                                    bind(mode);
                                    target = 0x9040 + offset;
                                    width = size;
                                    unsigned fc = user ? 1 : 5;
                                    cpu_state.sr = (user ? 0 : 0x2000) | trace << 14;
                                    cpu_state.ccx = cpu_state.ccc = cpu_state.ccv = 1;
                                    cpu_state.isp = 0xa000;
                                    EM_A7 = user ? 0xc000 : 0xa000;
                                    EM_A0 = target;
                                    EM_A1 = 0xb000;
                                    mmu.tc = 0x8000;
                                    mmu.dtt[0] = copyback ? 0xc020 : 0xc040;
                                    put(0xb000, 4, values[v]);
                                    unsigned value = get(0xb000, size), dirty = dirty_kind ? 15 : 1;
                                    unsigned victim = copyback ? 0xa040 : 0x9040;
                                    for (unsigned way = 0; way < (copyback ? 4u : 1u); ++way)
                                        seed(victim + way * 0x400, way ? 0 : dirty, way);
                                    cache.replacement[0] = copyback ? 3 : 0;
                                    unsigned char expected[16];
                                    memcpy(expected, cache.lines[0][4][0].bytes, sizeof expected);
                                    fail_address = victim + beat * 4;
                                    put(0x8000, 2, (size == 1 ? 0x1000 : size == 2 ? 0x3000 : 0x2000) | 0xd9);
                                    unsigned complete = copyback ? (size < 16 - offset ? size : 16 - offset) : 0;
                                    unsigned pending = complete < size;
                                    unsigned steps = cleanup(victim, pending, fc, size, 0, 0);
                                    Syn68kMappedRun run;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    unsigned traced = trace == 2,
                                             ccr = 16 | (!value ? 4 : 0) | (value & (1u << (size * 8 - 1)) ? 8 : 0);
                                    if (run.step.store_state != (traced ? 3u : 2u) || cpu.pc != 0x8002)
                                        fprintf(
                                            stderr,
                                            "store push retirement mode=%u target=%x size=%u cb=%u done=%u state=%u pc=%x\n",
                                            mode, target, size, copyback, run.step.access.completed,
                                            run.step.store_state, cpu.pc);
                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                           !run.completed && run.step.store_state == (traced ? 3u : 2u) &&
                                           cpu.pc == 0x8002);
                                    assert(run.step.access.completed == complete && !run.step.cache_restart &&
                                           EM_A0 == target + size && EM_A1 == 0xb000 + size);
                                    assert(source_reads == (copyback ? 4u : 1u) && !operand_writes);
                                    Syn68kMappedDelivery delivery;
                                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                                    unsigned sp = EM_A7;
                                    assert(visible(sp, 2) == ((user ? 0 : 0x2000) | trace << 14 | ccr));
                                    assert(visible(sp + 2, 4) == 0x8002 && visible(sp + 6, 2) == 0x7008);
                                    assert(visible(sp + 8, 4) == (traced ? 0x8000 : 0));
                                    assert(visible(sp + 12, 2) == ((dirty_kind ? 0x60 : 0) | (traced ? 0x2000 : 0)));
                                    assert(!visible(sp + 14, 2) && !visible(sp + 18, 2));
                                    assert(visible(sp + 16, 2) == (pending ? 0x80 | fc |
                                                                                 (size == 1   ? 0x20
                                                                                  : size == 2 ? 0x40
                                                                                              : 0)
                                                                           : 0));
                                    assert(visible(sp + 20, 4) == victim && visible(sp + 40, 4) == victim);
                                    assert(visible(sp + 32, 4) == (pending ? target : 0) &&
                                           visible(sp + 36, 4) == (pending ? value : 0));
                                    for (unsigned word = 0; word < 4; ++word) {
                                        unsigned want = 0;
                                        for (unsigned byte = 0; byte < 4; ++byte)
                                            want = want << 8 | expected[word * 4 + byte];
                                        assert(visible(sp + 44 + word * 4, 4) == want);
                                    }
                                    assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                                    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                           cpu.pc == 0x8002);
                                    if (traced) {
                                        assert(run.step.vector == 9 && run.step.format == 2 &&
                                               run.step.fault_address == 0x8000);
                                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8200);
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8002);
                                    }
                                    assert(visible(target, size) == value && EM_A7 == (user ? 0xc000 : 0xa000) &&
                                           EM_A0 == target + size && EM_A1 == 0xb000 + size && !cpu_state.dfc);
                                    assert(source_reads == (copyback ? 4u : 1u) &&
                                           operand_writes == (copyback ? 0u : size));
                                    if (copyback)
                                        assert(!memcmp(ram + victim, expected, sizeof expected));
                                    ++cases;
                                }
}

static void compound(unsigned mode)
{
    for (unsigned family = 0; family < 10; ++family)
        for (unsigned stage = 0; stage < 3; ++stage)
            for (unsigned copyback = 0; copyback < 2; ++copyback)
                for (unsigned trace = 0; trace < 3; ++trace)
                    for (unsigned beat = 0; beat < 4; ++beat) {
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
                        bind(mode);
                        mmu.dtt[0] = copyback ? 0xc020 : 0xc040;
                        target = start;
                        width = words * 4;
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
                        for (unsigned way = 0; way < (copyback ? 4u : 1u); ++way)
                            seed((copyback ? 0xa040 : 0x9040) + way * 0x400, 15, way);
                        unsigned char expected[16];
                        memcpy(expected, cache.lines[0][4][0].bytes, sizeof expected);
                        if (family == 1 && !copyback)
                            for (unsigned word = 0; word < words; ++word) {
                                unsigned address = start + word * 4;
                                if (address >= 0x9040 && address < 0x9050) {
                                    output[word] = 0;
                                    for (unsigned byte = 0; byte < 4; ++byte)
                                        output[word] = output[word] << 8 | expected[address - 0x9040 + byte];
                                }
                            }
                        any_victim = 1;
                        fail_beat = beat;
                        unsigned cm = family < 2, abort = !cm && index + 2 < words;
                        unsigned tail = !cm && !abort && index + 2 == words;
                        unsigned operand = family != 1 && !copyback;
                        unsigned current_size = (family == 4 || family == 5) && !index ? 2 : 4;
                        unsigned current_value = current_size == 2 ? output[index] >> 16 : output[index];
                        unsigned serial = family == 5 || family == 6 || family >= 7;
                        unsigned traced = trace == 2 || (trace == 1 && serial);
                        Syn68kMappedRun run;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        if (run.reason != SYN68K_MAPPED_EXCEPTION || run.step.vector != 2 ||
                            run.step.store_state != (cm       ? 0u
                                                     : abort  ? 4u
                                                     : traced ? 3u
                                                              : 2u))
                            fprintf(stderr,
                                    "compound push mode=%u family=%u stage=%u state=%u vector=%u pc=%x done=%u\n", mode,
                                    family, stage, run.step.store_state, run.step.vector, cpu.pc,
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
                        assert(failed_push == (copyback ? 0xa040 : 0x9040) ||
                               (copyback && (failed_push == 0xa440 || failed_push == 0xa840 || failed_push == 0xac40)));
                        unsigned steps = cleanup(failed_push, operand + tail, 5, operand ? current_size : 4, 5, 4);
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                        unsigned sp = EM_A7;
                        assert(visible(sp + 2, 4) == (cm || abort ? 0x8000 : next));
                        assert(visible(sp + 8, 4) == (cm ? start : !abort && traced ? 0x8000 : 0));
                        assert(visible(sp + 12, 2) == (0x60 | (cm ? 0x1000 : !abort && traced ? 0x2000 : 0)));
                        assert(visible(sp + 14, 2) == (operand && tail ? 0x85 : 0) &&
                               visible(sp + 16, 2) == (operand ? 0x85 | (current_size == 2 ? 0x40 : 0)
                                                       : tail  ? 0x85
                                                               : 0) &&
                               !visible(sp + 18, 2));
                        assert(visible(sp + 20, 4) == failed_push && visible(sp + 40, 4) == failed_push);
                        assert(visible(sp + 32, 4) == (operand ? 0x9040
                                                       : tail  ? 0x9044
                                                               : 0) &&
                               visible(sp + 36, 4) == (operand ? current_value
                                                       : tail  ? output[index + 1]
                                                               : 0));
                        assert(visible(sp + 24, 4) == (operand && tail ? 0x9044 : 0) &&
                               visible(sp + 28, 4) == (operand && tail ? output[index + 1] : 0));
                        assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                        if (cm || abort) {
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000);
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        }
                        assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                               cpu.pc == next);
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
                        if (copyback)
                            assert(!memcmp(ram + failed_push, expected, sizeof expected));
                        if (family >= 8)
                            assert(!cpu_state.fpu.frame_size && !cpu_state.fpu.pending_vector);
                        ++cases;
                    }
}

static void numeric_pending(unsigned mode)
{
    for (unsigned copyback = 0; copyback < 2; ++copyback)
        for (unsigned trace = 0; trace < 3; ++trace)
            for (unsigned offset = 0; offset < 2; ++offset)
                for (unsigned beat = 0; beat < 4; ++beat) {
                    Syn68kMappedCPU cpu = setup(mode);
                    bind(mode);
                    target = offset ? 0x904f : 0x9040;
                    width = 2;
                    cpu_state.sr = 0x2000 | trace << 14;
                    EM_A0 = target;
                    mmu.dtt[0] = copyback ? 0xc020 : 0xc040;
                    cpu_state.fpu.null_state = 0;
                    cpu_state.fpu.fpcr = 0x0200; /* Enabled INEX2: conversion of 1.5 to word. */
                    cpu_state.fpu.fpiar = 0x8000;
                    cpu_state.fpu.regs[0].exponent = 0x3fff;
                    cpu_state.fpu.regs[0].significand = UINT64_C(0xc000000000000000);
                    put(0x8000, 2, 0xf218);
                    put(0x8002, 2, 0x7000);
                    put(49 * 4, 4, 0x8300);
                    for (unsigned way = 0; way < (copyback ? 4u : 1u); ++way)
                        seed((copyback ? 0xa040 : 0x9040) + way * 0x400, 15, way);
                    any_victim = 1;
                    fail_beat = beat;
                    Syn68kMappedRun run;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.store_state == 2 &&
                           run.step.fpu_vector == 49 && cpu.pc == 0x8004 && EM_A0 == target + 2 &&
                           cpu_state.fpu.pending_vector == 49);
                    unsigned pending = !copyback || offset;
                    assert(run.step.access.completed == (copyback ? offset ? 1u : 2u : 0u));
                    unsigned frame_size = cpu_state.fpu.frame_size;
                    unsigned char frame[sizeof cpu_state.fpu.frame];
                    memcpy(frame, cpu_state.fpu.frame, sizeof frame);
                    unsigned steps = cleanup(failed_push, pending, 5, 2, 0, 0);
                    Syn68kMappedDelivery delivery;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
                    unsigned sp = EM_A7;
                    assert(visible(sp + 2, 4) == 0x8004 && visible(sp + 8, 4) == target);
                    assert(visible(sp + 12, 2) == 0x8060 && !visible(sp + 14, 2) &&
                           visible(sp + 16, 2) == (pending ? 0xc5 : 0) && !visible(sp + 18, 2));
                    assert(visible(sp + 20, 4) == failed_push && visible(sp + 40, 4) == failed_push);
                    assert(visible(sp + 32, 4) == (pending ? target : 0) && visible(sp + 36, 4) == (pending ? 2 : 0));
                    assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 49 && run.step.format == 3 &&
                           run.step.fault_address == target && cpu.pc == 0x8004);
                    assert(visible(target, 2) == 2 && EM_A0 == target + 2 && cpu_state.fpu.frame_size == frame_size &&
                           cpu_state.fpu.pending_vector == 49 && !memcmp(frame, cpu_state.fpu.frame, sizeof frame));
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8300);
                    ++cases;
                }
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(address, 4);
    return 1;
}

static int table_update(void* opaque, uint32_t address, uint32_t bits, int locked)
{
    assert(opaque == ram);
    (void)locked;
    put(address, 4, get(address, 4) | bits);
    return 1;
}

static void descriptor_store(unsigned mode)
{
    for (unsigned trace = 0; trace < 3; ++trace) {
        Syn68kMappedCPU cpu = setup(mode);
        bind(mode);
        target = 0x9040;
        width = 4;
        cpu_state.sr = 0x2000 | trace << 14;
        cpu_state.ccx = 1;
        EM_A0 = 0x04000040;
        EM_A1 = 0xb000;
        put(0xb000, 4, 0x87654321);
        put(0x8000, 2, 0x20d9);
        mmu.tc = 0x8000;
        mmu.srp = 0x1000;
        put(0x1008, 4, 0x200a);
        put(0x2000, 4, 0x300a);
        put(0x3000, 4, 0x9031);
        memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
        seed(0x3000, 1, 0);
        Syn68kCache040Line* pte = &cache.lines[0][0][0];
        pte->bytes[0] = pte->bytes[1] = 0;
        pte->bytes[2] = 0x90;
        pte->bytes[3] = 0x31;
        fail_address = 0x3000;
        if (mode == 2)
            native.ram = (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
        unsigned steps = cleanup(0x3000, 1, 5, 4, 0, 0);
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        if (!run.step.access.cache_table)
            fprintf(
                stderr,
                "descriptor mode=%u trace=%u reason=%u vector=%u state=%u pc=%x trans=%u bus=%u table=%u cache=%u done=%u fail=%x a0=%x\n",
                mode, trace, run.reason, run.step.vector, run.step.store_state, cpu.pc,
                run.step.access.translation.fault, run.step.access.bus_error, run.step.access.cache_table,
                run.step.access.cache.fault, run.step.access.completed, failed_push, EM_A0);
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
               run.step.store_state == (trace == 2 ? 3u : 2u) && cpu.pc == 0x8002 &&
               run.step.access.translation.fault == MMU040_PHYSICAL_ABORT && !run.step.access.bus_error &&
               run.step.access.cache_table && !run.step.access.completed);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
        unsigned sp = EM_A7;
        assert(visible(sp + 12, 2) == (0x200 | (trace == 2 ? 0x2000 : 0)) && visible(sp + 16, 2) == 0x85);
        assert(visible(sp + 20, 4) == 0x3000 && visible(sp + 40, 4) == 0x3000 && visible(sp + 32, 4) == 0x04000040 &&
               visible(sp + 36, 4) == 0x87654321 && visible(sp + 44, 4) == 0x9031);
        assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
        assert(run.reason == (trace == 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) && cpu.pc == 0x8002);
        if (trace == 2)
            assert(run.step.vector == 9 && run.step.fault_address == 0x8000);
        assert(visible(0x04000040, 4) == 0x87654321 && (get(0x3000, 4) & 0x18) == 0x18 && EM_A0 == 0x04000044 &&
               EM_A1 == 0xb004 && source_reads == 4);
        ++cases;
    }
}

static void invalid_metadata(unsigned mode)
{
    Syn68kMappedCPU cpu = setup(mode);
    bind(mode);
    target = EM_A0 = 0x9040;
    width = 4;
    put(0x8000, 2, 0x2080); /* MOVE.L D0,(A0) */
    for (unsigned way = 0; way < 4; ++way)
        seed(0xa040 + way * 0x400, 15, way);
    any_victim = 1;
    fail_beat = 0;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.step.store_state == 2 && run.step.access.completed == 4);
    const Syn68kIntegerStep original = run.step;
    assert(!syn68k_mmu040_operand_status(&original.access, NULL));
    uint16_t status = 0xaaaa;
    assert(!syn68k_mmu040_operand_status(NULL, &status) && status == 0xaaaa);
    for (unsigned bad = 0; bad < 22; ++bad) {
        Syn68kIntegerStep record = original;
        switch (bad) {
        case 0:
            record.access.completed = 5;
            record.access.logical = record.access.request + 5;
            break;
        case 1:
            record.access.cache.completed = 5;
            break;
        case 2:
            record.access.logical++;
            break;
        case 3:
            record.access.size = 0;
            break;
        case 4:
            record.access.fc = 7;
            break;
        case 5:
            record.access.writing = 2;
            break;
        case 6:
            record.access.rmw = 2;
            break;
        case 7:
            record.access.cache_exception = 0;
            break;
        case 8:
            record.access.cache.fault = CACHE040_OPERAND_FAULT;
            break;
        case 9:
            record.store_next |= 1;
            break;
        case 10:
            record.fpu_vector = 99;
            break;
        case 11:
            record.store_tail_valid = 3;
            break;
        case 12:
            record.store_tail_valid = 1;
            record.store_tail_fc = 7;
            break;
        case 13:
            record.cache_restart = 1;
            break;
        case 14:
            record.store_locked = 1;
            break;
        case 15:
            record.store_state = 0;
            break;
        case 16:
            record.movem_state = 1;
            break;
        case 17:
            record.restartable_read = 1;
            break;
        case 18:
            record.access.cache.push_size = 8;
            break;
        case 19:
            record.access.cache.bus_address++;
            break;
        case 20:
            record.access.bus_error = 0;
            break;
        case 21:
            record.access.cache.cycle.lock_end = 1;
            break;
        }
        if (bad < 9) {
            status = 0xaaaa;
            assert(!syn68k_mmu040_operand_status(&record.access, &status) && status == 0xaaaa);
        }
        unsigned char state[sizeof cpu_state];
        memcpy(state, &cpu_state, sizeof state);
        unsigned writes = operand_writes, reads = source_reads;
        cpu.stopped = 1;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && cpu.stopped && cpu.pc == 0x8002 &&
               !memcmp(state, &cpu_state, sizeof state) && operand_writes == writes && source_reads == reads);
        ++cases;
    }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        scalar(mode);
        compound(mode);
        numeric_pending(mode);
        descriptor_store(mode);
        invalid_metadata(mode);
    }
    printf("%u cache store retirement/PD/WB2/WB3/guest-cleanup/trace cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
