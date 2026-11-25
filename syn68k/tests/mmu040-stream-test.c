/*+===================================================================
  File:      mmu040-stream-test.c

  Summary:   Implements mmu040 stream test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Guest instruction streams enabling and using the explicit MMU backend. */
#include "syn68k_public.h"
#include "fpu.h"
#include "integer.h"
#include "mapped-cpu.h"
#include "native040.h"
#include "jit-x64.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x20000];
static Syn68kMMU040 mmu;
static unsigned cases, reads, writes, page_size, supervisor;
static unsigned native_execution;
static uint32_t fail_physical;
static uint32_t fail_write_physical, watched_physical;
static unsigned watched_attempts, watched_successes, watched_read_attempts;
static unsigned watched_read_fc, watched_write_fc;
static uint32_t watched_tail_physical = UINT32_MAX;
static unsigned watched_tail_successes, watched_tail_reads;
static uint32_t mutate_write_physical = UINT32_MAX;
static unsigned write_mutated, write_mutation;
static unsigned fault_fpu_mutation;
static unsigned replay_read_mutation;
static uint32_t replay_read_physical = UINT32_MAX;
static uint32_t trace_read_physical = UINT32_MAX;
static unsigned trace_read_sr;
static int trace_read_ccr = -1;
static unsigned trace_bus_kind; // Operand read, operand write, descriptor read.
static uint32_t late_read_physical = UINT32_MAX;
static uint32_t late_read_pte, late_read_descriptor;
static uint32_t late_write_physical = UINT32_MAX;
static uint32_t late_write_pte, late_write_descriptor;
static uint32_t read_callback_address = UINT32_MAX;
static unsigned read_callback_kind, read_callback_register, read_callback_mask;
static unsigned read_callback_fail, read_callback_hits;

/* Observe actual device/descriptor callbacks, including failure returns. */
static int address_callback(uint32_t address, unsigned kind)
{
    if (read_callback_address == UINT32_MAX || address != read_callback_address || kind != read_callback_kind)
        return 0;
    if (read_callback_register < 8)
        EM_AREG(read_callback_register) ^= read_callback_mask;
    else if (read_callback_register == 8)
        cpu_state.usp ^= read_callback_mask;
    else if (read_callback_register == 9)
        cpu_state.isp ^= read_callback_mask;
    else if (read_callback_register == 10)
        cpu_state.msp ^= read_callback_mask;
    else
        cpu_state.fpu.null_state ^= read_callback_mask;
    read_callback_address = UINT32_MAX;
    ++read_callback_hits;
    return read_callback_fail;
}

static void trace_bus(uint32_t address, unsigned kind)
{
    if (address == trace_read_physical && kind == trace_bus_kind) {
        cpu_state.sr = (cpu_state.sr & ~0xc000) | trace_read_sr;
        if (trace_read_ccr >= 0) {
            cpu_state.ccc = trace_read_ccr & 1;
            cpu_state.ccv = trace_read_ccr & 2;
            cpu_state.ccnz = !(trace_read_ccr & 4);
            cpu_state.ccn = trace_read_ccr & 8;
            cpu_state.ccx = trace_read_ccr & 16;
            trace_read_ccr = -1;
        }
        trace_read_physical = UINT32_MAX;
    }
}

static void put32(uint32_t address, uint32_t value)
{
    assert(address <= sizeof ram - 4);
    for (unsigned i = 0; i < 4; ++i)
        ram[address + i] = value >> (24 - i * 8);
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == &mmu);
    trace_bus(address, 2);
    if (address_callback(address, 2))
        return 0;
    if (address > sizeof ram - 4)
        return 0;
    *value = (uint32_t)ram[address] << 24 | (uint32_t)ram[address + 1] << 16 | (uint32_t)ram[address + 2] << 8 |
             ram[address + 3];
    return 1;
}

static int table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    uint32_t old;
    if (address_callback(address, 3))
        return 0;
    if (!table_read(opaque, address, &old))
        return 0;
    put32(address, locked ? old | value : value);
    return 1;
}

static int operand_read(void* opaque, uint32_t physical, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++reads;
    trace_bus(physical, 0);
    if (address_callback(physical, 1))
        return 0;
    if (physical == replay_read_physical) {
        switch (replay_read_mutation) {
        case 1:
            EM_D1 ^= 1;
            break;
        case 2:
            EM_A1 ^= 1;
            break;
        case 3:
            cpu_state.ccx ^= 1;
            break;
        case 4:
            cpu_state.usp ^= 2;
            break;
        case 5:
            cpu_state.fpu.fpcr ^= 0x10;
            break;
        case 6:
            cpu_state.fpu.fpsr ^= 0x200;
            break;
        case 7:
            cpu_state.fpu.fpiar ^= 2;
            break;
        case 8:
            cpu_state.fpu.frame[0] ^= 1;
            break;
        case 9:
            cpu_state.fpu.regs[1].significand ^= 1;
            break;
        case 10:
            EM_A0 ^= 0x10;
            break;
        }
        replay_read_physical = UINT32_MAX;
    }
    if (physical == watched_physical) {
        ++watched_read_attempts;
        watched_read_fc = fc;
    }
    if (physical == watched_tail_physical)
        ++watched_tail_reads;
    if (physical == fail_physical && fault_fpu_mutation) {
        switch (fault_fpu_mutation) {
        case 1:
            cpu_state.fpu.regs[0].exponent ^= 1;
            break;
        case 2:
            cpu_state.fpu.fpcr ^= 0x10;
            break;
        case 3:
            cpu_state.fpu.fpsr ^= 0x200;
            break;
        case 4:
            cpu_state.fpu.fpiar ^= 2;
            break;
        case 5:
            cpu_state.fpu.null_state = 1;
            break;
        case 6:
            cpu_state.fpu.frame_size = 52;
            break;
        }
    }
    if (physical == fail_physical || physical > sizeof ram - size)
        return 0;
    memcpy(bytes, ram + physical, size);
    if (physical == late_read_physical) {
        put32(late_read_pte, late_read_descriptor);
        syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
        late_read_physical = UINT32_MAX;
    }
    return 1;
}

static int operand_write(void* opaque, uint32_t physical, const unsigned char* bytes, unsigned size,
                         uint32_t attributes, unsigned fc)
{
    assert(opaque == ram);
    (void)attributes;
    (void)fc;
    ++writes;
    trace_bus(physical, 1);
    if (address_callback(physical, 4))
        return 0;
    if (physical == watched_physical) {
        ++watched_attempts;
        watched_write_fc = fc;
    }
    if (physical == mutate_write_physical && !write_mutated) {
        if (write_mutation & 1)
            for (unsigned reg = 0; reg < 8; ++reg)
                cpu_state.fpu.regs[reg].significand ^= UINT64_C(0x04030201);
        if (write_mutation & 2) {
            assert(fc == 5 && (cpu_state.sr & 0x3000) == 0x2000);
            cpu_state.isp = EM_A7;
            cpu_state.sr &= ~0x2000;
            EM_A7 = cpu_state.usp;
        }
        if (write_mutation & 4) {
            EM_D2 ^= 0x04030201;
            EM_D3 ^= 0x04030201;
            EM_A1 ^= 0x100;
        }
        if (write_mutation & 8) {
            cpu_state.fpu.fpcr ^= 0x30;
            cpu_state.fpu.fpsr ^= 0x04000000;
            cpu_state.fpu.fpiar ^= 0x04030201;
        }
        if (write_mutation & 32)
            EM_A0 ^= 0x10;
        if (write_mutation & 64)
            cpu_state.fpu.fpsr ^= 0x4000;
        if (write_mutation & 128)
            cpu_state.fpu.fpiar ^= 2;
        if (write_mutation & 256)
            cpu_state.fpu.fpcr ^= 0x10;
        if (write_mutation & 512)
            cpu_state.fpu.frame_size = 52;
        if (write_mutation & 1024)
            cpu_state.sr ^= 0x4000;
        if (write_mutation & 2048)
            cpu_state.ccx ^= 1;
        if (write_mutation & 4096)
            cpu_state.usp ^= 2;
        if (write_mutation & 8192)
            cpu_state.fpu.null_state ^= 1;
        if (write_mutation & 16)
            for (unsigned i = 0; i < sizeof cpu_state.fpu.frame; ++i)
                cpu_state.fpu.frame[i] ^= (i & 7) + 1;
        write_mutated = 1;
    }
    if (physical == fail_physical || physical == fail_write_physical || physical > sizeof ram - size)
        return 0;
    memcpy(ram + physical, bytes, size);
    if (physical == late_write_physical) {
        put32(late_write_pte, late_write_descriptor);
        syn68k_mmu040_flush(&mmu, 1, 0, 0, 1);
        late_write_physical = UINT32_MAX;
    }
    if (physical == watched_physical)
        ++watched_successes;
    if (physical == watched_tail_physical)
        ++watched_tail_successes;
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu,
                                    .tables = {&mmu, table_read, table_write},
                                    .opaque = ram,
                                    .read = operand_read,
                                    .write = operand_write,
                                    .native = NULL};

static uint32_t pte(uint32_t logical)
{
    return (supervisor ? 0x6000 : 0x3000) + (logical / page_size) * 4;
}

/* Independent literal mappings; these do not invoke the production walker. */
static uint32_t physical(uint32_t logical)
{
    if (logical >= 0x20000) {
        unsigned offset = logical - 0x20000;
        return (supervisor ? 0x18000 : 0x10000) + (offset < page_size ? offset : 0x4000 + offset - page_size);
    }
    unsigned offset = logical - 0x10000;
    return (supervisor ? 0xc000 : 0x8000) + (offset < page_size ? offset : 0x2000 + offset - page_size);
}

static void code_word(uint32_t logical, unsigned value)
{
    ram[physical(logical)] = value >> 8;
    ram[physical(logical + 1)] = value;
}

static void fixture(unsigned page8k, unsigned privileged)
{
    page_size = page8k ? 8192 : 4096;
    supervisor = privileged;
    memory.aligned_transfers = 0;
    syn68k_mmu040_init(&mmu);
    memset(ram + 0x1000, 0, 512);
    memset(ram + 0x2000, 0, 512);
    memset(ram + 0x3000, 0, 256);
    memset(ram + 0x4000, 0, 512);
    memset(ram + 0x5000, 0, 512);
    memset(ram + 0x6000, 0, 256);
    mmu.tc = page8k ? 0xc000 : 0x8000;
    mmu.urp = 0x1000;
    mmu.srp = 0x4000;
    put32(0x1000, 0x2002);
    put32(0x2000, 0x3002);
    put32(0x4000, 0x5002);
    put32(0x5000, 0x6002);
    put32(pte(0x10000), (supervisor ? 0xc000 : 0x8000) | 1);
    put32(pte(0x10000 + page_size), (supervisor ? 0xe000 : 0xa000) | 1);
    put32(pte(0x20000), (supervisor ? 0x18000 : 0x10000) | 1);
    put32(pte(0x20000 + page_size), (supervisor ? 0x1c000 : 0x14000) | 1);
    cpu_state.sr = supervisor ? 0x2000 : 0;
    EM_A7 = 0x76543210; // Stepping must not build a flat exception stack here.
    reads = writes = 0;
    fail_physical = 0xffffffff;
    fail_write_physical = watched_physical = 0xFFFFFFFF;
    watched_attempts = watched_successes = watched_read_attempts = 0;
    watched_read_fc = watched_write_fc = UINT32_MAX;
    mutate_write_physical = UINT32_MAX;
    write_mutated = 0;
    write_mutation = 1;
    fault_fpu_mutation = 0;
    replay_read_mutation = 0;
    replay_read_physical = UINT32_MAX;
    trace_read_physical = UINT32_MAX;
    trace_read_sr = 0;
    trace_read_ccr = -1;
    trace_bus_kind = 0;
    late_read_physical = late_write_physical = UINT32_MAX;
    read_callback_address = UINT32_MAX;
    read_callback_hits = 0;
}

static unsigned emit_pc;

static void word(unsigned value)
{
    ram[emit_pc++] = value >> 8;
    ram[emit_pc++] = value;
}

static void longword(uint32_t value)
{
    word(value >> 16);
    word(value);
}

static void store_immediate(uint32_t address, uint32_t value)
{
    word(0x23FC);
    longword(value);
    longword(address);
}

static void enable_from_guest(void)
{
    for (unsigned budget = 1; budget <= 32; ++budget) {
        memset(ram, 0, sizeof ram);
        syn68k_mmu040_init(&mmu);
        for (unsigned r = 0; r < 16; ++r)
            EM_DREG(r) = 0;
        cpu_state.sr = 0x2000;
        EM_A7 = 0xABCDEF00;
        cpu_state.dfc = 0;
        emit_pc = 0x8000;
        store_immediate(0x1000, 0x2002);
        store_immediate(0x2000, 0x3002);
        store_immediate(0x3020, 0x8001);  /* Keep executing code mapped in place. */
        store_immediate(0x3080, 0x10001); /* Data at a different physical address. */
        word(0x203C);
        longword(0x1000);
        word(0x4E7B);
        word(0x0807); /* D0 -> SRP */
        word(0x203C);
        longword(0x8000);
        word(0x4E7B);
        word(0x0003); /* D0 -> TC, enable 4K translation */
        word(0x207C);
        longword(0x20000);
        word(0x7205);
        word(0x4E7B);
        word(0x1001); /* D1 -> DFC */
        word(0xF568); /* PTESTR (A0), sets MMUSR */
        word(0xF518); /* PFLUSHA forces the next instruction to translate anew. */
        word(0x7003);
        word(0x5290);
        word(0x5340);
        word(0x66FA);
        word(0x4E7A);
        word(0x2805); /* MMUSR -> D2 */
        word(0x4E40);
        unsigned end = emit_pc;
        Syn68kMappedCPU cpu = {.pc = 0x8000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun result;
        unsigned completed = 0;
        do {
            assert(syn68k_mapped_cpu_run(&cpu, budget, &result));
            completed += result.completed;
        } while (result.reason == SYN68K_MAPPED_BUDGET);
        assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 32 && cpu.pc == end &&
               result.step.exception_pc == end);
        assert(completed == 24 && EM_D0 == 0 && EM_D2 == 0x10001);
        assert(EM_A7 == 0xABCDEF00 && mmu.tc == 0x8000 && mmu.srp == 0x1000);
        uint32_t value;
        assert(table_read(&mmu, 0x10000, &value) && value == 3);
        assert(table_read(&mmu, 0x3080, &value) && (value & 16));
        ++cases;
    }
}

static void streams(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned budget = 1; budget <= 40; ++budget) {
                fixture(page8k, privileged);
                unsigned pc = 0x10000 + page_size - 6;
                const unsigned program[] = {0x7001, 0x720F, 0x5280, 0x51C9, 0xFFFC, 0x4E71, 0xA123};
                for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                    code_word(pc + i * 2, program[i]);
                Syn68kMappedCPU cpu = {.pc = pc, .memory = &memory, .native = native_execution};
                Syn68kMappedRun result;
                unsigned completed = 0;
                do {
                    assert(syn68k_mapped_cpu_run(&cpu, budget, &result));
                    completed += result.completed;
                } while (result.reason == SYN68K_MAPPED_BUDGET);
                assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 10 && cpu.pc == pc + 12);
                assert(completed == 35 && EM_D0 == 17 && EM_D1 == 0xFFFF);
                assert(EM_A7 == 0x76543210);
                ++cases;
            }
}

static void single_fetch_and_coherence(void)
{
    for (unsigned privileged = 0; privileged < 2; ++privileged) {
        fixture(0, privileged);
        syn68k_fpu_reset();
        EM_A0 = 0x20040;
        ram[physical(EM_A0)] = 0x3F;
        ram[physical(EM_A0 + 1)] = 0x80;
        ram[physical(EM_A0 + 2)] = ram[physical(EM_A0 + 3)] = 0;
        const unsigned fp[] = {0xF210, 0x4400, 0xF200, 0x0022, 0xF210, 0x6400};
        for (unsigned i = 0; i < sizeof fp / sizeof *fp; ++i)
            code_word(0x10000 + i * 2, fp[i]);
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun result;
        assert(syn68k_mapped_cpu_run(&cpu, 3, &result));
        unsigned direct_ram = memory.native && memory.native->ram.bytes;
        assert(result.reason == SYN68K_MAPPED_BUDGET && result.completed == 3 && cpu.pc == 0x1000C &&
               reads == (direct_ram ? 0U : 7U) && writes == (direct_ram ? 0U : 1U));
        assert(ram[physical(EM_A0)] == 0x40 && !ram[physical(EM_A0 + 1)] && !ram[physical(EM_A0 + 2)] &&
               !ram[physical(EM_A0 + 3)]);
        ++cases;

        fixture(0, privileged);
        code_word(0x10000, 0x33FC);
        code_word(0x10002, 0x4E71);
        code_word(0x10004, 1);
        code_word(0x10006, 8);
        code_word(0x10008, 0x7007);
        code_word(0x1000A, 0xA123);
        EM_D0 = 123;
        cpu.pc = 0x10000;
        assert(syn68k_mapped_cpu_run(&cpu, 10, &result));
        assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 10 && result.completed == 2 &&
               EM_D0 == 123);
        ++cases;
    }
}

static void stops_and_faults(void)
{
    fixture(0, 1);
    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
    Syn68kMappedRun result;
    code_word(cpu.pc, 0x4E71);
    assert(syn68k_mapped_cpu_run(&cpu, 0, &result));
    assert(!reads && !writes && result.reason == SYN68K_MAPPED_BUDGET);
    cpu_state.interrupt_pending[3] = 1;
    assert(syn68k_mapped_cpu_run(&cpu, 10, &result));
    assert(result.reason == SYN68K_MAPPED_INTERRUPT && result.interrupt_level == 3 && !reads &&
           cpu_state.interrupt_pending[3]);
    cpu_state.interrupt_pending[3] = 0;
    code_word(cpu.pc, 0x4AFC);
    assert(syn68k_mapped_cpu_run(&cpu, 10, &result));
    assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 4 && !result.completed &&
           cpu.pc == 0x10000);
    code_word(cpu.pc, 0x4180); /* CHK.W D0,D0 now shares mapped bounds logic. */
    EM_D0 = 1;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &result));
    assert(result.reason == SYN68K_MAPPED_BUDGET && result.completed == 1);
    cpu.pc = 0x10001;
    unsigned previous = reads;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &result));
    assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 3 && reads == previous);
    cpu.pc = 0x10000;
    code_word(cpu.pc, 0x2018);
    EM_A0 = 0x20FFE;
    EM_D0 = 123;
    put32(pte(0x21000), 0);
    assert(syn68k_mapped_cpu_run(&cpu, 1, &result));
    assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 2 && result.step.access.completed == 2 &&
           EM_D0 == 123 && EM_A0 == 0x20FFE);
    cpu.pc = 0x30000;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &result));
    assert(result.reason == SYN68K_MAPPED_EXCEPTION && result.step.vector == 2 && result.step.access.fc == 6 &&
           result.step.access.request == cpu.pc);
    assert(!syn68k_mapped_cpu_run(NULL, 1, &result));
    assert(result.reason == SYN68K_MAPPED_BAD_REQUEST);
    assert(!syn68k_mapped_cpu_run(&cpu, 1, NULL));
    ++cases;
}

static unsigned status(void)
{
    return cpu_state.sr | !!cpu_state.ccc | (!!cpu_state.ccv << 1) | (!cpu_state.ccnz << 2) | (!!cpu_state.ccn << 3) |
           (!!cpu_state.ccx << 4);
}

static void set_status(unsigned sr)
{
    cpu_state.sr = sr & ~31;
    cpu_state.ccc = sr & 1;
    cpu_state.ccv = sr & 2;
    cpu_state.ccnz = !(sr & 4);
    cpu_state.ccn = sr & 8;
    cpu_state.ccx = sr & 16;
}

static unsigned mapped_value(uint32_t address, unsigned size)
{
    unsigned value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[physical(address + i)];
    return value;
}

static void check_frame(unsigned address, unsigned format, unsigned saved_sr, const Syn68kIntegerStep* record)
{
    assert(mapped_value(address, 2) == saved_sr);
    assert(mapped_value(address + 2, 4) == record->exception_pc);
    assert(mapped_value(address + 6, 2) == (format << 12 | record->vector * 4));
    if (format >= 2)
        assert(mapped_value(address + 8, 4) == record->fault_address);
}

static void exception_roundtrips(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned format = 0; format < 4; ++format)
                for (unsigned level = 0; level < 8; ++level)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        if (format == 1 || (level && format))
                            continue;
                        fixture(page8k, 0);
                        code_word(0x10000, 0x4E71);
                        supervisor = 1;
                        put32(pte(0x10000), 0xC001);
                        put32(pte(0x10000 + page_size), 0xE001);
                        put32(pte(0x20000), 0x18001);
                        put32(pte(0x20000 + page_size), 0x1C001);
                        code_word(0x10040, 0x4E73); /* Actual mapped handler/RTE. */
                        unsigned original_sr = mode * 0x1000 | (ccr & 3) * 0x4000 | 0x300 | ccr;
                        set_status(original_sr);
                        unsigned usp = 0xDEADBE00;
                        unsigned isp = 0x20000 + page_size + 4;
                        unsigned msp = 0x20000 + page_size * 2 - 16;
                        cpu_state.usp = usp;
                        cpu_state.isp = isp;
                        cpu_state.msp = msp;
                        EM_A7 = mode < 2 ? usp : mode == 2 ? isp : msp;
                        cpu_state.vbr = 0x20000;
                        unsigned vector = level ? 24 + level : format == 3 ? 48 : 6;
                        Syn68kIntegerStep record = {
                            .vector = vector, .format = format, .exception_pc = 0x10000, .fault_address = 0x12345678};
                        put32(physical(cpu_state.vbr + vector * 4), 0x10040);
                        if (level)
                            cpu_state.interrupt_pending[level] = 1;
                        Syn68kMappedCPU cpu = {.pc = 0x1234, .memory = &memory, .native = native_execution};
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_exception_enter(&cpu, &record, level, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                        unsigned next_sr = ((original_sr & 0x3FFF) | 0x2000);
                        if (level)
                            next_sr = (next_sr & ~0x1700) | level << 8;
                        assert(status() == next_sr && cpu.pc == 0x10040);
                        unsigned length = format ? 12 : 8;
                        unsigned active = level || !(next_sr & 0x1000) ? isp : msp;
                        assert(EM_A7 == active - length);
                        check_frame(EM_A7, level && mode == 3 ? 1 : format, original_sr, &record);
                        if (level && mode == 3) {
                            assert(cpu_state.msp == msp - 8);
                            check_frame(msp - 8, 0, original_sr, &record);
                        }
                        assert(!level || !cpu_state.interrupt_pending[level]);
                        Syn68kMappedRun result;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &result));
                        assert(result.reason == SYN68K_MAPPED_BUDGET && result.completed == 1 && cpu.pc == 0x10000);
                        assert(status() == original_sr);
                        assert(EM_A7 == (mode < 2 ? usp : mode == 2 ? isp : msp));
                        ++cases;
                    }
}

static void exception_delivery_failures(void)
{
    const unsigned offsets[] = {0, 2, 6, 8};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned vector = 2; vector <= 6; ++vector)
            for (unsigned failed = 0; failed <= 4; ++failed) {
                fixture(page8k, 1);
                EM_A7 = 0x20080;
                cpu_state.vbr = 0x20000;
                Syn68kIntegerStep record = {
                    .vector = vector, .format = 2, .exception_pc = 0x10000, .fault_address = 0xABCDEFFE};
                unsigned address = failed == 4 ? cpu_state.vbr + vector * 4 : EM_A7 - 12 + offsets[failed];
                fail_physical = physical(address);
                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
                assert(delivery.reason == (vector <= 3 ? SYN68K_DELIVERY_DOUBLE_FAULT : SYN68K_DELIVERY_ACCESS_FAULT));
                assert(delivery.stacking == (failed != 4));
                assert(delivery.access.fc == 5 && delivery.access.bus_error && delivery.access.request == address);
                assert(cpu.pc == 0x10000 && EM_A7 == 0x20074);
                assert(writes == (failed < 4 ? failed + 1 : 4));
                ++cases;
            }
    fixture(0, 1);
    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
    Syn68kIntegerStep record = {.vector = 2, .format = 7};
    Syn68kMappedDelivery delivery;
    CPUState before;
    memcpy(&before, &cpu_state, sizeof before);
    assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED);
    assert(!reads && !writes && !memcmp(&before, &cpu_state, sizeof before));
    assert(!syn68k_mapped_exception_enter(&cpu, &record, 8, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_BAD_REQUEST);
    assert(!syn68k_mapped_exception_enter(NULL, &record, 0, &delivery));
    assert(!syn68k_mapped_exception_enter(&cpu, &record, 0, NULL));
    ++cases;
}

static void guest_exception_handlers(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned vector = 32; vector < 48; ++vector) {
            fixture(page8k, 0);
            code_word(0x10000, 0x4E40 + vector - 32);
            supervisor = 1;
            put32(pte(0x10000), 0xC001);
            put32(pte(0x20000), 0x18001);
            code_word(0x10040, 0x5280); /* ADDQ.L #1,D0 */
            code_word(0x10042, 0x4E73);
            cpu_state.vbr = 0x20000;
            cpu_state.isp = 0x20100;
            put32(physical(cpu_state.vbr + vector * 4), 0x10040);
            EM_D0 = 41;
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_cpu_run(&cpu, 10, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == vector && !run.completed);
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
            assert(syn68k_mapped_cpu_run(&cpu, 2, &run));
            assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 2 && cpu.pc == 0x10002 && EM_D0 == 42 &&
                   !(cpu_state.sr & 0x2000) && EM_A7 == 0x76543210);
            ++cases;
        }
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned vector_fault = 0; vector_fault < 2; ++vector_fault) {
            fixture(page8k, 1);
            EM_A7 = 0x20100;
            cpu_state.vbr = vector_fault ? 0x30000 : 0x20000;
            if (!vector_fault)
                put32(pte(0x20000), 0);
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kIntegerStep record = {.vector = 27, .exception_pc = cpu.pc};
            Syn68kMappedDelivery delivery;
            cpu_state.interrupt_pending[3] = 1;
            assert(syn68k_mapped_exception_enter(&cpu, &record, 3, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_ACCESS_FAULT && delivery.stacking == !vector_fault);
            assert(delivery.access.translation.fault != MMU040_OK && !delivery.access.bus_error &&
                   delivery.access.fc == 5 && cpu_state.interrupt_pending[3]);
            assert(cpu.pc == 0x10000 && writes == vector_fault * 3);
            cpu_state.interrupt_pending[3] = 0;
            ++cases;
        }
}

static void guest_page_fault_handlers(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                for (unsigned ccr = 0; ccr < 32; ++ccr) {
                    fixture(page8k, privileged);
                    unsigned root = privileged ? 0x6000 : 0x3000;
                    unsigned code_physical = physical(0x10000);
                    unsigned fault_pte = pte(0x10000);
                    code_word(0x10000, 0x7011);
                    supervisor = 1;
                    put32(pte(0x10000), 0xC001);
                    put32(pte(0x10000 + page_size), 0xE001);
                    put32(pte(0x20000), 0x18001);
                    /* Map the real page-table page into supervisor logical data.
               The guest handler, not the test host, repairs its descriptor. */
                    unsigned table_page = root & ~(page_size - 1);
                    put32(pte(0x30000), table_page | 1);
                    unsigned target = 0x30000 + fault_pte - table_page;
                    unsigned handler = 0x10000 + page_size + 0x40;
                    const unsigned program[] = {0x23FC, 0,     code_physical | 1, target >> 16, target & 65535,
                                                0xF518, 0x4E73};
                    for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                        code_word(handler + i * 2, program[i]);
                    if (!physical_fault)
                        put32(fault_pte, 0);
                    else
                        fail_physical = code_physical;
                    cpu_state.vbr = 0x20000;
                    put32(physical(cpu_state.vbr + 8), handler);
                    cpu_state.isp = 0x20100;
                    EM_A7 = privileged ? cpu_state.isp : 0x76543210;
                    set_status((privileged ? 0x2000 : 0) | ccr);
                    EM_D0 = 123;
                    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                    Syn68kMappedRun run;
                    Syn68kMappedDelivery delivery;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.format == 7 &&
                           !run.completed && EM_D0 == 123);
                    fail_physical = 0xFFFFFFFF;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                    unsigned frame = 0x20100 - 60;
                    assert(EM_A7 == frame);
                    check_frame(frame, 7, (privileged ? 0x2000 : 0) | ccr,
                                &(Syn68kIntegerStep){.vector = 2, .exception_pc = 0x10000});
                    assert(mapped_value(frame + 6, 2) == 0x7008);
                    assert(mapped_value(frame + 12, 2) == ((physical_fault ? 0x140 : 0x540) | (privileged ? 6 : 2)));
                    assert(mapped_value(frame + 20, 4) == 0x10000);
                    for (unsigned offset = 8; offset < 60; offset += 2)
                        if (offset != 12 && offset != 20 && offset != 22)
                            assert(!mapped_value(frame + offset, 2));
                    assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 3 && cpu.pc == 0x10000 &&
                           status() == ((privileged ? 0x2000 : 0) | ccr));
                    assert(EM_A7 == (privileged ? 0x20100 : 0x76543210));
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu.pc == 0x10002 &&
                           EM_D0 == 17);
                    ++cases;
                }
}

