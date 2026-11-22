/*+===================================================================
  File:      mapped-fpu-source-test.c

  Summary:   Compare mapped native FPU memory inputs with checked execution and single-consumption exits.

  Classes:   None.

  Functions: main, __wrap_syn68k_fpu_step_decoded, __wrap_syn68k_fpu_finish_command,
             __wrap_syn68k_fpu_finish_command_at, __wrap_syn68k_fpu_finish_captured,
             __wrap_syn68k_effective_address and __wrap_syn68k_mmu040_translate;
             imports fpu_source_command_fixture from the command-test fixture.

  Origin:    Original ARDI Executor direct mapped FPU regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_source_command_fixture(void);
#define main fpu_source_command_fixture
#include "fpu-command-test.c"
#undef main
#include "jit-x64.h"
#include "mapped-cpu.h"
#include "ea.h"
#include <stdlib.h>

static unsigned forbid_ea;
static unsigned native_active, captures, command_exits, fault_call, source_kind, native_bus, through_cpu;
static Syn68kMMU040Native native_memory;
static unsigned paging, page8k, bank, direct_ram, warm, crossing, invalid_page;
static unsigned reenter, callbacks, indexed_extension;
static unsigned target_kind, enables;
static unsigned mutate_control;
static unsigned wrap_pc;
static unsigned char table[0x3000], expected_table[sizeof table];

static int source_table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == table && address >= 0x10000 && address <= 0x13000 - 4);
    address -= 0x10000;
    *value = 0;
    for (unsigned i = 0; i < 4; ++i)
        *value = *value << 8 | table[address + i];
    return 1;
}

static int source_table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    uint32_t old;
    assert(source_table_read(opaque, address, &old));
    if (locked)
        value |= old;
    address -= 0x10000;
    for (unsigned i = 0; i < 4; ++i)
        table[address + i] = value >> ((3 - i) * 8);
    return 1;
}

int __real_syn68k_fpu_finish_captured(const Syn68kFPUCaptured*, Syn68kFPUStep*);
int __real_syn68k_fpu_step_decoded(uint32_t, unsigned, Syn68kMMU040Memory*, Syn68kFPUStep*);
int __real_syn68k_fpu_finish_command(uint32_t, unsigned, unsigned, Syn68kMMU040Memory*, Syn68kFPUStep*);
Syn68kEA __real_syn68k_effective_address(Syn68kEAContext*, unsigned, unsigned, int, int);
Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

int __wrap_syn68k_fpu_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* bus, Syn68kFPUStep* result)
{
    assert(!native_active || (native_active == 2 && cpu_state.fpu.frame_size));
    unsigned saved = native_active;
    native_active = 0;
    int ok = __real_syn68k_fpu_step_decoded(pc, opcode, bus, result);
    native_active = saved;
    return ok;
}

int __wrap_syn68k_fpu_finish_command(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* bus,
                                     Syn68kFPUStep* result)
{
    ++command_exits;
    /* Only the explicitly tested unsupported command cases may use this exit. */
    assert(native_active == 2);
    native_active = 0;
    int ok = __real_syn68k_fpu_finish_command(pc, opcode, command, bus, result);
    native_active = 2;
    return ok;
}

int __real_syn68k_fpu_finish_command_at(uint32_t, unsigned, unsigned, Syn68kMMU040Memory*, const Syn68kFPUEntry*,
                                        Syn68kFPUStep*);

int __wrap_syn68k_fpu_finish_command_at(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* bus,
                                        const Syn68kFPUEntry* entry, Syn68kFPUStep* result)
{
    ++command_exits;
    assert(native_active == 2);
    native_active = 0;
    int ok = __real_syn68k_fpu_finish_command_at(pc, opcode, command, bus, entry, result);
    native_active = 2;
    return ok;
}

int __wrap_syn68k_fpu_finish_captured(const Syn68kFPUCaptured* captured, Syn68kFPUStep* result)
{
    assert(native_active);
    ++captures;
    return __real_syn68k_fpu_finish_captured(captured, result);
}

Syn68kEA __wrap_syn68k_effective_address(Syn68kEAContext* context, unsigned code, unsigned size, int writing,
                                         int registers)
{
    assert(!native_active && !forbid_ea);
    return __real_syn68k_effective_address(context, code, size, writing, registers);
}

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* state, const Syn68kMMU040Bus* bus, uint32_t address,
                                                  unsigned fc, int writing)
{
    assert(!native_active);
    return __real_syn68k_mmu040_translate(state, bus, address, fc, writing);
}

