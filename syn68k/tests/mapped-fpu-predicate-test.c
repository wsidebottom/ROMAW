/*+===================================================================
  File:      mapped-fpu-predicate-test.c

  Summary:   Compare mapped native FPU predicates, branch counters, exception priority and live callback state.

  Classes:   None.

  Functions: main; imports fpu_predicate_control_fixture and the bounded mapped FPU fixtures and linker wrappers.

  Origin:    Original ARDI Executor direct mapped 68040 predicate regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_predicate_control_fixture(int argc, char** argv);
#define MAPPED_FPU_CONTROL_TEST_ENTRY fpu_predicate_control_fixture
#include "mapped-fpu-control-test.c"
#undef MAPPED_FPU_CONTROL_TEST_ENTRY

static unsigned predicate_flags, predicate_enabled, predicate_counter, predicate_mutation, predicate_pending;
static uint32_t predicate_displacement, predicate_pc;
static unsigned predicate_finish;

typedef struct {
    uint32_t fpcr, fpsr, fpiar, null_state, regs[8];
} PredicatePipeline;

static PredicatePipeline predicate_pipeline[64], expected_predicate_pipeline[64];

static void predicate_observe(void)
{
    assert(transfer_count < 64);
    PredicatePipeline* state = &predicate_pipeline[transfer_count];
    state->fpcr = cpu_state.fpu.fpcr;
    state->fpsr = cpu_state.fpu.fpsr;
    state->fpiar = cpu_state.fpu.fpiar;
    state->null_state = cpu_state.fpu.null_state;
    for (unsigned reg = 0; reg < 8; ++reg)
        state->regs[reg] = cpu_state.regs[reg].ul.n;
}

static int predicate_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                          unsigned fc)
{
    predicate_observe();
    int ok = control_read(opaque, address, bytes, size, attributes, fc);
    if (ok && predicate_mutation && address == start + (predicate_mutation == 1 ? 2 : 4) && (fc == 2 || fc == 6)) {
        cpu_state.fpu.fpsr ^= 0x0d000000;
        cpu_state.fpu.fpcr ^= 0x8000;
        cpu_state.fpu.null_state ^= 1;
        for (unsigned reg = 0; reg < 8; ++reg)
            cpu_state.regs[reg].ul.n ^= 1;
    }
    return ok;
}

static int predicate_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                           uint32_t attributes, unsigned fc)
{
    predicate_observe();
    return control_write(opaque, address, bytes, size, attributes, fc);
}

static void predicate_put(uint32_t address, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) {
        uint32_t physical = address + i;
        if (paging && bank)
            physical ^= 0x4000;
        assert(physical < sizeof ram);
        ram[physical] = value >> ((size - i - 1) * 8);
    }
}

static void predicate_setup(unsigned opcode, unsigned condition, unsigned mode)
{
    unsigned save_warm = warm;
    warm = 0;
    control_setup(opcode & 63, condition, mode);
    warm = save_warm;
    if (predicate_pc)
        start = predicate_pc;
    if (!wrap_pc)
        predicate_put(start, opcode, 2);
    unsigned branch = (opcode & 0xff80) == 0xf280;
    predicate_put(start + 2, branch ? predicate_displacement : condition, branch && (opcode & 0x40) ? 4 : 2);
    if (!branch && (((opcode & 63) >= 8 && (opcode & 63) < 16) || (opcode & 63) == 0x3a || (opcode & 63) == 0x3b))
        predicate_put(start + 4, predicate_displacement, (opcode & 63) == 0x3b ? 4 : 2);
    cpu_state.fpu.fpcr = predicate_enabled ? 0x8000 : 0;
    cpu_state.fpu.fpsr = predicate_flags << 24 | 0x00001278;
    for (unsigned reg = 0; reg < 8; ++reg)
        cpu_state.regs[reg].ul.n = 0xa5b60000 | predicate_counter;
    if (predicate_pending) {
        cpu_state.fpu.frame_size = 4;
        cpu_state.fpu.pending_vector = 49;
    }
    if (warm && !wrap_pc) {
        uint32_t value;
        Syn68kMMU040Access access = syn68k_mmu040_read(&memory, start + 2, mode & 1 ? 6 : 2, 2, 0, &value);
        assert(!access.bus_error && access.translation.fault == MMU040_OK);
    }
    transfer_count = 0;
    memset(transfers, 0, sizeof transfers);
    memset(attributes_log, 0, sizeof attributes_log);
    memset(predicate_pipeline, 0, sizeof predicate_pipeline);
    memory.read = predicate_read;
    memory.write = predicate_write;
}

static void predicate_step(unsigned opcode, unsigned native, Syn68kFPUStep* result)
{
    if (native && predicate_finish) {
        unsigned saved = native_active;
        native_active = 0;
        Syn68kFPUEntry entry = {cpu_state.fpu.fpcr, cpu_state.fpu.null_state};
        uint32_t command;
        Syn68kMMU040Access access =
            syn68k_mmu040_read(&memory, start + 2, (cpu_state.sr >> 11 & 4) | 2, 2, 0, &command);
        assert(!access.bus_error && access.translation.fault == MMU040_OK);
        assert(__real_syn68k_fpu_finish_command_at(start, opcode, command, &memory, &entry, result));
        native_active = saved;
    }
    else if (through_cpu) {
        Syn68kMappedCPU cpu = {.pc = start, .memory = &memory, .native = native};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        *result = (Syn68kFPUStep){.next = cpu.pc,
                                  .exception_pc = run.step.exception_pc,
                                  .fault_address = run.step.fault_address,
                                  .vector = run.step.vector,
                                  .format = run.step.format,
                                  .access = run.step.access};
    }
    else if (native)
        assert(syn68k_x64_mapped_fpu(start, opcode, &memory, result) == 1);
    else
        assert(syn68k_fpu_step_decoded(start, opcode, &memory, result));
}

static void compare_predicate(unsigned opcode, unsigned condition, unsigned mode)
{
    predicate_setup(opcode, condition, mode);
    Syn68kFPUStep expected, actual;
    predicate_step(opcode, 0, &expected);
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    memcpy(expected_table, table, sizeof table);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    memcpy(expected_attributes, attributes_log, sizeof attributes_log);
    memcpy(expected_predicate_pipeline, predicate_pipeline, sizeof predicate_pipeline);
    predicate_setup(opcode, condition, mode);
    native_active = predicate_pending ? 2 : 1;
    captures = command_exits = 0;
    predicate_step(opcode, 1, &actual);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(
            stderr,
            "Mapped predicate mismatch op=%x cond=%x flags=%x enabled=%u counter=%x disp=%x fail=%u vectors=%u/%u next=%x/%x\n",
            opcode, condition, predicate_flags, predicate_enabled, predicate_counter, predicate_displacement,
            fault_call, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu) && !memcmp(expected_table, table, sizeof table));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing && attributes_log[i] == expected_attributes[i]);
        assert(!memcmp(&predicate_pipeline[i], &expected_predicate_pipeline[i], sizeof predicate_pipeline[i]));
    }
    assert(!captures && !command_exits && !syn68k_native_checkpoint());
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    if (argc == 3) {
        for (predicate_flags = 0; predicate_flags < 16; ++predicate_flags) {
            predicate_counter = predicate_flags & 3 ? 0xffff : 0;
            predicate_displacement = (predicate_flags & 2 ? 0xfffffffe : 0x2000) | (predicate_flags & 1);
            compare_predicate(0xf240, predicate_flags, 1);
            compare_predicate(0xf252, predicate_flags | 16, 1);
            compare_predicate(0xf248, predicate_flags, 1);
            compare_predicate(0xf27c, predicate_flags, 1);
            compare_predicate(0xf281, 0, 1);
        }
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]) * 5);
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]) * 5);
        syn68k_x64_mapped_clear();
        puts("Mapped predicate AOT consumed live conditions, counters, displacements and FPCCs");
        return 0;
    }
    const unsigned counters[] = {0, 1, 0xffff, 0x7fff};
    const uint32_t displacements[] = {0, 2, 0xfffffffe, 0x8000};
    for (predicate_flags = 0; predicate_flags < 16; ++predicate_flags)
        for (predicate_enabled = 0; predicate_enabled < 2; ++predicate_enabled)
            for (unsigned condition = 0; condition < 32; ++condition) {
                for (unsigned ea = 0; ea < 64; ++ea)
                    compare_predicate(0xf240 | ea, condition, condition & 1);
                for (unsigned odd = 0; odd < 2; ++odd)
                    for (unsigned counter = 0; counter < 4; ++counter) {
                        predicate_counter = counters[counter];
                        predicate_displacement = displacements[counter] | odd;
                        for (unsigned reg = 0; reg < 8; ++reg)
                            compare_predicate(0xf248 | reg, condition, 1);
                        compare_predicate(0xf280 | condition, 0, 1);
                        compare_predicate(0xf2c0 | condition, 0, 1);
                    }
            }
    predicate_flags = predicate_enabled = predicate_counter = predicate_displacement = 0;
    native_bus = 1;
    for (unsigned opcode = 0xf240; opcode <= 0xf2ff; ++opcode)
        for (fault_call = 0; fault_call <= 6; ++fault_call)
            compare_predicate(opcode, 31, 1);
    fault_call = 0;
    for (unsigned condition = 32; condition <= 65535; condition += 127)
        compare_predicate(0xf27b, condition, 1);
    for (predicate_mutation = 1; predicate_mutation <= 2; ++predicate_mutation)
        for (unsigned condition = 0; condition < 32; ++condition) {
            compare_predicate(0xf269, condition, 0);
            compare_predicate(0xf248, condition, 0);
            compare_predicate(0xf27b, condition, 0);
            compare_predicate(0xf281, 0, 0);
        }
    predicate_mutation = 0;
    predicate_pending = 1;
    for (unsigned opcode = 0xf240; opcode <= 0xf2ff; ++opcode)
        compare_predicate(opcode, 0xffff, 1);
    predicate_pending = 0;
    paging = 1;
    const unsigned examples[] = {0xf240, 0xf250, 0xf269, 0xf248, 0xf27a, 0xf27b, 0xf27c, 0xf281, 0xf2c1};
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (unsigned edge = 0; edge < 3; ++edge) {
                        predicate_pc = edge == 0 ? 0x1000 : edge == 1 ? 0x1ffc : 0x1ffe;
                        for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
                            for (predicate_flags = 0; predicate_flags < 16; ++predicate_flags)
                                compare_predicate(examples[i], 17, 1);
                    }
    paging = predicate_pc = direct_ram = warm = 0;
    through_cpu = 1;
    for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
        for (unsigned condition = 0; condition < 32; ++condition)
            for (fault_call = 0; fault_call <= 6; ++fault_call)
                compare_predicate(examples[i], condition, 1);
    through_cpu = fault_call = 0;
    wrap_pc = 1;
    for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
        for (unsigned condition = 0; condition < 32; ++condition)
            compare_predicate(examples[i], condition, 1);
    wrap_pc = 0;
    predicate_finish = 1;
    predicate_mutation = 1;
    for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
        if ((examples[i] & 0xffc0) == 0xf240)
            for (unsigned condition = 0; condition < 64; condition += 7)
                compare_predicate(examples[i], condition, 0);
    predicate_finish = predicate_mutation = 0;
    syn68k_x64_mapped_clear();
    printf("%u mapped FPU predicate/truth/counter/BSUN/trap/fault/callback cases passed\n", cases);
    return 0;
}
