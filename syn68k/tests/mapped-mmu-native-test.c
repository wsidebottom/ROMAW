/*+===================================================================
  File:      mapped-mmu-native-test.c

  Summary:   Compare mapped native MMU instructions with checked execution while forbidding normal semantic helpers.

  Classes:   None.

  Functions: main, __wrap_syn68k_integer_step_decoded, __wrap_syn68k_mmu040_control,
             __wrap_syn68k_mmu040_ptest, __wrap_syn68k_mmu040_flush; includes the mapped FPU bus fixture and wrappers.

  Origin:    Original ARDI Executor direct 68040 MMU instruction regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int mapped_fpu_matrix_unused(int argc, char** argv);
#define MAPPED_FPU_TEST_ENTRY mapped_fpu_matrix_unused
#include "mapped-fpu-native-test.c"

static unsigned stream_mode, injected_fault, wrapped_pc, current_opcode;
static unsigned descriptor_probe, descriptor_seen, descriptor_callbacks;

int __real_syn68k_integer_step_decoded(uint32_t, unsigned, Syn68kMMU040Memory*, Syn68kIntegerStep*);
int __real_syn68k_mmu040_control(Syn68kMMU040*, enum Syn68kMMU040Control, int, uint32_t*);
Syn68kMMU040Result __real_syn68k_mmu040_ptest(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);
void __real_syn68k_mmu040_flush(Syn68kMMU040*, int, uint32_t, int, int);

int __wrap_syn68k_integer_step_decoded(uint32_t pc, unsigned opcode, Syn68kMMU040Memory* bus, Syn68kIntegerStep* result)
{
    assert(!forbid_helpers);
    return __real_syn68k_integer_step_decoded(pc, opcode, bus, result);
}

int __wrap_syn68k_mmu040_control(Syn68kMMU040* state, enum Syn68kMMU040Control control, int writing, uint32_t* value)
{
    assert(!forbid_helpers);
    return __real_syn68k_mmu040_control(state, control, writing, value);
}

Syn68kMMU040Result __wrap_syn68k_mmu040_ptest(Syn68kMMU040* state, const Syn68kMMU040Bus* bus, uint32_t address,
                                              unsigned fc, int writing)
{
    assert(!forbid_helpers);
    return __real_syn68k_mmu040_ptest(state, bus, address, fc, writing);
}

void __wrap_syn68k_mmu040_flush(Syn68kMMU040* state, int all, uint32_t address, int supervisor, int global)
{
    assert(!forbid_helpers);
    __real_syn68k_mmu040_flush(state, all, address, supervisor, global);
}

static int mmu_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                    unsigned fc)
{
    if (wrapped_pc && address == UINT32_MAX - 1 && (fc == 2 || fc == 6)) {
        assert(size == 2 && transfer_count < 64);
        bytes[0] = current_opcode >> 8;
        bytes[1] = current_opcode;
        transfers[transfer_count++] = (Transfer){.address = address, .value = current_opcode, .size = size, .fc = fc};
        return 1;
    }
    return callback_read(opaque, address, bytes, size, attributes, fc);
}

static int reentrant_table_read(void* opaque, uint32_t address, uint32_t* value)
{
    int ok = table_read(opaque, address, value);
    if (ok && !descriptor_seen) {
        descriptor_seen = 1;
        ++descriptor_callbacks;
        CPUState saved = cpu_state;
        put(0x5006, 0x0801, 2);
        Syn68kIntegerStep child;
        /* PC1004/MOVEC and PC1000/PTEST share a cache slot. */
        assert(syn68k_x64_mapped_mmu(0x1004, 0x4e7a, &memory, &child) == 1);
        assert(!child.vector && child.next == 0x1008 && EM_D0 == cpu_state.vbr);
        syn68k_x64_mapped_clear();
        cpu_state = saved;
    }
    return ok;
}