static void fetch_frame_guards(void)
{
    for (unsigned field = 0; field < 17; ++field) {
        fixture(0, 1);
        EM_A7 = 0x20100;
        cpu_state.vbr = 0x20000;
        Syn68kIntegerStep record = {
            .vector = 2,
            .format = 7,
            .exception_pc = 0x10000,
            .access = {
                .translation = {.fault = MMU040_INVALID}, .logical = 0x10000, .request = 0x10000, .size = 2, .fc = 6}};
        unsigned offset = field < 3 ? (unsigned[]){0, 2, 6}[field] : (field - 3) * 4 + 8;
        unsigned logical = field == 16 ? cpu_state.vbr + 8 : EM_A7 - 60 + offset;
        fail_physical = physical(logical);
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_DOUBLE_FAULT && delivery.stacking == (field < 16) &&
               delivery.access.request == logical);
        assert(writes == (field < 16 ? field + 1 : 16) && cpu.pc == 0x10000 && EM_A7 == 0x20100 - 60);
        ++cases;
    }
    for (unsigned invalid = 0; invalid < 10; ++invalid) {
        fixture(0, 1);
        Syn68kIntegerStep record = {
            .vector = 2,
            .format = 7,
            .exception_pc = 0x10000,
            .access = {
                .translation = {.fault = MMU040_INVALID}, .logical = 0x10000, .request = 0x10000, .size = 2, .fc = 6}};
        switch (invalid) {
        case 0:
            record.access.completed = 1;
            break;
        case 1:
            record.access.writing = 1;
            break;
        case 2:
            record.access.rmw = 1;
            break;
        case 3:
            record.access.fc = 5;
            break;
        case 4:
            record.access.size = 4;
            break;
        case 5:
            record.vector = 4;
            break;
        case 6:
            record.exception_pc = 0x10002;
            break;
        case 7:
            record.exception_pc = record.access.request = 0x10001;
            break;
        case 8:
            record.access.translation.fault = MMU040_OK;
            break;
        case 9:
            record.access.translation.fault = MMU040_BAD_REQUEST;
            break;
        }
        CPUState before;
        memcpy(&before, &cpu_state, sizeof before);
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED);
        assert(!reads && !writes && !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
}

static void guest_operand_page_fault_handlers(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 0; kind < 4; ++kind)
                for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        fixture(page8k, privileged);
                        unsigned pc = kind == 3 ? 0x10000 + page_size - 2 : 0x10000;
                        unsigned fault = kind == 3 ? pc + 2 : 0x28000;
                        unsigned root = privileged ? 0x6000 : 0x3000;
                        unsigned fault_pte = pte(fault);
                        unsigned data = kind == 3 ? physical(fault) : 0x10000;
                        put32(fault_pte, data | 1);
                        code_word(pc, kind == 0 ? 0x1010 : kind == 1 ? 0x3010 : kind == 2 ? 0x2010 : 0x203C);
                        put32(data, 0x11223344);
                        supervisor = 1;
                        /* The handler resides in the opposite code page from a failed
                 extension, and has its own resident stack. */
                        unsigned handler = kind == 3 ? 0x10040 : 0x10000 + page_size + 0x40;
                        put32(pte(handler), (kind == 3 ? 0xC000 : 0xE000) | 1);
                        put32(pte(0x20000), 0x18001);
                        unsigned table_page = root & ~(page_size - 1);
                        put32(pte(0x30000), table_page | 1);
                        unsigned target = 0x30000 + fault_pte - table_page;
                        const unsigned program[] = {0x23FC,         data >> 16, data | 1, target >> 16,
                                                    target & 65535, 0xF518,     0x4E73};
                        for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                            code_word(handler + i * 2, program[i]);
                        if (physical_fault)
                            fail_physical = data;
                        else
                            put32(fault_pte, 0);
                        cpu_state.vbr = 0x20000;
                        put32(physical(cpu_state.vbr + 8), handler);
                        cpu_state.isp = 0x20100;
                        EM_A7 = privileged ? cpu_state.isp : 0x76543210;
                        set_status((privileged ? 0x2000 : 0) | ccr);
                        EM_D0 = 0xAABBCCDD;
                        EM_A0 = fault;
                        Syn68kMappedCPU cpu = {.pc = pc, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                               run.step.restartable_read && !run.completed && EM_D0 == 0xAABBCCDD && EM_A0 == fault);
                        fail_physical = 0xFFFFFFFF;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                        unsigned frame = 0x20100 - 60;
                        unsigned fc = (privileged ? 4 : 0) | (kind == 3 ? 2 : 1);
                        unsigned size_bits = kind == 0 ? 0x20 : kind == 1 ? 0x40 : 0;
                        assert(mapped_value(frame + 12, 2) == ((physical_fault ? 0x100 : 0x500) | size_bits | fc));
                        assert(mapped_value(frame + 20, 4) == fault);
                        assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc &&
                               status() == ((privileged ? 0x2000 : 0) | ccr));
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc + (kind == 3 ? 6 : 2));
                        assert(EM_D0 == (kind == 0 ? 0xAABBCC11 : kind == 1 ? 0xAABB1122 : 0x11223344));
                        ++cases;
                    }
    /* Nonindexed MOVEM read continuation remains a separate case. */
    fixture(0, 1);
    code_word(0x10000, 0x4CD0);
    code_word(0x10002, 1);
    EM_A0 = 0x28000;
    put32(pte(EM_A0), 0);
    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
    Syn68kMappedRun run;
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.step.restartable_read);
    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED);
    ++cases;
}

static unsigned physical_value(unsigned address, unsigned size)
{
    unsigned value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[address + i];
    return value;
}

static unsigned scalar_store_physical(unsigned address)
{
    return (address < 0x28000 + page_size ? 0x10000 : 0x14000) + (address & (page_size - 1));
}

static unsigned scalar_store_value(unsigned address, unsigned size)
{
    unsigned value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[scalar_store_physical(address + i)];
    return value;
}

static void guest_boundary_priority(void)
{
    for (unsigned odd = 0; odd < 2; ++odd)
        for (unsigned trace = 0; trace < 4; ++trace) {
            fixture(0, 1);
            set_status(0x2000 | trace << 14 | 31);
            cpu_state.interrupt_pending[7] = 1;
            Syn68kMappedCPU cpu = {.pc = 0x10000 + odd, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(!reads && !writes && cpu_state.interrupt_pending[7]);
            if (odd)
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 3 && run.step.format == 2 &&
                       run.step.exception_pc == 0x10001 && run.step.fault_address == 0x10000 && !run.interrupt_level);
            else
                assert(run.reason == SYN68K_MAPPED_INTERRUPT && run.interrupt_level == 7);
            cpu_state.interrupt_pending[7] = 0;
            ++cases;
        }
}

static void guest_fpu_branch_prefetches(void)
{
    unsigned first = cases;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned form = 0; form < 7; ++form)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned environment = 0; environment < 4; ++environment)
                        for (unsigned callback = 0; callback < 2; ++callback) {
                            fixture(page8k, privileged);
                            syn68k_fpu_reset();
                            unsigned db = form >= 4, condition = db ? (form == 6 ? 15 : 0) : (form & 1 ? 15 : 0);
                            if (environment == 1 || environment == 2)
                                condition |= 16;
                            unsigned opcode = db ? 0xf248 : (form >= 2 ? 0xf2c0 : 0xf280) | condition;
                            code_word(0x10000, opcode);
                            code_word(0x10002, db ? condition : form >= 2 ? 0 : 1);
                            code_word(0x10004, 1);
                            EM_D0 = form == 4 ? 0 : 1;
                            EM_A7 = 0x20100;
                            if (environment == 1 || environment == 2)
                                cpu_state.fpu.fpsr = 0x01000000;
                            if (environment == 2)
                                cpu_state.fpu.fpcr = 0x8000;
                            if (environment == 3)
                                fail_physical = physical(0x10000 + (db ? 4 : 2));
                            unsigned sr = (privileged ? 0x2000 : 0) | trace << 14 | 31;
                            set_status(sr);
                            if (callback) {
                                trace_read_physical = physical(0x10000);
                                trace_bus_kind = 0;
                                trace_read_sr = (trace ^ 3) << 14;
                                sr ^= 0xc000;
                            }
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            unsigned vector = environment == 2 ? 48 : environment == 3 ? 2 : 3;
                            unsigned counter = db && form != 6 && environment < 2 ? (form == 4 ? 0xffff : 0)
                                               : form == 4                        ? 0
                                                                                  : 1;
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == vector &&
                                   run.step.exception_pc == 0x10000 && cpu.pc == 0x10000 && EM_D0 == counter &&
                                   EM_A7 == 0x20100 && status() == sr && !writes);
                            assert(reads == (db ? 3u : 2u)); // No target prefetch bus cycle or operand replay.
                            if (vector == 3)
                                assert(run.step.format == 2 && run.step.fault_address == (db ? 0x10004u : 0x10002u));
                            else if (vector == 2)
                                assert(run.step.format == 7 && run.step.access.bus_error);
                            else
                                assert(!run.step.format);
                            ++cases;
                        }
    printf("%u FPU branch prefetch/trace/BSUN/fetch-fault cases passed\n", cases - first);
}

static unsigned integer_condition(unsigned condition, unsigned ccr)
{
    unsigned c = !!(ccr & 1), v = !!(ccr & 2), z = !!(ccr & 4), n = !!(ccr & 8);
    const unsigned truth[] = {1,  0, !(c || z), c || z, !c,     c,      !z,           z,
                              !v, v, !n,        n,      n == v, n != v, !z && n == v, z || n != v};
    return truth[condition];
}

static void guest_control_branches(void)
{
    static const unsigned offsets[] = {2, 4, 0xfffffffe, 1};
    unsigned first = cases;
    for (unsigned dbcc = 0; dbcc < 2; ++dbcc)
        for (unsigned condition = 0; condition < 16; ++condition)
            for (unsigned ccr = 0; ccr < 32; ++ccr)
                for (unsigned width = 0; width < 3; ++width)
                    for (unsigned sample = 0; sample < 4; ++sample)
                        for (unsigned trace = 0; trace < 4; ++trace)
                            for (unsigned callback = 0; callback < 2; ++callback) {
                                fixture(0, 1);
                                unsigned counter = width == 0 ? 0 : width == 1 ? 1 : 0xffff;
                                unsigned extension = dbcc ? 2 : width * 2;
                                unsigned offset = offsets[sample];
                                unsigned next = 0x10002 + extension, target = 0x10002 + offset;
                                unsigned opcode = dbcc ? 0x50c8 | condition << 8
                                                       : 0x6000 | condition << 8 |
                                                             (width == 0   ? offset & 255
                                                              : width == 1 ? 0
                                                                           : 255);
                                code_word(0x10000, opcode);
                                code_word(0x10002, extension == 2 ? offset : offset >> 16);
                                code_word(0x10004, offset);
                                EM_D0 = 0x12340000 | counter;
                                EM_A7 = 0x20100;
                                unsigned sr = 0x2000 | trace << 14 | ccr;
                                set_status(sr);
                                unsigned condition_met = integer_condition(condition, ccr);
                                unsigned call = !dbcc && condition == 1;
                                unsigned taken = dbcc ? !condition_met && counter != 0 : call || condition_met;
                                unsigned expected_d0 = EM_D0;
                                if (dbcc && !condition_met)
                                    expected_d0 = 0x12340000 | ((counter - 1) & 65535);
                                unsigned expected_pc = taken ? target : next;
                                unsigned odd = target & 1;
                                unsigned traced = !!(trace & 2) || ((trace & 1) && taken);
                                if (callback) {
                                    trace_read_physical = physical(0x10000);
                                    trace_bus_kind = 0;
                                    trace_read_sr = (trace ^ 3) << 14;
                                    sr ^= 0xc000;
                                }
                                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                                Syn68kMappedRun run;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                unsigned vector = odd ? 3 : traced ? 9 : 0;
                                assert(run.reason == (vector ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                       run.step.vector == vector && cpu.pc == (odd ? 0x10000u : expected_pc) &&
                                       EM_D0 == expected_d0 && EM_A7 == 0x20100u - (call ? 4 : 0) && status() == sr);
                                assert(reads == 1u + !!extension && writes == call);
                                if (call)
                                    assert(mapped_value(0x200fc, 4) == next);
                                if (vector)
                                    assert(run.step.format == 2 &&
                                           run.step.exception_pc == (odd ? 0x10000u : expected_pc) &&
                                           run.step.fault_address == (odd ? target & ~1u : 0x10000u));
                                ++cases;
                            }
    printf("%u branch decision/trace/odd-prefetch cases passed\n", cases - first);
}

/* Keep the explicit stack source and deferred LINK assignments independent of
   the runtime implementation. No exception frame is constructed in this group;
   guest cleanup and stack/frame aliasing have separate coverage. */
static unsigned stack_store_setup(unsigned page8k, unsigned privileged, unsigned form, unsigned operand,
                                  unsigned displacement, unsigned sr)
{
    fixture(page8k, privileged);
    put32(pte(0x28000), 0x10001);
    put32(pte(0x28000 + page_size), 0x14001);
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x12340000 + i * 0x111;
    EM_A0 = 0x24321;
    EM_A7 = operand + 4;
    cpu_state.isp = 0x20100;
    set_status((privileged ? 0x2000 : 0) | sr);
    for (unsigned i = 0; i < 4; ++i)
        ram[scalar_store_physical(operand + i)] = 0x71 + i;
    unsigned opcode = form < 8     ? 0x4e50 + form
                      : form < 16  ? 0x4808 + form - 8
                      : form == 16 ? 0x4850
                      : form == 17 ? 0x4857
                      : form == 18 ? 0x486f
                                   : 0x4879;
    code_word(0x10000, opcode);
    code_word(0x10002, form < 8 || form == 18 ? displacement : displacement >> 16);
    code_word(0x10004, displacement);
    return form < 8 || form == 18 ? 0x10004 : form < 16 || form == 19 ? 0x10006 : 0x10002;
}

/* Exercise a real guest cleanup/RTE with a separate resident supervisor stack.
   Preserve scratch registers in guest code, including a LINK-owned A2. */
static void stack_store_guest_cleanup(const Syn68kIntegerStep* step, const M68kReg expected[16], unsigned expected_sr,
                                      unsigned operand, unsigned next, unsigned fault, unsigned completed)
{
    unsigned original_dfc = cpu_state.dfc;
    fail_write_physical = UINT32_MAX;
    unsigned fault_pte = pte(operand + completed);
    unsigned table_page = 0x3000 & ~(page_size - 1);
    supervisor = 1;
    put32(pte(0x10000), 0xc001);
    put32(pte(0x10000 + page_size), 0xe001);
    put32(pte(0x20000), 0x18001);
    put32(pte(0x30000), table_page | 1);
    unsigned target = 0x30000 + fault_pte - table_page;
    unsigned descriptor = operand + completed >= 0x28000 + page_size ? 0x4001 : 1;
    unsigned wb_address = fault == 1 ? 40 : 32;
    unsigned wb_data = fault == 1 ? 44 : 36;
    const unsigned program[] = {
        0x2f0a,          0x2f04, 0x2f03,       0x2f02, /* A2/D4/D3/D2, then restore ascending. */
        0x4e7a,          0x3001, 0x2f03,               /* Preserve DFC above the saved general registers. */
        0x23fc,          1,      descriptor,   target >> 16, target & 65535, 0xf518, 0x246f,
        wb_address + 20, 0x242f, wb_data + 20, 0x7601,       0x4e7b,         0x3001, 0x0e92,
        0x2800,          0x261f, 0x4e7b,       0x3001,       0x4cdf,         0x041c, 0x4e73};
    unsigned handler = 0x10000 + page_size + 0x40, end = handler;
    for (unsigned i = 0; i < sizeof program / sizeof *program; ++i) {
        if (fault == 1 && i == 20) {
            const unsigned rotation[] = {0x260a, 0x0283, 0, 3, 0xe78b, 0xe7ba};
            for (unsigned j = 0; j < sizeof rotation / sizeof *rotation; ++j) {
                code_word(end, rotation[j]);
                end += 2;
            }
        }
        code_word(end, program[i]);
        end += 2;
    }
    cpu_state.vbr = 0x20000;
    put32(physical(cpu_state.vbr + 8), handler);
    Syn68kMappedCPU cpu = {.pc = next, .memory = &memory, .native = native_execution};
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_exception_enter(&cpu, step, 0, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
    unsigned frame = 0x20100 - 60;
    assert(mapped_value(frame, 2) == expected_sr && mapped_value(frame + 2, 4) == next &&
           mapped_value(frame + 20, 4) == operand && mapped_value(frame + wb_address, 4) == operand);
    unsigned instructions = 17 + (fault == 1 ? 4 : 0);
    unsigned traced = !!(step->store_state == 3);
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, instructions, &run));
    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
           run.completed == instructions - traced && cpu.pc == next && status() == expected_sr &&
           !memcmp(expected, cpu_state.regs, sizeof cpu_state.regs));
    if (traced) {
        unsigned trace_handler = 0x10000 + page_size + 0xc0;
        put32(physical(cpu_state.vbr + 36), trace_handler);
        code_word(trace_handler, 0x4e73);
        assert(run.step.vector == 9 && run.step.format == 2 && run.step.fault_address == 0x10000);
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next && status() == expected_sr &&
               !memcmp(expected, cpu_state.regs, sizeof cpu_state.regs));
    }
    assert(scalar_store_value(operand, 4) == step->access.value && cpu_state.dfc == original_dfc);
}

static unsigned call_store_setup(unsigned page8k, unsigned form, unsigned operand, unsigned odd, unsigned near,
                                 unsigned sr, unsigned* target)
{
    static const unsigned lengths[] = {2, 4, 6, 2, 2, 4, 4, 4, 4, 6, 4, 4, 8};
    fixture(page8k, 0);
    syn68k_fpu_reset();
    put32(pte(0x28000), 0x10001);
    put32(pte(0x28000 + page_size), 0x14001);
    put32(pte(0x24000), 0x1c001);
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x33440000 + i * 0x101;
    EM_A7 = operand + 4;
    cpu_state.usp = EM_A7;
    cpu_state.isp = 0x20100;
    cpu_state.msp = 0x20200;
    set_status(sr);
    cpu_state.dfc = (form + (sr >> 14)) & 7;
    unsigned next = 0x10000 + lengths[form];
    *target = form == 4 ? EM_A7 : (form == 8 ? 0x7080 : near && form ? next : 0x10080) | odd;
    for (unsigned i = 0; i < 4; ++i)
        ram[scalar_store_physical(operand + i)] = 0x91 + i;
    unsigned displacement = *target - 0x10002;
    unsigned opcode = form == 0    ? 0x6100 | (displacement & 255)
                      : form == 1  ? 0x6100
                      : form == 2  ? 0x61ff
                      : form == 3  ? 0x4e90
                      : form == 4  ? 0x4e97
                      : form == 5  ? 0x4ea8
                      : form == 8  ? 0x4eb8
                      : form == 9  ? 0x4eb9
                      : form == 10 ? 0x4eba
                      : form == 11 ? 0x4ebb
                                   : 0x4eb0;
    code_word(0x10000, opcode);
    EM_A0 = *target;
    if (form == 1 || form == 10)
        code_word(0x10002, displacement);
    else if (form == 2 || form == 9) {
        unsigned value = form == 2 ? displacement : *target;
        code_word(0x10002, value >> 16);
        code_word(0x10004, value);
    }
    else if (form == 5) {
        EM_A0 = *target - 0x20;
        code_word(0x10002, 0x20);
    }
    else if (form == 6 || form == 11) {
        EM_A0 = *target - 0x14;
        EM_D1 = form == 6 ? 0x10 : *target - 0x10006;
        code_word(0x10002, 0x1804);
    }
    else if (form == 7) {
        EM_A0 = 0x24080;
        put32(0x1c080, *target);
        code_word(0x10002, 0x0151);
    }
    else if (form == 8)
        code_word(0x10002, *target);
    else if (form == 12) {
        EM_A0 = *target - 0x1234;
        code_word(0x10002, 0x0170);
        code_word(0x10004, 0);
        code_word(0x10006, 0x1234);
    }
    watched_tail_physical = 0x1c080;
    watched_tail_reads = watched_tail_successes = 0;
    return next;
}

static void guest_call_writebacks(void)
{
    unsigned first = cases;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned form = 0; form < 13; ++form)
            for (unsigned odd = 0; odd < 2; ++odd)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned fault = 1; fault < 4; ++fault)
                        for (unsigned completed = 0; completed < 4; ++completed)
                            for (unsigned lane = 0; lane < 4; ++lane)
                                for (unsigned ccr = 0; ccr <= 31; ccr += 31) {
                                    if ((completed && lane != ((4 - completed) & 3)) ||
                                        (form == 4 && (lane & 1) != odd))
                                        continue;
                                    unsigned operand = fault == 1 ? 0x28100 + lane
                                                                  : 0x28000 + (page8k ? 8192 : 4096) - completed +
                                                                        (completed ? 0 : lane);
                                    unsigned target;
                                    unsigned sr = trace << 14 | ccr;
                                    unsigned next =
                                        call_store_setup(page8k, form, operand, odd, lane >> 1, sr, &target);
                                    assert((target & 1) == odd);
                                    M68kReg expected[16];
                                    memcpy(expected, cpu_state.regs, sizeof expected);
                                    Syn68kFPUState expected_fpu = cpu_state.fpu;
                                    if (!odd)
                                        expected[15].ul.n -= 4;
                                    memory.aligned_transfers = 1;
                                    if (fault == 1)
                                        fail_write_physical = scalar_store_physical(operand + completed);
                                    else
                                        put32(pte(operand + completed), fault == 3 ? 0 : 0x14005);
                                    Syn68kMappedCPU cpu = {
                                        .pc = 0x10000, .memory = &memory, .native = native_execution};
                                    Syn68kMappedRun run;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                           run.step.store_state == (odd     ? 4u
                                                                    : trace ? 3u
                                                                            : 2u) &&
                                           cpu.pc == (odd ? 0x10000u : target) && status() == sr &&
                                           !memcmp(expected, cpu_state.regs, sizeof expected) &&
                                           !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                    assert(run.step.access.request == operand && run.step.access.size == 4 &&
                                           run.step.access.logical == operand + completed &&
                                           run.step.access.completed == completed && run.step.access.value == next &&
                                           watched_tail_reads == (form == 7));
                                    for (unsigned i = 0; i < 4; ++i)
                                        assert(ram[scalar_store_physical(operand + i)] ==
                                               (i < completed ? (next >> (24 - i * 8)) & 255 : 0x91 + i));
                                    stack_store_guest_cleanup(&run.step, expected, sr, operand, odd ? 0x10000 : target,
                                                              fault, completed);
                                    assert(cpu_state.isp == 0x20100 &&
                                           !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                    if (odd) {
                                        expected[15].ul.n -= 4;
                                        cpu.pc = 0x10000;
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 3 &&
                                               run.step.format == 2 && run.step.exception_pc == 0x10000 &&
                                               run.step.fault_address == (target & ~1u) &&
                                               !memcmp(expected, cpu_state.regs, sizeof expected) && status() == sr &&
                                               scalar_store_value(operand, 4) == next);
                                    }
                                    assert(watched_tail_reads == (form == 7 ? 1u + odd : 0u) &&
                                           !watched_tail_successes);
                                    ++cases;
                                }
    memory.aligned_transfers = 0;
    printf("%u call writeback/abort/guest-RTE/address-priority cases passed\n", cases - first);
}

static void call_replay_guards(void)
{
    static const unsigned mutations[] = {0, 1, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    unsigned first = cases;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned odd = 0; odd < 2; ++odd)
            for (unsigned trace = 0; trace < 4; ++trace)
                for (unsigned mutation = 0; mutation <= sizeof mutations / sizeof *mutations + 1; ++mutation) {
                    CPUState expected = {0};
                    unsigned target = 0, expected_status = 0;
                    for (unsigned component = 0; component < 2; ++component) {
                        call_store_setup(page8k, 3, 0x28100, odd, 0, trace << 14 | 31, &target);
                        memory.aligned_transfers = 1;
                        fail_write_physical = scalar_store_physical(0x28100);
                        if (mutation == sizeof mutations / sizeof *mutations) {
                            read_callback_address = fail_write_physical;
                            read_callback_kind = 4;
                            read_callback_register = 7;
                            read_callback_mask = 0x40;
                        }
                        else if (mutation == sizeof mutations / sizeof *mutations + 1) {
                            trace_read_physical = fail_write_physical;
                            trace_bus_kind = 1;
                            trace_read_sr = trace << 14;
                            trace_read_ccr = 0;
                        }
                        else {
                            mutate_write_physical = fail_write_physical;
                            write_mutation = mutations[mutation];
                        }
                        if (!component) {
                            Syn68kIntegerStep step;
                            assert(syn68k_integer_step(0x10000, &memory, &step));
                            assert(step.vector == 2 && step.store_state == 1 && step.store_next == target);
                            expected = cpu_state;
                            expected_status = status();
                            if (odd && !mutation)
                                expected.regs[15].ul.n += 4;
                        }
                        else {
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            unsigned state = odd ? (mutation ? 1 : 4) : trace ? 3 : 2;
                            CPUState actual = cpu_state;
                            if (!odd) {
                                /* Retired flags may use a different truthy cache
                                   representation; compare architectural CCR separately. */
                                actual.ccc = expected.ccc;
                                actual.ccv = expected.ccv;
                                actual.ccnz = expected.ccnz;
                                actual.ccn = expected.ccn;
                                actual.ccx = expected.ccx;
                            }
                            assert(status() == expected_status);
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                   run.step.store_state == state && cpu.pc == (odd ? 0x10000u : target) &&
                                   !memcmp(&expected, &actual, sizeof expected));
                            if (odd && mutation) {
                                CPUState before = cpu_state;
                                unsigned old_reads = reads, old_writes = writes;
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads &&
                                       writes == old_writes && !memcmp(&before, &cpu_state, sizeof before));
                            }
                            ++cases;
                        }
                    }
                }
    memory.aligned_transfers = 0;
    printf("%u call callback/replay certification guards passed\n", cases - first);
}

static void stack_store_completions(void)
{
    static const unsigned displacements[] = {0, 0xfffffff0, 64, 0x80000000};
    unsigned first = cases;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned form = 0; form < 20; ++form)
                for (unsigned sample = 0; sample < 4; ++sample)
                    for (unsigned trace = 0; trace < 4; ++trace)
                        for (unsigned fault = 0; fault < 4; ++fault)
                            for (unsigned completed = 0; completed < 4; ++completed)
                                for (unsigned callback = 0; callback < 3; ++callback) {
                                    if ((!fault && completed) || (callback && fault != 1))
                                        continue;
                                    unsigned lane = completed ? (4 - completed) & 3 : sample;
                                    unsigned operand = fault >= 2 ? 0x28000 + (page8k ? 8192 : 4096) - completed +
                                                                        (completed ? 0 : lane)
                                                                  : 0x28100 + lane;
                                    unsigned displacement = form == 18   ? 0x23
                                                            : form == 19 ? 0x12345679
                                                                         : displacements[sample];
                                    unsigned delta = form < 8 ? (unsigned)(int)(short)displacement : displacement;
                                    unsigned next = stack_store_setup(page8k, privileged, form, operand, displacement,
                                                                      trace << 14 | sample);
                                    M68kReg initial[16], expected[16];
                                    memcpy(initial, cpu_state.regs, sizeof initial);
                                    memcpy(expected, initial, sizeof expected);
                                    unsigned saved = form < 16    ? initial[8 + (form & 7)].ul.n
                                                     : form == 16 ? EM_A0
                                                     : form == 17 ? EM_A7
                                                     : form == 18 ? EM_A7 + 0x23
                                                                  : displacement;
                                    unsigned live_sp = operand ^ (callback == 1 ? 0x40 : 0);
                                    if (form < 16)
                                        expected[8 + (form & 7)].ul.n = live_sp;
                                    expected[15].ul.n = live_sp + (form < 16 ? delta : 0);
                                    unsigned expected_sr = (privileged ? 0x2000 : 0) | trace << 14 | sample;
                                    if (callback == 2)
                                        expected_sr = (expected_sr ^ 0xc000) ^ 31;
                                    for (unsigned component = 0; component < 2; ++component) {
                                        stack_store_setup(page8k, privileged, form, operand, displacement,
                                                          trace << 14 | sample);
                                        memory.aligned_transfers = 1;
                                        if (fault == 1)
                                            fail_write_physical = scalar_store_physical(operand + completed);
                                        else if (fault >= 2)
                                            put32(pte(operand + completed), fault == 3 ? 0 : 0x14005);
                                        if (callback == 1) {
                                            read_callback_address = fail_write_physical;
                                            read_callback_kind = 4;
                                            read_callback_register = 7;
                                            read_callback_mask = 0x40;
                                        }
                                        if (callback == 2) {
                                            trace_read_physical = fail_write_physical;
                                            trace_bus_kind = 1;
                                            trace_read_sr = (trace ^ 3) << 14;
                                            trace_read_ccr = sample ^ 31;
                                        }
                                        Syn68kIntegerStep step;
                                        if (component) {
                                            assert(syn68k_integer_step(0x10000, &memory, &step));
                                            if (fault) {
                                                assert(step.vector == 2 && step.store_state == 1 &&
                                                       step.store_next == next && step.exception_pc == 0x10000 &&
                                                       step.store_link_displacement == (form < 16 ? delta : 0));
                                                initial[15].ul.n = live_sp;
                                                assert(!memcmp(initial, cpu_state.regs, sizeof initial));
                                            }
                                            else
                                                assert(!step.vector && step.next == next &&
                                                       !memcmp(expected, cpu_state.regs, sizeof expected));
                                        }
                                        else {
                                            Syn68kMappedCPU cpu = {
                                                .pc = 0x10000, .memory = &memory, .native = native_execution};
                                            Syn68kMappedRun run;
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                            step = run.step;
                                            assert(cpu.pc == next &&
                                                   !memcmp(expected, cpu_state.regs, sizeof expected));
                                            unsigned traced = !!(trace & 2);
                                            if (fault)
                                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && step.vector == 2 &&
                                                       step.store_state == (traced ? 3U : 2U) &&
                                                       !step.store_link_displacement && step.store_trace_pc == 0x10000);
                                            else if (traced)
                                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && step.vector == 9 &&
                                                       step.exception_pc == next && step.fault_address == 0x10000);
                                            else
                                                assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1);
                                        }
                                        assert(status() == expected_sr && read_callback_hits == (callback == 1));
                                        if (fault) {
                                            assert(step.access.request == operand && step.access.size == 4 &&
                                                   step.access.logical == operand + completed &&
                                                   step.access.completed == completed && step.access.value == saved);
                                        }
                                        for (unsigned i = 0; i < 4; ++i)
                                            assert(
                                                ram[scalar_store_physical(operand + i)] ==
                                                ((!fault || i < completed) ? (saved >> (24 - i * 8)) & 255 : 0x71 + i));
                                        if (!component && fault && !privileged && callback != 1)
                                            stack_store_guest_cleanup(&step, expected, expected_sr, operand, next,
                                                                      fault, completed);
                                        ++cases;
                                    }
                                }
    memory.aligned_transfers = 0;
    printf("%u stack-store component/retirement cases passed\n", cases - first);
}