static int source_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                       unsigned fc)
{
    int ok = read_operand(opaque, address, bytes, size, attributes, fc);
    if (ok && mutate_control && address == (mutate_control == 1 ? start + 2 : 0x6000)) {
        if (mutate_control == 3)
            cpu_state.sr ^= 0x2000;
        else {
            cpu_state.fpu.fpcr = mutate_control == 1 ? 0x30 : 0x230;
            cpu_state.fpu.null_state = 1;
        }
    }
    if (ok && reenter && address == 0x6000) {
        reenter = 0;
        ++callbacks;
        CPUState saved = cpu_state;
        Syn68kFPUStep child;
        assert(syn68k_x64_mapped_fpu(0x1020, 0xf200, &memory, &child) == 1);
        assert(!child.vector && child.next == 0x1024);
        syn68k_x64_mapped_clear(); // Child and parent deliberately share a cache bucket.
        cpu_state = saved;
        reenter = 1;
    }
    return fault_call && transfer_count == fault_call ? 0 : ok;
}

static void setup(unsigned ea, unsigned command, unsigned mode)
{
    fixture(command, 0);
    if (wrap_pc) {
        start = UINT32_MAX - 1;
        put(0, command, 2);
    }
    memory.native = NULL;
    memory.tables = (Syn68kMMU040Bus){0};
    memory.read = source_read;
    cpu_state.sr = mode & 1 ? 0x2000 : 0;
    cpu_state.fpu.fpcr = ((mode & 3) << 4) | (((mode >> 2) & 3) << 6);
    cpu_state.fpu.fpcr |= enables;
    cpu_state.fpu.null_state = mode & 1;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.regs[i].ul.n = 0;
        cpu_state.regs[i + 8].ul.n = crossing ? 0x5fff : 0x6000;
        cpu_state.fpu.regs[i] = (Syn68kFP80){0x4000, UINT64_C(0x8000000000000000)};
        if (target_kind == 1)
            cpu_state.fpu.regs[i] = (Syn68kFP80){0x7fff, UINT64_C(0xa000000000000000)};
        else if (target_kind == 2)
            cpu_state.fpu.regs[i] = (Syn68kFP80){0, 1};
        else if (target_kind == 3)
            cpu_state.fpu.regs[i] = (Syn68kFP80){0x7ffe, UINT64_MAX};
    }
    if (!wrap_pc)
        put(start, 0xf200 | ea, 2);
    put(0x1022, 0x0022, 2);
    unsigned format = command >> 10 & 7;
    const unsigned sizes[] = {4, 4, 12, 12, 2, 8, 1, 12};
    uint32_t address = crossing ? 0x5fff : 0x6000;
    switch (ea >> 3) {
    case 4:
        address -= sizes[format] == 1 && (ea & 7) == 7 ? 2 : sizes[format];
        break;
    case 5:
        put(start + 4, 0x10, 2);
        address += 0x10;
        break;
    case 6:
        if (indexed_extension) {
            put(start + 4, indexed_extension, 2);
            put(start + 6, 0x10, 2);
            put(start + 8, 0x20, 2);
            address += 0x10;
            put(address, 0x6100, 4);
            address = 0x6100;
        }
        else {
            put(start + 4, 0x10, 2); // D0.W plus displacement, with D0 initially zero.
            address += 0x10;
        }
        break;
    case 7:
        switch (ea & 7) {
        case 0:
            put(start + 4, address, 2);
            break;
        case 1:
            put(start + 4, address, 4);
            break;
        case 2:
            put(start + 4, address - (start + 4), 2);
            break;
        case 3:
            put(start + 4, 0x10, 2);
            address = start + 4 + 0x10;
            break;
        case 4:
            address = start + 4 + (sizes[format] == 1);
            break;
        }
    }
    if (format == 2) {
        put(address, (source_kind == 1 ? 0x7fff : source_kind == 2 ? 0 : 0x3fff) << 16 | 0xbeef, 4);
        put(address + 4, source_kind == 1 ? 0xa0000000 : source_kind == 2 ? 1 : 0xc0000000, 4);
        put(address + 8, 0, 4);
    }
    else if (format == 5) {
        put(address, source_kind == 1 ? 0x7ff40000 : source_kind == 2 ? 0 : 0x3ff80000, 4);
        put(address + 4, source_kind == 2 ? 1 : 0, 4);
    }
    else
        put(address,
            format == 1 ? source_kind == 1   ? 0x7fa00000
                          : source_kind == 2 ? 1
                                             : 0x3fc00000
                        : 3,
            sizes[format] > 4 ? 4 : sizes[format]);
    memset(table, 0, sizeof table);
    if (paging) {
        mmu.tc = page8k ? 0xc000 : 0x8000;
        mmu.urp = mmu.srp = 0x10000;
        memory.tables = (Syn68kMMU040Bus){table, source_table_read, source_table_write};
        source_table_write(table, 0x10000, 0x11002, 0);
        source_table_write(table, 0x11000, 0x12002, 0);
        unsigned shift = page8k ? 13 : 12;
        for (unsigned page = 0; page < sizeof ram >> shift; ++page)
            source_table_write(table, 0x12000 + page * 4, ((page << shift) ^ (bank ? 0x4000 : 0)) | 1, 0);
        if (bank) {
            unsigned char logical[sizeof ram];
            memcpy(logical, ram, sizeof ram);
            for (unsigned i = 0; i < sizeof ram; ++i)
                ram[i ^ 0x4000] = logical[i];
        }
        if (invalid_page)
            source_table_write(table, 0x12000 + ((address + invalid_page - 1) >> shift) * 4, 0, 0);
        if (warm && !invalid_page) {
            uint32_t value;
            Syn68kMMU040Access access = syn68k_mmu040_read(&memory, start + 2, mode & 1 ? 6 : 2, 2, 0, &value);
            assert(!access.bus_error && access.translation.fault == MMU040_OK && value == command);
            transfer_count = 0;
            memset(transfers, 0, sizeof transfers);
        }
    }
    if (native_bus || direct_ram)
        assert(syn68k_native040_memory(
            &memory, &native_memory, direct_ram ? (Syn68kNative040RAM){ram, 0, sizeof ram} : (Syn68kNative040RAM){0}));
}