static void prepare_mmu(unsigned opcode, unsigned command, unsigned mode, uint32_t value, unsigned dfc)
{
    prepare(0, command, mode, 1);
    descriptor_seen = 0;
    if (descriptor_probe)
        memory.tables.read32 = reentrant_table_read;
    cpu_state.sr = mode << 12;
    cpu_state.fpu.frame_size = 4;
    cpu_state.fpu.pending_vector = 49; // Integer/MMU instructions must not post an unrelated pending FPU exception.
    cpu_state.usp = 0x11223344;
    cpu_state.isp = 0x55667788;
    cpu_state.msp = 0x99aabbcc;
    cpu_state.vbr = 0x12345678;
    cpu_state.sfc = 0xfedcba98;
    cpu_state.dfc = dfc;
    cpu_state.cacr = 0xabcdef01;
    for (unsigned reg = 0; reg < 16; ++reg)
        cpu_state.regs[reg].ul.n = value + reg * 0x1111;
    if ((opcode & 0xfffe) == 0x4e7a)
        cpu_state.regs[command >> 12].ul.n = value;
    else {
        unsigned shift = page8k ? 13 : 12;
        uint32_t address = 0x20000 + (opcode & 7) * (1u << shift);
        cpu_state.regs[8 + (opcode & 7)].ul.n = address;
        put(0x1000 + (address >> shift) * 4, 0x8001 | (value & 1 ? 4 : 0), 4);
        for (unsigned b = 0; b < 2; ++b)
            for (unsigned set = 0; set < 16; ++set)
                for (unsigned way = 0; way < 4; ++way)
                    mmu.atc[b][set][way] = (Syn68kMMU040Entry){.logical_page = (address >> shift) + (way & 1),
                                                               .physical_page = 0x8000,
                                                               .status = 1 | (way & 2 ? 0x400 : 0),
                                                               .descriptor_address = 0x1000,
                                                               .valid = 1,
                                                               .supervisor = (set + way) & 1,
                                                               .shift = shift};
    }
    mmu.mmusr = 0x89abcdef;
    unsigned shift = page8k ? 13 : 12, mask = (1u << shift) - 1;
    unsigned first = bank ? 0x6000 : page8k ? 0x4000 : 0x5000;
    unsigned second = bank ? page8k ? 0x4000 : 0x5000 : 0x6000;
    unsigned command_page = (start + 2) >> shift;
    unsigned physical = command_page == (start >> shift) ? first : second;
    put(first + (start & mask), opcode, 2);
    failed = injected_fault == 1 ? physical + ((start + 2) & mask) : UINT32_MAX;
    if (injected_fault == 2)
        put(0x1000 + command_page * 4, 0, 4);
    if (wrapped_pc) {
        start = UINT32_MAX - 1;
        mmu.tc = 0;
        put(0, command, 2);
    }
    current_opcode = opcode;
}