static void scalar_store_setup(unsigned page8k, unsigned privileged, unsigned opcode, unsigned operand,
                               unsigned predecrement, unsigned ccr, unsigned extension, unsigned size)
{
    fixture(page8k, privileged);
    put32(pte(0x28000), 0x10001);
    put32(pte(0x28000 + page_size), 0x14001);
    put32(pte(0x24000), 0x1c001);
    put32(0x1c080, 0x10203040);
    for (unsigned i = 0; i < size; ++i)
        ram[scalar_store_physical(operand + i)] = 0x718293a4u >> (24 - i * 8);
    code_word(0x10000, opcode);
    code_word(0x10002, extension);
    EM_D0 = 0x80FEDCBA;
    EM_D4 = 0;
    EM_A0 = operand + predecrement;
    EM_A1 = 0x24080 + (((opcode & 0xf138) == 0xd108 || (opcode & 0xf138) == 0x9108) ? size : 0);
    cpu_state.isp = 0x20100;
    EM_A7 = privileged ? cpu_state.isp : 0x76543210;
    set_status((privileged ? 0x2000 : 0) | ccr);
}

/* boundary: zero selects ordinary lanes, one crosses a page, two stays in-page.
   completed is the successful prefix of the faulted scalar, never a suffix. */
static void guest_scalar_bus_writebacks(unsigned boundary, unsigned completed, int live_trace)
{
    static const struct {
        unsigned op, size, pre, ext, read;
    } forms[] = {
        {0x1080, 1, 0, 0, 0}, {0x3080, 2, 0, 0, 0}, {0x2080, 4, 0, 0, 0}, {0x10c0, 1, 0, 0, 0}, {0x20c0, 4, 0, 0, 0},
        {0x3100, 2, 2, 0, 0}, {0x4250, 2, 0, 0, 0}, {0x4490, 4, 0, 0, 1}, {0x4610, 1, 0, 0, 1}, {0x5250, 2, 0, 0, 1},
        {0x5390, 4, 0, 0, 1}, {0x0010, 1, 0, 1, 1}, {0x50d0, 1, 0, 0, 0}, {0x51d0, 1, 0, 0, 0}, {0xe2d0, 2, 0, 0, 1},
        {0x08d0, 1, 0, 3, 1}, {0x30c0, 2, 0, 0, 0}, {0x2100, 4, 4, 0, 0}, {0x4210, 1, 0, 0, 0}, {0x4290, 4, 0, 0, 0},
        {0x0050, 2, 0, 1, 1}, {0xd190, 4, 0, 0, 1}, {0x9190, 4, 0, 0, 1}, {0xc190, 4, 0, 0, 1}, {0x8190, 4, 0, 0, 1},
        {0xb190, 4, 0, 0, 1}, {0x4090, 4, 0, 0, 1}, {0xd189, 4, 4, 0, 1}, {0x9189, 4, 4, 0, 1}, {0x20d9, 4, 0, 0, 0},
        {0x30d9, 2, 0, 0, 0}, {0x42d0, 2, 0, 0, 0}, {0x42d8, 2, 0, 0, 0}, {0x42e0, 2, 2, 0, 0}, {0x40d0, 2, 0, 0, 0},
        {0x40d8, 2, 0, 0, 0}, {0x40e0, 2, 2, 0, 0}};

    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned form = 0; form < sizeof forms / sizeof *forms; ++form)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        for (unsigned fault = 0; fault < 3; ++fault)
                            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                                unsigned size = forms[form].size;
                                if (!privileged && (forms[form].op & 0xffc0) == 0x40c0)
                                    continue;
                                if (completed >= size || (completed && lane != ((4 - completed) & 3)) ||
                                    (boundary == 2 && fault != 1))
                                    continue;
                                unsigned status_store =
                                    (forms[form].op & 0xffc0) == 0x40c0 || (forms[form].op & 0xffc0) == 0x42c0;
                                if (live_trace >= 0 &&
                                    (page8k || !privileged || ccr || (lane != (completed ? 2U : 0U)) ||
                                     (forms[form].op != 0x2080 && forms[form].op != 0xd189 && !status_store) ||
                                     (status_store && fault != 1)))
                                    continue;
                                unsigned operand = boundary == 2   ? 0x28104 - completed + (completed ? 0 : lane)
                                                   : boundary == 1 ? 0x28000 + (page8k ? 8192 : 4096) - completed +
                                                                         (completed ? 0 : lane)
                                                                   : 0x28000 + lane;
                                scalar_store_setup(page8k, privileged, forms[form].op, operand, forms[form].pre,
                                                   ccr | trace << 14, forms[form].ext, size);
                                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                                Syn68kMappedRun run;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                       run.step.vector == (trace & 2 ? 9U : 0U));
                                if (trace & 2)
                                    assert(run.step.format == 2 && run.step.exception_pc == cpu.pc &&
                                           run.step.fault_address == 0x10000);
                                unsigned expected_pc = cpu.pc, expected_sr = status();
                                unsigned expected_data = scalar_store_value(operand, size);
                                M68kReg expected_regs[16];
                                memcpy(expected_regs, cpu_state.regs, sizeof expected_regs);
                                scalar_store_setup(page8k, privileged, forms[form].op, operand, forms[form].pre,
                                                   ccr | trace << 14, forms[form].ext, size);
                                unsigned initial[4];
                                for (unsigned i = 0; i < size; ++i)
                                    initial[i] = ram[scalar_store_physical(operand + i)];
                                unsigned fault_pte = pte(operand + completed), root = privileged ? 0x6000 : 0x3000;
                                if (fault == 1) {
                                    memory.aligned_transfers = 1;
                                    fail_write_physical = scalar_store_physical(operand + completed);
                                }
                                else if (fault == 2 && forms[form].read) {
                                    late_read_physical = scalar_store_physical(operand + completed);
                                    late_read_pte = fault_pte;
                                    late_read_descriptor = 0;
                                }
                                else
                                    put32(fault_pte,
                                          fault == 2
                                              ? 0
                                              : ((operand + completed >= 0x28000 + page_size ? 0x14000 : 0x10000) | 5));
                                watched_physical = scalar_store_physical(operand);
                                watched_tail_physical = 0x1c080;
                                watched_tail_reads = watched_tail_successes = 0;
                                if (live_trace >= 0) {
                                    trace_read_physical = fault == 1 ? fail_write_physical : physical(0x10000);
                                    trace_bus_kind = fault == 1;
                                    trace_read_sr = (unsigned)live_trace << 14;
                                    expected_sr = (expected_sr & ~0xc000) | trace_read_sr;
                                    if (status_store) {
                                        trace_read_ccr = ccr ^ 31;
                                        expected_sr = (expected_sr & ~31) | trace_read_ccr;
                                    }
                                }
                                cpu.pc = 0x10000;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                unsigned traced = !!(trace & 2);
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.store_state == (traced ? 3U : 2U));
                                assert(cpu.pc == expected_pc && status() == expected_sr && !run.completed &&
                                       !memcmp(expected_regs, cpu_state.regs, sizeof expected_regs));
                                assert(run.step.access.request == operand &&
                                       run.step.access.logical == operand + completed && run.step.access.size == size &&
                                       run.step.access.completed == completed &&
                                       run.step.access.value == expected_data &&
                                       run.step.access.bus_error == (fault == 1));
                                unsigned source_read = forms[form].op == 0xd189 || forms[form].op == 0x9189 ||
                                                       forms[form].op == 0x20d9 || forms[form].op == 0x30d9;
                                assert(watched_successes == !!completed && watched_read_attempts == forms[form].read &&
                                       watched_tail_reads == source_read && !watched_tail_successes);
                                for (unsigned i = 0; i < size; ++i)
                                    assert(
                                        ram[scalar_store_physical(operand + i)] ==
                                        (i < completed ? (expected_data >> ((size - i - 1) * 8)) & 255 : initial[i]));
                                fail_write_physical = UINT32_MAX;
                                supervisor = 1;
                                put32(pte(0x10000 + page_size), 0xe001);
                                put32(pte(0x20000), 0x18001);
                                unsigned table_page = root & ~(page_size - 1);
                                put32(pte(0x30000), table_page | 1);
                                unsigned target = 0x30000 + fault_pte - table_page;
                                unsigned wb_address = fault == 1 ? 40 : 32;
                                unsigned rotate = fault == 1 && size > 1;
                                unsigned wb_data = fault == 1 ? size == 1 ? 44 + (operand & 3) : 44 : 36 + 4 - size;
                                unsigned descriptor = operand + completed >= 0x28000 + page_size ? 0x4001 : 1;
                                const unsigned program[] = {0x23fc,
                                                            1,
                                                            descriptor,
                                                            target >> 16,
                                                            target & 65535,
                                                            0xf518,
                                                            0x246f,
                                                            wb_address,
                                                            rotate      ? 0x242f
                                                            : size == 1 ? 0x142f
                                                            : size == 2 ? 0x342f
                                                                        : 0x242f,
                                                            wb_data,
                                                            0x7600 | (privileged ? 5 : 1),
                                                            0x4e7b,
                                                            0x3001,
                                                            size == 1   ? 0x0e12
                                                            : size == 2 ? 0x0e52
                                                                        : 0x0e92,
                                                            0x2800,
                                                            0x5284,
                                                            0x4e73};
                                unsigned handler = 0x10000 + page_size + 0x40, end = handler;
                                for (unsigned i = 0; i < sizeof program / sizeof *program; ++i) {
                                    if (rotate && i == sizeof program / sizeof *program - 4) {
                                        const unsigned rotation[] = {0x260a, 0x0283, 0, 3, 0xe78b, 0xe7ba};
                                        for (unsigned j = 0; j < sizeof rotation / sizeof *rotation; ++j) {
                                            code_word(end, rotation[j]);
                                            end += 2;
                                        }
                                        if (size == 2) {
                                            code_word(end, 0x4842);
                                            end += 2;
                                        }
                                    }
                                    code_word(end, program[i]);
                                    end += 2;
                                }
                                cpu_state.vbr = 0x20000;
                                put32(physical(cpu_state.vbr + 8), handler);
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                unsigned frame = 0x20100 - 60;
                                unsigned size_bits = size == 1 ? 0x20 : size == 2 ? 0x40 : 0, fc = privileged ? 5 : 1;
                                assert(mapped_value(frame, 2) == expected_sr &&
                                       mapped_value(frame + 2, 4) == expected_pc);
                                assert(mapped_value(frame + 12, 2) ==
                                       ((fault == 1 ? 0 : 0x400) | size_bits | fc | (traced ? 0x2000 : 0) |
                                        (fault != 1 && completed ? 0x800 : 0)));
                                assert(mapped_value(frame + 8, 4) == (traced ? 0x10000 : 0));
                                assert(mapped_value(frame + (fault == 1 ? 18 : 16), 2) == (0x80 | size_bits | fc) &&
                                       !mapped_value(frame + (fault == 1 ? 16 : 18), 2) &&
                                       !mapped_value(frame + 14, 2));
                                assert(mapped_value(frame + 20, 4) == operand &&
                                       mapped_value(frame + wb_address, 4) == operand);
                                unsigned data = mapped_value(frame + (fault == 1 ? 44 : 36), 4);
                                if (fault == 1) {
                                    unsigned rotation = (operand & 3) * 8;
                                    if (rotation)
                                        data = data << rotation | data >> (32 - rotation);
                                    data >>= (4 - size) * 8;
                                }
                                assert(data == expected_data);
                                unsigned instructions = 9 + (rotate ? 4 + (size == 2) : 0);
                                assert(syn68k_mapped_cpu_run(&cpu, instructions, &run));
                                assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                       run.completed == instructions - traced && cpu.pc == expected_pc &&
                                       status() == expected_sr && EM_A0 == expected_regs[8].ul.n &&
                                       EM_A1 == expected_regs[9].ul.n);
                                if (traced) {
                                    unsigned trace_handler = 0x10000 + page_size + 0xc0;
                                    put32(physical(cpu_state.vbr + 36), trace_handler);
                                    code_word(trace_handler, 0x4e73);
                                    assert(run.step.vector == 9 && run.step.format == 2 &&
                                           run.step.fault_address == 0x10000);
                                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == trace_handler);
                                    assert(mapped_value(EM_A7, 2) == expected_sr &&
                                           mapped_value(EM_A7 + 2, 4) == expected_pc &&
                                           mapped_value(EM_A7 + 8, 4) == 0x10000);
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == expected_pc &&
                                           status() == expected_sr);
                                }
                                assert(scalar_store_value(operand, size) == expected_data &&
                                       watched_successes == 1 + !!completed &&
                                       watched_attempts == 1u + !!(fault == 1 || completed) && EM_D4 == 1 &&
                                       watched_read_attempts == forms[form].read && watched_tail_reads == source_read &&
                                       !watched_tail_successes);
                                ++cases;
                            }
    memory.aligned_transfers = 0;
    fail_write_physical = watched_physical = watched_tail_physical = trace_read_physical = UINT32_MAX;
    set_status(0x2000);
}

static void guest_scalar_writebacks(void)
{
    guest_scalar_bus_writebacks(0, 0, -1);
}

static void guest_partial_scalar_writebacks(void)
{
    for (unsigned boundary = 1; boundary <= 2; ++boundary)
        for (unsigned completed = 0; completed < 4; ++completed)
            guest_scalar_bus_writebacks(boundary, completed, -1);
}

static void guest_scalar_trace_writebacks(void)
{
    for (int live = 0; live < 4; ++live) {
        guest_scalar_bus_writebacks(0, 0, live);
        guest_scalar_bus_writebacks(1, 2, live);
        guest_scalar_bus_writebacks(2, 2, live);
    }
}

/* Literal mappings and instruction encodings for all alterable MOVES EA
   classes. Alternate spaces deliberately have no ordinary operand mapping. */
static unsigned moves_page, moves_fc;

static unsigned moves_physical(unsigned logical)
{
    if (moves_fc == 0 || moves_fc == 3 || moves_fc == 4 || moves_fc == 7)
        return logical;
    unsigned offset = logical - moves_page;
    return offset < page_size ? 0x10000 + offset : 0x14000 + offset - page_size;
}

static unsigned moves_value(unsigned operand, unsigned size)
{
    unsigned value = 0;
    for (unsigned i = 0; i < size; ++i)
        value = value << 8 | ram[moves_physical(operand + i)];
    return value;
}

static unsigned moves_setup(unsigned page8k, unsigned fc, unsigned size, unsigned form, unsigned reg, unsigned operand,
                            unsigned sr, unsigned writing)
{
    static const unsigned eas[] = {0x10, 0x18, 0x20, 0x28, 0x30, 0x30, 0x38, 0x39, 0x30};
    fixture(page8k, 1);
    syn68k_fpu_reset();
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x11335500 + i * 4;
    EM_D0 = 0x12345678;
    EM_D1 = 0x10;
    EM_D4 = 0;
    EM_A1 = 0x89abcdef;
    cpu_state.usp = 0x76543210;
    cpu_state.isp = EM_A7 = 0x20100;
    cpu_state.msp = 0x201c0;
    cpu_state.sfc = cpu_state.dfc = fc;
    set_status(0x2000 | sr);
    moves_fc = fc == 2 || fc == 6 ? fc - 1 : fc;
    moves_page = operand & ~(page_size - 1);
    supervisor = !!(moves_fc & 4);
    unsigned alternate = moves_fc != 1 && moves_fc != 5;
    put32(pte(moves_page), alternate ? 0 : 0x10001);
    put32(pte(moves_page + page_size), alternate ? 0 : 0x14001);
    supervisor = 1;
    put32(pte(0x24000), 0x1c001);
    put32(0x1c080, operand);
    code_word(0x10000, (size == 1 ? 0x0e00 : size == 2 ? 0x0e40 : 0x0e80) | eas[form]);
    code_word(0x10002, reg << 12 | (writing ? 0x0800 : 0));
    EM_A0 = operand;
    unsigned next = 0x10004;
    if (form == 2)
        EM_A0 += size;
    if (form == 3 || form == 4) {
        EM_A0 = operand - (form == 3 ? 0x20 : 0x14);
        code_word(next, form == 3 ? 0x20 : 0x1804);
        next += 2;
    }
    else if (form == 5) {
        EM_A0 = 0x24080;
        code_word(next, 0x0151); // ([A0]), suppressed index and null BD/OD
        next += 2;
    }
    else if (form == 6 || form == 7) {
        EM_A0 = 0x13579bdf;
        if (form == 7) {
            code_word(next, operand >> 16);
            next += 2;
        }
        code_word(next, operand & 65535);
        next += 2;
    }
    else if (form == 8) {
        EM_A0 = operand - 0x1234;
        code_word(next, 0x0170); // Long base displacement, suppressed index
        code_word(next + 2, 0);
        code_word(next + 4, 0x1234);
        next += 6;
    }
    for (unsigned i = 0; i < 4; ++i)
        ram[moves_physical(operand + i)] = 0xa5 + i;
    if (!writing)
        for (unsigned i = 0; i < size; ++i)
            ram[moves_physical(operand + i)] = 0x80fedcbau >> ((size - i - 1) * 8);
    watched_physical = moves_physical(operand);
    watched_tail_physical = 0x1c080;
    watched_tail_reads = watched_tail_successes = 0;
    return next;
}

static void moves_normal(int live_trace)
{
    static const unsigned sources[] = {0, 9, 8, 15};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc = 0; fc < 8; ++fc)
            for (unsigned size = 1; size <= 4; size *= 2)
                for (unsigned form = 0; form < 9; ++form)
                    for (unsigned source = 0; source < 4; ++source)
                        for (unsigned writing = 0; writing < 2; ++writing)
                            for (unsigned trace = 0; trace < 4; ++trace)
                                for (unsigned ccr = 0; ccr < 32; ++ccr)
                                    for (unsigned lane = 0; lane < 4; ++lane) {
                                        if (ccr && (form || source || lane))
                                            continue;
                                        if (live_trace >= 0 && (page8k || form > 2 || size != 4 || ccr || lane))
                                            continue;
                                        unsigned operand = (form == 6                        ? 0x7000
                                                            : (fc & 3) == 0 || (fc & 3) == 3 ? 0x8040
                                                                                             : 0x28040) +
                                                           lane;
                                        unsigned reg = sources[source];
                                        unsigned next = moves_setup(page8k, fc, size, form, reg, operand,
                                                                    trace << 14 | ccr, writing);
                                        M68kReg expected[16];
                                        memcpy(expected, cpu_state.regs, sizeof expected);
                                        unsigned updated = EM_A0 + (form == 1 ? size : form == 2 ? -size : 0);
                                        expected[8].ul.n = updated;
                                        unsigned mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                                        unsigned data = (reg == 8 ? updated : cpu_state.regs[reg].ul.n) & mask;
                                        if (!writing) {
                                            data = 0x80fedcba & mask;
                                            expected[reg].ul.n = reg >= 8 ? (size == 1   ? (uint32)(int8)data
                                                                             : size == 2 ? (uint32)(int16)data
                                                                                         : data)
                                                                          : (expected[reg].ul.n & ~mask) | data;
                                        }
                                        unsigned expected_sr = 0x2000 | trace << 14 | ccr;
                                        if (live_trace >= 0) {
                                            trace_read_physical = physical(0x10000);
                                            trace_bus_kind = 0;
                                            trace_read_sr = (unsigned)live_trace << 14;
                                            expected_sr = 0x2000 | trace_read_sr | ccr;
                                        }
                                        Syn68kMappedCPU cpu = {
                                            .pc = 0x10000, .memory = &memory, .native = native_execution};
                                        Syn68kMappedRun run;
                                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                        assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                               run.step.vector == (trace ? 9u : 0u) && cpu.pc == next &&
                                               run.completed == !trace);
                                        assert(!memcmp(expected, cpu_state.regs, sizeof expected) &&
                                               status() == expected_sr && moves_value(operand, size) == data &&
                                               watched_tail_reads == (form == 5) && !watched_tail_successes);
                                        assert(writing ? watched_attempts == 1 && watched_successes == 1 &&
                                                             !watched_read_attempts && watched_write_fc == moves_fc
                                                       : watched_read_attempts == 1 && !watched_attempts &&
                                                             watched_read_fc == moves_fc);
                                        if (trace)
                                            assert(run.step.format == 2 && run.step.exception_pc == next &&
                                                   run.step.fault_address == 0x10000);
                                        ++cases;
                                    }
    watched_physical = watched_tail_physical = trace_read_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static void moves_faults(unsigned writing, int live_trace)
{
    static const unsigned sources[] = {0, 9, 8, 15};
    static const unsigned transfers[] = {0x10, 1, 1, 0x13, 0x14, 5, 5, 0x17};
    /* Successful naturally aligned prefixes, from MC68040UM 7.3. */
    static const unsigned prefixes[3][4] = {{1, 1, 1, 1}, {1, 3, 1, 3}, {1, 11, 5, 11}};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned fc = 0; fc < 8; ++fc)
            for (unsigned size_index = 0; size_index < 3; ++size_index)
                for (unsigned form = 0; form < 9; ++form)
                    for (unsigned source = 0; source < 4; ++source)
                        for (unsigned trace = 0; trace < 4; ++trace)
                            for (unsigned ccr = 0; ccr < 32; ++ccr)
                                for (unsigned fault = 0; fault < 3; ++fault)
                                    for (unsigned boundary = 0; boundary < 2; ++boundary)
                                        for (unsigned lane = 0; lane < 4; ++lane)
                                            for (unsigned completed = 0; completed < 4; ++completed) {
                                                unsigned size = 1u << size_index;
                                                unsigned alternate = (fc & 3) == 0 || (fc & 3) == 3;
                                                if (completed >= size ||
                                                    !(prefixes[size_index][lane] & (1u << completed)) ||
                                                    (boundary && (!completed || lane != ((4 - completed) & 3))) ||
                                                    (!boundary && completed && fault != 1) ||
                                                    (alternate && fault != 1) ||
                                                    (!writing && fault == 0 && fc != 1 && fc != 2))
                                                    continue;
                                                if (ccr &&
                                                    (form || source || lane || completed || boundary || fault != 1))
                                                    continue;
                                                if (live_trace >= 0 &&
                                                    (!writing || page8k || form > 2 || source == 1 || source == 3 ||
                                                     size != 4 || ccr || (completed != 0 && completed != 2) ||
                                                     lane != (completed ? 2u : 0u)))
                                                    continue;
                                                unsigned bytes = page8k ? 8192 : 4096;
                                                unsigned base = form == 6 ? 0x7000 : alternate ? 0x8040 : 0x28040;
                                                unsigned operand =
                                                    boundary ? (form == 6 ? 0x8000 : (base & ~(bytes - 1)) + bytes) -
                                                                   completed
                                                             : base + lane;
                                                unsigned reg = sources[source];
                                                unsigned next = moves_setup(page8k, fc, size, form, reg, operand,
                                                                            trace << 14 | ccr, writing);
                                                M68kReg original[16], expected[16];
                                                memcpy(original, cpu_state.regs, sizeof original);
                                                memcpy(expected, original, sizeof expected);
                                                unsigned updated = EM_A0 + (form == 1 ? size : form == 2 ? -size : 0);
                                                expected[8].ul.n = updated;
                                                unsigned mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1;
                                                unsigned data = (reg == 8 ? updated : original[reg].ul.n) & mask;
                                                if (!writing) {
                                                    data = 0x80fedcba & mask;
                                                    expected[reg].ul.n = reg >= 8 ? (size == 1   ? (uint32)(int8)data
                                                                                     : size == 2 ? (uint32)(int16)data
                                                                                                 : data)
                                                                                  : (expected[reg].ul.n & ~mask) | data;
                                                }
                                                unsigned initial[4];
                                                for (unsigned i = 0; i < size; ++i)
                                                    initial[i] = ram[moves_physical(operand + i)];
                                                unsigned data_root = moves_fc & 4 ? 0x6000 : 0x3000;
                                                unsigned fault_pte =
                                                    data_root + ((operand + completed) / page_size) * 4;
                                                unsigned descriptor =
                                                    (operand + completed < moves_page + page_size ? 0x10000 : 0x14000) |
                                                    1;
                                                if (fault == 1) {
                                                    memory.aligned_transfers = 1;
                                                    if (writing)
                                                        fail_write_physical = moves_physical(operand + completed);
                                                    else
                                                        fail_physical = moves_physical(operand + completed);
                                                }
                                                else
                                                    put32(fault_pte,
                                                          fault == 2 ? 0 : descriptor | (writing ? 4 : 0x80));
                                                unsigned expected_sr = 0x2000 | trace << 14 | ccr;
                                                if (live_trace >= 0) {
                                                    trace_read_physical =
                                                        fault == 1 ? fail_write_physical : physical(0x10000);
                                                    trace_bus_kind = fault == 1;
                                                    trace_read_sr = (unsigned)live_trace << 14;
                                                    trace_read_ccr = ccr ^ 31;
                                                    expected_sr = 0x2000 | trace_read_sr | (ccr ^ 31);
                                                }
                                                Syn68kMappedCPU cpu = {
                                                    .pc = 0x10000, .memory = &memory, .native = native_execution};
                                                Syn68kMappedRun run;
                                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                int valid =
                                                    run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                                    run.step.format == 7 &&
                                                    run.step.store_state == (writing ? (trace ? 3u : 2u) : 0u) &&
                                                    run.step.restartable_read == !writing &&
                                                    cpu.pc == (writing ? next : 0x10000);
                                                if (!valid)
                                                    fprintf(
                                                        stderr,
                                                        "MOVES fault w=%u fc=%u size=%u form=%u source=%u trace=%u fault=%u boundary=%u lane=%u prefix=%u live=%d: reason=%u vector=%u store=%u read=%u PC=%x\n",
                                                        writing, fc, size, form, reg, trace, fault, boundary, lane,
                                                        completed, live_trace, run.reason, run.step.vector,
                                                        run.step.store_state, run.step.restartable_read, cpu.pc);
                                                assert(valid);
                                                /* A failed read has not committed its destination or EA update. */
                                                M68kReg store_regs[16];
                                                memcpy(store_regs, original, sizeof store_regs);
                                                store_regs[8].ul.n = updated;
                                                assert(!memcmp(cpu_state.regs, writing ? store_regs : original,
                                                               sizeof original) &&
                                                       status() == expected_sr);
                                                assert(run.step.access.request == operand &&
                                                       run.step.access.logical == operand + completed &&
                                                       run.step.access.completed == completed &&
                                                       run.step.access.size == size && run.step.access.fc == moves_fc &&
                                                       run.step.access.bus_error == (fault == 1));
                                                assert(watched_tail_reads == (form == 5) && !watched_tail_successes &&
                                                       (writing ? !watched_read_attempts : !watched_attempts));
                                                if (watched_attempts || watched_read_attempts)
                                                    assert((writing ? watched_write_fc : watched_read_fc) == moves_fc);
                                                for (unsigned i = 0; i < size; ++i)
                                                    assert(ram[moves_physical(operand + i)] ==
                                                           (writing && i < completed
                                                                ? (data >> ((size - i - 1) * 8)) & 255
                                                                : initial[i]));
                                                if (writing)
                                                    assert(run.step.access.value == data);
                                                fail_physical = fail_write_physical = UINT32_MAX;
                                                supervisor = 1;
                                                put32(pte(0x10000 + page_size), 0xe001);
                                                put32(pte(0x20000), 0x18001);
                                                unsigned handler = 0x10000 + page_size + 0x40, at = handler,
                                                         instructions = 0;
                                                if (fault != 1) {
                                                    unsigned table_page = data_root & ~(page_size - 1);
                                                    put32(pte(0x30000), table_page | 1);
                                                    unsigned target = 0x30000 + fault_pte - table_page;
                                                    const unsigned repair[] = {
                                                        0x23fc,       descriptor >> 16, descriptor & 65535,
                                                        target >> 16, target & 65535,   0xf518};
                                                    for (unsigned i = 0; i < sizeof repair / sizeof *repair;
                                                         ++i, at += 2)
                                                        code_word(at, repair[i]);
                                                    instructions += 2;
                                                }
                                                unsigned wb_address = fault == 1 ? 40 : 32;
                                                unsigned rotate = fault == 1 && size > 1;
                                                unsigned wb_data =
                                                    fault == 1 ? size == 1 ? 44 + (operand & 3) : 44 : 36 + 4 - size;
                                                if (writing) {
                                                    const unsigned load[] = {
                                                        0x4e7a,
                                                        0x5001,
                                                        0x246f,
                                                        wb_address,
                                                        rotate ? 0x242f : size == 1 ? 0x142f : size == 2 ? 0x342f : 0x242f,
                                                        wb_data,
                                                        0x362f,
                                                        12,
                                                        0x0283,
                                                        0,
                                                        7,
                                                        0x4e7b,
                                                        0x3001};
                                                    for (unsigned i = 0; i < sizeof load / sizeof *load; ++i, at += 2)
                                                        code_word(at, load[i]);
                                                    instructions += 6;
                                                    if (rotate) {
                                                        const unsigned rotation[] = {0x260a, 0x0283, 0,
                                                                                     3,      0xe78b, 0xe7ba};
                                                        for (unsigned i = 0; i < sizeof rotation / sizeof *rotation;
                                                             ++i, at += 2)
                                                            code_word(at, rotation[i]);
                                                        instructions += 4;
                                                        if (size == 2) {
                                                            code_word(at, 0x4842);
                                                            at += 2;
                                                            ++instructions;
                                                        }
                                                    }
                                                    const unsigned drain[] = {size == 1   ? 0x0e12
                                                                              : size == 2 ? 0x0e52
                                                                                          : 0x0e92,
                                                                              0x2800, 0x4e7b, 0x5001, 0x5284};
                                                    for (unsigned i = 0; i < sizeof drain / sizeof *drain; ++i, at += 2)
                                                        code_word(at, drain[i]);
                                                    instructions += 3;
                                                }
                                                code_word(at, 0x4e73);
                                                ++instructions;
                                                cpu_state.vbr = 0x20000;
                                                put32(physical(cpu_state.vbr + 8), handler);
                                                Syn68kMappedDelivery delivery;
                                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                unsigned frame = 0x20100 - 60, size_bits = size == 1   ? 0x20
                                                                                           : size == 2 ? 0x40
                                                                                                       : 0;
                                                unsigned ssw = transfers[fc] | size_bits | (writing ? 0 : 0x100) |
                                                               (fault == 1 ? 0 : 0x400) |
                                                               (fault != 1 && completed ? 0x800 : 0) |
                                                               (writing && trace ? 0x2000 : 0);
                                                assert(mapped_value(frame, 2) == expected_sr &&
                                                       mapped_value(frame + 2, 4) == (writing ? next : 0x10000) &&
                                                       mapped_value(frame + 12, 2) == ssw &&
                                                       mapped_value(frame + 20, 4) == operand);
                                                assert(mapped_value(frame + 8, 4) == (writing && trace ? 0x10000 : 0));
                                                assert(!mapped_value(frame + 14, 2) &&
                                                       mapped_value(frame + (fault == 1 ? 18 : 16), 2) ==
                                                           (writing ? 0x80 | size_bits | transfers[fc] : 0) &&
                                                       !mapped_value(frame + (fault == 1 ? 16 : 18), 2));
                                                if (writing) {
                                                    assert(mapped_value(frame + wb_address, 4) == operand);
                                                    unsigned captured = mapped_value(frame + (fault == 1 ? 44 : 36), 4);
                                                    if (fault == 1) {
                                                        unsigned rotation = (operand & 3) * 8;
                                                        if (rotation)
                                                            captured =
                                                                captured << rotation | captured >> (32 - rotation);
                                                        captured >>= (4 - size) * 8;
                                                    }
                                                    assert(captured == data);
                                                }
                                                assert(syn68k_mapped_cpu_run(&cpu, instructions, &run));
                                                assert(run.reason == (writing && trace ? SYN68K_MAPPED_EXCEPTION
                                                                                       : SYN68K_MAPPED_BUDGET) &&
                                                       run.completed == instructions - !!(writing && trace) &&
                                                       cpu.pc == (writing ? next : 0x10000) &&
                                                       status() == expected_sr &&
                                                       EM_A0 == (writing ? updated : original[8].ul.n) &&
                                                       cpu_state.dfc == fc && cpu_state.sfc == fc);
                                                if (!writing) {
                                                    assert(watched_tail_reads == (form == 5));
                                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                    assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION
                                                                                : SYN68K_MAPPED_BUDGET) &&
                                                           run.step.vector == (trace ? 9u : 0u) && cpu.pc == next &&
                                                           status() == expected_sr &&
                                                           !memcmp(expected, cpu_state.regs, sizeof expected));
                                                }
                                                else if (trace) {
                                                    unsigned trace_handler = 0x10000 + page_size + 0x180;
                                                    put32(physical(cpu_state.vbr + 36), trace_handler);
                                                    code_word(trace_handler, 0x4e73);
                                                    assert(run.step.vector == 9 && run.step.format == 2 &&
                                                           run.step.fault_address == 0x10000);
                                                    assert(
                                                        syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next &&
                                                           status() == expected_sr);
                                                }
                                                assert(moves_value(operand, size) == data &&
                                                       watched_tail_reads == (form == 5 ? (writing ? 1u : 2u) : 0u));
                                                if (writing)
                                                    assert(EM_A0 == updated && EM_A1 == original[9].ul.n &&
                                                           EM_D0 == original[0].ul.n && EM_A7 == 0x20100 &&
                                                           EM_D4 == 1 && watched_read_attempts == 0 &&
                                                           watched_attempts == 1u + !!(fault == 1 || completed) &&
                                                           watched_successes == 1 + !!completed);
                                                else
                                                    assert(!watched_attempts &&
                                                           watched_read_attempts == 1u + !!(fault == 1 || completed));
                                                ++cases;
                                            }
    memory.aligned_transfers = 0;
    fail_physical = fail_write_physical = watched_physical = watched_tail_physical = trace_read_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static void moves_rejection_guards(void)
{
    for (unsigned privileged = 0; privileged < 2; ++privileged)
        for (unsigned trace = 0; trace < 4; ++trace)
            for (unsigned bad = 0; bad < 7; ++bad) {
                if (privileged && bad == 6)
                    continue;
                unsigned next = moves_setup(0, 5, 4, 1, 0, 0x28040, trace << 14 | 0x15, 1);
                const unsigned illegal[] = {0x0e80, 0x0e88, 0x0eba, 0x0ebb, 0x0ebc};
                if (bad < 5)
                    code_word(0x10000, illegal[bad]);
                else if (bad == 5)
                    code_word(0x10002, 0x0801); // Reserved MOVES extension bit
                if (!privileged) {
                    supervisor = 0;
                    put32(pte(0x10000), 0x8001);
                    supervisor = 1;
                    memcpy(ram + 0x8000, ram + 0xc000, next - 0x10000);
                    set_status(trace << 14 | 0x15);
                    EM_A7 = cpu_state.usp;
                }
                M68kReg before[16];
                memcpy(before, cpu_state.regs, sizeof before);
                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                unsigned vector = bad < 5 || privileged ? 4 : 8;
                int valid = run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == vector && !run.step.format &&
                            !run.step.store_state && !run.step.restartable_read && cpu.pc == 0x10000 &&
                            !memcmp(before, cpu_state.regs, sizeof before) && !watched_attempts &&
                            !watched_read_attempts && !watched_tail_reads && !writes &&
                            reads == (privileged && bad == 5 ? 2u : 1u);
                if (!valid)
                    fprintf(
                        stderr,
                        "MOVES rejection native=%u privileged=%u trace=%u bad=%u: reason=%u vector=%u/%u format=%u store=%u read=%u PC=%x reads=%u writes=%u regs=%d watches=%u/%u/%u\n",
                        native_execution, privileged, trace, bad, run.reason, run.step.vector, vector, run.step.format,
                        run.step.store_state, run.step.restartable_read, cpu.pc, reads, writes,
                        memcmp(before, cpu_state.regs, sizeof before), watched_attempts, watched_read_attempts,
                        watched_tail_reads);
                assert(valid);
                ++cases;
            }
    for (unsigned writing = 0; writing < 2; ++writing)
        for (unsigned bad = 0; bad < (writing ? 10u : 8u); ++bad) {
            fixture(0, 1);
            Syn68kIntegerStep record = {.vector = 2,
                                        .format = 7,
                                        .exception_pc = 0x10004,
                                        .store_state = writing ? 2 : 0,
                                        .store_next = 0x10004,
                                        .restartable_read = !writing,
                                        .access = {.translation = {.fault = MMU040_OK},
                                                   .bus_error = 1,
                                                   .request = 0x28040,
                                                   .logical = 0x28040,
                                                   .size = 4,
                                                   .fc = 0,
                                                   .writing = writing}};
            switch (bad) {
            case 0:
                record.access.fc = 8;
                break;
            case 1:
                record.access.translation.fault = MMU040_INVALID;
                record.access.bus_error = 0;
                break;
            case 2:
                record.access.rmw = 1;
                record.store_locked = writing;
                break;
            case 3:
                record.access.completed = 4;
                break;
            case 4:
                if (writing)
                    ++record.access.logical;
                else
                    record.access.size = 0;
                break;
            case 5:
                record.access.size = 3;
                break;
            case 6:
                if (writing)
                    ++record.store_next;
                else
                    ++record.exception_pc;
                break;
            case 7:
                if (writing) {
                    record.store_tail_valid = 1;
                    record.store_tail_fc = 5;
                }
                else
                    record.restartable_read = 0;
                break;
            case 8:
                record.access.fc = 2;
                break;
            case 9:
                record.access.fc = 6;
                break;
            }
            CPUState before = cpu_state;
            unsigned old_reads = reads, old_writes = writes;
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                   !memcmp(&before, &cpu_state, sizeof before));
            ++cases;
        }
}