static void compare_source(unsigned ea, unsigned command, unsigned mode, unsigned unsupported)
{
    setup(ea, command, mode);
    Syn68kFPUStep expected, actual;
    if (through_cpu) {
        Syn68kMappedCPU cpu = {.pc = start, .memory = &memory};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        expected = (Syn68kFPUStep){.next = cpu.pc,
                                   .exception_pc = run.step.exception_pc,
                                   .fault_address = run.step.fault_address,
                                   .vector = run.step.vector,
                                   .format = run.step.format,
                                   .access = run.step.access};
    }
    else
        assert(syn68k_fpu_step_decoded(start, 0xf200 | ea, &memory, &expected));
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_table, table, sizeof table);
    memcpy(expected_ram, ram, sizeof ram);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    setup(ea, command, mode);
    native_active = unsupported ? 2 : 1;
    captures = command_exits = 0;
    if (through_cpu) {
        Syn68kMappedCPU cpu = {.pc = start, .memory = &memory, .native = 1};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        actual = (Syn68kFPUStep){.next = cpu.pc,
                                 .exception_pc = run.step.exception_pc,
                                 .fault_address = run.step.fault_address,
                                 .vector = run.step.vector,
                                 .format = run.step.format,
                                 .access = run.step.access};
    }
    else
        assert(syn68k_x64_mapped_fpu(start, 0xf200 | ea, &memory, &actual) == 1);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Mapped source mismatch ea=%x cmd=%x mode=%u kind=%u fail=%u vectors=%u/%u next=%x/%x\n", ea,
                command, mode, source_kind, fault_call, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu));
    assert(!memcmp(expected_table, table, sizeof table));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing);
    }
    assert(command_exits == unsupported);
    if (!source_kind && !unsupported && !indexed_extension && !target_kind && !mutate_control)
        assert(!captures);
    if (source_kind && !fault_call && !unsupported && (ea >= 16 && ea <= 60) &&
        ((command >> 10 & 7) == 1 || (command >> 10 & 7) == 2 || (command >> 10 & 7) == 5))
        assert(captures == 1);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static void live_sources(void)
{
    for (unsigned kind = 0; kind < 3; ++kind) {
        source_kind = kind;
        compare_source(24, 0x4022, 1, 0);
        compare_source(24, 0x4422, 2, 0);
        compare_source(24, 0x4822, 3, 0);
        compare_source(24, 0x5422, 4, 0);
    }
    source_kind = 0;
    paging = 1;
    for (bank = 0; bank < 2; ++bank)
        for (direct_ram = 0; direct_ram < 2; ++direct_ram)
            for (warm = 0; warm < 2; ++warm)
                compare_source(24, 0x4822, 1, 0);
    paging = bank = direct_ram = warm = 0;
}

