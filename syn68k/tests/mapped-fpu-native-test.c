/*+===================================================================
  File:      mapped-fpu-native-test.c

  Summary:   Validate mapped register FPU bodies, live commands, checked exits and callback-safe cache eviction.

  Classes:   None.

  Functions: main, __wrap_syn68k_fpu_step_decoded, __wrap_syn68k_fpu_finish_command,
             __wrap_syn68k_fpu_finish_command_at,
             __wrap_syn68k_mmu040_translate;
             includes fpu_command_matrix_unused and the bounded command-test fixture.

  Origin:    Original ARDI Executor direct mapped FPU regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_command_matrix_unused(void);
#define main fpu_command_matrix_unused
#include "fpu-command-test.c"
#undef main
#include "jit-x64.h"
#include "mapped-cpu.h"
#include <stdlib.h>

static unsigned forbid_helpers, checked_commands, checked_steps, reenter, callbacks;
static unsigned paging, page8k, bank, ram_mapping, warm;
static uint32_t command_pc;
static Syn68kMMU040Native native_memory;
static unsigned command_fault, pending_frame, through_cpu, invalid_page;

int __real_syn68k_fpu_step_decoded(uint32_t, unsigned, Syn68kMMU040Memory*, Syn68kFPUStep*);
int __real_syn68k_fpu_finish_command(uint32_t, unsigned, unsigned, Syn68kMMU040Memory*, Syn68kFPUStep*);
Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* state, const Syn68kMMU040Bus* bus, uint32_t address,
                                                  unsigned fc, int writing)
{
    assert(!forbid_helpers);
    return __real_syn68k_mmu040_translate(state, bus, address, fc, writing);
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    if (address > sizeof ram - 4)
        return 0;
    *value = 0;
    for (unsigned i = 0; i < 4; ++i)
        *value = *value << 8 | ram[address + i];
    return 1;
}

static int table_write(void* opaque, uint32_t address, uint32_t value, int locked)
{
    uint32_t previous;
    if (!table_read(opaque, address, &previous))
        return 0;
    put(address, locked ? previous | value : value, 4);
    return 1;
}

int __wrap_syn68k_fpu_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* bus, Syn68kFPUStep* result)
{
    assert(!forbid_helpers);
    ++checked_steps;
    return __real_syn68k_fpu_step_decoded(pc, opcode, bus, result);
}

int __wrap_syn68k_fpu_finish_command(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* bus,
                                     Syn68kFPUStep* result)
{
    assert(!forbid_helpers);
    ++checked_commands;
    return __real_syn68k_fpu_finish_command(pc, opcode, command, bus, result);
}

int __real_syn68k_fpu_finish_command_at(uint32_t, unsigned, unsigned, Syn68kMMU040Memory*, const Syn68kFPUEntry*,
                                        Syn68kFPUStep*);

int __wrap_syn68k_fpu_finish_command_at(uint32_t pc, unsigned opcode, unsigned command, Syn68kMMU040Memory* bus,
                                        const Syn68kFPUEntry* entry, Syn68kFPUStep* result)
{
    assert(!forbid_helpers);
    ++checked_commands;
    return __real_syn68k_fpu_finish_command_at(pc, opcode, command, bus, entry, result);
}

static int callback_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    int ok = read_operand(opaque, address, bytes, size, attributes, fc);
    if (ok && reenter && address == 0x1002) {
        ++callbacks;
        CPUState saved = cpu_state;
        put(0x1022, 0x0022, 2);
        cpu_state.fpu.fpcr = 0;
        cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff, UINT64_C(0x8000000000000000)};
        Syn68kFPUStep child;
        assert(syn68k_x64_mapped_fpu(0x1020, 0xf200, &memory, &child) == 1);
        assert(!child.vector && child.next == 0x1024 && cpu_state.fpu.fpiar == 0x1020);
        syn68k_x64_mapped_clear(); // Evict both the child and the still-returning parent.
        cpu_state = saved;
    }
    return ok;
}

static void prepare(unsigned ea, unsigned command, unsigned mode, int normal)
{
    fixture(command, mode & 1);
    memory.native = NULL;
    memory.tables = (Syn68kMMU040Bus){0};
    cpu_state.fpu.fpcr = normal ? ((mode & 3) << 4) | (((mode >> 2) & 3) << 6) : 0xff00;
    if ((command >> 13) == 2) {
        unsigned format = (command >> 10) & 7;
        cpu_state.regs[ea & 7].ul.n = format == 1 ? 0x3fc00000 : 3;
    }
    if (paging) {
        start = command_pc ? command_pc : 0x1000;
        unsigned shift = page8k ? 13 : 12, mask = (1u << shift) - 1;
        unsigned first = bank ? 0x6000 : page8k ? 0x4000 : 0x5000;
        unsigned second = bank ? page8k ? 0x4000 : 0x5000 : 0x6000;
        mmu.tc = page8k ? 0xc000 : 0x8000;
        mmu.urp = mmu.srp = 0x2000;
        memory.tables = (Syn68kMMU040Bus){ram, table_read, table_write};
        put(0x2000, 0x3002, 4);
        put(0x3000, 0x1002, 4);
        put(0x1000 + (start >> shift) * 4, first | 1, 4);
        unsigned command_page = (start + 2) >> shift;
        unsigned physical = first;
        if (command_page != (start >> shift)) {
            put(0x1000 + command_page * 4, second | 1, 4);
            physical = second;
        }
        put(first + (start & mask), 0xf200 | ea, 2);
        put(physical + ((start + 2) & mask), command, 2);
        if (invalid_page)
            put(0x1000 + command_page * 4, 0, 4);
        if (ram_mapping)
            assert(syn68k_native040_memory(&memory, &native_memory, (Syn68kNative040RAM){ram, 0, sizeof ram}));
        if (warm) {
            uint32_t value;
            Syn68kMMU040Access access = syn68k_mmu040_read(&memory, start + 2, mode & 1 ? 6 : 2, 2, 0, &value);
            assert(!access.bus_error && access.translation.fault == MMU040_OK && value == command);
            transfer_count = 0;
            memset(transfers, 0, sizeof transfers);
        }
    }
    if (command_fault)
        failed = start + 2;
    if (pending_frame) {
        cpu_state.fpu.frame_size = 4;
        cpu_state.fpu.pending_vector = 49;
    }
    if (through_cpu && !paging)
        put(start, 0xf200 | ea, 2);
}

static void compare_native(unsigned ea, unsigned command, unsigned mode, int normal)
{
    prepare(ea, command, mode, normal);
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
        assert(__real_syn68k_fpu_step_decoded(start, 0xf200 | ea, &memory, &expected));
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    prepare(ea, command, mode, normal);
    forbid_helpers = normal;
    checked_commands = checked_steps = 0;
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
    forbid_helpers = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Mapped FPU mismatch ea=%x command=%x mode=%u normal=%u vectors=%u/%u next=%x/%x\n", ea,
                command, mode, normal, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing);
    }
    if (normal)
        assert(!checked_commands && !checked_steps);
    else if (pending_frame)
        assert(checked_steps == 1 && !checked_commands);
    else
        assert(checked_commands == 1 && !checked_steps);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static void live_commands(void)
{
    compare_native(0, 0x0022, 0, 1);
    compare_native(0, 0x9000, 0, 1);
    compare_native(0, 0x4400, 0, 1);
    compare_native(0, 0x7800, 0, 1);
    compare_native(0, 0x6400, 0, 1);
}

#ifndef MAPPED_FPU_TEST_ENTRY
#define MAPPED_FPU_TEST_ENTRY main
#endif
int MAPPED_FPU_TEST_ENTRY(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    syn68k_x64_mapped_clear();
    memory.read = callback_read;
    if (argc == 3) {
        live_commands();
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]));
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]));
        syn68k_x64_mapped_clear();
        puts("Mapped AOT register body replayed live arithmetic/control/input/output commands");
        return 0;
    }
    const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27, 0x38, 0x3a, 0x40, 0x44,
                                   0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    const unsigned formats[] = {0, 1, 4, 6};
    for (unsigned mode = 0; mode < 16; ++mode) {
        for (unsigned source = 0; source < 8; ++source)
            for (unsigned target = 0; target < 8; ++target)
                for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
                    compare_native(0, source << 10 | target << 7 | operations[op], mode, 1);
        for (unsigned reg = 0; reg < 8; ++reg)
            for (unsigned format = 0; format < 4; ++format) {
                for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
                    compare_native(reg, 0x4000 | formats[format] << 10 | 3 << 7 | operations[op], mode, 1);
                for (unsigned fp = 0; fp < 8; ++fp)
                    compare_native(reg, 0x6000 | formats[format] << 10 | fp << 7, mode, 1);
            }
        for (unsigned reg = 0; reg < 16; ++reg)
            for (unsigned mask = 1; mask <= 4; mask *= 2)
                if (reg < 8 || mask == 1)
                    for (unsigned direction = 0; direction < 2; ++direction)
                        compare_native(reg, 0x8000 | direction << 13 | mask << 10, mode, 1);
    }
    compare_native(3, 0x0022, 0, 0); // Register arithmetic cannot have a nonzero EA field.
    compare_native(0, 0x4800, 0, 0); // Extended input cannot come from Dn.
    compare_native(0, 0x6001, 0, 0); // Reserved move-out low bits.
    compare_native(8, 0x9000, 0, 0); // An cannot transfer FPCR.
    command_fault = 1;
    for (unsigned mode = 0; mode < 2; ++mode) {
        compare_native(0, 0x0022, mode, 1); // Access faults leave generated code without a semantic replay.
        pending_frame = 1;
        compare_native(0, 0x0022, mode, 0); // Pending exceptions precede the failed command transfer.
        pending_frame = 0;
    }
    command_fault = pending_frame = 0;
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (ram_mapping = 0; ram_mapping < 2; ++ram_mapping)
                for (warm = 0; warm < 2; ++warm)
                    for (unsigned crossing = 0; crossing < 2; ++crossing) {
                        command_pc = crossing ? 0x1ffe : 0x1000;
                        live_commands();
                        compare_native(0, 0x0022, 1, 1);
                        if (!warm) {
                            invalid_page = 1;
                            compare_native(0, 0x0022, 0, 1);
                            compare_native(0, 0x0022, 1, 1);
                            invalid_page = 0;
                        }
                        through_cpu = 1;
                        compare_native(0, 0x0022, 0, 1);
                        compare_native(0, 0x0022, 1, 1);
                        through_cpu = 0;
                    }
    paging = ram_mapping = warm = command_pc = 0;
    through_cpu = 1;
    live_commands();
    through_cpu = 0;
    reenter = 1;
    live_commands();
    assert(callbacks == 10);
    reenter = 0;
    syn68k_x64_mapped_clear();
    printf(
        "%u mapped register FPU cases passed; normal paths rejected semantic helpers; callback eviction returned safely\n",
        cases);
    return 0;
}