static void moves_streams(void)
{
    moves_normal(-1);
    moves_faults(0, -1);
    moves_faults(1, -1);
    moves_rejection_guards();
    for (int live = 0; live < 4; ++live) {
        moves_normal(live);
        moves_faults(1, live);
    }
}

static void guest_partial_read_restarts(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 0; kind < 4; ++kind)
                for (unsigned predecrement = 0; predecrement < 2; ++predecrement)
                    for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            fixture(page8k, privileged);
                            unsigned size = kind ? 4 : 2, prefix = kind ? kind : 1;
                            unsigned operand = 0x20000 + page_size - prefix;
                            unsigned root = privileged ? 0x6000 : 0x3000;
                            unsigned fault_pte = pte(operand + prefix);
                            unsigned suffix = physical(operand + prefix);
                            for (unsigned i = 0; i < size; ++i)
                                ram[physical(operand + i)] = 0x11 * (i + 1);
                            code_word(0x10000, (size == 2 ? 0x3018 : 0x2018) + predecrement * 8);
                            EM_A0 = operand + predecrement * size;
                            unsigned original_a0 = EM_A0;
                            EM_D0 = 0xAABBCCDD;
                            cpu_state.isp = 0x20100;
                            EM_A7 = privileged ? cpu_state.isp : 0x76543210;
                            set_status((privileged ? 0x2000 : 0) | ccr);
                            if (physical_fault)
                                fail_physical = suffix;
                            else
                                put32(fault_pte, 0);
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                   run.step.restartable_read && run.step.access.completed == prefix);
                            assert(EM_A0 == original_a0 && EM_D0 == 0xAABBCCDD &&
                                   status() == ((privileged ? 0x2000 : 0) | ccr));
                            fail_physical = 0xFFFFFFFF;
                            supervisor = 1;
                            put32(pte(0x10000 + page_size), 0xE001);
                            put32(pte(0x20000), 0x18001);
                            unsigned table_page = root & ~(page_size - 1);
                            put32(pte(0x30000), table_page | 1);
                            unsigned target = 0x30000 + fault_pte - table_page;
                            unsigned descriptor = suffix | 1;
                            const unsigned program[] = {0x23FC,       descriptor >> 16, descriptor & 65535,
                                                        target >> 16, target & 65535,   0xF518,
                                                        0x4E73};
                            unsigned handler = 0x10000 + page_size + 0x40;
                            for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                code_word(handler + i * 2, program[i]);
                            cpu_state.vbr = 0x20000;
                            put32(physical(cpu_state.vbr + 8), handler);
                            Syn68kMappedDelivery delivery;
                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                            unsigned frame = 0x20100 - 60;
                            assert(mapped_value(frame + 20, 4) == operand);
                            assert(mapped_value(frame + 12, 2) ==
                                   ((physical_fault ? 0x100 : 0xD00) | (size == 2 ? 0x40 : 0) | (privileged ? 5 : 1)));
                            assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_A0 == original_a0);
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10002 &&
                                   EM_D0 == (size == 2 ? 0xAABB1122 : 0x11223344) &&
                                   EM_A0 == (predecrement ? operand : operand + size));
                            ++cases;
                        }
}

static void guest_movem_read_continuations(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 0; kind < 5; ++kind)
                for (unsigned size = 2; size <= 4; size += 2)
                    for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            fixture(page8k, privileged);
                            unsigned pc = kind == 3 ? 0x10080 : kind == 4 ? 0x10000 + page_size - 8 : 0x10000;
                            unsigned operand = (kind == 3 ? 0x10000 : 0x20000) + page_size - size;
                            unsigned root = privileged ? 0x6000 : 0x3000;
                            unsigned fault_pte = pte(operand + size);
                            unsigned suffix = physical(operand + size);
                            unsigned first = size == 2 ? 0xBEEF : 0xDEADBEEF;
                            for (unsigned i = 0; i < size; ++i)
                                ram[physical(operand + i)] = first >> ((size - 1 - i) * 8);
                            put32(suffix, 0xCAFEBABE);
                            unsigned op = (size == 2 ? 0x4CB0 : 0x4CF0) + (kind == 2 || kind == 4 ? 11
                                                                           : kind == 3            ? 10
                                                                                                  : 0);
                            code_word(pc, op);
                            code_word(pc + 2, 0x0300); /* A0 and A1 */
                            unsigned length = kind == 2 || kind == 4 ? 10 : 6;
                            EM_D0 = 16;
                            EM_A0 = kind == 1 ? 0x20040 : operand - 32;
                            EM_A1 = 0x12345678;
                            unsigned pointer_physical = physical(0x20040);
                            if (kind == 0)
                                code_word(pc + 4, 0x0810); /* Brief A0+D0.L+16 */
                            else if (kind == 1) {
                                code_word(pc + 4, 0x0915); /* ([A0],D0.L) */
                                put32(pointer_physical, operand - 16);
                            }
                            else if (kind == 2 || kind == 4) {
                                unsigned bd = operand - (pc + 4) - 16;
                                code_word(pc + 4, 0x0930); /* Full PC+D0.L+bd.L */
                                code_word(pc + 6, bd >> 16);
                                code_word(pc + 8, bd);
                            }
                            else
                                code_word(pc + 4, operand - (pc + 4));
                            unsigned displacement_physical = physical(pc + 4);
                            unsigned opcode_physical = physical(pc);
                            unsigned fetch_requests[] = {pc + 2, pc + (kind == 2 || kind == 4 ? 6 : 4)};
                            unsigned fetch_sizes[] = {2, kind == 2 || kind == 4 ? 4 : 2};
                            unsigned fetch_faults[] = {physical(pc + 2), physical(pc + (kind == 4   ? 8
                                                                                        : kind == 2 ? 6
                                                                                                    : 4))};
                            cpu_state.isp = 0x20100;
                            EM_A7 = privileged ? cpu_state.isp : 0x76543210;
                            set_status((privileged ? 0x2000 : 0) | ccr);
                            if (physical_fault)
                                fail_physical = suffix;
                            else
                                put32(fault_pte, 0);
                            watched_physical = physical(operand);
                            watched_tail_physical = suffix;
                            watched_tail_reads = 0;
                            Syn68kMappedCPU cpu = {.pc = pc, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            Syn68kMappedDelivery delivery;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.movem_state == 1 &&
                                   run.step.movem_ea == operand && EM_A1 == 0x12345678 &&
                                   EM_A0 == (size == 2 ? 0xFFFFBEEF : 0xDEADBEEF) && watched_read_attempts == 1 &&
                                   watched_tail_reads == physical_fault);
                            /* Make recalculation observably wrong. The loaded A0 and
                   changed D0 cannot recover the original brief/full address;
                   the indirect pointer is also unreadable during restart. */
                            fail_physical = kind == 1 ? pointer_physical : 0xFFFFFFFF;
                            if (kind == 3)
                                ram[displacement_physical] = 0x7F, ram[displacement_physical + 1] = 0xFC;
                            supervisor = 1;
                            put32(pte(0x10000), 0xC001);
                            put32(pte(0x20000), 0x18001);
                            unsigned table_page = root & ~(page_size - 1);
                            put32(pte(0x30000), table_page | 1);
                            unsigned target = 0x30000 + fault_pte - table_page;
                            unsigned descriptor = suffix | 1;
                            const unsigned program[] = {
                                0x23FC,
                                descriptor >> 16,
                                descriptor & 65535,
                                target >> 16,
                                target & 65535,
                                0xF518,
                                0x7063,
                                0x4E73 /* Change D0 before returning to MOVEM. */
                            };
                            unsigned handler = 0x10040;
                            for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                code_word(handler + i * 2, program[i]);
                            cpu_state.vbr = 0x20000;
                            put32(physical(cpu_state.vbr + 8), handler);
                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                            unsigned frame = 0x20100 - 60;
                            assert(mapped_value(frame + 8, 4) == operand &&
                                   (mapped_value(frame + 12, 2) & 0xF000) == 0x1000);
                            assert(syn68k_mapped_cpu_run(&cpu, 4, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc && cpu.movem_pending &&
                                   cpu.movem_ea == operand && EM_D0 == 99);
                            if (!ccr && !physical_fault) {
                                for (unsigned fetch = 0; fetch < (length > 4 ? 2U : 1U); ++fetch) {
                                    fail_physical = fetch_faults[fetch];
                                    CPUState before = cpu_state;
                                    Syn68kIntegerStep component;
                                    assert(syn68k_integer_resume_movem(pc, op, &memory, operand, &component));
                                    assert(component.vector == 2 && component.movem_state == 1 &&
                                           component.movem_ea == operand && !component.restartable_read &&
                                           !memcmp(&before, &cpu_state, sizeof before));
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    unsigned completed = kind == 4 && fetch ? 2 : 0;
                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                           run.step.movem_state == 1 && run.step.movem_ea == operand &&
                                           run.step.access.request == fetch_requests[fetch] &&
                                           run.step.access.size == fetch_sizes[fetch] &&
                                           run.step.access.completed == completed &&
                                           run.step.access.logical == fetch_requests[fetch] + completed &&
                                           run.step.access.fc == (privileged ? 6U : 2U) &&
                                           component.access.request == run.step.access.request &&
                                           component.access.size == run.step.access.size &&
                                           component.access.completed == completed && watched_read_attempts == 1);
                                    fail_physical = kind == 1 ? pointer_physical : UINT32_MAX;
                                    /* Component records have no stream read certificate.
                                       Exercise their independent CM validation too. */
                                    const Syn68kIntegerStep* record = privileged ? &run.step : &component;
                                    assert(syn68k_mapped_exception_enter(&cpu, record, 0, &delivery));
                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                    assert(syn68k_mapped_cpu_run(&cpu, 4, &run));
                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.movem_pending && cpu.pc == pc &&
                                           cpu.movem_ea == operand && watched_read_attempts == 1);
                                    ++cases;
                                }
                                fail_physical = opcode_physical;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && cpu.movem_pending && cpu.pc == pc &&
                                       run.step.movem_state == 1 && run.step.movem_ea == operand &&
                                       run.step.access.request == pc && run.step.access.size == 2 &&
                                       run.step.access.fc == (privileged ? 6U : 2U));
                                fail_physical = kind == 1 ? pointer_physical : 0xFFFFFFFF;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && !cpu.movem_pending &&
                                       cpu.pc == handler);
                                /* The nested handler must execute normally instead of
                                   being mistaken for a resumed MOVEM instruction. */
                                assert(syn68k_mapped_cpu_run(&cpu, 4, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc && cpu.movem_pending &&
                                       cpu.movem_ea == operand);
                                ++cases;
                                ram[opcode_physical] = 0x4E;
                                ram[opcode_physical + 1] = 0x71;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_UNSUPPORTED && cpu.movem_pending && cpu.pc == pc);
                                ++cases;
                                ram[opcode_physical] = op >> 8;
                                ram[opcode_physical + 1] = op;
                            }
                            cpu_state.interrupt_pending[3] = 1;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc + length && !cpu.movem_pending &&
                                   EM_A0 == (size == 2 ? 0xFFFFBEEF : 0xDEADBEEF) &&
                                   EM_A1 == (size == 2 ? 0xFFFFCAFE : 0xCAFEBABE) && EM_D0 == 99 &&
                                   status() == ((privileged ? 0x2000 : 0) | ccr) && watched_read_attempts == 2 &&
                                   watched_tail_reads == physical_fault + 1);
                            unsigned previous_reads = reads;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_INTERRUPT && run.interrupt_level == 3 &&
                                   reads == previous_reads);
                            cpu_state.interrupt_pending[3] = 0;
                            ++cases;
                        }
    watched_physical = watched_tail_physical = UINT32_MAX;
}

static void movem_fetch_record_guards(void)
{
    for (unsigned invalid = 0; invalid < 12; ++invalid) {
        fixture(0, 1);
        syn68k_fpu_reset();
        cpu_state.isp = EM_A7 = 0x20100;
        Syn68kIntegerStep record = {
            .vector = 2,
            .format = 7,
            .exception_pc = 0x10000,
            .restartable_read = 1,
            .movem_state = 1,
            .movem_ea = 0x28000,
            .access = {.translation = {.fault = MMU040_INVALID}, .request = 0x10002, .size = 2, .fc = 6}};
        switch (invalid) {
        case 0:
            record.access.request = 0x10001;
            break;
        case 1:
            record.access.request = 0x1000e;
            break;
        case 2:
            record.access.request = 0x0fffe;
            break;
        case 3:
            record.access.size = 1;
            break;
        case 4:
            record.access.request = 0x1000c;
            record.access.size = 4;
            break;
        case 5:
            record.access.fc = 7;
            break;
        case 6:
            record.access.writing = 1;
            break;
        case 7:
            record.movem_state = 2;
            break;
        case 8:
            record.access.rmw = 1;
            break;
        case 9:
            record.store_state = 1;
            break;
        case 10:
            record.format = 0;
            break;
        case 11:
            record.exception_pc |= 1;
            break;
        }
        record.access.logical = record.access.request;
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    syn68k_fpu_reset();
}

static void guest_fpu_read_restarts(void)
{
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    const unsigned sizes[] = {4, 4, 12, 2, 8, 1};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f)
                for (unsigned part = 0; part < (sizes[f] + 3) / 4; ++part)
                    for (unsigned mode = 2; mode <= 4; ++mode)
                        for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                            for (unsigned null = 0; null < 2; ++null) {
                                fixture(page8k, privileged);
                                syn68k_fpu_reset();
                                cpu_state.fpu.null_state = null;
                                unsigned pc = 0x10000;
                                unsigned fault = 0x28000 + page_size;
                                unsigned source = fault - part * 4;
                                unsigned fault_pte = pte(fault);
                                put32(pte(0x28000), 0x10001);
                                put32(fault_pte, 0x14001);
                                const unsigned char extended[] = {0x40, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0};
                                const unsigned char real[] = {0x40, 0, 0, 0, 0, 0, 0, 0};
                                for (unsigned i = 0; i < sizes[f]; ++i) {
                                    unsigned at = source + i;
                                    unsigned data = at < fault ? 0x10000 + at - 0x28000 : 0x14000 + at - fault;
                                    ram[data] = formats[f] == 2                      ? extended[i]
                                                : formats[f] == 1 || formats[f] == 5 ? real[i]
                                                : i == sizes[f] - 1                  ? 2
                                                                                     : 0;
                                }
                                code_word(pc, 0xf200 | mode << 3);
                                code_word(pc + 2, 0x4000 | formats[f] << 10); // FMOVE source,FP0
                                unsigned original_a0 = source + (mode == 4 ? sizes[f] : 0);
                                EM_A0 = original_a0;
                                unsigned root = privileged ? 0x6000 : 0x3000;
                                supervisor = 1;
                                unsigned handler = 0x10000 + page_size + 0x40;
                                put32(pte(handler), 0xe001);
                                put32(pte(0x20000), 0x18001);
                                unsigned table_page = root & ~(page_size - 1);
                                put32(pte(0x30000), table_page | 1);
                                unsigned target = 0x30000 + fault_pte - table_page;
                                const unsigned program[] = {0x23fc,         1,      0x4001, target >> 16,
                                                            target & 65535, 0xf518, 0x4e73};
                                for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                    code_word(handler + i * 2, program[i]);
                                cpu_state.vbr = 0x20000;
                                put32(physical(cpu_state.vbr + 8), handler);
                                cpu_state.isp = 0x20100;
                                EM_A7 = privileged ? cpu_state.isp : 0x76543210;
                                set_status((privileged ? 0x2000 : 0) | 0x15);
                                if (physical_fault)
                                    fail_physical = 0x14000;
                                else
                                    put32(fault_pte, 0);
                                Syn68kFPUState expected = cpu_state.fpu;
                                expected.null_state = 0;
                                Syn68kMappedCPU cpu = {.pc = pc, .memory = &memory, .native = native_execution};
                                Syn68kMappedRun run;
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.restartable_read && !run.completed && EM_A0 == original_a0);
                                unsigned scalar_size = sizes[f] < 4 ? sizes[f] : 4;
                                unsigned fc = privileged ? 5 : 1;
                                assert(run.step.access.request == fault && run.step.access.size == scalar_size &&
                                       run.step.access.fc == fc && !run.step.access.writing &&
                                       !run.step.access.completed);
                                assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                                fail_physical = UINT32_MAX;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                unsigned frame = 0x20100 - 60;
                                unsigned size_bits = scalar_size == 1 ? 0x20 : scalar_size == 2 ? 0x40 : 0;
                                assert(mapped_value(frame + 12, 2) ==
                                       ((physical_fault ? 0x100 : 0x500) | size_bits | fc));
                                assert(mapped_value(frame + 20, 4) == fault);
                                for (unsigned i = 14; i < 20; i += 2)
                                    assert(!mapped_value(frame + i, 2));
                                assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc &&
                                       status() == ((privileged ? 0x2000 : 0) | 0x15));
                                assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == pc + 4 &&
                                       cpu_state.fpu.regs[0].exponent == 0x4000 &&
                                       cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
                                assert(EM_A0 == (mode == 3 ? source + sizes[f] : source));
                                ++cases;
                            }
}

/* Component stepping independently supplies the unrolled-back fault state.
   The runner may restore owned EA updates only when callbacks preserve An. */
static void callback_read_setup(unsigned page8k, unsigned privileged, unsigned fpu, unsigned mode, unsigned null,
                                unsigned kind, unsigned reg, unsigned mutate)
{
    fixture(page8k, privileged);
    syn68k_fpu_reset();
    cpu_state.fpu.null_state = null;
    for (unsigned i = 0; i < 16; ++i)
        cpu_state.regs[i].ul.n = 0x12345600 + i * 4;
    cpu_state.usp = 0x34560000;
    cpu_state.isp = 0x20100;
    cpu_state.msp = 0x201c0;
    EM_A7 = privileged ? cpu_state.isp : cpu_state.usp;
    EM_A0 = 0x28000 + (mode == 4 ? 4 : 0);
    set_status((privileged ? 0x2000 : 0) | 0x15);
    code_word(0x10000, (fpu ? 0xf200 : 0x2000) | mode << 3);
    if (fpu)
        code_word(0x10002, 0x4400); // FMOVE.S <ea>,FP0
    if (mode == 6) {
        code_word(fpu ? 0x10004 : 0x10002, 0x0151); // ([A0]), null BD/OD, suppressed index
        EM_A0 = 0x24080;
        put32(pte(0x24000), 0x1c001);
        put32(0x1c080, 0x28000);
    }
    put32(pte(0x28000), 0x10001);
    fail_physical = 0x10000;
    read_callback_kind = kind >= 6 ? 1 : kind >= 4 ? kind - 2 : kind;
    read_callback_address = kind == 6   ? 0x1c080
                            : kind == 7 ? physical(0x10000)
                            : kind == 8 ? physical(0x10002)
                            : kind == 1 ? fail_physical
                                        : pte(0x28000);
    read_callback_register = reg;
    read_callback_mask = !mutate ? 0 : reg == 11 ? 1 : 0x10;
    read_callback_fail = kind == 4 || kind == 5;
}