#ifndef MAPPED_FPU_SOURCE_TEST_ENTRY
#define MAPPED_FPU_SOURCE_TEST_ENTRY main
#endif
int MAPPED_FPU_SOURCE_TEST_ENTRY(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    if (argc == 3) {
        live_sources();
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]));
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]));
        syn68k_x64_mapped_clear();
        puts("Mapped FPU AOT consumed live memory formats, values and exceptional sources");
        return 0;
    }
    for (mutate_control = 1; mutate_control <= 2; ++mutate_control)
        for (unsigned mode = 0; mode < 16; ++mode)
            compare_source(24, 0x4804, mode, 0);
    mutate_control = 1;
    compare_source(24, 0x407f, 0, 1); // Reserved command restores the original null state.
    compare_source(24, 0x4010, 0, 1); // Consumed-command FPSP exit retains entry rounding.
    mutate_control = 3;
    compare_source(24, 0x4800, 1, 0); // Later DWORDs use the callback's current SR/FC.
    mutate_control = 0;
    const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27, 0x38, 0x3a, 0x40, 0x44,
                                   0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    for (unsigned mode = 0; mode < 16; ++mode)
        for (unsigned ea = 16; ea < 64; ++ea)
            for (unsigned format = 0; format < 6; ++format)
                for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
                    compare_source(ea, 0x4000 | formats[format] << 10 | (mode & 7) << 7 | operations[op], mode, 0);
    native_bus = 1;
    for (source_kind = 0; source_kind < 3; ++source_kind)
        for (unsigned ea = 16; ea <= 60; ++ea)
            for (unsigned format = 0; format < 6; ++format)
                for (fault_call = 0; fault_call <= 6; ++fault_call)
                    compare_source(ea, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    source_kind = fault_call = 0;
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (crossing = 0; crossing < 2; ++crossing)
                        for (unsigned ea = 16; ea <= 60; ++ea)
                            for (unsigned format = 0; format < 6; ++format)
                                compare_source(ea, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    warm = direct_ram = 0;
    for (invalid_page = 1; invalid_page <= 12; ++invalid_page)
        for (unsigned ea = 16; ea <= 60; ++ea)
            for (unsigned format = 0; format < 6; ++format)
                compare_source(ea, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    paging = crossing = invalid_page = 0;
    through_cpu = 1;
    for (unsigned ea = 16; ea <= 60; ++ea)
        for (unsigned format = 0; format < 6; ++format)
            compare_source(ea, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    through_cpu = 0;
    wrap_pc = 1;
    for (source_kind = 0; source_kind < 3; ++source_kind)
        for (unsigned ea = 16; ea <= 60; ++ea)
            for (unsigned format = 0; format < 6; ++format)
                compare_source(ea, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    wrap_pc = source_kind = 0;
    reenter = 1;
    callbacks = 0;
    compare_source(24, 0x4822, 1, 0);
    assert(callbacks == 2);
    reenter = 0;
    const unsigned extensions[] = {0x0161, 0x0108, 0x0104, 0x0160, 0x0171, 0x0131};
    for (unsigned i = 0; i < sizeof extensions / sizeof *extensions; ++i) {
        indexed_extension = extensions[i];
        for (unsigned format = 0; format < 6; ++format)
            for (fault_call = 0; fault_call <= 8; ++fault_call)
                compare_source(48, 0x4000 | formats[format] << 10 | 0x22, 1, 0);
    }
    indexed_extension = fault_call = 0;
    for (target_kind = 1; target_kind <= 3; ++target_kind)
        for (enables = 0; enables <= 0xff00; enables += 0xff00)
            for (unsigned format = 0; format < 6; ++format)
                for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
                    compare_source(24, 0x4000 | formats[format] << 10 | operations[op], 0, 0);
    target_kind = enables = 0;
    for (unsigned ea = 16; ea <= 60; ++ea) {
        compare_source(ea, 0x4c22, 1, 1); // Packed input remains a checked exceptional path.
        compare_source(ea, 0x4010, 1, 1); // FPSP operation, before any native EA side effect.
    }
    syn68k_x64_mapped_clear();
    printf("%u mapped FPU memory-source arithmetic/EA/fault/capture cases passed\n", cases);
    return 0;
}
