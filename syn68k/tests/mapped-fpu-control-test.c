/*+===================================================================
  File:      mapped-fpu-control-test.c

  Summary:   Validate direct mapped FPU control lists, partial faults and callback-visible ordering.

  Classes:   None.

  Functions: main; imports fpu_control_source_fixture and the bounded mapped-source fixture and linker wrappers.

  Origin:    Original ARDI Executor direct mapped FPU control-transfer regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int fpu_control_source_fixture(int argc, char** argv);
#define MAPPED_FPU_SOURCE_TEST_ENTRY fpu_control_source_fixture
#include "mapped-fpu-source-test.c"
#undef MAPPED_FPU_SOURCE_TEST_ENTRY

static unsigned control_callback, control_callbacks, protected_page;
static uint32_t attributes_log[64], expected_attributes[64];

static void control_reenter(uint32_t address, int ok)
{
    if (!ok || !control_callback || address != 0x6000)
        return;
    ++control_callbacks;
    if (control_callback == 1) {
        CPUState saved = cpu_state;
        Syn68kFPUStep child;
        assert(syn68k_x64_mapped_fpu(0x1010, 0xf200, &memory, &child) == 1);
        assert(!child.vector && child.next == 0x1014);
        syn68k_x64_mapped_clear();
        cpu_state = saved;
    }
    else {
        cpu_state.sr ^= 0x2000;
        cpu_state.fpu.fpcr ^= 0x90;
        cpu_state.fpu.fpsr ^= 0x04000300;
        cpu_state.fpu.fpiar ^= 0x12345678;
        cpu_state.fpu.null_state = 1;
    }
}

static int control_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    (void)opaque;
    assert(transfer_count < 64);
    attributes_log[transfer_count] = attributes;
    Transfer* transfer = &transfers[transfer_count++];
    *transfer = (Transfer){.address = address, .size = size, .fc = fc};
    int ok = !(fault_call && transfer_count == fault_call) && address != failed && address < sizeof ram &&
             size <= sizeof ram - address;
    if (ok) {
        memcpy(bytes, ram + address, size);
        for (unsigned i = 0; i < size; ++i)
            transfer->value = transfer->value << 8 | bytes[i];
    }
    control_reenter(address, ok);
    return ok;
}

static int control_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    (void)opaque;
    assert(transfer_count < 64);
    attributes_log[transfer_count] = attributes;
    Transfer* transfer = &transfers[transfer_count++];
    *transfer = (Transfer){.address = address, .size = size, .fc = fc, .writing = 1};
    for (unsigned i = 0; i < size; ++i)
        transfer->value = transfer->value << 8 | bytes[i];
    int ok = !(fault_call && transfer_count == fault_call) && address != failed && address < sizeof ram &&
             size <= sizeof ram - address;
    if (ok)
        memcpy(ram + address, bytes, size);
    control_reenter(address, ok);
    return ok;
}

static void control_setup(unsigned ea, unsigned command, unsigned mode)
{
    fixture(command, 0);
    memset(attributes_log, 0, sizeof attributes_log);
    memory.native = NULL;
    memory.tables = (Syn68kMMU040Bus){0};
    memory.read = control_read;
    memory.write = control_write;
    if (wrap_pc) {
        start = UINT32_MAX - 1;
        put(0, command, 2);
    }
    else
        put(start, 0xf200 | ea, 2);
    put(0x1012, 0x0022, 2);
    cpu_state.sr = mode & 1 ? 0x2000 : 0;
    cpu_state.fpu.fpcr = 0xff00fff0 ^ mode * 0x1234567;
    cpu_state.fpu.fpsr = 0x8fedcba9 ^ mode * 0x2468ace;
    cpu_state.fpu.fpiar = 0x76543210 ^ mode * 0x13579bdf;
    cpu_state.fpu.null_state = mode & 1;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.regs[i].ul.n = 0;
        cpu_state.regs[i + 8].ul.n = crossing ? 0x5fff : 0x6000;
    }
    unsigned mask = command >> 10 & 7;
    unsigned count = !!(mask & 4) + !!(mask & 2) + !!(mask & 1);
    uint32_t address = crossing ? 0x5fff : 0x6000;
    switch (ea >> 3) {
    case 4:
        address -= 4 * count;
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
            put(address + 0x10, 0x6100, 4);
            address = 0x6100;
        }
        else {
            put(start + 4, 0x10, 2);
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
            address = start + 4;
            break;
        }
    }
    for (unsigned i = 0; i < 3; ++i)
        if (mask & (4 >> i)) {
            put(address, 0xfedcba98 ^ i * 0x76543210 ^ mode * 0x13579bdf, 4);
            address += 4;
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
        }
    }
    if (native_bus || direct_ram)
        assert(syn68k_native040_memory(
            &memory, &native_memory, direct_ram ? (Syn68kNative040RAM){ram, 0, sizeof ram} : (Syn68kNative040RAM){0}));
}

static void control_step(unsigned ea, unsigned native, Syn68kFPUStep* result)
{
    if (through_cpu) {
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
        assert(syn68k_x64_mapped_fpu(start, 0xf200 | ea, &memory, result) == 1);
    else
        assert(syn68k_fpu_step_decoded(start, 0xf200 | ea, &memory, result));
}

static void compare_control(unsigned ea, unsigned command, unsigned mode, unsigned unsupported)
{
    control_setup(ea, command, mode);
    Syn68kFPUStep expected, actual;
    control_step(ea, 0, &expected);
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    memcpy(expected_table, table, sizeof table);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    memcpy(expected_attributes, attributes_log, sizeof attributes_log);
    control_setup(ea, command, mode);
    native_active = unsupported ? 2 : 1;
    captures = command_exits = 0;
    control_step(ea, 1, &actual);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Mapped control mismatch ea=%x cmd=%x mode=%u fail=%u vectors=%u/%u next=%x/%x\n", ea, command,
                mode, fault_call, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu) && !memcmp(expected_table, table, sizeof table));
    assert(!memcmp(expected_ram, ram, sizeof ram) && transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = transfers[i], b = expected_transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing && attributes_log[i] == expected_attributes[i]);
    }
    assert(command_exits == unsupported && !captures);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

#ifndef MAPPED_FPU_CONTROL_TEST_ENTRY
#define MAPPED_FPU_CONTROL_TEST_ENTRY main
#endif
int MAPPED_FPU_CONTROL_TEST_ENTRY(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    if (argc == 3) {
        for (unsigned mask = 1; mask <= 7; ++mask)
            for (unsigned direction = 0; direction < 2; ++direction)
                compare_control(24, 0x8000 | direction << 13 | mask << 10, mask, 0);
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]));
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]));
        syn68k_x64_mapped_clear();
        puts("Mapped FPU control AOT replayed live masks, directions and register values");
        return 0;
    }
    for (unsigned mode = 0; mode < 16; ++mode)
        for (unsigned ea = 16; ea < 64; ++ea)
            for (unsigned mask = 0; mask <= 7; ++mask)
                for (unsigned direction = 0; direction < 2; ++direction)
                    compare_control(ea, 0x8000 | direction << 13 | mask << 10, mode,
                                    !mask || ea > 60 || (direction && ea >= 58));
    native_bus = 1;
    for (unsigned ea = 16; ea <= 60; ++ea)
        for (unsigned mask = 1; mask <= 7; ++mask)
            for (unsigned direction = 0; direction < 2; ++direction)
                for (fault_call = 1; fault_call <= 9; ++fault_call)
                    if (!direction || ea < 58)
                        compare_control(ea, 0x8000 | direction << 13 | mask << 10, 1, 0);
    fault_call = 0;
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (crossing = 0; crossing < 2; ++crossing)
                        for (unsigned mask = 1; mask <= 7; ++mask)
                            for (unsigned direction = 0; direction < 2; ++direction)
                                compare_control(24, 0x8000 | direction << 13 | mask << 10, 1, 0);
    warm = direct_ram = 0;
    for (invalid_page = 1; invalid_page <= 12; ++invalid_page)
        for (unsigned direction = 0; direction < 2; ++direction)
            compare_control(24, 0x9c00 | direction << 13, 1, 0);
    invalid_page = 0;
    protected_page = 1;
    for (unsigned direction = 0; direction < 2; ++direction)
        compare_control(24, 0x9c00 | direction << 13, 1, 0);
    protected_page = 0;
    paging = direct_ram = warm = crossing = 0;
    for (control_callback = 1; control_callback <= 2; ++control_callback) {
        control_callbacks = 0;
        for (unsigned direction = 0; direction < 2; ++direction)
            compare_control(24, 0x9c00 | direction << 13, 1, 0);
        assert(control_callbacks == 4);
    }
    control_callback = 0;
    wrap_pc = 1;
    for (unsigned ea = 16; ea <= 60; ++ea)
        for (unsigned mask = 1; mask <= 7; ++mask)
            for (unsigned direction = 0; direction < 2; ++direction)
                if (!direction || ea < 58)
                    compare_control(ea, 0x8000 | direction << 13 | mask << 10, 1, 0);
    wrap_pc = 0;
    through_cpu = 1;
    for (unsigned ea = 16; ea <= 60; ++ea)
        for (unsigned direction = 0; direction < 2; ++direction)
            if (!direction || ea < 58)
                for (fault_call = 0; fault_call <= 6; ++fault_call)
                    compare_control(ea, 0x9c00 | direction << 13, 1, 0);
    fault_call = through_cpu = 0;
    const unsigned extensions[] = {0x0161, 0x0108, 0x0104, 0x0160, 0x0171, 0x0131};
    for (unsigned i = 0; i < sizeof extensions / sizeof *extensions; ++i) {
        indexed_extension = extensions[i];
        for (unsigned direction = 0; direction < 2; ++direction)
            for (fault_call = 0; fault_call <= 10; ++fault_call)
                compare_control(48, 0x9c00 | direction << 13, 1, 0);
    }
    indexed_extension = fault_call = 0;
    for (unsigned bit = 0; bit < 10; ++bit)
        for (unsigned direction = 0; direction < 2; ++direction)
            compare_control(24, 0x9c00 | direction << 13 | 1u << bit, 1, 1);
    syn68k_x64_mapped_clear();
    printf("%u mapped FPU control-memory list/mask/order/partial-fault/callback cases passed\n", cases);
    return 0;
}