static void read_callback_guards(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned fpu = 0; fpu < 2; ++fpu)
                for (unsigned mode = 2; mode <= 6; ++mode)
                    for (unsigned null = 0; null <= fpu; ++null)
                        for (unsigned kind = 1; kind <= 8; ++kind)
                            for (unsigned reg = 0; reg < 12; ++reg)
                                for (unsigned mutate = 0; mutate < 2; ++mutate) {
                                    if (mode == 5 || (kind == 6 && mode != 6) || (kind == 8 && !fpu && mode != 6) ||
                                        (kind >= 7 && !reg))
                                        continue;
                                    callback_read_setup(page8k, privileged, fpu, mode, null, kind, reg, mutate);
                                    if (fpu) {
                                        Syn68kFPUStep fp;
                                        assert(syn68k_fpu_step(0x10000, &memory, &fp));
                                        assert(fp.vector == 2 && fp.format == 7);
                                    }
                                    else {
                                        Syn68kIntegerStep integer;
                                        assert(syn68k_integer_step(0x10000, &memory, &integer));
                                        assert(integer.vector == 2 && integer.format == 7);
                                    }
                                    assert(read_callback_hits == 1 && read_callback_address == UINT32_MAX);
                                    CPUState expected = cpu_state;
                                    callback_read_setup(page8k, privileged, fpu, mode, null, kind, reg, mutate);
                                    M68kReg original[16];
                                    memcpy(original, cpu_state.regs, sizeof original);
                                    Syn68kMappedCPU cpu = {
                                        .pc = 0x10000, .memory = &memory, .native = native_execution};
                                    Syn68kMappedRun run;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(read_callback_hits == 1 && read_callback_address == UINT32_MAX);
                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                           run.step.format == 7 && run.step.restartable_read == !mutate);
                                    assert(!memcmp(cpu_state.regs, mutate ? expected.regs : original, sizeof original));
                                    assert(!memcmp(&cpu_state.fpu, &expected.fpu, sizeof expected.fpu) &&
                                           cpu_state.usp == expected.usp && cpu_state.isp == expected.isp &&
                                           cpu_state.msp == expected.msp &&
                                           status() == ((privileged ? 0x2000 : 0) | 0x15));
                                    if (mutate) {
                                        CPUState before = cpu_state;
                                        unsigned old_reads = reads, old_writes = writes;
                                        Syn68kMappedDelivery delivery;
                                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads &&
                                               writes == old_writes && !memcmp(&before, &cpu_state, sizeof before));
                                    }
                                    ++cases;
                                }
    read_callback_address = fail_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static void fpu_read_restart_guards(void)
{
    for (unsigned mutation = 1; mutation <= 6; ++mutation) {
        fixture(0, 1);
        syn68k_fpu_reset();
        cpu_state.fpu.null_state = 0;
        code_word(0x10000, 0xf220);
        code_word(0x10002, 0x4400); // FMOVE.S -(A0),FP0
        put32(pte(0x28000), 0x10001);
        EM_A0 = 0x28004;
        fail_physical = 0x10000;
        fault_fpu_mutation = mutation;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.restartable_read &&
               EM_A0 == 0x28000);
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fault_fpu_mutation = 0;
    fail_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static void rte_fpu_continuations(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned trace = 0; trace < 4; ++trace)
                for (unsigned ccr = 0; ccr < 32; ++ccr)
                    for (unsigned kind = 0; kind < 9; ++kind) {
                        fixture(page8k, 1);
                        syn68k_fpu_reset();
                        unsigned frame = 0x20080, target = 0x10080, ea = 0x12345678;
                        unsigned saved_sr = mode * 0x1000 | trace << 14 | ccr;
                        unsigned vector = kind ? 47 + kind : 11;
                        for (unsigned i = 0; i < 60; i += 4)
                            put32(physical(frame + i), 0);
                        code_word(frame, saved_sr);
                        put32(physical(frame + 2), target);
                        code_word(frame + 6, 0x7008);
                        put32(physical(frame + 8), ea);
                        code_word(frame + 12, kind ? 0x8000 : 0x4000);
                        code_word(0x10000, 0x4e73);
                        cpu_state.usp = 0xdeadbe00;
                        cpu_state.isp = EM_A7 = frame;
                        cpu_state.msp = 0x201c0;
                        cpu_state.fpu.null_state = 0;
                        cpu_state.fpu.frame_size = 100;
                        cpu_state.fpu.pending_vector = vector;
                        Syn68kFPUState expected = cpu_state.fpu;
                        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.completed && cpu.pc == target &&
                               run.step.vector == vector && run.step.format == (kind ? 3U : 2U) &&
                               run.step.exception_pc == target && run.step.fault_address == ea);
                        assert(status() == saved_sr && EM_A7 == (mode < 2    ? 0xdeadbe00
                                                                 : mode == 2 ? frame + 60
                                                                             : 0x201c0));
                        assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                        cpu_state.vbr = 0x20000;
                        put32(physical(cpu_state.vbr + vector * 4), 0x10040);
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x10040);
                        check_frame(EM_A7, kind ? 3 : 2, saved_sr, &run.step);
                        ++cases;
                    }
    syn68k_fpu_reset();
}

static void rte_fpu_continuation_guards(void)
{
    const unsigned invalid[] = {0, 11, 47, 56, UINT32_MAX};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        fixture(0, 1);
        syn68k_fpu_reset();
        unsigned frame = 0x20080;
        for (unsigned offset = 0; offset < 60; offset += 4)
            put32(physical(frame + offset), 0);
        code_word(frame, 0x801f);
        put32(physical(frame + 2), 0x10080);
        code_word(frame + 6, 0x7008);
        code_word(frame + 12, 0x8000);
        code_word(0x10000, 0x4e73);
        EM_A7 = cpu_state.isp = frame;
        set_status(0x2015);
        cpu_state.fpu.pending_vector = invalid[i];
        Syn68kFPUState expected = cpu_state.fpu;
        unsigned char saved_frame[60];
        memcpy(saved_frame, ram + physical(frame), sizeof saved_frame);
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 14 && !run.step.format &&
               run.step.exception_pc == 0x10000 && EM_A7 == frame && status() == 0x2015);
        assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected) &&
               !memcmp(saved_frame, ram + physical(frame), sizeof saved_frame));
        cpu_state.vbr = 0x20000;
        put32(physical(cpu_state.vbr + 14 * 4), 0x10040);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && EM_A7 == frame - 8);
        check_frame(EM_A7, 0, 0x2015, &run.step);
        assert(!memcmp(saved_frame, ram + physical(frame), sizeof saved_frame));
        ++cases;
    }
    syn68k_fpu_reset();
}

static void rte_header(unsigned frame, unsigned sr, unsigned continuation)
{
    code_word(frame, sr);
    put32(physical(frame + 2), 0x10080);
    code_word(frame + 6, 0x7008);
    put32(physical(frame + 8), 0x12345678);
    code_word(frame + 12, continuation);
    code_word(0x10000, 0x4e73);
    cpu_state.usp = 0xdeadbe00;
    cpu_state.isp = EM_A7 = frame;
    cpu_state.msp = 0x201c0;
    set_status(0x2000);
    syn68k_fpu_reset();
    cpu_state.fpu.pending_vector = continuation == 0x8000 ? 49 : continuation == 0x4000 ? 11 : 0;
}

static void rte_header_only(void)
{
    const unsigned continuations[] = {0, 0x1000, 0x2000, 0x4000, 0x8000};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned trace = 0; trace < 4; ++trace)
                for (unsigned kind = 0; kind < 5; ++kind)
                    for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault) {
                        fixture(page8k, 1);
                        unsigned frame = 0x20000 + page_size - 14;
                        unsigned saved_sr = mode * 0x1000 | trace << 14 | 0x15;
                        rte_header(frame, saved_sr, continuations[kind]);
                        if (physical_fault)
                            fail_physical = physical(0x20000 + page_size);
                        else
                            put32(pte(0x20000 + page_size), 0);
                        Syn68kFPUState expected = cpu_state.fpu;
                        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(cpu.pc == 0x10080 && status() == saved_sr &&
                               EM_A7 == (mode < 2    ? 0xdeadbe00
                                         : mode == 2 ? frame + 60
                                                     : 0x201c0));
                        if (kind >= 2)
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.completed &&
                                   run.step.vector == (kind == 2   ? 9U
                                                       : kind == 3 ? 11U
                                                                   : 49U) &&
                                   run.step.format == (kind == 4 ? 3U : 2U) && run.step.fault_address == 0x12345678);
                        else
                            assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 &&
                                   cpu.movem_pending == (kind == 1) && (!kind || cpu.movem_ea == 0x12345678));
                        assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                        ++cases;
                    }
    const unsigned offsets[] = {0, 2, 6, 8, 12};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned kind = 0; kind < 5; ++kind)
            for (unsigned at = 0; at < sizeof offsets / sizeof *offsets; ++at) {
                fixture(page8k, 1);
                unsigned frame = 0x20000 + page_size - 14;
                rte_header(frame, 0x801f, continuations[kind]);
                Syn68kFPUState expected = cpu_state.fpu;
                fail_physical = physical(frame + offsets[at]);
                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                       run.step.exception_pc == 0x10000 && run.step.access.request == frame + offsets[at] &&
                       EM_A7 == frame && status() == 0x2000 && !cpu.movem_pending);
                assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                ++cases;
            }
    fail_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static void guest_rte_header_restarts(void)
{
    const unsigned offsets[] = {0, 2, 6, 8, 12};
    const unsigned sizes[] = {2, 4, 2, 4, 2};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned at = 0; at < sizeof offsets / sizeof *offsets; ++at)
                for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        fixture(page8k, 1);
                        unsigned fault = 0x20000 + page_size;
                        unsigned frame = fault - offsets[at];
                        unsigned saved_sr = (privileged ? 0x2000 : 0) | ((ccr + 1) & 31);
                        rte_header(frame, saved_sr, 0);
                        set_status(0x2000 | ccr);
                        EM_D0 = 0xdeadc0de;
                        EM_A0 = 0xabcdef00 | ccr;
                        unsigned char original[14];
                        for (unsigned i = 0; i < sizeof original; ++i)
                            original[i] = ram[physical(frame + i)];
                        Syn68kFPUState expected = cpu_state.fpu;
                        unsigned fault_pte = pte(fault), table_page = 0x6000;
                        put32(pte(0x30000), table_page | 1);
                        unsigned target = 0x30000 + fault_pte - table_page;
                        const unsigned program[] = {0x23fc, 1, 0xc001, target >> 16, target & 65535, 0xf518, 0x4e73};
                        for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                            code_word(0x10040 + i * 2, program[i]);
                        cpu_state.vbr = 0x20000;
                        put32(physical(cpu_state.vbr + 8), 0x10040);
                        if (physical_fault)
                            fail_physical = physical(fault);
                        else
                            put32(fault_pte, 0);
                        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                               run.step.restartable_read && EM_A7 == frame && status() == (0x2000 | ccr));
                        assert(run.step.access.request == fault && run.step.access.size == sizes[at] &&
                               run.step.access.fc == 5 && !run.step.access.writing && !run.step.access.completed);
                        assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                        fail_physical = UINT32_MAX;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && EM_A7 == frame - 60);
                        unsigned ssw = (physical_fault ? 0x100 : 0x500) | (sizes[at] == 2 ? 0x40 : 0) | 5;
                        assert(mapped_value(EM_A7 + 12, 2) == ssw && mapped_value(EM_A7 + 20, 4) == fault);
                        assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_A7 == frame &&
                               status() == (0x2000 | ccr));
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10080 && status() == saved_sr &&
                               EM_A7 == (privileged ? frame + 60 : 0xdeadbe00));
                        assert(EM_D0 == 0xdeadc0de && EM_A0 == (0xabcdef00 | ccr) &&
                               !memcmp(&expected, &cpu_state.fpu, sizeof expected));
                        for (unsigned i = 0; i < sizeof original; ++i)
                            assert(original[i] == ram[physical(frame + i)]);
                        ++cases;
                    }
}

static void guest_rte_progress_restarts(void)
{
    const unsigned offsets[] = {0, 2, 6, 8, 12};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned master = 0; master < 2; ++master)
            for (unsigned at = 0; at < sizeof offsets / sizeof *offsets; ++at)
                for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                    for (unsigned ccr = 0; ccr < 32; ++ccr) {
                        fixture(page8k, 1);
                        unsigned fault = 0x20000 + page_size;
                        unsigned next = fault - offsets[at], first = master ? 0x20040 : next - 8;
                        unsigned prefix_sr = (master ? 0x3000 : 0x2000) | ccr;
                        unsigned saved_sr = (prefix_sr & ~31u) | ((ccr + 1) & 31);
                        rte_header(next, saved_sr, 0);
                        set_status(0x2000 | ((ccr + 2) & 31));
                        EM_A7 = cpu_state.isp = first;
                        cpu_state.msp = master ? next : 0x201c0;
                        code_word(first, prefix_sr);
                        put32(physical(first + 2), 0x10080);
                        code_word(first + 6, 0x1000);
                        Syn68kFPUState expected = cpu_state.fpu;
                        unsigned fault_pte = pte(fault);
                        put32(pte(0x30000), 0x6001);
                        unsigned target = 0x30000 + fault_pte - 0x6000;
                        const unsigned program[] = {0x23fc, 1, 0xc001, target >> 16, target & 65535, 0xf518, 0x4e73};
                        for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                            code_word(0x10040 + i * 2, program[i]);
                        cpu_state.vbr = 0x20000;
                        put32(physical(cpu_state.vbr + 8), 0x10040);
                        if (physical_fault)
                            fail_physical = physical(fault);
                        else
                            put32(fault_pte, 0);
                        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        Syn68kMappedDelivery delivery;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                               run.step.restartable_read && EM_A7 == next && status() == prefix_sr);
                        if (master)
                            assert(cpu_state.isp == first + 8);
                        fail_physical = UINT32_MAX;
                        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && EM_A7 == next - 60);
                        assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_A7 == next &&
                               status() == prefix_sr);
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10080 && EM_A7 == next + 60 &&
                               status() == saved_sr && !memcmp(&expected, &cpu_state.fpu, sizeof expected));
                        ++cases;
                    }
    fail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void rte_user_progress_guards(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned master = 0; master < 2; ++master) {
            fixture(page8k, 1);
            syn68k_fpu_reset();
            set_status(0x2015);
            unsigned frame = 0x20080, next = 0x28000;
            EM_A7 = cpu_state.isp = frame;
            cpu_state.usp = next;
            code_word(0x10000, 0x4e73);
            code_word(frame, master ? 0x1015 : 0x0015);
            put32(physical(frame + 2), 0x10080);
            code_word(frame + 6, 0x1000);
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.restartable_read &&
                   EM_A7 == next && status() == (master ? 0x1015 : 0x0015));
            CPUState before = cpu_state;
            unsigned old_reads = reads, old_writes = writes;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                   !memcmp(&before, &cpu_state, sizeof before));
            ++cases;
        }
    fail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void rte_callback_restarts(void)
{
    for (unsigned mutation = 1; mutation <= 6; ++mutation) {
        fixture(0, 1);
        unsigned frame = 0x20080;
        rte_header(frame, 0x2015, 0);
        cpu_state.fpu.null_state = 0;
        Syn68kFPUState before = cpu_state.fpu;
        code_word(0x10040, 0x4e73);
        cpu_state.vbr = 0x20000;
        put32(physical(cpu_state.vbr + 8), 0x10040);
        fail_physical = physical(frame + 12);
        fault_fpu_mutation = mutation;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.restartable_read &&
               EM_A7 == frame && memcmp(&before, &cpu_state.fpu, sizeof before));
        Syn68kFPUState expected = cpu_state.fpu;
        fail_physical = UINT32_MAX;
        fault_fpu_mutation = 0;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && EM_A7 == frame - 60);
        assert(syn68k_mapped_cpu_run(&cpu, 2, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10080 && EM_A7 == frame + 60 && status() == 0x2015 &&
               !memcmp(&expected, &cpu_state.fpu, sizeof expected));
        ++cases;
    }
    fail_physical = UINT32_MAX;
    fault_fpu_mutation = 0;
    set_status(0x2000);
    syn68k_fpu_reset();
}

/* RTE retains its explicit certificate at the live privileged SP. Even A7
   changes must not cause the generic checkpoint to restore earlier registers. */
static void rte_address_callback_restarts(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned reg = 0; reg < 8; ++reg) {
            fixture(page8k, 1);
            unsigned frame = 0x20080, alternate = frame ^ 0x200;
            rte_header(frame, 0x2015, 0);
            code_word(alternate, 0x2015);
            put32(physical(alternate + 2), 0x10080);
            code_word(alternate + 6, 0x7008);
            put32(physical(alternate + 8), 0x12345678);
            code_word(alternate + 12, 0);
            code_word(0x10040, 0x4e73);
            cpu_state.vbr = 0x20000;
            put32(physical(cpu_state.vbr + 8), 0x10040);
            unsigned original = EM_AREG(reg);
            fail_physical = read_callback_address = physical(frame + 12);
            read_callback_kind = 1;
            read_callback_register = reg;
            read_callback_mask = 0x200;
            read_callback_fail = 0;
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            unsigned live_frame = reg == 7 ? alternate : frame;
            assert(read_callback_hits == 1 && read_callback_address == UINT32_MAX &&
                   run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.restartable_read &&
                   EM_AREG(reg) == (original ^ 0x200) && EM_A7 == live_frame);
            fail_physical = UINT32_MAX;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && EM_A7 == live_frame - 60);
            assert(syn68k_mapped_cpu_run(&cpu, 2, &run));
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10080 && EM_A7 == live_frame + 60 &&
                   status() == 0x2015 && (reg == 7 || EM_AREG(reg) == (original ^ 0x200)));
            ++cases;
        }
    fail_physical = read_callback_address = UINT32_MAX;
    syn68k_fpu_reset();
}

static void guest_locked_read_restarts(void)
{
    const unsigned ops[] = {0x4ad0, 0x0ad0, 0x0cd0, 0x0ed0, 0x0cfc, 0x0efc};
    const unsigned sizes[] = {1, 1, 2, 4, 2, 4};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 0; kind < 6; ++kind)
                for (unsigned mode = 2; mode <= 4; ++mode) {
                    if (kind >= 4 && mode != 2)
                        continue;
                    for (unsigned which = 0; which < (kind >= 4 ? 2U : 1U); ++which)
                        for (unsigned fault_kind = 0; fault_kind < 3; ++fault_kind)
                            for (unsigned ccr = 0; ccr < 32; ++ccr) {
                                fixture(page8k, privileged);
                                syn68k_fpu_reset();
                                unsigned size = sizes[kind], first = 0x28000, second = first + page_size;
                                unsigned fault = which ? second : first, data = which ? 0x14000 : 0x10000;
                                put32(pte(first), 0x10001);
                                put32(pte(second), 0x14001);
                                unsigned fault_pte = pte(fault), root = privileged ? 0x6000 : 0x3000;
                                unsigned op = kind >= 4 ? ops[kind] : (ops[kind] & ~63u) | mode << 3;
                                code_word(0x10000, op);
                                if (kind)
                                    code_word(0x10002, kind >= 4 ? 0x8080 : 0x0080);
                                if (kind >= 4)
                                    code_word(0x10004, 0x90c1);
                                EM_D0 = 0x11223344;
                                EM_D1 = 0x55667788;
                                EM_D2 = 0xa5b6c7d8;
                                EM_D3 = 0x98765432;
                                for (unsigned i = 0; i < size; ++i) {
                                    ram[0x10000 + i] = EM_D0 >> ((size - i - 1) * 8);
                                    ram[0x14000 + i] = EM_D1 >> ((size - i - 1) * 8);
                                }
                                unsigned original_a0 = first + (mode == 4 ? size : 0);
                                EM_A0 = original_a0;
                                EM_A1 = second;
                                supervisor = 1;
                                put32(pte(0x10000), 0xc001);
                                put32(pte(0x20000), 0x18001);
                                unsigned table_page = root & ~(page_size - 1);
                                put32(pte(0x30000), table_page | 1);
                                unsigned target = 0x30000 + fault_pte - table_page;
                                const unsigned program[] = {0x23fc,         data >> 16, data | 1, target >> 16,
                                                            target & 65535, 0xf518,     0x4e73};
                                for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                    code_word(0x10040 + i * 2, program[i]);
                                cpu_state.vbr = 0x20000;
                                put32(physical(cpu_state.vbr + 8), 0x10040);
                                cpu_state.isp = 0x20100;
                                EM_A7 = privileged ? cpu_state.isp : 0xdeadbe00;
                                set_status((privileged ? 0x2000 : 0) | ccr);
                                if (fault_kind == 2)
                                    fail_physical = data;
                                else
                                    put32(fault_pte, fault_kind ? data | 5 : 0);
                                watched_physical = 0x10000;
                                Syn68kFPUState expected = cpu_state.fpu;
                                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                                Syn68kMappedRun run;
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.restartable_read && run.step.access.rmw && !run.step.access.writing &&
                                       run.step.access.request == fault && run.step.access.size == size &&
                                       !run.step.access.completed && EM_A0 == original_a0 && !watched_attempts);
                                assert(status() == ((privileged ? 0x2000 : 0) | ccr));
                                unsigned first_reads = which || fault_kind == 2 ? 1 : 0;
                                assert(watched_read_attempts == first_reads);
                                assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                                fail_physical = UINT32_MAX;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                unsigned frame = 0x20100 - 60;
                                unsigned ssw = (fault_kind == 2 ? 0x300 : 0x700) |
                                               (size == 1   ? 0x20
                                                : size == 2 ? 0x40
                                                            : 0) |
                                               (privileged ? 5 : 1);
                                assert(mapped_value(frame + 12, 2) == ssw && mapped_value(frame + 20, 4) == fault);
                                for (unsigned i = 14; i < 20; i += 2)
                                    assert(!mapped_value(frame + i, 2));
                                assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 &&
                                       status() == ((privileged ? 0x2000 : 0) | ccr) && EM_A0 == original_a0);
                                assert(watched_read_attempts == first_reads);
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 + (kind >= 4 ? 6
                                                                                                  : kind    ? 4
                                                                                                            : 2));
                                unsigned mask = size == 1 ? 0xff : size == 2 ? 0xffff : UINT32_MAX;
                                assert(physical_value(0x10000, size) == (kind ? EM_D2 & mask : 0xc4));
                                if (kind >= 4)
                                    assert(physical_value(0x14000, size) == (EM_D3 & mask));
                                assert(EM_A0 == (mode == 3 ? first + size : first) && EM_A1 == second &&
                                       EM_D0 == 0x11223344 && EM_D1 == 0x55667788 && EM_D2 == 0xa5b6c7d8 &&
                                       EM_D3 == 0x98765432 && watched_attempts == 1 && watched_successes == 1);
                                assert(watched_read_attempts == first_reads + 1);
                                assert(status() == ((privileged ? 0x2000 : 0) | (ccr & 16) | (kind ? 4 : 0)));
                                assert(!memcmp(&expected, &cpu_state.fpu, sizeof expected));
                                ++cases;
                            }
                }
    fail_physical = watched_physical = UINT32_MAX;
    syn68k_fpu_reset();
}

static const unsigned locked_write_ops[] = {0x4ad0, 0x0ad0, 0x0cd0, 0x0ed0, 0x0cfc, 0x0efc};
static const unsigned locked_write_sizes[] = {1, 1, 2, 4, 2, 4};

static void locked_write_setup(unsigned page8k, unsigned privileged, unsigned kind, unsigned mode, unsigned lane,
                               unsigned sample, unsigned sr)
{
    fixture(page8k, privileged);
    syn68k_fpu_reset();
    unsigned size = locked_write_sizes[kind], first = 0x28000 + lane, second = first + page_size;
    put32(pte(first), 0x10001);
    put32(pte(second), 0x14001);
    code_word(0x10000, kind >= 4 ? locked_write_ops[kind] : (locked_write_ops[kind] & ~63u) | mode << 3);
    if (kind)
        code_word(0x10002, kind >= 4 ? 0x8080 : 0x0080);
    if (kind >= 4)
        code_word(0x10004, 0x90c1);
    const unsigned values[] = {0x44, 0x80, 0};
    EM_D0 = kind ? 0x11223344 : values[sample];
    EM_D1 = 0x55667788;
    EM_D2 = 0xa5b6c7d8;
    EM_D3 = 0x98765432;
    for (unsigned i = 0; i < size; ++i) {
        ram[0x10000 + lane + i] = EM_D0 >> ((size - i - 1) * 8);
        ram[0x14000 + lane + i] = EM_D1 >> ((size - i - 1) * 8);
    }
    EM_A0 = first + (mode == 4 ? size : 0);
    EM_A1 = second;
    cpu_state.vbr = 0x20000;
    cpu_state.dfc = privileged ? 5 : 1;
    cpu_state.isp = 0x20100;
    EM_A7 = privileged ? cpu_state.isp : 0xdeadbe00;
    set_status((privileged ? 0x2000 : 0) | sr);
    watched_physical = 0x10000 + lane;
    watched_tail_physical = kind >= 4 ? 0x14000 + lane : UINT32_MAX;
    watched_tail_successes = watched_tail_reads = 0;
}

