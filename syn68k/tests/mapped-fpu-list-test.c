/*+===================================================================
  File:      mapped-fpu-list-test.c

  Summary:   Compare native FMOVEM masks, raw registers, ordered transfers and callback-safe partial effects.

  Classes:   None.

  Functions: main; imports fpu_list_control_fixture and the bounded mapped FPU fixtures and linker wrappers.

  Origin:    Original ARDI Executor direct mapped 68040 register-list regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_list_control_fixture(int argc, char** argv);
#define MAPPED_FPU_CONTROL_TEST_ENTRY fpu_list_control_fixture
#include "mapped-fpu-control-test.c"
#undef MAPPED_FPU_CONTROL_TEST_ENTRY

static unsigned list_mask, list_register, list_mutation, list_callbacks;

typedef struct {
    uint32_t fpcr, fpsr, fpiar, null_state;
    Syn68kFP80 regs[8];
} ListPipeline;

static ListPipeline list_pipeline[64], expected_list_pipeline[64];

static void list_observe(void)
{
    assert(transfer_count < 64);
    ListPipeline* state = &list_pipeline[transfer_count];
    state->fpcr = cpu_state.fpu.fpcr;
    state->fpsr = cpu_state.fpu.fpsr;
    state->fpiar = cpu_state.fpu.fpiar;
    state->null_state = cpu_state.fpu.null_state;
    memcpy(state->regs, cpu_state.fpu.regs, sizeof state->regs);
}

static int list_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                     unsigned fc)
{
    list_observe();
    int ok = control_read(opaque, address, bytes, size, attributes, fc);
    if (ok && list_mutation == 1 && address == start + 4 && (fc == 2 || fc == 6)) {
        ++list_callbacks;
        cpu_state.regs[list_register].ul.n ^= 255;
        cpu_state.fpu.null_state = 1;
    }
    return ok;
}

static int list_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                      unsigned fc)
{
    list_observe();
    int ok = control_write(opaque, address, bytes, size, attributes, fc);
    if (ok && list_mutation == 2 && address == 0x6000) {
        ++list_callbacks;
        cpu_state.fpu.regs[0].exponent = 0x7fff;
        cpu_state.fpu.regs[0].significand = UINT64_C(0xa000000000000123);
        cpu_state.fpu.regs[1].exponent = 0x8000;
        cpu_state.fpu.regs[1].significand = 0;
        cpu_state.fpu.null_state = 1;
    }
    return ok;
}

static void list_setup(unsigned ea, unsigned command, unsigned mode)
{
    fixture(command, 0);
    memory.native = NULL;
    memory.tables = (Syn68kMMU040Bus){0};
    memory.read = list_read;
    memory.write = list_write;
    memset(attributes_log, 0, sizeof attributes_log);
    memset(list_pipeline, 0, sizeof list_pipeline);
    if (wrap_pc) {
        start = UINT32_MAX - 1;
        put(0, command, 2);
    }
    else
        put(start, 0xf200 | ea, 2);
    put(0x1012, 0x0022, 2);
    cpu_state.sr = mode & 1 ? 0x2000 : 0;
    cpu_state.fpu.fpcr = 0xff00;
    cpu_state.fpu.null_state = mode & 1;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.regs[i].ul.n = 0;
        cpu_state.regs[i + 8].ul.n = crossing ? 0x5fff : 0x6000;
        cpu_state.fpu.regs[i].exponent = 0x7fff ^ i * 0x1333;
        cpu_state.fpu.regs[i].significand = UINT64_C(0xa123456789abcdef) ^ (uint64_t)i * UINT64_C(0x1234fedca9876543);
    }
    // FP0+FP0 is exact in the nested callback probe; other registers retain raw special encodings.
    if (control_callback == 1) {
        cpu_state.fpu.regs[0].exponent = 0x3fff;
        cpu_state.fpu.regs[0].significand = UINT64_C(0x8000000000000000);
    }
    unsigned dynamic = !!(command & 0x0800);
    if (dynamic)
        cpu_state.regs[list_register].ul.n = 0xc0ff0000 | list_mask;
    unsigned mask = dynamic ? list_mask : command & 255, count = __builtin_popcount(mask);
    uint32_t address = crossing ? 0x5fff : 0x6000;
    switch (ea >> 3) {
    case 4:
        address -= count * 12;
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
            put(address + 0x10, 0x6200, 4);
            address = 0x6200;
        }
        else {
            put(start + 4, 0x10, 2);
            address += 0x10 + (dynamic && !list_register ? list_mask : 0);
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
            address = start + 4 + 0x10 + (dynamic && !list_register ? list_mask : 0);
            break;
        }
    }
    for (unsigned slot = 0; slot < 8; ++slot) {
        put(address + slot * 12, (0x7fff ^ slot * 0x1133) << 16 | 0xbeef, 4);
        put(address + slot * 12 + 4, 0x81234567 ^ slot * 0x79346, 4);
        put(address + slot * 12 + 8, 0xfedcba98 ^ slot * 0x15765767, 4);
    }
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
        if (protected_page) {
            uint32_t pte;
            uint32_t location = 0x12000 + (0x6000 >> shift) * 4;
            assert(source_table_read(table, location, &pte));
            source_table_write(table, location, pte | 4, 0);
        }
        if (invalid_page)
            source_table_write(table, 0x12000 + ((0x6000 + invalid_page - 1) >> shift) * 4, 0, 0);
        if (warm && !invalid_page) {
            uint32_t value;
            Syn68kMMU040Access access = syn68k_mmu040_read(&memory, start + 2, mode & 1 ? 6 : 2, 2, 0, &value);
            assert(!access.bus_error && access.translation.fault == MMU040_OK && value == command);
            transfer_count = 0;
            memset(transfers, 0, sizeof transfers);
            memset(list_pipeline, 0, sizeof list_pipeline);
        }
    }
    if (native_bus || direct_ram)
        assert(syn68k_native040_memory(
            &memory, &native_memory, direct_ram ? (Syn68kNative040RAM){ram, 0, sizeof ram} : (Syn68kNative040RAM){0}));
}

static void compare_list(unsigned ea, unsigned command, unsigned mode, unsigned invalid)
{
    list_setup(ea, command, mode);
    Syn68kFPUStep expected, actual;
    control_step(ea, 0, &expected);
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    memcpy(expected_table, table, sizeof table);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    memcpy(expected_attributes, attributes_log, sizeof attributes_log);
    memcpy(expected_list_pipeline, list_pipeline, sizeof list_pipeline);
    list_setup(ea, command, mode);
    native_active = invalid ? 2 : 1;
    captures = command_exits = 0;
    control_step(ea, 1, &actual);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Mapped list mismatch ea=%x cmd=%x mask=%x mode=%u fail=%u vectors=%u/%u next=%x/%x\n", ea,
                command, list_mask, mode, fault_call, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu) && !memcmp(expected_table, table, sizeof table));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing && attributes_log[i] == expected_attributes[i]);
        assert(!memcmp(&list_pipeline[i], &expected_list_pipeline[i], sizeof list_pipeline[i]));
    }
    assert(!captures);
    if (!invalid)
        assert(!command_exits);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static unsigned list_command(unsigned ea, unsigned writing, unsigned dynamic)
{
    unsigned mode = (ea >> 3 == 4 ? 0 : 2) | dynamic;
    return 0xc000 | writing << 13 | mode << 11 | (dynamic ? list_register << 4 : list_mask);
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    if (argc == 3) {
        for (list_mask = 0; list_mask <= 255; list_mask += 17) {
            list_register = list_mask & 7;
            for (unsigned writing = 0; writing < 2; ++writing)
                for (unsigned dynamic = 0; dynamic < 2; ++dynamic)
                    compare_list(16, list_command(16, writing, dynamic), list_mask & 1, 0);
        }
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]));
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]));
        syn68k_x64_mapped_clear();
        puts("Mapped FMOVEM AOT replayed live masks, register selectors and transfer directions");
        return 0;
    }
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned ea = 16; ea < 64; ++ea)
            for (list_mask = 0; list_mask <= 255; ++list_mask) {
                list_register = list_mask & 7;
                for (unsigned writing = 0; writing < 2; ++writing)
                    for (unsigned dynamic = 0; dynamic < 2; ++dynamic) {
                        unsigned invalid = (ea >> 3 == 3 && writing) || (ea >> 3 == 4 && !writing);
                        compare_list(ea, list_command(ea, writing, dynamic), mode, invalid);
                    }
            }
    native_bus = 1;
    list_mask = 255;
    for (unsigned ea = 16; ea <= 59; ++ea)
        for (unsigned writing = 0; writing < 2; ++writing)
            if (!(ea >> 3 == 3 && writing) && !(ea >> 3 == 4 && !writing))
                for (fault_call = 0; fault_call <= 36; ++fault_call)
                    compare_list(ea, list_command(ea, writing, 0), 1, 0);
    fault_call = 0;
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (crossing = 0; crossing < 2; ++crossing)
                        for (unsigned writing = 0; writing < 2; ++writing)
                            compare_list(16, list_command(16, writing, 0), 1, 0);
    warm = direct_ram = 0;
    for (invalid_page = 1; invalid_page <= 96; invalid_page += 11)
        for (unsigned writing = 0; writing < 2; ++writing)
            compare_list(16, list_command(16, writing, 0), 1, 0);
    invalid_page = 0;
    protected_page = 1;
    for (unsigned writing = 0; writing < 2; ++writing)
        compare_list(16, list_command(16, writing, 0), 1, 0);
    protected_page = 0;
    paging = direct_ram = warm = crossing = 0;
    list_register = 3;
    list_mutation = 1;
    list_callbacks = 0;
    compare_list(41, list_command(41, 0, 1), 1, 0);
    assert(list_callbacks == 2);
    list_mutation = 2;
    list_callbacks = 0;
    compare_list(16, list_command(16, 1, 0), 1, 0);
    assert(list_callbacks == 2);
    list_mutation = 0;
    for (control_callback = 1; control_callback <= 2; ++control_callback) {
        control_callbacks = 0;
        compare_list(24, list_command(24, 0, 0), 1, 0);
        assert(control_callbacks == 2);
    }
    control_callback = 0;
    through_cpu = 1;
    for (unsigned ea = 16; ea <= 59; ++ea)
        for (unsigned writing = 0; writing < 2; ++writing)
            if (!(ea >> 3 == 3 && writing) && !(ea >> 3 == 4 && !writing))
                for (fault_call = 0; fault_call <= 35; ++fault_call)
                    compare_list(ea, list_command(ea, writing, 1), 1, 0);
    through_cpu = fault_call = 0;
    wrap_pc = 1;
    for (unsigned ea = 16; ea <= 59; ++ea)
        for (unsigned writing = 0; writing < 2; ++writing)
            if (!(ea >> 3 == 3 && writing) && !(ea >> 3 == 4 && !writing))
                for (unsigned dynamic = 0; dynamic < 2; ++dynamic)
                    compare_list(ea, list_command(ea, writing, dynamic), 1, 0);
    wrap_pc = 0;
    const unsigned extensions[] = {0x0161, 0x0108, 0x0104, 0x0160, 0x0171, 0x0131};
    for (unsigned i = 0; i < sizeof extensions / sizeof *extensions; ++i) {
        indexed_extension = extensions[i];
        for (unsigned writing = 0; writing < 2; ++writing)
            for (fault_call = 0; fault_call <= 36; ++fault_call)
                compare_list(48, list_command(48, writing, 0), 1, 0);
    }
    indexed_extension = fault_call = 0;
    for (unsigned bit = 0; bit < 11; ++bit)
        if (bit < 4 || bit >= 7)
            for (unsigned writing = 0; writing < 2; ++writing)
                compare_list(16, list_command(16, writing, 1) | 1u << bit, 1, 1);
    compare_list(16, 0xc0ff, 1, 1); // Non-predecrement requires mode2/3.
    compare_list(32, 0xf0ff, 1, 1); // Predecrement requires mode0/1.
    syn68k_x64_mapped_clear();
    printf("%u mapped FMOVEM mask/encoding/order/partial-fault/pipeline/callback cases passed\n", cases);
    return 0;
}