static void compare_mmu(unsigned opcode, unsigned command, unsigned mode, uint32_t value, unsigned dfc)
{
    prepare_mmu(opcode, command, mode, value, dfc);
    Syn68kIntegerStep expected, actual;
    unsigned expected_reason = 0, expected_stopped = 0;
    uint32 expected_pc = 0;
    if (stream_mode) {
        Syn68kMappedCPU cpu = {.pc = start, .memory = &memory};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        expected = run.step;
        expected_reason = run.reason;
        expected_stopped = cpu.stopped;
        expected_pc = cpu.pc;
    }
    else
        assert(__real_syn68k_integer_step_decoded(start, opcode, &memory, &expected));
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    prepare_mmu(opcode, command, mode, value, dfc);
    forbid_helpers = 1;
    if (stream_mode) {
        Syn68kMappedCPU cpu = {.pc = start, .memory = &memory, .native = 1};
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        actual = run.step;
        assert(run.reason == expected_reason && cpu.stopped == expected_stopped && cpu.pc == expected_pc);
    }
    else
        assert(syn68k_x64_mapped_mmu(start, opcode, &memory, &actual) == 1);
    forbid_helpers = 0;
    if (memcmp(&expected, &actual, sizeof expected) || memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) ||
        memcmp(&expected_mmu, &mmu, sizeof mmu))
        fprintf(stderr, "Mapped MMU mismatch opcode=%x command=%x mode=%u value=%x dfc=%u vectors=%u/%u\n", opcode,
                command, mode, value, dfc, expected.vector, actual.vector);
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) && !memcmp(&expected_mmu, &mmu, sizeof mmu));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing);
    }
    assert(!syn68k_native_checkpoint());
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    memory.read = mmu_read;
    paging = 1;
    if (argc == 3) {
        compare_mmu(0x4e7a, 0x0801, 2, 0x12345678, 5);
        compare_mmu(0x4e7a, 0x1804, 3, 0, 5);
        compare_mmu(0x4e7a, 0x2805, 2, 0, 5);
        compare_mmu(0x4e7b, 0xf803, 3, 0xabcdef01, 5);
        compare_mmu(0x4e7b, 0x3003, 2, 0, 5);
        for (unsigned dfc = 0; dfc < 8; ++dfc) {
            compare_mmu(0xf508, 0, 2, 0, dfc);
            compare_mmu(0xf548, 0, 2, 1, dfc);
        }
        /* PTEST and PFLUSH deliberately share a bounded RAM-cache slot.
           Their alternating restores also exercise same-process disk hits. */
        assert(syn68k_aot_hit_count() == (atoi(argv[2]) ? 18U : 14U));
        assert(syn68k_native_compile_count() == (atoi(argv[2]) ? 0U : 4U));
        uint64 stop_emissions = syn68k_native_compile_count(), stop_hits = syn68k_aot_hit_count();
        const unsigned statuses[] = {0, 0x2000, 0x3000, 0x2700, 0xffff, 0x801f, 0x1000, 1};
        for (stream_mode = 0; stream_mode < 2; ++stream_mode)
            for (unsigned i = 0; i < sizeof statuses / sizeof *statuses; ++i)
                for (unsigned mode = 0; mode < 4; ++mode) {
                    compare_mmu(0x4e72, statuses[i], mode, 0x12345678, 5);
                    // Force disk restoration, rather than an uncounted RAM hit.
                    syn68k_x64_mapped_clear();
                }
        assert(syn68k_native_compile_count() - stop_emissions == (atoi(argv[2]) ? 0U : 1U));
        assert(syn68k_aot_hit_count() - stop_hits == (atoi(argv[2]) ? 64U : 63U));
        syn68k_x64_mapped_clear();
        puts("Mapped MMU AOT replayed live control selectors, values, DFCs and ATC contents");
        return 0;
    }
    const unsigned selectors[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x800, 0x801, 0x803, 0x804, 0x805, 0x806, 0x807};
    const uint32_t values[] = {0, 1, UINT32_MAX, 0x80008000, 0x12345678};
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned value = 0; value < 65536; value += 257)
            compare_mmu(0x4e72, value, mode, 0x12345678, 5);
    for (unsigned i = 0; i < sizeof selectors / sizeof *selectors; ++i)
        for (unsigned reg = 0; reg < 16; ++reg)
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned mode = 0; mode < 4; ++mode)
                    for (unsigned v = 0; v < sizeof values / sizeof *values; ++v)
                        compare_mmu(0x4e7a | writing, reg << 12 | selectors[i], mode, values[v], 5);
    for (unsigned selector = 0; selector < 4096; ++selector)
        compare_mmu(0x4e7a, selector, 2, 0x12345678, 5);
    for (unsigned opcode = 0xf500; opcode <= 0xf56f; ++opcode)
        if ((opcode & 0xfff0) == 0xf500 || opcode == 0xf510 || opcode == 0xf518 || (opcode & 0xffd8) == 0xf548)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned dfc = 0; dfc < 8; ++dfc)
                    for (unsigned v = 0; v < sizeof values / sizeof *values; ++v)
                        compare_mmu(opcode, 0, mode, values[v], dfc);
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (ram_mapping = 0; ram_mapping < 2; ++ram_mapping)
                for (unsigned crossing = 0; crossing < 2; ++crossing) {
                    command_pc = crossing ? 0x1ffe : 0x1000;
                    for (injected_fault = 0; injected_fault < 3; ++injected_fault)
                        for (unsigned mode = 0; mode < 4; ++mode) {
                            compare_mmu(0x4e7b, 0xf803, mode, 0xaabbccdd, 5);
                            compare_mmu(0x4e72, 0x2000 | mode << 12, mode, 0xaabbccdd, 5);
                        }
                    injected_fault = 0;
                    stream_mode = 1;
                    compare_mmu(0x4e7a, 0x1801, 2, 0, 5);
                    compare_mmu(0xf548, 0, 2, 0, 5);
                    compare_mmu(0xf508, 0, 2, 0, 5);
                    compare_mmu(0x4e72, 0x001f, 2, 0, 5);
                    stream_mode = 0;
                }
    page8k = bank = ram_mapping = warm = 0;
    command_pc = 0x1000;
    descriptor_probe = 1;
    compare_mmu(0xf548, 0, 2, 0, 5);
    assert(descriptor_callbacks == 2);
    descriptor_probe = 0;
    paging = 0;
    wrapped_pc = 1;
    for (stream_mode = 0; stream_mode < 2; ++stream_mode)
        for (unsigned mode = 0; mode < 4; ++mode) {
            compare_mmu(0x4e7a, 0x0801, mode, 0, 5);
            compare_mmu(0x4e7b, 0xf803, mode, 0xaabbccdd, 5);
            compare_mmu(0x4e7a, 8, mode, 0, 5);
            compare_mmu(0xf548, 0, mode, 0, 5);
            compare_mmu(0xf508, 0, mode, 0, 5);
            compare_mmu(0x4e72, 0x2700, mode, 0, 5);
        }
    syn68k_x64_mapped_clear();
    printf(
        "%u mapped MOVEC/PTEST/PFLUSH selector/mask/bank/DFC/page/fault/native-dispatch cases passed without host semantic helpers\n",
        cases);
    return 0;
}