static void guest_locked_writebacks(void)
{
    const unsigned ccrs[] = {0, 15, 16, 31};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 0; kind < 6; ++kind)
                for (unsigned which = 0; which < (kind >= 4 ? 2U : 1U); ++which)
                    for (unsigned mode = 2; mode <= 4; ++mode)
                        for (unsigned lane = 0; lane < 4; ++lane) {
                            unsigned size = locked_write_sizes[kind];
                            if ((kind >= 4 && mode != 2) || (lane & (size - 1)))
                                continue;
                            for (unsigned sample = 0; sample < (kind ? 1U : 3U); ++sample)
                                for (unsigned trace = 0; trace < 4; ++trace)
                                    for (unsigned c = 0; c < sizeof ccrs / sizeof *ccrs; ++c)
                                        for (unsigned fault_kind = 0; fault_kind < 3; ++fault_kind) {
                                            unsigned sr = trace << 14 | ccrs[c];
                                            unsigned traced = !!(trace & 2) || ((trace & 1) && kind);
                                            unsigned pair = kind >= 4 && !which;
                                            unsigned callback = pair && fault_kind == 2;
                                            locked_write_setup(page8k, privileged, kind, mode, lane, sample, sr);
                                            if (callback) {
                                                mutate_write_physical = 0x10000 + lane;
                                                write_mutation = 4;
                                            }
                                            Syn68kMappedCPU cpu = {
                                                .pc = 0x10000, .memory = &memory, .native = native_execution};
                                            Syn68kMappedRun run;
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                            unsigned next = 0x10000 + (kind >= 4 ? 6 : kind ? 4 : 2);
                                            assert(run.reason ==
                                                       (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                                   run.step.vector == (traced ? 9U : 0U) && cpu.pc == next);
                                            if (traced)
                                                assert(run.step.format == 2 && run.step.exception_pc == next &&
                                                       run.step.fault_address == 0x10000);
                                            unsigned expected_sr = status(), expected_a0 = EM_A0;
                                            unsigned expected_a1 = EM_A1, expected_d2 = EM_D2, expected_d3 = EM_D3;
                                            unsigned expected_first = physical_value(0x10000 + lane, size);
                                            unsigned expected_second = physical_value(0x14000 + lane, size);
                                            Syn68kFPUState expected_fpu = cpu_state.fpu;
                                            locked_write_setup(page8k, privileged, kind, mode, lane, sample, sr);
                                            if (callback) {
                                                mutate_write_physical = 0x10000 + lane;
                                                write_mutation = 4;
                                            }
                                            unsigned second = kind >= 4 && which;
                                            unsigned fault = 0x28000 + lane + (second ? page_size : 0);
                                            unsigned data = (second ? 0x14000 : 0x10000) + lane;
                                            unsigned descriptor = second ? 0x14001 : 0x10001;
                                            unsigned fault_pte = pte(fault), root = privileged ? 0x6000 : 0x3000;
                                            if (fault_kind == 2)
                                                fail_write_physical = data;
                                            else {
                                                late_read_physical = data;
                                                late_read_pte = fault_pte;
                                                late_read_descriptor = fault_kind ? descriptor | 4 : 0;
                                            }
                                            cpu.pc = 0x10000;
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                                   run.step.store_state == (traced ? 3U : 2U) &&
                                                   run.step.store_locked == 1 && run.step.access.rmw &&
                                                   run.step.access.writing && cpu.pc == next &&
                                                   status() == expected_sr && EM_A0 == expected_a0 &&
                                                   watched_read_attempts == 1 && watched_tail_reads == (kind >= 4) &&
                                                   watched_successes == second && !watched_tail_successes &&
                                                   !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                            assert(!!run.step.access.bus_error == (fault_kind == 2));
                                            assert(run.step.store_tail_valid == (pair ? 2U : 0U));
                                            if (pair)
                                                assert(run.step.store_tail_address == 0x28000 + lane + page_size &&
                                                       run.step.store_tail == expected_second &&
                                                       run.step.store_tail_fc == (privileged ? 5U : 1U));
                                            fail_write_physical = UINT32_MAX;
                                            supervisor = 1;
                                            put32(pte(0x10000), 0xc001);
                                            put32(pte(0x20000), 0x18001);
                                            unsigned table_page = root & ~(page_size - 1);
                                            put32(pte(0x30000), table_page | 1);
                                            unsigned target = 0x30000 + fault_pte - table_page;
                                            unsigned wb_address = fault_kind == 2 ? 40 : 32;
                                            unsigned wb_data = fault_kind == 2 ? 44 + (fault & 3) : 36 + 4 - size;
                                            const unsigned program[] = {0x23fc,
                                                                        descriptor >> 16,
                                                                        descriptor & 65535,
                                                                        target >> 16,
                                                                        target & 65535,
                                                                        0xf518,
                                                                        0x2c6f,
                                                                        wb_address,
                                                                        size == 1   ? 0x1c2f
                                                                        : size == 2 ? 0x3c2f
                                                                                    : 0x2c2f,
                                                                        wb_data,
                                                                        0x7e00 | (privileged ? 5 : 1),
                                                                        0x4e7b,
                                                                        0x7001,
                                                                        size == 1   ? 0x0e16
                                                                        : size == 2 ? 0x0e56
                                                                                    : 0x0e96,
                                                                        0x6800};
                                            unsigned cursor = 0x10040;
                                            for (unsigned i = 0; i < sizeof program / sizeof *program; ++i) {
                                                code_word(cursor, program[i]);
                                                cursor += 2;
                                            }
                                            if (pair) {
                                                const unsigned tail_program[] = {0x2c6f,
                                                                                 fault_kind == 2 ? 32U : 24U,
                                                                                 size == 2 ? 0x3c2fU : 0x2c2fU,
                                                                                 (fault_kind == 2 ? 36U : 28U) + 4 -
                                                                                     size,
                                                                                 size == 2 ? 0x0e56U : 0x0e96U,
                                                                                 0x6800};
                                                for (unsigned i = 0; i < sizeof tail_program / sizeof *tail_program;
                                                     ++i) {
                                                    code_word(cursor, tail_program[i]);
                                                    cursor += 2;
                                                }
                                            }
                                            code_word(cursor, 0x4e73);
                                            put32(physical(cpu_state.vbr + 8), 0x10040);
                                            Syn68kMappedDelivery delivery;
                                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                            unsigned frame = 0x20100 - 60, fc = privileged ? 5 : 1;
                                            unsigned size_bits = size == 1 ? 0x20 : size == 2 ? 0x40 : 0;
                                            assert(mapped_value(frame + 2, 4) == next &&
                                                   mapped_value(frame + 12, 2) ==
                                                       ((fault_kind == 2 ? 0 : 0x400) | 0x200 | size_bits | fc |
                                                        (traced ? 0x2000 : 0)) &&
                                                   mapped_value(frame + (fault_kind == 2 ? 18 : 16), 2) ==
                                                       (0x80 | size_bits | fc));
                                            if (pair)
                                                assert(mapped_value(frame + (fault_kind == 2 ? 16 : 14), 2) ==
                                                           (0x80 | size_bits | fc) &&
                                                       mapped_value(frame + (fault_kind == 2 ? 32 : 24), 4) ==
                                                           0x28000 + lane + page_size &&
                                                       mapped_value(frame + (fault_kind == 2 ? 36 : 28), 4) ==
                                                           expected_second);
                                            assert(syn68k_mapped_cpu_run(&cpu, pair ? 11 : 8, &run));
                                            assert(run.reason ==
                                                       (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                                   cpu.pc == next && status() == expected_sr && EM_A0 == expected_a0 &&
                                                   EM_A1 == expected_a1 && watched_read_attempts == 1 &&
                                                   watched_tail_reads == (kind >= 4) && watched_successes == 1 &&
                                                   watched_tail_successes == (kind >= 4));
                                            if (traced) {
                                                assert(run.step.vector == 9 && run.step.format == 2 &&
                                                       run.step.fault_address == 0x10000);
                                                put32(physical(cpu_state.vbr + 36), 0x10080);
                                                code_word(0x10080, 0x4e73);
                                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next &&
                                                       status() == expected_sr);
                                            }
                                            assert(physical_value(0x10000 + lane, size) == expected_first &&
                                                   physical_value(0x14000 + lane, size) == expected_second &&
                                                   !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                            assert(EM_D0 == (kind          ? 0x11223344U
                                                             : sample == 0 ? 0x44U
                                                             : sample == 1 ? 0x80U
                                                                           : 0U) &&
                                                   EM_D1 == 0x55667788 && EM_D2 == expected_d2 && EM_D3 == expected_d3);
                                            ++cases;
                                        }
                        }
    fail_write_physical = watched_physical = watched_tail_physical = late_read_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void locked_writeback_guards(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned kind = 4; kind < 6; ++kind) {
                locked_write_setup(page8k, privileged, kind, 2, 0, 0, 31);
                unsigned size = locked_write_sizes[kind], offset = size / 2;
                EM_A1 += offset;
                watched_tail_physical += offset;
                for (unsigned i = 0; i < size; ++i)
                    ram[watched_tail_physical + i] = EM_D1 >> ((size - i - 1) * 8);
                fail_write_physical = 0x10000;
                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                Syn68kMappedRun run;
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.store_state == 1 &&
                       run.step.store_tail_valid == 2 && run.step.store_locked == 1 && run.step.access.rmw &&
                       run.step.access.writing && cpu.pc == 0x10000 && watched_read_attempts == 1 &&
                       watched_tail_reads == 1 && !watched_successes && !watched_tail_successes);
                CPUState before = cpu_state;
                unsigned old_reads = reads, old_writes = writes;
                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                       !memcmp(&before, &cpu_state, sizeof before));
                ++cases;
            }
    for (unsigned kind = 2; kind <= 3; ++kind) {
        unsigned lane = kind == 2 ? 1 : 2;
        locked_write_setup(0, 1, kind, 2, lane, 0, 31);
        fail_write_physical = 0x10000 + lane;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.store_state < 2 &&
               cpu.pc == 0x10000 && status() == 0x201f && !watched_successes);
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    locked_write_setup(0, 1, 0, 2, 0, 0, 31);
    fail_write_physical = 0x10000;
    Syn68kIntegerStep component;
    assert(syn68k_integer_step(0x10000, &memory, &component));
    assert(component.vector == 2 && component.next == 0x10000 && component.store_state == 1 &&
           component.store_locked == 1 && component.store_next == 0x10002 && component.store_ccr == 16 &&
           status() == 0x201f && watched_read_attempts == 1 && !watched_successes);
    ++cases;
    fail_write_physical = watched_physical = watched_tail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void locked_writeback_record_guards(void)
{
    for (unsigned invalid = 0; invalid < 16; ++invalid) {
        fixture(0, 1);
        syn68k_fpu_reset();
        cpu_state.isp = EM_A7 = 0x20100;
        Syn68kIntegerStep record = {.vector = 2,
                                    .format = 7,
                                    .exception_pc = 0x10002,
                                    .store_state = 2,
                                    .store_next = 0x10002,
                                    .store_locked = 1,
                                    .access = {.translation = {.fault = MMU040_INVALID},
                                               .logical = 0x28000,
                                               .request = 0x28000,
                                               .size = 1,
                                               .writing = 1,
                                               .rmw = 1,
                                               .fc = 5}};
        if (invalid >= 8) {
            record.access.size = 4;
            record.store_tail_valid = 2;
            record.store_tail_fc = 5;
            record.store_tail_address = 0x2a000;
        }
        switch (invalid) {
        case 0:
            record.store_locked = 0;
            break;
        case 1:
            record.store_locked = 2;
            break;
        case 2:
            record.access.rmw = 0;
            break;
        case 3:
            record.fpu_vector = 49;
            break;
        case 4:
            record.access.size = 4;
            record.store_tail_valid = 1;
            record.store_tail_fc = 5;
            break;
        case 5:
            record.movem_state = 1;
            break;
        case 6:
            record.format = 0;
            break;
        case 7:
            record.access.fc = 2;
            break;
        case 8:
            record.store_tail_valid = 3;
            break;
        case 9:
            record.store_locked = record.access.rmw = 0;
            break;
        case 10:
            record.access.size = 1;
            break;
        case 11:
            record.store_tail_address += 2;
            break;
        case 12:
            record.store_tail_fc = 2;
            break;
        case 13:
            record.fpu_vector = 49;
            break;
        case 14:
            record.store_state = 1;
            break;
        case 15:
            record.format = 0;
            break;
        }
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        Syn68kMappedCPU cpu = {.pc = 0x10002, .memory = &memory, .native = native_execution};
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    syn68k_fpu_reset();
}

static void locked_read_restart_guards(void)
{
    for (unsigned mutation = 1; mutation <= 6; ++mutation) {
        fixture(0, 1);
        syn68k_fpu_reset();
        cpu_state.fpu.null_state = 0;
        code_word(0x10000, 0x4ae0); // TAS -(A0)
        put32(pte(0x28000), 0x10001);
        EM_A0 = 0x28001;
        fail_physical = 0x10000;
        fault_fpu_mutation = mutation;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.access.rmw &&
               !run.step.restartable_read && EM_A0 == 0x28000);
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fail_physical = UINT32_MAX;
    fault_fpu_mutation = 0;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static unsigned fpu_store_physical(unsigned logical)
{
    unsigned boundary = 0x28000 + page_size;
    return logical < boundary ? 0x10000 + logical - 0x28000 : 0x14000 + logical - boundary;
}

static void fpu_store_setup(unsigned page8k, unsigned privileged, unsigned format, unsigned mode, unsigned operand,
                            unsigned rounding, unsigned sign, unsigned sr)
{
    static const unsigned sizes[] = {4, 4, 12, 12, 2, 8, 1, 12};
    fixture(page8k, privileged);
    syn68k_fpu_reset();
    cpu_state.fpu.fpcr = rounding << 4;
    cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff | sign << 15, UINT64_C(0xa000000000000000)};
    put32(pte(0x28000), 0x10001);
    put32(pte(0x28000 + page_size), 0x14001);
    for (unsigned i = 0; i < sizes[format]; ++i)
        ram[fpu_store_physical(operand + i)] = 0xa5;
    code_word(0x10000, 0xf200 | mode << 3);
    code_word(0x10002, 0x6000 | format << 10);
    EM_A0 = operand + (mode == 4 ? sizes[format] : 0);
    EM_D4 = 0;
    cpu_state.isp = 0x20100;
    EM_A7 = privileged ? cpu_state.isp : 0x76543210;
    set_status((privileged ? 0x2000 : 0) | sr);
}

static void fpu_store_exception(unsigned exception)
{
    if (!exception)
        return;
    cpu_state.fpu.fpcr |= 0x200;
    unsigned sign = cpu_state.fpu.regs[0].exponent & 0x8000;
    if (exception == 2)
        cpu_state.fpu.regs[0] = (Syn68kFP80){sign | 0x403e, UINT64_C(0x8000000000000000)};
    else if (exception == 3)
        cpu_state.fpu.regs[0] = (Syn68kFP80){sign | 0x7ffe, UINT64_MAX};
    else if (exception == 4)
        cpu_state.fpu.regs[0] = (Syn68kFP80){sign | 1, UINT64_C(0x8000000000000000)};
}

static void fpu_predicate_store(unsigned mode, unsigned condition, unsigned nan)
{
    code_word(0x10000, 0xf240 | mode << 3);
    code_word(0x10002, condition);
    cpu_state.fpu.fpcr = 0; // BSUN disabled: retain its status before the store.
    cpu_state.fpu.fpsr = nan ? 0x01000000 : 0x04000000;
    cpu_state.fpu.null_state = nan;
}

enum FPUStoreKind { FPU_STORE_MOVE, FPU_STORE_CONDITION, FPU_STORE_LIST, FPU_STORE_CONTROL, FPU_STORE_FRAME };

static void fpu_list_store(unsigned mode, unsigned mask, unsigned dynamic, unsigned operand, unsigned whole)
{
    for (unsigned reg = 0; reg < 8; ++reg)
        cpu_state.fpu.regs[reg] = (Syn68kFP80){0x3fff + reg, UINT64_C(0x8123456789abcdef) + reg};
    cpu_state.fpu.fpsr = 0x0a120088;
    cpu_state.fpu.fpiar = 0x11223344;
    cpu_state.fpu.null_state = dynamic;
    for (unsigned i = 0; i < whole; ++i)
        ram[fpu_store_physical(operand + i)] = 0xa5;
    code_word(0x10000, 0xf200 | mode << 3);
    code_word(0x10002, (mode == 4 ? 0xe000 : 0xf000) | (dynamic ? 0x0810 : mask));
    EM_D1 = mask;
    EM_A0 = operand + (mode == 4 ? whole : 0);
}

static void fpu_control_store(unsigned mode, unsigned mask, unsigned sign, unsigned operand, unsigned whole)
{
    cpu_state.fpu.fpcr = 0xff80 ^ sign << 4;
    cpu_state.fpu.fpsr = 0x0a120088 ^ sign << 24;
    cpu_state.fpu.fpiar = 0x11223344 ^ sign << 31;
    cpu_state.fpu.null_state = sign;
    for (unsigned i = 0; i < whole; ++i)
        ram[fpu_store_physical(operand + i)] = 0xa5;
    code_word(0x10000, 0xf200 | mode << 3);
    code_word(0x10002, 0xa000 | mask << 10);
    EM_A0 = operand + (mode == 4 ? whole : 0);
}

static void fpu_frame_store(unsigned mode, unsigned frame_kind, unsigned operand, unsigned whole)
{
    Syn68kFPUStep prepared;
    if (!frame_kind)
        syn68k_fpu_reset();
    else if (frame_kind == 1)
        cpu_state.fpu.null_state = 0;
    else if (frame_kind == 2) {
        code_word(0x10002, 0x5c00); // FMOVECR builds a real unimplemented frame.
        assert(syn68k_fpu_step_decoded(0x10000, 0xf200, &memory, &prepared));
        assert(prepared.vector == 11 && cpu_state.fpu.frame_size == 52);
    }
    else {
        cpu_state.fpu.fpcr |= 0x200;
        code_word(0x10002, 0x6000); // Inexact integer move-out to D0 builds a busy frame.
        assert(syn68k_fpu_step_decoded(0x10000, 0xf200, &memory, &prepared));
        assert(prepared.vector == 49 && cpu_state.fpu.frame_size == 100);
    }
    for (unsigned i = 0; i < whole; ++i)
        ram[fpu_store_physical(operand + i)] = 0xa5;
    code_word(0x10000, 0xf300 | mode << 3);
    code_word(0x10002, 0x4e71);
    EM_A0 = operand + (mode == 4 ? whole : 0);
}

static void guest_fpu_bus_writebacks(unsigned exception, unsigned tail, enum FPUStoreKind kind, unsigned unaligned,
                                     unsigned completed, int live_trace, unsigned physical_prefix)
{
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    const unsigned sizes[] = {4, 4, 12, 2, 8, 1};
    const unsigned ccrs[] = {0, 15, 16, 31};
    const unsigned vectors[] = {0, 49, 52, 53, 51};
    const unsigned conditions[] = {0, 15, 16, 31};
    const unsigned list_masks[] = {1, 0x80, 0x81, 0xff};
    const unsigned list_sizes[] = {12, 12, 24, 96};
    const unsigned control_masks[] = {1, 4, 5, 7, 6};
    const unsigned control_sizes[] = {4, 4, 8, 12, 8};
    const unsigned frame_sizes[] = {4, 4, 52, 100};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f)
                for (unsigned mode = 2; mode <= 4; ++mode)
                    for (unsigned trace = 0; trace < 4; ++trace)
                        for (unsigned lane = 0; lane < 4; ++lane) {
                            if ((exception && exception <= 2 &&
                                 !(formats[f] == 0 || formats[f] == 4 || formats[f] == 6)) ||
                                (exception >= 3 && !(formats[f] == 1 || formats[f] == 5)))
                                continue;
                            if (tail && kind == FPU_STORE_MOVE && !(formats[f] == 2 || formats[f] == 5))
                                continue;
                            if ((kind == FPU_STORE_CONDITION && formats[f] != 6) ||
                                (kind == FPU_STORE_LIST && (formats[f] != 2 || mode == 3)) ||
                                (kind == FPU_STORE_CONTROL && formats[f] != 0) ||
                                (kind == FPU_STORE_FRAME && (formats[f] != 2 || mode == 3 || !privileged)))
                                continue;
                            unsigned size = sizes[f] < 4 ? sizes[f] : 4;
                            if ((completed && (completed >= size || lane != ((4 - completed) & 3))) ||
                                (unaligned ? !(lane & (size - 1)) : (lane & (size - 1))))
                                continue;
                            for (unsigned physical_fault = 0; physical_fault < 2; ++physical_fault)
                                for (unsigned rounding = 0; rounding < (kind == FPU_STORE_CONTROL ? 5U : 4U);
                                     ++rounding)
                                    for (unsigned sign = 0; sign < 2; ++sign)
                                        for (unsigned c = 0; c < sizeof ccrs / sizeof *ccrs; ++c) {
                                            if ((physical_prefix && !physical_fault) ||
                                                (completed && physical_fault && !physical_prefix) ||
                                                (unaligned &&
                                                 ((kind == FPU_STORE_MOVE && rounding) || (c != 0 && c != 3))))
                                                continue;
                                            if (live_trace >= 0 &&
                                                (trace == 3 || mode != 2 || lane ||
                                                 physical_fault != (kind == FPU_STORE_FRAME) || rounding || sign || c ||
                                                 formats[f] !=
                                                     (kind == FPU_STORE_LIST || kind == FPU_STORE_FRAME ? 2U : 0U)))
                                                continue;
                                            unsigned whole = kind == FPU_STORE_LIST      ? list_sizes[rounding]
                                                             : kind == FPU_STORE_CONTROL ? control_sizes[rounding]
                                                             : kind == FPU_STORE_FRAME   ? frame_sizes[rounding]
                                                                                         : sizes[f];
                                            if (tail && whole < 8)
                                                continue;
                                            unsigned mutation = !tail || (!physical_fault && !completed) ? 0
                                                                : kind == FPU_STORE_LIST                 ? 1
                                                                : kind == FPU_STORE_CONTROL              ? 8
                                                                : kind == FPU_STORE_FRAME                ? 16
                                                                                                         : 0;
                                            unsigned prefix = whole - size - tail * 4;
                                            unsigned page_bytes = page8k ? 8192 : 4096;
                                            unsigned fault = physical_prefix == 2 ? 0x28104 - completed
                                                             : completed
                                                                 ? 0x28000 + page_bytes - completed
                                                                 : (unaligned ? 0x28100 : 0x28000 + page_bytes) + lane;
                                            unsigned operand = fault - prefix;
                                            unsigned data = physical_prefix == 2 ? 0x10104 - completed
                                                            : completed          ? 0x10000 + page_bytes - completed
                                                                        : (unaligned ? 0x10100 : 0x14000) + lane;
                                            unsigned sr = trace << 14 | ccrs[c];
                                            unsigned traced =
                                                (trace & 2) ||
                                                ((trace & 1) && (kind == FPU_STORE_LIST || kind == FPU_STORE_CONTROL ||
                                                                 kind == FPU_STORE_FRAME));
                                            fpu_store_setup(page8k, privileged, formats[f], mode, operand, rounding,
                                                            sign, sr);
                                            fpu_store_exception(exception);
                                            if (kind == FPU_STORE_CONDITION)
                                                fpu_predicate_store(mode, conditions[rounding], sign);
                                            else if (kind == FPU_STORE_LIST)
                                                fpu_list_store(mode, list_masks[rounding], sign, operand, whole);
                                            else if (kind == FPU_STORE_CONTROL)
                                                fpu_control_store(mode, control_masks[rounding], sign, operand, whole);
                                            else if (kind == FPU_STORE_FRAME)
                                                fpu_frame_store(mode, rounding, operand, whole);
                                            if (mutation) {
                                                mutate_write_physical = data;
                                                write_mutation = mutation;
                                            }
                                            unsigned next = kind == FPU_STORE_FRAME ? 0x10002 : 0x10004;
                                            Syn68kMappedCPU cpu = {
                                                .pc = 0x10000, .memory = &memory, .native = native_execution};
                                            Syn68kMappedRun run;
                                            if (live_trace >= 0) {
                                                trace_read_physical =
                                                    kind == FPU_STORE_FRAME ? data : physical(0x10002);
                                                trace_read_sr = (unsigned)live_trace << 14;
                                                trace_bus_kind = kind == FPU_STORE_FRAME;
                                            }
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                            assert(run.reason == (exception || traced ? SYN68K_MAPPED_EXCEPTION
                                                                                      : SYN68K_MAPPED_BUDGET) &&
                                                   cpu.pc == next);
                                            assert(write_mutated == !!mutation);
                                            if (exception)
                                                assert(run.step.vector == vectors[exception] && run.step.format == 3 &&
                                                       run.step.fault_address == operand);
                                            else if (traced)
                                                assert(run.step.vector == 9 && run.step.format == 2 &&
                                                       run.step.fault_address == 0x10000);
                                            Syn68kFPUState expected_fpu = cpu_state.fpu;
                                            unsigned expected_a0 = EM_A0, expected_sr = status();
                                            unsigned char expected[100];
                                            for (unsigned i = 0; i < whole; ++i)
                                                expected[i] = ram[fpu_store_physical(operand + i)];
                                            fpu_store_setup(page8k, privileged, formats[f], mode, operand, rounding,
                                                            sign, sr);
                                            fpu_store_exception(exception);
                                            if (kind == FPU_STORE_CONDITION)
                                                fpu_predicate_store(mode, conditions[rounding], sign);
                                            else if (kind == FPU_STORE_LIST)
                                                fpu_list_store(mode, list_masks[rounding], sign, operand, whole);
                                            else if (kind == FPU_STORE_CONTROL)
                                                fpu_control_store(mode, control_masks[rounding], sign, operand, whole);
                                            else if (kind == FPU_STORE_FRAME)
                                                fpu_frame_store(mode, rounding, operand, whole);
                                            unsigned fault_pte = pte(fault + completed),
                                                     root = privileged ? 0x6000 : 0x3000;
                                            if (physical_fault) {
                                                memory.aligned_transfers = !!physical_prefix;
                                                fail_write_physical = fpu_store_physical(fault + completed);
                                            }
                                            else if (!completed && unaligned && prefix) {
                                                unsigned previous_size =
                                                    tail && (kind == FPU_STORE_LIST ||
                                                             (kind == FPU_STORE_MOVE && formats[f] == 2))
                                                        ? 2
                                                        : 4;
                                                late_write_physical = data - previous_size;
                                                late_write_pte = fault_pte;
                                                late_write_descriptor = 0x10005;
                                            }
                                            else
                                                put32(fault_pte, unaligned && !completed ? 0x10005 : 0x14005);
                                            watched_physical = data;
                                            watched_tail_physical = !tail                  ? UINT32_MAX
                                                                    : physical_prefix == 2 ? data + 4
                                                                    : completed            ? 0x14000 + 4 - completed
                                                                                           : data + 4;
                                            watched_tail_successes = 0;
                                            if (mutation) {
                                                mutate_write_physical = data;
                                                write_mutation = mutation;
                                            }
                                            cpu.pc = 0x10000;
                                            if (live_trace >= 0) {
                                                trace_read_physical =
                                                    kind == FPU_STORE_FRAME ? data : physical(0x10002);
                                                trace_read_sr = (unsigned)live_trace << 14;
                                                trace_bus_kind = kind == FPU_STORE_FRAME;
                                            }
                                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                                   run.step.store_state == (!exception && traced ? 3U : 2U) &&
                                                   cpu.pc == next && EM_A0 == expected_a0 && status() == expected_sr);
                                            assert(!memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu) &&
                                                   watched_successes == !!completed && !watched_tail_successes);
                                            assert(write_mutated == !!mutation);
                                            assert(run.step.access.request == fault && run.step.access.size == size &&
                                                   run.step.access.completed == completed &&
                                                   run.step.access.logical == fault + completed);
                                            if (completed) {
                                                unsigned value = 0;
                                                for (unsigned i = prefix; i < prefix + size; ++i)
                                                    value = value << 8 | expected[i];
                                                assert(run.step.access.bus_error == (int)physical_fault &&
                                                       run.step.access.value == value);
                                            }
                                            assert(run.step.store_tail_valid == tail);
                                            for (unsigned i = 0; i < prefix + completed; ++i)
                                                assert(ram[fpu_store_physical(operand + i)] == expected[i]);
                                            for (unsigned i = prefix + completed; i < whole; ++i)
                                                assert(ram[fpu_store_physical(operand + i)] == 0xa5);
                                            fail_write_physical = UINT32_MAX;
                                            supervisor = 1;
                                            put32(pte(0x10000 + page_size), 0xe001);
                                            put32(pte(0x20000), 0x18001);
                                            unsigned table_page = root & ~(page_size - 1);
                                            put32(pte(0x30000), table_page | 1);
                                            unsigned target = 0x30000 + fault_pte - table_page;
                                            unsigned wb_address = physical_fault ? 40 : 32;
                                            unsigned rotate = unaligned && physical_fault;
                                            unsigned wb_data =
                                                physical_fault ? rotate ? 44 : 44 + (fault & 3) : 36 + 4 - size;
                                            unsigned descriptor =
                                                physical_prefix == 2 || (unaligned && !completed) ? 0x10001 : 0x14001;
                                            const unsigned program[] = {0x23fc,
                                                                        1,
                                                                        descriptor & 65535,
                                                                        target >> 16,
                                                                        target & 65535,
                                                                        0xf518,
                                                                        0x226f,
                                                                        wb_address,
                                                                        rotate      ? 0x242f
                                                                        : size == 1 ? 0x142f
                                                                        : size == 2 ? 0x342f
                                                                                    : 0x242f,
                                                                        wb_data,
                                                                        0x7600 | (privileged ? 5 : 1),
                                                                        0x4e7b,
                                                                        0x3001,
                                                                        size == 1   ? 0x0e11
                                                                        : size == 2 ? 0x0e51
                                                                                    : 0x0e91,
                                                                        0x2800};
                                            unsigned handler = 0x10000 + page_size + 0x40;
                                            unsigned end = handler;
                                            unsigned words = sizeof program / sizeof *program;
                                            for (unsigned i = 0; i < words - 2; ++i) {
                                                code_word(end, program[i]);
                                                end += 2;
                                            }
                                            if (rotate) {
                                                /* Undo memory-lane alignment using the guest address.
                                                   DFC is already set; D3 is now kernel scratch. */
                                                const unsigned rotation[] = {0x2609, 0x0283, 0, 3, 0xe78b, 0xe7ba};
                                                for (unsigned i = 0; i < sizeof rotation / sizeof *rotation; ++i) {
                                                    code_word(end, rotation[i]);
                                                    end += 2;
                                                }
                                                if (size == 2) {
                                                    code_word(end, 0x4842); // SWAP returns the word to the low half.
                                                    end += 2;
                                                }
                                            }
                                            for (unsigned i = words - 2; i < words; ++i) {
                                                code_word(end, program[i]);
                                                end += 2;
                                            }
                                            if (tail) {
                                                const unsigned pending[] = {0x226f, physical_fault ? 32U : 24U,
                                                                            0x242f, physical_fault ? 36U : 28U,
                                                                            0x0e91, 0x2800};
                                                for (unsigned i = 0; i < sizeof pending / sizeof *pending; ++i)
                                                    code_word(end + i * 2, pending[i]);
                                                end += sizeof pending / sizeof *pending * 2;
                                            }
                                            code_word(end, 0x5284);
                                            code_word(end + 2, 0x4e73);
                                            cpu_state.vbr = 0x20000;
                                            put32(physical(cpu_state.vbr + 8), handler);
                                            Syn68kMappedDelivery delivery;
                                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                            unsigned frame = 0x20100 - 60;
                                            unsigned size_bits = size == 1 ? 0x20 : size == 2 ? 0x40 : 0;
                                            unsigned fc = privileged ? 5 : 1;
                                            assert(mapped_value(frame + 2, 4) == next &&
                                                   mapped_value(frame + 12, 2) ==
                                                       ((physical_fault ? 0 : 0x400) |
                                                        (completed && !physical_fault ? 0x800 : 0) | size_bits | fc |
                                                        (exception ? 0x8000
                                                         : traced  ? 0x2000
                                                                   : 0)));
                                            assert(mapped_value(frame + 8, 4) == (exception ? operand
                                                                                  : traced  ? 0x10000
                                                                                            : 0));
                                            assert(mapped_value(frame + 20, 4) == fault &&
                                                   mapped_value(frame + wb_address, 4) == fault &&
                                                   mapped_value(frame + (physical_fault ? 18 : 16), 2) ==
                                                       (0x80 | size_bits | fc));
                                            if (tail) {
                                                unsigned tail_value = 0;
                                                for (unsigned i = whole - 4; i < whole; ++i)
                                                    tail_value = tail_value << 8 | expected[i];
                                                assert(
                                                    mapped_value(frame + (physical_fault ? 16 : 14), 2) ==
                                                        (0x80 | fc) &&
                                                    mapped_value(frame + (physical_fault ? 32 : 24), 4) == fault + 4 &&
                                                    mapped_value(frame + (physical_fault ? 36 : 28), 4) == tail_value);
                                            }
                                            assert(syn68k_mapped_cpu_run(
                                                &cpu, 9 + tail * 3 + (rotate ? 4 + (size == 2) : 0), &run));
                                            assert(run.reason == (exception || traced ? SYN68K_MAPPED_EXCEPTION
                                                                                      : SYN68K_MAPPED_BUDGET) &&
                                                   cpu.pc == next && EM_A0 == expected_a0 && status() == expected_sr &&
                                                   EM_D4 == 1 && watched_successes == 1 + !!completed &&
                                                   watched_tail_successes == tail);
                                            assert(!memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                            if (exception) {
                                                assert(run.step.vector == vectors[exception] && run.step.format == 3 &&
                                                       run.step.fault_address == operand);
                                                unsigned post_handler = handler + 0x80;
                                                put32(physical(cpu_state.vbr + vectors[exception] * 4), post_handler);
                                                EM_A2 = 0x20080;
                                                EM_A3 = 0x20020;
                                                put32(physical(EM_A3), 0);
                                                code_word(post_handler, 0xf312); // Save the actual pending busy frame.
                                                code_word(post_handler + 2, 0xf353); // Restore null to consume it.
                                                code_word(post_handler + 4, 0x4e73);
                                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                assert(mapped_value(EM_A7 + 8, 4) == operand &&
                                                       mapped_value(EM_A7, 2) == expected_sr);
                                                assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next &&
                                                       status() == expected_sr && cpu_state.fpu.null_state &&
                                                       !cpu_state.fpu.pending_vector && !cpu_state.fpu.frame_size);
                                                for (unsigned i = 0; i < expected_fpu.frame_size; ++i)
                                                    assert(ram[physical(0x20080 + i)] == expected_fpu.frame[i]);
                                            }
                                            else if (traced) {
                                                assert(run.step.vector == 9 && run.step.format == 2 &&
                                                       run.step.fault_address == 0x10000);
                                                unsigned trace_handler = handler + 0x80;
                                                put32(physical(cpu_state.vbr + 36), trace_handler);
                                                code_word(trace_handler, 0x4e73);
                                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                                            }
                                            if (!exception)
                                                assert(!memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                                            for (unsigned i = 0; i < whole; ++i)
                                                assert(ram[fpu_store_physical(operand + i)] == expected[i]);
                                            ++cases;
                                        }
                        }
    memory.aligned_transfers = 0;
    fail_write_physical = watched_physical = UINT32_MAX;
    watched_tail_physical = UINT32_MAX;
    mutate_write_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void guest_fpu_writebacks(unsigned exception, unsigned tail, enum FPUStoreKind kind, unsigned unaligned,
                                 unsigned completed, int live_trace)
{
    guest_fpu_bus_writebacks(exception, tail, kind, unaligned, completed, live_trace, 0);
}

