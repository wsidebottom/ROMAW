/*+===================================================================
  File:      mapped-fpu-destination-test.c

  Summary:   Compare native FPU memory stores, captured conversion exits and callback-visible pipeline state.

  Classes:   None.

  Functions: main and __wrap_syn68k_fpu_finish_destination; imports the bounded control/source fixtures.

  Origin:    Original ARDI Executor direct mapped FPU store regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_destination_control_fixture(int argc, char** argv);
#define MAPPED_FPU_CONTROL_TEST_ENTRY fpu_destination_control_fixture
#include "mapped-fpu-control-test.c"
#undef MAPPED_FPU_CONTROL_TEST_ENTRY

static unsigned destination_exits, forbid_destination, sample_index, source_register, change_source;

typedef struct {
    uint32_t fpcr, fpsr, fpiar, null_state;
    uint16_t exponent;
    uint64_t significand;
} Pipeline;

static Pipeline pipeline[64], expected_pipeline[64];

int __real_syn68k_fpu_finish_destination(const Syn68kFPUCaptured*, Syn68kMMU040Memory*, Syn68kFPUStep*);

int __wrap_syn68k_fpu_finish_destination(const Syn68kFPUCaptured* captured, Syn68kMMU040Memory* bus,
                                         Syn68kFPUStep* result)
{
    assert(native_active && !forbid_destination && !syn68k_native_checkpoint());
    ++destination_exits;
    unsigned saved = native_active;
    native_active = 0; // The exceptional checked writes may use the host MMU bus.
    forbid_ea = 1;     // Their consumed destination must never be evaluated again.
    int ok = __real_syn68k_fpu_finish_destination(captured, bus, result);
    forbid_ea = 0;
    native_active = saved;
    return ok;
}

static void observe_pipeline(void)
{
    assert(transfer_count < 64);
    Pipeline* state = &pipeline[transfer_count];
    state->fpcr = cpu_state.fpu.fpcr;
    state->fpsr = cpu_state.fpu.fpsr;
    state->fpiar = cpu_state.fpu.fpiar;
    state->null_state = cpu_state.fpu.null_state;
    state->exponent = cpu_state.fpu.regs[source_register].exponent;
    state->significand = cpu_state.fpu.regs[source_register].significand;
}

static int destination_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                            unsigned fc)
{
    observe_pipeline();
    int ok = control_read(opaque, address, bytes, size, attributes, fc);
    if (ok && change_source && address == start + 4) {
        cpu_state.fpu.regs[source_register] = (Syn68kFP80){0x4002, UINT64_C(0xa000000000000000)};
        cpu_state.fpu.fpcr ^= 0x30;
        cpu_state.fpu.null_state = 1;
    }
    return ok;
}

static int destination_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                             uint32_t attributes, unsigned fc)
{
    observe_pipeline();
    return control_write(opaque, address, bytes, size, attributes, fc);
}

static const Syn68kFP80 samples[] = {
    {0, 0},
    {0x8000, 0},
    {0x4000, UINT64_C(0xc000000000000000)}, // Exact integer three in every destination format.
    {0x3fff, UINT64_C(0xc000000000000000)}, // Fractional integer conversion.
    {0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaaa)},
    {0x7ffe, UINT64_MAX},
    {0xbffe, UINT64_C(0xfffffffffffffffe)},
    {0xc01e, UINT64_C(0x8000000000000000)}, // Exact negative integer minimum erratum.
    {1, UINT64_C(0x8000000000000000)},
    {0x7fff, UINT64_C(0x8000000000000000)}, // Special inputs retain the checked pre-EA path.
    {0x7fff, UINT64_C(0xc000000000000001)},
    {0x7fff, UINT64_C(0xa000000000000001)},
    {0, 1},
    {0x3fff, UINT64_C(0x7000000000000000)},
};

static void destination_setup(unsigned ea, unsigned command, unsigned mode)
{
    setup(ea, command, mode);
    memory.read = destination_read;
    memory.write = destination_write;
    memset(pipeline, 0, sizeof pipeline);
    memset(attributes_log, 0, sizeof attributes_log);
    source_register = command >> 7 & 7;
    cpu_state.fpu.regs[source_register] = samples[sample_index];
    cpu_state.fpu.fpcr |= enables;
    put(0x1012, 0x0022, 2);
    // RAM bindings see the same physical storage; guest command data is already mapped by setup.
}

static void compare_destination(unsigned ea, unsigned command, unsigned mode, unsigned normal)
{
    destination_setup(ea, command, mode);
    Syn68kFPUStep expected, actual;
    control_step(ea, 0, &expected);
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    memcpy(expected_table, table, sizeof table);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    memcpy(expected_attributes, attributes_log, sizeof attributes_log);
    memcpy(expected_pipeline, pipeline, sizeof pipeline);
    destination_setup(ea, command, mode);
    native_active = normal ? 1 : 2;
    forbid_destination = normal;
    captures = destination_exits = command_exits = 0;
    control_step(ea, 1, &actual);
    native_active = forbid_destination = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Mapped destination mismatch ea=%x cmd=%x mode=%u sample=%u fail=%u vectors=%u/%u next=%x/%x\n",
                ea, command, mode, sample_index, fault_call, expected.vector, actual.vector, expected.next,
                actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu) && !memcmp(expected_table, table, sizeof table));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing && attributes_log[i] == expected_attributes[i]);
        assert(!memcmp(&pipeline[i], &expected_pipeline[i], sizeof pipeline[i]));
    }
    assert(!captures);
    if (normal)
        assert(!destination_exits && !command_exits);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static void invalid_destination_packets(void)
{
    Syn68kFPUCaptured valid = {.pc = 0x1000,
                               .next = 0x1004,
                               .ea = 0x6000,
                               .opcode = 0xf210,
                               .command = 0x6000,
                               .source = {0x3fff, UINT64_C(0x8000000000000000)},
                               .has_entry = 1};
    for (unsigned bad = 0; bad < 20; ++bad) {
        Syn68kFPUCaptured packet = valid;
        destination_setup(16, valid.command, 0);
        Syn68kMMU040Memory broken = {0};
        Syn68kMMU040Memory* bus = &memory;
        switch (bad) {
        case 0:
            packet.pc |= 1;
            break;
        case 1:
            packet.next |= 1;
            break;
        case 2:
            packet.next = packet.pc + 2;
            break;
        case 3:
            packet.next = packet.pc + 18;
            break;
        case 4:
            packet.opcode = 0xf240;
            break;
        case 5:
            packet.opcode = 0xf200;
            break;
        case 6:
            packet.opcode = 0xf23a;
            break;
        case 7:
            packet.command = 0x4000;
            break;
        case 8:
            packet.command = 0x6c00;
            break;
        case 9:
            packet.command = 0x7c00;
            break;
        case 10:
            packet.command |= 1;
            break;
        case 11:
            packet.command |= 0x10000;
            break;
        case 12:
            packet.original_null = 2;
            break;
        case 13:
            packet.has_entry = 2;
            break;
        case 14:
            packet.entry.original_null = 1;
            break;
        case 15:
            packet.unsupported = 1;
            break;
        case 16:
            packet.signaling = 1;
            break;
        case 17:
            packet.source_tag = 0xa0;
            break;
        case 18:
            cpu_state.fpu.frame_size = 52;
            break;
        case 19:
            bus = &broken;
            break;
        }
        CPUState before = cpu_state;
        Syn68kFPUStep result, saved;
        memset(&result, 0xa5, sizeof result);
        saved = result;
        assert(!__real_syn68k_fpu_finish_destination(&packet, bus, &result));
        assert(!memcmp(&before, &cpu_state, sizeof before) && !memcmp(&saved, &result, sizeof result));
        assert(!transfer_count);
        ++cases;
    }
    assert(!__real_syn68k_fpu_finish_destination(NULL, &memory, NULL));
    assert(!__real_syn68k_fpu_finish_destination(&valid, &memory, NULL));
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    if (argc == 3) {
        for (sample_index = 2; sample_index < 9; ++sample_index)
            for (unsigned format = 0; format < 6; ++format)
                compare_destination(24, 0x6000 | formats[format] << 10, sample_index & 15, sample_index == 2);
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]));
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]));
        syn68k_x64_mapped_clear();
        puts("Mapped destination AOT consumed live formats, source registers and conversion exceptions");
        return 0;
    }
    for (sample_index = 0; sample_index < 3; ++sample_index)
        for (unsigned mode = 0; mode < 16; ++mode)
            for (unsigned ea = 16; ea < 64; ++ea)
                for (unsigned format = 0; format < 6; ++format)
                    for (unsigned fp = 0; fp < 8; ++fp)
                        compare_destination(ea, 0x6000 | formats[format] << 10 | fp << 7, mode, 1);
    native_bus = 1;
    for (sample_index = 3; sample_index < sizeof samples / sizeof *samples; ++sample_index)
        for (unsigned mode = 0; mode < 16; ++mode)
            for (unsigned format = 0; format < 6; ++format)
                for (fault_call = 0; fault_call <= 6; ++fault_call)
                    compare_destination(24, 0x6000 | formats[format] << 10, mode, 0);
    sample_index = 2;
    fault_call = 0;
    enables = 0xff00;
    for (unsigned format = 0; format < 6; ++format) {
        sample_index = 3;
        compare_destination(24, 0x6000 | formats[format] << 10, 0, 0);
    }
    enables = 0;
    sample_index = 2;
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (crossing = 0; crossing < 2; ++crossing)
                        for (unsigned format = 0; format < 6; ++format)
                            compare_destination(24, 0x6000 | formats[format] << 10, 1, 1);
    warm = direct_ram = 0;
    for (invalid_page = 1; invalid_page <= 12; ++invalid_page)
        for (unsigned format = 0; format < 6; ++format)
            compare_destination(24, 0x6000 | formats[format] << 10, 1, 1);
    invalid_page = 0;
    paging = direct_ram = warm = crossing = 0;
    change_source = 1;
    for (sample_index = 2; sample_index <= 3; ++sample_index)
        for (unsigned format = 0; format < 6; ++format)
            compare_destination(41, 0x6000 | formats[format] << 10, 0, 1);
    change_source = 0;
    sample_index = 2;
    for (control_callback = 1; control_callback <= 2; ++control_callback) {
        control_callbacks = 0;
        compare_destination(24, 0x6800, 1, 1);
        assert(control_callbacks == 2);
    }
    control_callback = 0;
    through_cpu = 1;
    for (unsigned ea = 16; ea <= 57; ++ea)
        for (unsigned format = 0; format < 6; ++format)
            for (fault_call = 0; fault_call <= 7; ++fault_call)
                compare_destination(ea, 0x6000 | formats[format] << 10, 1, 1);
    through_cpu = fault_call = 0;
    wrap_pc = 1;
    for (sample_index = 2; sample_index <= 3; ++sample_index)
        for (unsigned ea = 16; ea <= 57; ++ea)
            for (unsigned format = 0; format < 6; ++format)
                compare_destination(ea, 0x6000 | formats[format] << 10, 1, 1);
    wrap_pc = 0;
    const unsigned extensions[] = {0x0161, 0x0108, 0x0104, 0x0160, 0x0171, 0x0131};
    sample_index = 2;
    for (unsigned i = 0; i < sizeof extensions / sizeof *extensions; ++i) {
        indexed_extension = extensions[i];
        for (unsigned format = 0; format < 6; ++format)
            for (fault_call = 0; fault_call <= 10; ++fault_call)
                compare_destination(48, 0x6000 | formats[format] << 10, 1, 1);
    }
    indexed_extension = fault_call = 0;
    invalid_destination_packets();
    syn68k_x64_mapped_clear();
    printf("%u mapped FPU memory-destination/conversion/fault/pipeline/callback cases passed\n", cases);
    return 0;
}