/* Physical faults retain the original scalar in WB1, including successful
   prefixes from byte/word/byte transfers (MC68040UM 7.3 and 8.4.6.7). */
static void guest_fpu_physical_writebacks(void)
{
    for (unsigned boundary = 1; boundary <= 2; ++boundary)
        for (unsigned completed = 1; completed < 4; ++completed) {
            for (unsigned exception = 0; exception < 5; ++exception)
                for (unsigned tail = 0; tail < 2; ++tail)
                    guest_fpu_bus_writebacks(exception, tail, FPU_STORE_MOVE, 1, completed, -1, boundary);
            for (unsigned tail = 0; tail < 2; ++tail) {
                guest_fpu_bus_writebacks(0, tail, FPU_STORE_LIST, 1, completed, -1, boundary);
                guest_fpu_bus_writebacks(0, tail, FPU_STORE_CONTROL, 1, completed, -1, boundary);
                guest_fpu_bus_writebacks(0, tail, FPU_STORE_FRAME, 1, completed, -1, boundary);
            }
        }
}

static const unsigned fpu_tail_sizes[] = {8, 12, 24};

static void guest_fpu_final_writebacks(unsigned exception, unsigned tail, enum FPUStoreKind kind)
{
    guest_fpu_writebacks(exception, tail, kind, 0, 0, -1);
}

static void guest_fpu_unaligned_writebacks(void)
{
    for (unsigned exception = 0; exception < 5; ++exception)
        for (unsigned tail = 0; tail < 2; ++tail)
            guest_fpu_writebacks(exception, tail, FPU_STORE_MOVE, 1, 0, -1);
    for (unsigned tail = 0; tail < 2; ++tail) {
        guest_fpu_writebacks(0, tail, FPU_STORE_LIST, 1, 0, -1);
        guest_fpu_writebacks(0, tail, FPU_STORE_CONTROL, 1, 0, -1);
        guest_fpu_writebacks(0, tail, FPU_STORE_FRAME, 1, 0, -1);
    }
}

static void guest_fpu_partial_atc_writebacks(void)
{
    for (unsigned completed = 1; completed < 4; ++completed) {
        for (unsigned exception = 0; exception < 5; ++exception)
            for (unsigned tail = 0; tail < 2; ++tail)
                guest_fpu_writebacks(exception, tail, FPU_STORE_MOVE, 1, completed, -1);
        for (unsigned tail = 0; tail < 2; ++tail) {
            guest_fpu_writebacks(0, tail, FPU_STORE_LIST, 1, completed, -1);
            guest_fpu_writebacks(0, tail, FPU_STORE_CONTROL, 1, completed, -1);
            guest_fpu_writebacks(0, tail, FPU_STORE_FRAME, 1, completed, -1);
        }
    }
}

static void guest_fpu_trace_writebacks(void)
{
    for (int live = 0; live < 3; ++live) {
        guest_fpu_writebacks(0, 0, FPU_STORE_MOVE, 0, 0, live);
        guest_fpu_writebacks(0, 0, FPU_STORE_LIST, 0, 0, live);
        guest_fpu_writebacks(0, 0, FPU_STORE_CONTROL, 0, 0, live);
        guest_fpu_writebacks(0, 0, FPU_STORE_FRAME, 0, 0, live);
    }
}

static unsigned control_replay_code(unsigned mode, unsigned operand, unsigned reg)
{
    code_word(0x10000, 0xf200 | mode << 3 | (mode == 7 ? 1 : reg));
    EM_AREG(reg) = EM_A0;
    EM_D0 = 0;
    if (mode == 5 || mode == 6) {
        code_word(0x10004, 0);
        return 0x10006;
    }
    if (mode == 7) {
        code_word(0x10004, operand >> 16);
        code_word(0x10006, operand & 65535);
        return 0x10008;
    }
    return 0x10004;
}

static void guest_fpu_control_replays(unsigned reg)
{
    const unsigned modes[] = {2, 3, 4, 5, 6, 7};
    const unsigned ccrs[] = {0, 15, 16, 31};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned m = 0; m < sizeof modes / sizeof *modes; ++m)
                for (unsigned lane = 0; lane < 4; ++lane)
                    for (unsigned sign = 0; sign < 2; ++sign)
                        for (unsigned c = 0; c < sizeof ccrs / sizeof *ccrs; ++c) {
                            unsigned mode = modes[m], operand = 0x28000 + (page8k ? 8192 : 4096) + lane;
                            if ((reg && mode != 3 && mode != 4) || (reg == 7 && privileged))
                                continue;
                            fpu_store_setup(page8k, privileged, 0, mode, operand, 0, sign, ccrs[c]);
                            fpu_control_store(mode, 7, sign, operand, 12);
                            unsigned next = control_replay_code(mode, operand, reg);
                            unsigned initial_address = EM_AREG(reg);
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next);
                            unsigned expected_address = EM_AREG(reg);
                            Syn68kFPUState expected = cpu_state.fpu;
                            unsigned expected_sr = status();
                            unsigned char bytes[12];
                            memcpy(bytes, ram + 0x14000 + lane, sizeof bytes);
                            fpu_store_setup(page8k, privileged, 0, mode, operand, 0, sign, ccrs[c]);
                            fpu_control_store(mode, 7, sign, operand, 12);
                            control_replay_code(mode, operand, reg);
                            unsigned fault_pte = pte(operand), root = privileged ? 0x6000 : 0x3000;
                            put32(fault_pte, 0x14005);
                            watched_physical = 0x14000 + lane;
                            watched_tail_physical = 0x14008 + lane;
                            watched_tail_successes = 0;
                            cpu.pc = 0x10000;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.store_state == 4 &&
                                   run.step.vector == 2 && cpu.pc == 0x10000 && EM_AREG(reg) == initial_address &&
                                   status() == expected_sr && !watched_successes && !watched_tail_successes &&
                                   !memcmp(&expected, &cpu_state.fpu, sizeof expected));
                            assert(run.step.store_next == 0x10000 && run.step.access.request == operand &&
                                   run.step.access.value == expected.fpcr && !run.step.store_tail_valid);
                            supervisor = 1;
                            put32(pte(0x10000 + page_size), 0xe001);
                            put32(pte(0x20000), 0x18001);
                            unsigned table_page = root & ~(page_size - 1);
                            put32(pte(0x30000), table_page | 1);
                            unsigned target = 0x30000 + fault_pte - table_page;
                            unsigned scratch = reg == 1 ? 2 : 1;
                            const unsigned program[] = {0x23fc,
                                                        1,
                                                        0x4001,
                                                        target >> 16,
                                                        target & 65535,
                                                        0xf518,
                                                        0x206f | scratch << 9,
                                                        32,
                                                        0x242f,
                                                        36,
                                                        0x7600 | (privileged ? 5 : 1),
                                                        0x4e7b,
                                                        0x3001,
                                                        0x0e90 | scratch,
                                                        0x2800,
                                                        0x5284,
                                                        0x4e73};
                            unsigned handler = 0x10000 + page_size + 0x40;
                            for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                code_word(handler + i * 2, program[i]);
                            cpu_state.vbr = 0x20000;
                            put32(physical(cpu_state.vbr + 8), handler);
                            Syn68kMappedDelivery delivery;
                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                            unsigned frame = 0x20100 - 60, fc = privileged ? 5 : 1;
                            assert(
                                mapped_value(frame + 2, 4) == 0x10000 && mapped_value(frame + 12, 2) == (0x400 | fc) &&
                                mapped_value(frame + 16, 2) == (0x80 | fc) && !mapped_value(frame + 14, 2) &&
                                !mapped_value(frame + 18, 2) && mapped_value(frame + 20, 4) == operand &&
                                mapped_value(frame + 32, 4) == operand && mapped_value(frame + 36, 4) == expected.fpcr);
                            assert(syn68k_mapped_cpu_run(&cpu, 9, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_D4 == 1 &&
                                   watched_successes == 1 && !watched_tail_successes);
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next &&
                                   EM_AREG(reg) == expected_address && status() == expected_sr &&
                                   watched_successes == 2 && watched_tail_successes == 1 &&
                                   !memcmp(&expected, &cpu_state.fpu, sizeof expected) &&
                                   !memcmp(bytes, ram + 0x14000 + lane, sizeof bytes));
                            ++cases;
                        }
    watched_physical = watched_tail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_control_replay_guards(void)
{
    for (unsigned guard = 0; guard < 14; ++guard) {
        unsigned mode = guard == 0 ? 3 : guard == 1 ? 4 : 2;
        unsigned sr = guard == 2 ? 0x4000 : guard == 3 ? 0x8000 : 0;
        fpu_store_setup(0, 1, 0, mode, 0x29000, 0, 0, sr);
        fpu_control_store(mode, 7, 0, 0x29000, 12);
        if (guard == 2 || guard == 3) {
            trace_read_physical = physical(0x10000);
            trace_read_sr = 0;
        }
        if (guard == 4) {
            memory.aligned_transfers = 1;
            fail_write_physical = mutate_write_physical = 0x14000;
            write_mutation = 4;
        }
        else
            put32(pte(0x29000), 0x14005);
        if (guard < 2) {
            replay_read_physical = physical(0x10002);
            replay_read_mutation = 10;
        }
        if (guard >= 5) {
            replay_read_physical = physical(0x10002);
            replay_read_mutation = guard - 4;
        }
        unsigned expected_a1 = EM_A1 ^ 0x100;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state);
        if (guard == 4)
            assert(write_mutated && EM_A1 == expected_a1);
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    memory.aligned_transfers = 0;
    fail_write_physical = mutate_write_physical = replay_read_physical = UINT32_MAX;
    replay_read_mutation = 0;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_control_replay_bus_record_guards(unsigned physical_bus)
{
    fpu_store_setup(0, 1, 0, 2, 0x29000, 0, 0, 0);
    fpu_control_store(2, 7, 0, 0x29000, 12);
    if (physical_bus) {
        memory.aligned_transfers = 1;
        fail_write_physical = fpu_store_physical(0x29000);
    }
    else
        put32(pte(0x29000), 0x14005);
    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.store_state == 4);
    for (unsigned guard = 0; guard < 23; ++guard) {
        Syn68kIntegerStep record = run.step;
        switch (guard) {
        case 0:
            record.store_state = 5;
            break;
        case 1:
            record.next += 2;
            break;
        case 2:
            record.exception_pc |= 1;
            break;
        case 3:
            record.store_next += 2;
            break;
        case 4:
            record.fault_address += 1;
            break;
        case 5:
            record.store_ccr = 1;
            break;
        case 6:
            record.store_trace_pc = 2;
            break;
        case 7:
            record.movem_state = 1;
            break;
        case 8:
            record.fpu_vector = 49;
            break;
        case 9:
            record.store_tail_valid = 1;
            break;
        case 10:
            record.store_locked = 1;
            break;
        case 11:
            record.restartable_read = 1;
            break;
        case 12:
            record.access.writing = 0;
            break;
        case 13:
            record.access.rmw = 1;
            break;
        case 14:
            record.access.completed = 1;
            break;
        case 15:
            record.access.bus_error = !physical_bus;
            break;
        case 16:
            record.access.size = 1;
            break;
        case 17:
            record.access.logical += 1;
            break;
        case 18:
            record.access.fc = 2;
            break;
        case 19:
            record.format = 2;
            break;
        case 20:
            record.access.completed = 1;
            record.fault_address = ++record.access.logical;
            if (physical_bus) {
                record.access.bus_error = 0;
                record.access.translation.fault = MMU040_WRITE_PROTECTED;
            }
            break;
        case 21:
            record.access.completed = record.access.size;
            record.access.logical += record.access.size;
            record.fault_address = record.access.logical;
            break;
        case 22:
            record.access.translation.fault = physical_bus ? MMU040_BAD_REQUEST : MMU040_OK;
            break;
        }
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
        cpu_state.sr &= ~0xc000;
    }
    memory.aligned_transfers = 0;
    fail_write_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_control_replay_record_guards(void)
{
    fpu_control_replay_bus_record_guards(0);
    fpu_control_replay_bus_record_guards(1);
}

static unsigned pure_store_setup(enum FPUStoreKind kind, unsigned page8k, unsigned privileged, unsigned mode,
                                 unsigned operand, unsigned variant, unsigned sign, unsigned ccr, unsigned whole)
{
    const unsigned masks[] = {0x80, 0x81, 0xff};
    fpu_store_setup(page8k, privileged, kind == FPU_STORE_MOVE ? 2 : 0, mode, operand, 0, sign, ccr);
    if (kind == FPU_STORE_MOVE) {
        cpu_state.fpu.fpsr = 0x0a12ff88;
        cpu_state.fpu.fpiar = 0x88776655;
        if (variant)
            cpu_state.fpu.regs[0] = (Syn68kFP80){0x7fff | sign << 15, variant == 1 ? UINT64_C(0xc123456789abcdef)
                                                                                   : UINT64_C(0x8123456789abcdef)};
    }
    else if (kind == FPU_STORE_LIST)
        fpu_list_store(mode, masks[variant], sign, operand, whole);
    else if (kind == FPU_STORE_FRAME)
        fpu_frame_store(mode, variant + 2, operand, whole);
    else
        fpu_control_store(mode, 7, sign, operand, whole);
    unsigned next = control_replay_code(mode, operand, 0);
    if (kind == FPU_STORE_FRAME)
        code_word(0x10000, 0xf300 | mode << 3 | (mode == 7));
    /* FSAVE has no command word; its EA extension starts two bytes earlier. */
    if (kind == FPU_STORE_FRAME) {
        for (unsigned pc = 0x10002; pc < next - 2; pc += 2) {
            unsigned value = ram[physical(pc + 2)] << 8 | ram[physical(pc + 3)];
            code_word(pc, value);
        }
        next -= 2;
    }
    return next;
}

static void guest_fpu_pure_bus_replays(enum FPUStoreKind kind, unsigned completed, unsigned physical_bus)
{
    const unsigned modes[] = {2, 3, 4, 5, 6, 7};
    const unsigned list_sizes[] = {12, 24, 96}, frame_sizes[] = {52, 100};
    const unsigned statuses[] = {0, 15, 16, 31, 0x4000, 0x400f, 0x4010, 0x401f, 0x8000, 0x800f, 0x8010, 0x801f};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = kind == FPU_STORE_FRAME; privileged < 2; ++privileged)
            for (unsigned m = 0; m < sizeof modes / sizeof *modes; ++m)
                for (unsigned variant = 0; variant < (kind == FPU_STORE_LIST || kind == FPU_STORE_MOVE ? 3U
                                                      : kind == FPU_STORE_FRAME                        ? 2U
                                                                                                       : 1U);
                     ++variant)
                    for (unsigned sign = 0; sign < 2; ++sign)
                        for (unsigned c = 0; c < sizeof statuses / sizeof *statuses; ++c) {
                            unsigned whole = kind == FPU_STORE_LIST    ? list_sizes[variant]
                                             : kind == FPU_STORE_FRAME ? frame_sizes[variant]
                                                                       : 12;
                            for (unsigned lane = 0; lane < (physical_bus && !completed ? 4U : 1U); ++lane)
                                for (unsigned offset = 0; offset < whole - 8;) {
                                    unsigned size =
                                        (kind == FPU_STORE_LIST || kind == FPU_STORE_MOVE) && offset % 12 < 4 ? 2 : 4;
                                    if (completed >= size) {
                                        offset += size;
                                        continue;
                                    }
                                    unsigned fault = (physical_bus == 2 ? 0x28104 : 0x28000 + (page8k ? 8192 : 4096)) -
                                                     completed + lane,
                                             operand = fault - offset;
                                    unsigned mode = modes[m];
                                    if (kind != FPU_STORE_CONTROL && kind != FPU_STORE_MOVE && mode == 3) {
                                        offset += size;
                                        continue;
                                    }
                                    unsigned next = pure_store_setup(kind, page8k, privileged, mode, operand, variant,
                                                                     sign, statuses[c], whole);
                                    unsigned initial_a0 = EM_A0;
                                    Syn68kMappedCPU cpu = {
                                        .pc = 0x10000, .memory = &memory, .native = native_execution};
                                    Syn68kMappedRun run;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    unsigned traced = !!(statuses[c] & (kind == FPU_STORE_MOVE ? 0x8000 : 0xc000));
                                    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                           cpu.pc == next);
                                    if (traced)
                                        assert(run.step.vector == 9 && run.step.format == 2 &&
                                               run.step.fault_address == 0x10000);
                                    unsigned expected_a0 = EM_A0;
                                    Syn68kFPUState expected = cpu_state.fpu;
                                    unsigned expected_sr = status();
                                    unsigned char bytes[100];
                                    for (unsigned i = 0; i < whole; ++i)
                                        bytes[i] = ram[fpu_store_physical(operand + i)];
                                    pure_store_setup(kind, page8k, privileged, mode, operand, variant, sign,
                                                     statuses[c], whole);
                                    Syn68kFPUState at_fault = cpu_state.fpu;
                                    if (kind == FPU_STORE_MOVE)
                                        at_fault = expected;
                                    else if (kind != FPU_STORE_FRAME)
                                        at_fault.null_state = 0;
                                    unsigned fault_pte = pte(fault + completed), root = privileged ? 0x6000 : 0x3000;
                                    if (physical_bus) {
                                        memory.aligned_transfers = 1;
                                        fail_write_physical = fpu_store_physical(fault + completed);
                                    }
                                    else
                                        put32(fault_pte, 0x14005);
                                    watched_physical = fpu_store_physical(fault);
                                    watched_tail_physical = offset ? fpu_store_physical(operand) : UINT32_MAX;
                                    watched_tail_successes = 0;
                                    cpu.pc = 0x10000;
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                           run.step.store_state == 4 && cpu.pc == 0x10000 && EM_A0 == initial_a0 &&
                                           status() == expected_sr && watched_successes == !!completed &&
                                           watched_tail_successes == !!offset && !run.step.store_tail_valid &&
                                           !memcmp(&at_fault, &cpu_state.fpu, sizeof at_fault));
                                    unsigned value = 0;
                                    for (unsigned i = offset; i < offset + size; ++i)
                                        value = value << 8 | bytes[i];
                                    assert(run.step.access.size == size && run.step.access.request == fault &&
                                           run.step.access.value == value && run.step.access.completed == completed &&
                                           run.step.access.logical == fault + completed &&
                                           run.step.access.bus_error == !!physical_bus);
                                    for (unsigned i = 0; i < whole; ++i)
                                        assert(ram[fpu_store_physical(operand + i)] ==
                                               (i < offset + completed ? bytes[i] : 0xa5));
                                    fail_write_physical = UINT32_MAX;
                                    supervisor = 1;
                                    put32(pte(0x10000 + page_size), 0xe001);
                                    put32(pte(0x20000), 0x18001);
                                    unsigned table_page = root & ~(page_size - 1);
                                    put32(pte(0x30000), table_page | 1);
                                    unsigned target = 0x30000 + fault_pte - table_page;
                                    const unsigned program[] = {0x23fc,
                                                                1,
                                                                physical_bus == 2 ? 1U : 0x4001,
                                                                target >> 16,
                                                                target & 65535,
                                                                0xf518,
                                                                0x226f,
                                                                physical_bus ? 40U : 32U,
                                                                physical_bus ? 0x242f
                                                                : size == 2  ? 0x342f
                                                                             : 0x242f,
                                                                physical_bus ? 44U
                                                                : size == 2  ? 38U
                                                                             : 36U,
                                                                0x7600 | (privileged ? 5 : 1),
                                                                0x4e7b,
                                                                0x3001,
                                                                size == 2 ? 0x0e51 : 0x0e91,
                                                                0x2800,
                                                                0x5284,
                                                                0x4e73};
                                    unsigned handler = 0x10000 + page_size + 0x40;
                                    unsigned end = handler;
                                    for (unsigned i = 0; i < sizeof program / sizeof *program; ++i) {
                                        if (physical_bus && i == sizeof program / sizeof *program - 4) {
                                            const unsigned rotation[] = {0x2609, 0x0283, 0, 3, 0xe78b, 0xe7ba};
                                            for (unsigned j = 0; j < sizeof rotation / sizeof *rotation; ++j) {
                                                code_word(end, rotation[j]);
                                                end += 2;
                                            }
                                            if (size == 2) {
                                                code_word(end, 0x4842);
                                                end += 2;
                                            }
                                        }
                                        code_word(end, program[i]);
                                        end += 2;
                                    }
                                    cpu_state.vbr = 0x20000;
                                    put32(physical(cpu_state.vbr + 8), handler);
                                    Syn68kMappedDelivery delivery;
                                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                    unsigned frame = 0x20100 - 60, fc = privileged ? 5 : 1;
                                    unsigned data = value;
                                    if (physical_bus) {
                                        data <<= (4 - size) * 8;
                                        unsigned rotation = (fault & 3) * 8;
                                        if (rotation)
                                            data = data >> rotation | data << (32 - rotation);
                                    }
                                    assert(mapped_value(frame + 2, 4) == 0x10000 &&
                                           mapped_value(frame + 12, 2) ==
                                               ((physical_bus ? 0 : 0x400) | fc | (size == 2 ? 0x40 : 0) |
                                                (!physical_bus && completed ? 0x800 : 0)) &&
                                           mapped_value(frame + (physical_bus ? 18 : 16), 2) ==
                                               (0x80 | fc | (size == 2 ? 0x40 : 0)) &&
                                           !mapped_value(frame + (physical_bus ? 16 : 18), 2) &&
                                           !mapped_value(frame + 14, 2) && mapped_value(frame + 20, 4) == fault &&
                                           mapped_value(frame + (physical_bus ? 40 : 32), 4) == fault &&
                                           mapped_value(frame + (physical_bus ? 44 : 36), 4) == data);
                                    assert(syn68k_mapped_cpu_run(&cpu, 9 + (physical_bus ? 4 + (size == 2) : 0), &run));
                                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_D4 == 1 &&
                                           watched_successes == 1 + !!completed && watched_tail_successes == !!offset &&
                                           !memcmp(&at_fault, &cpu_state.fpu, sizeof at_fault));
                                    for (unsigned i = 0; i < whole; ++i)
                                        assert(ram[fpu_store_physical(operand + i)] ==
                                               (i < offset + size ? bytes[i] : 0xa5));
                                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                    assert(run.reason == (traced ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                           cpu.pc == next && EM_A0 == expected_a0 && status() == expected_sr &&
                                           watched_successes == 2 + !!completed &&
                                           watched_tail_successes == 2 * !!offset &&
                                           !memcmp(&expected, &cpu_state.fpu, sizeof expected));
                                    if (traced)
                                        assert(run.step.vector == 9 && run.step.format == 2 &&
                                               run.step.fault_address == 0x10000);
                                    for (unsigned i = 0; i < whole; ++i)
                                        assert(ram[fpu_store_physical(operand + i)] == bytes[i]);
                                    ++cases;
                                    offset += size;
                                }
                        }
    memory.aligned_transfers = 0;
    fail_write_physical = watched_physical = watched_tail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void guest_fpu_pure_replays(enum FPUStoreKind kind, unsigned completed)
{
    guest_fpu_pure_bus_replays(kind, completed, 0);
}

static void guest_fpu_physical_replays(void)
{
    for (unsigned bus = 1; bus <= 2; ++bus)
        for (unsigned completed = 0; completed < 4; ++completed) {
            guest_fpu_pure_bus_replays(FPU_STORE_MOVE, completed, bus);
            guest_fpu_pure_bus_replays(FPU_STORE_CONTROL, completed, bus);
            guest_fpu_pure_bus_replays(FPU_STORE_LIST, completed, bus);
            guest_fpu_pure_bus_replays(FPU_STORE_FRAME, completed, bus);
        }
}

static void fpu_auto_control_replay_bus_guards(unsigned physical_bus)
{
    for (unsigned mode = 3; mode <= 4; ++mode)
        for (unsigned mutation = 0; mutation < 6; ++mutation) {
            unsigned operand = physical_bus == 2 ? 0x28102 : 0x28ffe;
            pure_store_setup(FPU_STORE_CONTROL, 0, 1, mode, operand, 0, 0, 0, 12);
            if (physical_bus) {
                memory.aligned_transfers = 1;
                fail_write_physical = fpu_store_physical(operand + 2);
            }
            else
                put32(pte(0x29000), 0x14005);
            mutate_write_physical = fpu_store_physical(operand);
            const unsigned mutations[] = {1, 4, 8, 16, 32, 2};
            write_mutation = mutations[mutation];
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state);
            assert(write_mutated && run.step.access.completed == 2);
            unsigned expected_a0 = operand + (mode == 3 ? 12 : 0);
            assert(EM_A0 == (expected_a0 ^ (mutation == 4 ? 0x10 : 0)));
            CPUState before = cpu_state;
            unsigned old_reads = reads, old_writes = writes;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                   !memcmp(&before, &cpu_state, sizeof before));
            ++cases;
        }
    mutate_write_physical = replay_read_physical = UINT32_MAX;
    memory.aligned_transfers = 0;
    fail_write_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_auto_control_replay_guards(void)
{
    for (unsigned bus = 0; bus < 3; ++bus)
        fpu_auto_control_replay_bus_guards(bus);
}

static void fpu_numeric_replay_bus_guards(unsigned physical_bus)
{
    const unsigned mutations[] = {1, 4, 8, 16, 32, 2, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    for (unsigned mode = 2; mode <= 4; ++mode) {
        for (unsigned i = 0; i < sizeof mutations / sizeof *mutations; ++i) {
            unsigned operand = physical_bus == 2 ? 0x28103 : 0x28fff;
            pure_store_setup(FPU_STORE_MOVE, 0, 1, mode, operand, 0, 0, 0, 12);
            if (physical_bus) {
                memory.aligned_transfers = 1;
                fail_write_physical = fpu_store_physical(operand + 1);
            }
            else
                put32(pte(0x29000), 0x14005);
            mutate_write_physical = fpu_store_physical(operand);
            write_mutation = mutations[i];
            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
                   run.step.access.completed == 1 && write_mutated);
            unsigned mutation = mutations[i], expected_fpsr = 0x0a120088, expected_fpiar = 0x10000;
            if (mutation & 8) {
                expected_fpsr ^= 0x04000000;
                expected_fpiar ^= 0x04030201;
            }
            if (mutation & 64)
                expected_fpsr ^= 0x4000;
            if (mutation & 128)
                expected_fpiar ^= 2;
            unsigned expected_address = operand + (mode == 3 ? 12 : 0);
            assert(cpu_state.fpu.fpsr == expected_fpsr && cpu_state.fpu.fpiar == expected_fpiar &&
                   EM_A0 == (expected_address ^ (mutation & 32 ? 0x10 : 0)));
            if (mutation & 1)
                assert(cpu_state.fpu.regs[0].significand == (UINT64_C(0xa000000000000000) ^ UINT64_C(0x04030201)));
            if (mutation & 512)
                assert(cpu_state.fpu.frame_size == 52);
            if (mutation & 8192)
                assert(cpu_state.fpu.null_state == 1);
            CPUState before = cpu_state;
            unsigned old_reads = reads, old_writes = writes;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                   !memcmp(&before, &cpu_state, sizeof before));
            ++cases;
        }
        /* Enabled extended SNAN cannot produce an early access fault: the
           numeric exception suppresses stores, even on a protected page. */
        pure_store_setup(FPU_STORE_MOVE, 0, 1, mode, 0x29000, 2, 0, 0x8000, 12);
        cpu_state.fpu.fpcr |= 0x4000;
        if (physical_bus) {
            memory.aligned_transfers = 1;
            fail_write_physical = fpu_store_physical(0x29000);
        }
        else
            put32(pte(0x29000), 0x14005);
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 54 && !run.step.store_state && !writes &&
               cpu_state.fpu.frame_size == 100 && cpu_state.fpu.pending_vector == 54);
        ++cases;
    }
    mutate_write_physical = UINT32_MAX;
    memory.aligned_transfers = 0;
    fail_write_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_numeric_replay_guards(void)
{
    for (unsigned bus = 0; bus < 3; ++bus)
        fpu_numeric_replay_bus_guards(bus);
}

static void fpu_list_frame_replay_guards(enum FPUStoreKind kind)
{
    const unsigned mutations[] = {1, 4, 8, 16};
    for (unsigned guard = 0; guard < 10; ++guard) {
        unsigned mode = guard == 0 ? 4 : 2, whole = kind == FPU_STORE_LIST ? 24 : 52;
        unsigned fault = guard >= 8 ? 0x28fff : 0x29000, operand = fault - 4;
        unsigned trace = guard == 1 ? 0x4000 : guard == 2 || guard == 8 ? 0x8000 : 0;
        pure_store_setup(kind, 0, 1, mode, operand, kind == FPU_STORE_LIST, 0, trace, whole);
        if (guard == 1 || guard == 2 || guard == 8) {
            trace_read_physical = physical(0x10000);
            trace_read_sr = 0;
        }
        if (guard == 3 || guard == 9) {
            memory.aligned_transfers = 1;
            fail_write_physical = 0x14000;
        }
        else
            put32(pte(0x29000), 0x14005);
        if (guard == 0 || guard == 3 || guard == 9 || (guard >= 4 && guard <= 7)) {
            mutate_write_physical = fpu_store_physical(operand);
            write_mutation = guard == 0 ? 32 : guard == 3 || guard == 9 ? 4 : mutations[guard - 4];
        }
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
               run.step.exception_pc == 0x10000 && run.step.access.completed == (guard >= 8) &&
               write_mutated == (guard == 0 || guard == 3 || guard == 9 || (guard >= 4 && guard <= 7)));
        if (kind == FPU_STORE_FRAME)
            assert(cpu_state.fpu.frame_size == 52 && cpu_state.fpu.pending_vector);
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    memory.aligned_transfers = 0;
    fail_write_physical = mutate_write_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static unsigned fpu_tail_fc_setup(unsigned page8k, unsigned kind, unsigned mode)
{
    unsigned whole = fpu_tail_sizes[kind], boundary = 0x28000 + (page8k ? 8192 : 4096);
    unsigned operand = boundary - (whole - 8);
    fpu_store_setup(page8k, 1, kind ? 2 : 5, mode, operand, 0, 0, 15);
    if (kind == 2)
        fpu_list_store(mode, 0x81, 0, operand, whole);
    supervisor = 0;
    put32(pte(boundary), 0x1c001);
    supervisor = 1;
    memset(ram + 0x1c000, 0xa5, 8);
    cpu_state.usp = 0x20500;
    cpu_state.vbr = 0x20000;
    mutate_write_physical = watched_physical = 0x14000;
    watched_tail_physical = 0x1c004;
    watched_tail_successes = 0;
    write_mutation = 2;
    return operand;
}

static void fpu_tail_function_codes(void)
{
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned kind = 0; kind < 3; ++kind)
            for (unsigned mode = 2; mode <= 4; mode += 2) {
                unsigned whole = fpu_tail_sizes[kind];
                unsigned operand = fpu_tail_fc_setup(page8k, kind, mode);
                Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10004 && status() == 15 && EM_A7 == 0x20500 &&
                       write_mutated && watched_successes == 1 && watched_tail_successes == 1);
                Syn68kFPUState expected_fpu = cpu_state.fpu;
                unsigned expected_a0 = EM_A0;
                unsigned char expected[24];
                for (unsigned i = 0; i < whole; ++i)
                    expected[i] = ram[i < whole - 4 ? fpu_store_physical(operand + i) : 0x1c004 + i - (whole - 4)];
                assert(fpu_tail_fc_setup(page8k, kind, mode) == operand);
                fail_write_physical = 0x14000;
                cpu.pc = 0x10000;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.store_state == 2 &&
                       run.step.store_tail_valid && cpu.pc == 0x10004 && status() == 15 && EM_A7 == 0x20500 &&
                       EM_A0 == expected_a0 && write_mutated && !watched_successes && !watched_tail_successes &&
                       !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                fail_write_physical = UINT32_MAX;
                const unsigned program[] = {0x226f, 40,     0x242f, 44,     0x7605, 0x4e7b, 0x3001,
                                            0x0e91, 0x2800, 0x226f, 32,     0x242f, 36,     0x7601,
                                            0x4e7b, 0x3001, 0x0e91, 0x2800, 0x4e73};
                for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                    code_word(0x10040 + i * 2, program[i]);
                put32(physical(cpu_state.vbr + 8), 0x10040);
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                unsigned frame = 0x20100 - 60;
                assert(mapped_value(frame + 18, 2) == 0x85 && mapped_value(frame + 16, 2) == 0x81);
                assert(syn68k_mapped_cpu_run(&cpu, 11, &run));
                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10004 && status() == 15 && EM_A7 == 0x20500 &&
                       cpu_state.isp == 0x20100 && EM_A0 == expected_a0 && watched_successes == 1 &&
                       watched_tail_successes == 1 && !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu));
                for (unsigned i = 0; i < whole; ++i)
                    assert(ram[i < whole - 4 ? fpu_store_physical(operand + i) : 0x1c004 + i - (whole - 4)] ==
                           expected[i]);
                for (unsigned i = 0; i < 4; ++i)
                    assert(ram[0x14004 + i] == 0xa5 && ram[0x1c000 + i] == 0xa5);
                ++cases;
            }
    mutate_write_physical = fail_write_physical = watched_physical = watched_tail_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_final_writeback_guards(void)
{
    const struct {
        unsigned format;
        int offset;
        unsigned failed_offset, exception;
    } forms[] = {{2, 0, 0, 0}, {2, -2, 2, 0}};

    for (unsigned i = 0; i < sizeof forms / sizeof *forms; ++i) {
        unsigned operand = 0x29000 + forms[i].offset;
        fpu_store_setup(0, 1, forms[i].format, 2, operand, 0, 0, 15);
        fpu_store_exception(forms[i].exception);
        fail_write_physical = fpu_store_physical(operand + forms[i].failed_offset);
        mutate_write_physical = fail_write_physical;
        write_mutation = 4;
        unsigned d2 = EM_D2, d3 = EM_D3, a1 = EM_A1;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
               run.step.exception_pc == 0x10000 && cpu.pc == 0x10000);
        if (forms[i].exception)
            assert(cpu_state.fpu.frame_size == 100 &&
                   cpu_state.fpu.pending_vector == (forms[i].exception == 3 ? 53U : 49U) &&
                   (cpu_state.fpu.frame[0x49] & 0x10));
        assert(write_mutated && EM_D2 == (d2 ^ 0x04030201) && EM_D3 == (d3 ^ 0x04030201) && EM_A1 == (a1 ^ 0x100));
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fail_write_physical = mutate_write_physical = watched_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_list_writeback_guards(void)
{
    const unsigned offsets[] = {0, 2, 4, 8};
    for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i) {
        unsigned operand = 0x29000;
        fpu_store_setup(0, 1, 2, 2, operand, 0, 0, 15);
        fpu_list_store(2, 0x81, 0, operand, 24);
        fail_write_physical = fpu_store_physical(operand + offsets[i]);
        watched_physical = fail_write_physical;
        mutate_write_physical = fail_write_physical;
        write_mutation = 4;
        unsigned d2 = EM_D2, d3 = EM_D3, a1 = EM_A1;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
               !watched_successes && run.step.exception_pc == 0x10000 && cpu.pc == 0x10000);
        assert(write_mutated && EM_D2 == (d2 ^ 0x04030201) && EM_D3 == (d3 ^ 0x04030201) && EM_A1 == (a1 ^ 0x100));
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fail_write_physical = mutate_write_physical = watched_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_control_writeback_guards(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned offsets[] = {0, 0};
        unsigned operand = 0x29000 + (i ? 2 : 0);
        fpu_store_setup(0, 1, 0, 2, operand, 0, 0, 15);
        fpu_control_store(2, 7, 0, operand, 12);
        fail_write_physical = fpu_store_physical(operand + offsets[i]);
        watched_physical = fail_write_physical;
        mutate_write_physical = fail_write_physical;
        write_mutation = 4;
        unsigned d2 = EM_D2, d3 = EM_D3, a1 = EM_A1;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
               !watched_successes && run.step.exception_pc == 0x10000 && cpu.pc == 0x10000);
        assert(write_mutated && EM_D2 == (d2 ^ 0x04030201) && EM_D3 == (d3 ^ 0x04030201) && EM_A1 == (a1 ^ 0x100));
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fail_write_physical = mutate_write_physical = watched_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_frame_writeback_guards(void)
{
    const unsigned kinds[] = {2, 3, 3};
    const unsigned sizes[] = {52, 100, 100};
    const unsigned offsets[] = {0, 0, 88};
    for (unsigned i = 0; i < sizeof kinds / sizeof *kinds; ++i) {
        unsigned operand = 0x29000;
        fpu_store_setup(0, 1, 2, 2, operand, kinds[i], i & 1, 15);
        fpu_frame_store(2, kinds[i], operand, sizes[i]);
        Syn68kFPUState pending = cpu_state.fpu;
        fail_write_physical = fpu_store_physical(operand + offsets[i]);
        watched_physical = fail_write_physical;
        mutate_write_physical = fail_write_physical;
        write_mutation = 4;
        unsigned d2 = EM_D2, d3 = EM_D3, a1 = EM_A1;
        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
        Syn68kMappedRun run;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.step.store_state &&
               !watched_successes && run.step.exception_pc == 0x10000 && cpu.pc == 0x10000 &&
               !memcmp(&pending, &cpu_state.fpu, sizeof pending));
        assert(write_mutated && EM_D2 == (d2 ^ 0x04030201) && EM_D3 == (d3 ^ 0x04030201) && EM_A1 == (a1 ^ 0x100));
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    fpu_store_setup(0, 1, 2, 2, 0x29000, 3, 0, 15);
    fpu_frame_store(2, 3, 0x29000, 100);
    Syn68kFPUState pending = cpu_state.fpu;
    fail_write_physical = fpu_store_physical(0x29000 + 96);
    Syn68kFPUStep component;
    assert(syn68k_fpu_step_decoded(0x10000, 0xf310, &memory, &component));
    assert(component.vector == 2 && component.next == 0x10000 && component.exception_pc == 0x10000 &&
           component.store_state == 1 && component.store_next == 0x10002 && component.store_frame_clear == 1 &&
           !memcmp(&pending, &cpu_state.fpu, sizeof pending));
    ++cases;
    fail_write_physical = mutate_write_physical = watched_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

static void fpu_post_writeback_record_guards(void)
{
    for (unsigned invalid = 0; invalid < 16; ++invalid) {
        fixture(0, 1);
        syn68k_fpu_reset();
        cpu_state.fpu.pending_vector = 49;
        cpu_state.fpu.frame_size = 100;
        cpu_state.isp = EM_A7 = 0x20100;
        Syn68kIntegerStep record = {.vector = 2,
                                    .format = 7,
                                    .exception_pc = 0x10004,
                                    .store_state = 2,
                                    .store_next = 0x10004,
                                    .fpu_vector = 49,
                                    .fpu_ea = 0x28000,
                                    .access = {.translation = {.fault = MMU040_INVALID},
                                               .logical = 0x28000,
                                               .request = 0x28000,
                                               .size = 4,
                                               .writing = 1,
                                               .fc = 5}};
        if (invalid >= 8) {
            record.fpu_vector = 0;
            record.store_tail_valid = 1;
            record.store_tail_fc = 5;
        }
        switch (invalid) {
        case 0:
            record.fpu_vector = 47;
            break;
        case 1:
            record.fpu_vector = 56;
            break;
        case 2:
            record.store_state = 3;
            break;
        case 3:
            record.format = 0;
            break;
        case 4:
            record.movem_state = 1;
            break;
        case 5:
            record.access.writing = 0;
            record.restartable_read = 1;
            break;
        case 6:
            record.store_next |= 1;
            break;
        case 7:
            record.access.rmw = 1;
            break;
        case 8:
            record.store_tail_valid = 2;
            break;
        case 9:
            record.access.size = 2;
            break;
        case 10:
            record.store_state = 1;
            break;
        case 11:
            record.format = 0;
            break;
        case 12:
            record.store_tail_fc = 0;
            break;
        case 13:
            record.store_tail_fc = 2;
            break;
        case 14:
            record.store_tail_fc = 6;
            break;
        case 15:
            record.store_tail_fc = 7;
            break;
        }
        CPUState before = cpu_state;
        unsigned old_reads = reads, old_writes = writes;
        Syn68kMappedCPU cpu = {.pc = 0x10004, .memory = &memory, .native = native_execution};
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
               !memcmp(&before, &cpu_state, sizeof before));
        ++cases;
    }
    syn68k_fpu_reset();
}

static void partial_writeback_record_guards(void)
{
    for (unsigned physical = 0; physical < 2; ++physical)
        for (unsigned invalid = 0; invalid < 14; ++invalid) {
            fixture(0, 1);
            syn68k_fpu_reset();
            cpu_state.isp = EM_A7 = 0x20100;
            Syn68kIntegerStep record = {
                .vector = 2,
                .format = 7,
                .exception_pc = 0x10004,
                .store_state = 2,
                .store_next = 0x10004,
                .access = {.translation = {.fault = physical ? MMU040_OK : MMU040_WRITE_PROTECTED},
                           .bus_error = physical,
                           .request = 0x28ffe,
                           .logical = 0x29000,
                           .completed = 2,
                           .size = 4,
                           .fc = 5,
                           .writing = 1}};
            switch (invalid) {
            case 0:
                record.access.completed = 0;
                break;
            case 1:
                record.access.completed = 4;
                break;
            case 2:
                record.access.logical += 1;
                break;
            case 3:
                record.access.bus_error = !physical;
                break;
            case 4:
                record.access.translation.fault = MMU040_BAD_REQUEST;
                record.access.bus_error = 1;
                break;
            case 5:
                record.store_state = 1;
                break;
            case 6:
                record.access.size = 1;
                break;
            case 7:
                record.store_locked = record.access.rmw = 1;
                break;
            case 8:
                record.access.writing = 0;
                break;
            case 9:
                record.format = 0;
                break;
            case 10:
                record.access.fc = 2;
                break;
            case 11:
                record.access.logical = record.access.request;
                break;
            case 12:
                record.access.request = 0x28000;
                record.access.logical = 0x28002;
                if (physical)
                    record.access.rmw = 1;
                break;
            case 13:
                record.restartable_read = 1;
                break;
            }
            CPUState before = cpu_state;
            unsigned old_reads = reads, old_writes = writes;
            Syn68kMappedCPU cpu = {.pc = 0x10004, .memory = &memory, .native = native_execution};
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && reads == old_reads && writes == old_writes &&
                   !memcmp(&before, &cpu_state, sizeof before));
            ++cases;
        }
    syn68k_fpu_reset();
}

static void guest_fpu_mmu_traces(void)
{
    static const struct {
        unsigned opcode, command, extra, next, flow, privileged, vector, format;
    } instructions[] = {
        {0xf200, 0x0022, 0, 4, 0, 0, 0, 0}, // Exact FADD.
        {0xf240, 15, 0, 4, 0, 0, 0, 0},     // FScc D0.
        {0xf280, 2, 0, 4, 0, 0, 0, 0},      // Untaken FBcc, also the FNOP family.
        {0xf28f, 2, 0, 4, 1, 0, 0, 0},      // Taken FBcc targets its fallthrough.
        {0xf248, 15, 2, 6, 1, 0, 0, 0},     // FDBcc condition true.
        {0xf248, 0, 2, 6, 1, 0, 0, 0},      // FDBcc exhausted counter.
        {0xf248, 0, 2, 6, 1, 0, 0, 0},      // Taken FDBcc targets its fallthrough.
        {0xf200, 0xb000, 0, 4, 1, 0, 0, 0}, // Single control register is an FMOVEM alias.
        {0xf200, 0x9000, 0, 4, 1, 0, 0, 0},  {0xf210, 0xf080, 0, 4, 1, 0, 0, 0}, // FMOVEM data register.
        {0xf310, 0x4e71, 0, 2, 1, 1, 0, 0},  {0xf350, 0x4e71, 0, 2, 1, 1, 0, 0},
        {0x4e7a, 0, 0, 4, 1, 1, 0, 0}, // MOVEC read/write SFC.
        {0x4e7b, 0, 0, 4, 1, 1, 0, 0},       {0xf548, 0x4e71, 0, 2, 1, 1, 0, 0},
        {0xf518, 0x4e71, 0, 2, 1, 1, 0, 0},  {0xf27c, 15, 0, 4, 0, 0, 7, 2}, // Forced exceptions own trace handling.
        {0xf200, 0x5c00, 0, 4, 0, 0, 11, 2}, {0xf240, 32, 0, 4, 0, 0, 11, 0},
        {0xf240, 16, 0, 4, 0, 0, 48, 0},     {0xf210, 0x6000, 0, 4, 0, 0, 49, 3},
    };

    const unsigned ccrs[] = {0, 31};
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned entry_trace = 0; entry_trace < 3; ++entry_trace)
                for (unsigned live_trace = 0; live_trace < 3; ++live_trace)
                    for (unsigned i = 0; i < sizeof instructions / sizeof *instructions; ++i)
                        for (unsigned c = 0; c < sizeof ccrs / sizeof *ccrs; ++c) {
                            unsigned no_callback = (instructions[i].privileged && !privileged) || i == 15;
                            if (no_callback && live_trace != entry_trace)
                                continue;
                            fpu_store_setup(page8k, privileged, 0, 2, 0x28000, 0, 0, entry_trace << 14 | ccrs[c]);
                            code_word(0x10000, instructions[i].opcode);
                            code_word(0x10002, instructions[i].command);
                            code_word(0x10004, instructions[i].extra);
                            EM_D0 = i == 5 ? 0 : i == 6 ? 1 : 0x30;
                            cpu_state.sfc = cpu_state.dfc = privileged ? 5 : 1;
                            cpu_state.vbr = 0x20000;
                            if (i == 11)
                                put32(fpu_store_physical(0x28000), 0x41000000);
                            if (i == 19) {
                                cpu_state.fpu.fpcr = 0x8000;
                                cpu_state.fpu.fpsr = 0x01000000;
                            }
                            if (i == 20)
                                cpu_state.fpu.fpcr = 0x200;
                            trace_bus_kind = i == 10 ? 1 : i == 14 ? 2 : 0;
                            trace_read_physical = no_callback          ? UINT32_MAX
                                                  : i == 10 || i == 11 ? fpu_store_physical(0x28000)
                                                  : i == 14            ? pte(0x28000)
                                                                       : physical(0x10002);
                            trace_read_sr = live_trace << 14;
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            unsigned vector = instructions[i].privileged && !privileged ? 8 : instructions[i].vector;
                            unsigned format = vector == 8 ? 0 : instructions[i].format;
                            unsigned next = 0x10000 + instructions[i].next;
                            unsigned expected_pc = vector && !format ? 0x10000 : next;
                            unsigned trace =
                                !vector && (entry_trace == 2 || (entry_trace == 1 && instructions[i].flow));
                            if (trace)
                                vector = 9, format = 2;
                            int valid = trace_read_physical == UINT32_MAX && cpu.pc == expected_pc &&
                                        run.reason == (vector ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                        run.step.vector == vector && run.step.format == format &&
                                        run.completed == !vector;
                            if (!valid)
                                fprintf(
                                    stderr,
                                    "trace case %u native=%u priv=%u entry=%u live=%u: PC=%x/%x vector=%u/%u format=%u/%u reason=%u completed=%u\n",
                                    i, native_execution, privileged, entry_trace, live_trace, cpu.pc, expected_pc,
                                    run.step.vector, vector, run.step.format, format, run.reason, run.completed);
                            assert(valid);
                            unsigned live_sr = (privileged ? 0x2000 : 0) | live_trace << 14 | ccrs[c];
                            assert(status() == live_sr);
                            if (trace) {
                                assert(run.step.exception_pc == next && run.step.fault_address == 0x10000);
                                supervisor = 1;
                                put32(pte(0x20000), 0x18001);
                                put32(pte(0x10000 + page_size), 0xe001);
                                unsigned handler = 0x10000 + page_size + 0x40;
                                put32(physical(cpu_state.vbr + 36), handler);
                                code_word(handler, 0x4e73);
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE);
                                unsigned frame = 0x20100 - 12;
                                assert(mapped_value(frame, 2) == live_sr && mapped_value(frame + 2, 4) == next &&
                                       mapped_value(frame + 6, 2) == 0x2024 && mapped_value(frame + 8, 4) == 0x10000);
                                assert(!(cpu_state.sr & 0xc000));
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == next && status() == live_sr);
                            }
                            ++cases;
                        }
    trace_read_physical = UINT32_MAX;
    set_status(0x2000);
    syn68k_fpu_reset();
}

/* MC68040UM 8.2.6: T1 applies to normal completion; these T0 classes
   serialize the pipeline even though their next PC is linear. */
static void guest_integer_traces(void)
{
    const struct {
        unsigned opcode, extension, words, serial, privileged;
    } forms[] = {{0x4e71, 0, 1, 1, 0}, {0x7001, 0, 1, 0, 0},      {0x027c, 0xffff, 2, 1, 1}, {0x007c, 0, 2, 1, 1},
                 {0x0a7c, 0, 2, 1, 1}, {0x46fc, 0x2000, 2, 1, 1}, {0x46c0, 0, 1, 1, 1},      {0x4e60, 0, 1, 1, 1},
                 {0x4e68, 0, 1, 1, 1}, {0x4e76, 0, 1, 0, 0},      {0x08c0, 0, 2, 0, 0}};

    unsigned initial = cases;
    for (unsigned page8k = 0; page8k < 2; ++page8k)
        for (unsigned privileged = 0; privileged < 2; ++privileged)
            for (unsigned f = 0; f < sizeof forms / sizeof *forms; ++f)
                for (unsigned trace = 0; trace < 4; ++trace)
                    for (unsigned live = 0; live < 5; ++live) {
                        fixture(page8k, privileged);
                        set_status((privileged ? 0x2000 : 0) | trace << 14 | 17);
                        cpu_state.usp = 0x20080;
                        EM_D0 = 0x2000;
                        EM_A0 = 0x20040;
                        code_word(0x10000, forms[f].opcode);
                        if (forms[f].words == 2)
                            code_word(0x10002, forms[f].extension);
                        if (live < 4) {
                            trace_read_physical = physical(0x10000);
                            trace_read_sr = live << 14;
                        }
                        unsigned expected_sr = (privileged ? 0x2000 : 0) | (live < 4 ? live : trace) << 14 | 17;
                        M68kReg expected_regs[16];
                        memcpy(expected_regs, cpu_state.regs, sizeof expected_regs);
                        Syn68kFPUState expected_fpu = cpu_state.fpu;
                        unsigned expected_usp = cpu_state.usp;
                        unsigned denied = forms[f].privileged && !privileged;
                        unsigned vector = denied ? 8 : (trace & 2) || ((trace & 1) && forms[f].serial) ? 9 : 0;
                        unsigned next = denied ? 0x10000 : 0x10000 + forms[f].words * 2;
                        if (!denied) {
                            if (forms[f].opcode == 0x7001) {
                                expected_regs[0].ul.n = 1;
                                expected_sr &= ~15u;
                            }
                            else if (forms[f].opcode == 0x08c0) {
                                expected_regs[0].ul.n |= 1;
                                expected_sr |= 4;
                            }
                            else if (forms[f].opcode == 0x46fc || forms[f].opcode == 0x46c0)
                                expected_sr = 0x2000;
                            else if (forms[f].opcode == 0x4e60)
                                expected_usp = EM_A0;
                            else if (forms[f].opcode == 0x4e68)
                                expected_regs[8].ul.n = expected_usp;
                        }
                        Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = native_execution};
                        Syn68kMappedRun run;
                        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                        if (run.step.vector != vector || cpu.pc != next || status() != expected_sr)
                            fprintf(stderr,
                                    "Integer trace op=%04x entry=%u live=%u user=%u vectors=%u/%u PC=%x/%x SR=%x/%x\n",
                                    forms[f].opcode, trace, live, !privileged, run.step.vector, vector, cpu.pc, next,
                                    status(), expected_sr);
                        assert(run.step.vector == vector && cpu.pc == next && status() == expected_sr);
                        assert(run.reason == (vector ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                        assert(run.completed == !vector);
                        if (vector)
                            assert(run.step.format == (vector == 9 ? 2 : 0) && run.step.exception_pc == next &&
                                   run.step.fault_address == (vector == 9 ? 0x10000 : 0));
                        assert(!memcmp(expected_regs, cpu_state.regs, sizeof expected_regs) &&
                               !memcmp(&expected_fpu, &cpu_state.fpu, sizeof expected_fpu) &&
                               cpu_state.usp == expected_usp && !writes);
                        assert(trace_read_physical == UINT32_MAX);
                        ++cases;
                    }
    printf("%u integer T1/serializing-T0/entry-SR/privilege cases passed\n", cases - initial);
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    if (argc == 3) {
        uint64 emitted_before = syn68k_native_compile_count(), hits_before = syn68k_aot_hit_count();
        native_execution = 1;
        Syn68kMMU040Native native;
        for (unsigned emitted = 0; emitted < 2; ++emitted) {
            memory.native = NULL;
            if (emitted)
                assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
            guest_scalar_writebacks();
            guest_partial_scalar_writebacks();
            guest_scalar_trace_writebacks();
            read_callback_guards();
            rte_address_callback_restarts();
            moves_streams();
            stack_store_completions();
            guest_control_branches();
            guest_integer_traces();
            guest_fpu_branch_prefetches();
            guest_boundary_priority();
            guest_call_writebacks();
            call_replay_guards();
            guest_fpu_physical_writebacks();
            guest_fpu_physical_replays();
            partial_writeback_record_guards();
            for (unsigned reg = 0; reg < 8; ++reg)
                guest_fpu_control_replays(reg);
            fpu_control_replay_guards();
            fpu_control_replay_record_guards();
            fpu_auto_control_replay_guards();
            fpu_numeric_replay_guards();
            for (unsigned completed = 0; completed < 4; ++completed) {
                guest_fpu_pure_replays(FPU_STORE_CONTROL, completed);
                guest_fpu_pure_replays(FPU_STORE_MOVE, completed);
                guest_fpu_pure_replays(FPU_STORE_LIST, completed);
                guest_fpu_pure_replays(FPU_STORE_FRAME, completed);
            }
            fpu_list_frame_replay_guards(FPU_STORE_LIST);
            fpu_list_frame_replay_guards(FPU_STORE_FRAME);
        }
        uint64 emissions = syn68k_native_compile_count() - emitted_before;
        uint64 hits = syn68k_aot_hit_count() - hits_before;
        assert(atoi(argv[2]) ? !emissions && hits : emissions);
        printf("%u guest mapped recovery cases passed; emitted=%llu hits=%llu\n", cases, (unsigned long long)emissions,
               (unsigned long long)hits);
        syn68k_x64_mapped_clear();
        return 0;
    }
    for (native_execution = 0; native_execution < 2; ++native_execution) {
        memory.native = NULL;
        Syn68kMMU040Native native;
        for (unsigned emitted = 0; emitted < 2; ++emitted) {
            if (emitted)
                assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
            enable_from_guest();
            streams();
            single_fetch_and_coherence();
            stops_and_faults();
            exception_roundtrips();
            exception_delivery_failures();
            guest_exception_handlers();
            guest_page_fault_handlers();
            fetch_frame_guards();
            guest_operand_page_fault_handlers();
            guest_scalar_writebacks();
            guest_partial_scalar_writebacks();
            guest_scalar_trace_writebacks();
            read_callback_guards();
            rte_address_callback_restarts();
            moves_streams();
            stack_store_completions();
            guest_control_branches();
            guest_integer_traces();
            guest_fpu_branch_prefetches();
            guest_boundary_priority();
            guest_call_writebacks();
            call_replay_guards();
            guest_partial_read_restarts();
            guest_movem_read_continuations();
            movem_fetch_record_guards();
            guest_fpu_read_restarts();
            fpu_read_restart_guards();
            rte_fpu_continuations();
            rte_fpu_continuation_guards();
            rte_header_only();
            guest_rte_header_restarts();
            guest_rte_progress_restarts();
            rte_user_progress_guards();
            rte_callback_restarts();
            guest_locked_read_restarts();
            guest_locked_writebacks();
            locked_writeback_guards();
            locked_writeback_record_guards();
            locked_read_restart_guards();
            for (unsigned exception = 0; exception < 5; ++exception)
                for (unsigned tail = 0; tail < 2; ++tail)
                    guest_fpu_final_writebacks(exception, tail, FPU_STORE_MOVE);
            guest_fpu_final_writebacks(0, 0, FPU_STORE_CONDITION);
            guest_fpu_final_writebacks(0, 0, FPU_STORE_LIST);
            guest_fpu_final_writebacks(0, 1, FPU_STORE_LIST);
            guest_fpu_final_writebacks(0, 0, FPU_STORE_CONTROL);
            guest_fpu_final_writebacks(0, 1, FPU_STORE_CONTROL);
            guest_fpu_final_writebacks(0, 0, FPU_STORE_FRAME);
            guest_fpu_final_writebacks(0, 1, FPU_STORE_FRAME);
            guest_fpu_unaligned_writebacks();
            guest_fpu_partial_atc_writebacks();
            guest_fpu_physical_writebacks();
            guest_fpu_physical_replays();
            guest_fpu_mmu_traces();
            guest_fpu_trace_writebacks();
            for (unsigned reg = 0; reg < 8; ++reg)
                guest_fpu_control_replays(reg);
            fpu_control_replay_guards();
            fpu_control_replay_record_guards();
            fpu_auto_control_replay_guards();
            fpu_numeric_replay_guards();
            for (unsigned completed = 0; completed < 4; ++completed) {
                guest_fpu_pure_replays(FPU_STORE_LIST, completed);
                guest_fpu_pure_replays(FPU_STORE_FRAME, completed);
                guest_fpu_pure_replays(FPU_STORE_CONTROL, completed);
                guest_fpu_pure_replays(FPU_STORE_MOVE, completed);
            }
            fpu_list_frame_replay_guards(FPU_STORE_LIST);
            fpu_list_frame_replay_guards(FPU_STORE_FRAME);
            fpu_tail_function_codes();
            fpu_final_writeback_guards();
            fpu_list_writeback_guards();
            fpu_control_writeback_guards();
            fpu_frame_writeback_guards();
            fpu_post_writeback_record_guards();
            partial_writeback_record_guards();
        }
        /* No injected callback faults in these successful streams: ordinary RAM
           may bypass every descriptor and operand callback. */
        native.ram = (Syn68kNative040RAM){ram, 0, sizeof ram};
        unsigned outer_attempted = 0;
        native.write_attempted = &outer_attempted;
        streams();
        single_fetch_and_coherence();
        assert(memory.native == &native && native.write_attempted == &outer_attempted && !outer_attempted);
    }
    printf("%u mapped stream/MMU/FPU/handler/RTE/delivery-fault cases passed.\n", cases);
    return 0;
}
