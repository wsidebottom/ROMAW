/*+===================================================================
  File:      mapped-fpu-frame-test.c

  Summary:   Compare mapped native FSAVE/FRESTORE ordering, idle/null state and captured exceptional frames.

  Classes:   None.

  Functions: main, __wrap_syn68k_fpu_finish_frame; imports frame_control_fixture and bounded mapped bus fixtures.

  Origin:    Original ARDI Executor direct mapped 68040 frame regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int frame_control_fixture(int argc, char** argv);
#define MAPPED_FPU_CONTROL_TEST_ENTRY frame_control_fixture
#include "mapped-fpu-control-test.c"
#undef MAPPED_FPU_CONTROL_TEST_ENTRY

static uint32_t frame_header;
static unsigned frame_pending, frame_mutation, frame_exits;
static uint32_t frame_pc;
static CPUState pipeline[64], expected_pipeline[64];

int __real_syn68k_fpu_finish_frame(const Syn68kFPUFrame*, Syn68kMMU040Memory*, Syn68kFPUStep*);

int __wrap_syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* bus, Syn68kFPUStep* result)
{
    assert(native_active == 2);
    ++frame_exits;
    native_active = 0;
    int ok = __real_syn68k_fpu_finish_frame(captured, bus, result);
    native_active = 2;
    return ok;
}

static void frame_callback(uint32_t address, int ok)
{
    if (!ok || !frame_mutation)
        return;
    if ((frame_mutation == 1 || frame_mutation == 3) && address == start + 2) {
        cpu_state.fpu.fpcr ^= 0x90;
        cpu_state.fpu.fpsr ^= 0x0f000300;
        cpu_state.fpu.null_state ^= 1;
        cpu_state.sr ^= 0x2000;
        if (frame_mutation == 3) {
            cpu_state.fpu.frame_size = 52;
            cpu_state.fpu.pending_vector = 55;
        }
    }
    if (frame_mutation == 4 && address == 0x6000) {
        CPUState saved = cpu_state;
        Syn68kFPUStep child;
        assert(syn68k_x64_mapped_fpu(0x1010, 0xf200, &memory, &child) == 1);
        assert(!child.vector && child.next == 0x1014);
        syn68k_x64_mapped_clear();
        cpu_state = saved;
    }
    if (frame_mutation == 2 && address == 0x6000) {
        cpu_state.fpu.fpcr ^= 0x90;
        cpu_state.fpu.null_state ^= 1;
        cpu_state.fpu.frame_size = 100;
        cpu_state.fpu.pending_vector = 55;
        cpu_state.regs[8].ul.n ^= 0x100;
    }
}

static int frame_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attrs, unsigned fc)
{
    assert(transfer_count < 64);
    pipeline[transfer_count] = cpu_state;
    int ok = control_read(opaque, address, bytes, size, attrs, fc);
    frame_callback(address, ok);
    return ok;
}

static unsigned mutate_save, software_e1, require_native_save;

static int frame_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attrs,
                       unsigned fc)
{
    assert(transfer_count < 64);
    pipeline[transfer_count] = cpu_state;
    int ok = control_write(opaque, address, bytes, size, attrs, fc);
    if (mutate_save && transfer_count == mutate_save) {
        cpu_state.fpu.frame[48] ^= 0xa5;
        cpu_state.fpu.frame[96] ^= 0x5a;
        cpu_state.fpu.frame_size ^= 52;
        cpu_state.fpu.pending_vector = 54;
    }
    frame_callback(address, ok);
    return ok;
}

static void frame_put(uint32_t address, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) {
        unsigned physical = address + i;
        if (paging && bank)
            physical ^= 0x4000;
        assert(physical < sizeof ram);
        ram[physical] = value >> ((size - 1 - i) * 8);
    }
}

static void frame_setup(unsigned opcode, unsigned mode)
{
    unsigned save_warm = warm;
    warm = 0;
    control_setup(opcode & 63, 0, mode);
    warm = save_warm;
    if (frame_pc)
        start = frame_pc;
    if (!wrap_pc)
        frame_put(start, opcode, 2);
    uint32_t address = crossing ? 0x5fff : 0x6000;
    unsigned ea = opcode & 63;
    switch (ea >> 3) {
    case 4:
        address -= 4;
        break;
    case 5:
        frame_put(start + 2, 0x10, 2);
        address += 0x10;
        break;
    case 6:
        frame_put(start + 2, indexed_extension ? indexed_extension : 0x10, 2);
        if (indexed_extension) {
            frame_put(start + 4, 0x10, 2);
            frame_put(start + 6, 0x20, 2);
            frame_put(address + 0x10, 0x6100, 4);
            address = 0x6100;
        }
        else
            address += 0x10;
        break;
    case 7:
        switch (ea & 7) {
        case 0:
            frame_put(start + 2, address, 2);
            break;
        case 1:
            frame_put(start + 2, address, 4);
            break;
        case 2:
            frame_put(start + 2, address - (start + 2), 2);
            break;
        case 3:
            frame_put(start + 2, 0x10, 2);
            address = start + 2 + 0x10;
            break;
        case 4:
            address = start + 2;
            break;
        }
    }
    if (address < sizeof ram - 100) {
        for (unsigned i = 0; i < 100; ++i)
            frame_put(address + i, 0, 1);
        frame_put(address, frame_header, 4);
    }
    for (unsigned i = 0; i < sizeof cpu_state.fpu.frame; ++i)
        cpu_state.fpu.frame[i] = i ^ 0xa5;
    cpu_state.fpu.frame_size = frame_pending;
    cpu_state.fpu.pending_vector = frame_pending ? 55 : 0;
    if (software_e1) {
        cpu_state.fpu.frame[0x48] = 5;
        cpu_state.fpu.frame[0x49] = 0x80;
        cpu_state.fpu.pending_vector = software_e1;
    }
    memset(pipeline, 0, sizeof pipeline);
    transfer_count = 0;
    memset(transfers, 0, sizeof transfers);
    memset(attributes_log, 0, sizeof attributes_log);
    if (warm && paging && !invalid_page) {
        uint32_t value;
        Syn68kMMU040Access access = syn68k_mmu040_read(&memory, start + 2, 6, 2, 0, &value);
        assert(!access.bus_error && !access.translation.fault);
        transfer_count = 0;
        memset(transfers, 0, sizeof transfers);
        memset(attributes_log, 0, sizeof attributes_log);
    }
    memory.read = frame_read;
    memory.write = frame_write;
}

static void frame_step(unsigned opcode, int native, Syn68kFPUStep* result)
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
        assert(syn68k_x64_mapped_fpu(start, opcode, &memory, result) == 1);
    else
        assert(syn68k_fpu_step_decoded(start, opcode, &memory, result));
}

static void compare_frame(unsigned opcode, unsigned mode)
{
    frame_setup(opcode, mode);
    Syn68kFPUStep expected, actual;
    frame_step(opcode, 0, &expected);
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    memcpy(expected_table, table, sizeof table);
    unsigned expected_count = transfer_count;
    memcpy(expected_transfers, transfers, sizeof transfers);
    memcpy(expected_attributes, attributes_log, sizeof attributes_log);
    memcpy(expected_pipeline, pipeline, sizeof pipeline);
    frame_setup(opcode, mode);
    native_active = !require_native_save &&
                            (frame_pending || frame_mutation == 3 || (frame_header >> 24 && frame_header != 0x41000000))
                        ? 2
                        : 1;
    captures = command_exits = frame_exits = 0;
    frame_step(opcode, 1, &actual);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Frame mismatch op=%x mode=%u hdr=%x pending=%u mutate=%u fail=%u vector=%u/%u next=%x/%x\n",
                opcode, mode, frame_header, frame_pending, frame_mutation, fault_call, expected.vector, actual.vector,
                expected.next, actual.next);
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
    assert(!command_exits && !captures && !syn68k_native_checkpoint());
    ++cases;
}

static void busy_save_checks(void)
{
    const unsigned opcodes[] = {0xf310, 0xf320, 0xf328, 0xf330, 0xf338, 0xf339};
    require_native_save = 1;
    for (unsigned kind = 0; kind < 5; ++kind) {
        frame_pending = kind ? 100 : 52;
        software_e1 = kind >= 2 ? 46 + kind * 2 : 0;
        for (unsigned i = 0; i < sizeof opcodes / sizeof *opcodes; ++i)
            for (crossing = 0; crossing < 2; ++crossing)
                for (mutate_save = 0; mutate_save <= 2; ++mutate_save)
                    for (fault_call = 0; fault_call <= 28; ++fault_call)
                        compare_frame(opcodes[i], 1);
    }
    require_native_save = frame_pending = software_e1 = mutate_save = crossing = fault_call = 0;
}

static void reserved_frame_eas(void)
{
    const unsigned invalid[] = {0xf33a, 0xf33b, 0xf33c, 0xf37c, 0xf37d};
    unsigned cases = 0;
    frame_header = 0x41000000;
    fault_call = 1;
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i)
        for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
            for (unsigned native = 0; native < 2; ++native) {
                frame_setup(invalid[i], supervisor);
                CPUState before = cpu_state;
                Syn68kMMU040 old_mmu = mmu;
                Syn68kFPUStep result;
                native_active = native;
                frame_step(invalid[i], native, &result);
                native_active = 0;
                assert(result.vector == (supervisor ? 11 : 8) && !result.format &&
                       result.next == start && result.exception_pc == start);
                assert(!transfer_count && !memcmp(&before, &cpu_state, sizeof before) &&
                       !memcmp(&old_mmu, &mmu, sizeof mmu));
                ++cases;
            }
    fault_call = 0;
    printf("%u reserved frame EA/privilege cases trapped before operand access\n", cases);
}
int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    const uint32_t headers[] = {0, 0x123456, 0x41000000, 0x41300000, 0x41600000, 0x42000000, 0x41000001};
    if (argc == 3) {
        for (unsigned i = 0; i < 16; ++i) {
            frame_header = headers[i % 3];
            compare_frame(0xf310, i | 1);
            compare_frame(0xf358, i | 1);
        }
        assert(syn68k_aot_hit_count() == (unsigned)atoi(argv[2]) * 2);
        assert(syn68k_native_compile_count() == (unsigned)!atoi(argv[2]) * 2);
        uint64_t before_emitted = syn68k_native_compile_count(), before_hits = syn68k_aot_hit_count();
        busy_save_checks();
        uint64_t emitted = syn68k_native_compile_count() - before_emitted;
        uint64_t hits = syn68k_aot_hit_count() - before_hits;
        assert(atoi(argv[2]) ? !emitted && hits : emitted > 0);
        printf("%u mapped frame AOT cases passed; busy emitted=%llu hits=%llu\n", cases, (unsigned long long)emitted,
               (unsigned long long)hits);
        before_emitted = syn68k_native_compile_count();
        before_hits = syn68k_aot_hit_count();
        reserved_frame_eas();
        emitted = syn68k_native_compile_count() - before_emitted;
        hits = syn68k_aot_hit_count() - before_hits;
        assert(atoi(argv[2]) ? !emitted && hits : emitted > 0);
        return 0;
    }
    reserved_frame_eas();
    busy_save_checks();
    for (unsigned h = 0; h < sizeof headers / sizeof *headers; ++h) {
        frame_header = headers[h];
        for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode)
            for (unsigned mode = 0; mode < 16; ++mode)
                compare_frame(opcode, mode);
    }
    frame_header = 0x41000000;
    for (frame_pending = 52; frame_pending <= 100; frame_pending += 48)
        for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode)
            compare_frame(opcode, 1);
    frame_pending = 0;
    for (frame_mutation = 1; frame_mutation <= 4; ++frame_mutation)
        for (unsigned h = 0; h < sizeof headers / sizeof *headers; ++h) {
            frame_header = headers[h];
            for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode)
                compare_frame(opcode, 1);
        }
    frame_mutation = 0;
    for (fault_call = 1; fault_call <= 28; ++fault_call)
        for (unsigned h = 0; h < sizeof headers / sizeof *headers; ++h) {
            frame_header = headers[h];
            for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode)
                compare_frame(opcode, 1);
        }
    for (frame_pending = 52; frame_pending <= 100; frame_pending += 48)
        for (fault_call = 1; fault_call <= 28; ++fault_call)
            for (unsigned opcode = 0xf300; opcode < 0xf340; ++opcode)
                compare_frame(opcode, 1);
    frame_pending = 0;
    frame_mutation = 3;
    for (fault_call = 1; fault_call <= 6; ++fault_call) {
        compare_frame(0xf328, 1);
        compare_frame(0xf330, 1);
    }
    frame_mutation = fault_call = 0;
    const unsigned examples[] = {0xf310, 0xf320, 0xf328, 0xf330, 0xf338, 0xf339, 0xf350, 0xf358,
                                 0xf368, 0xf370, 0xf378, 0xf379, 0xf37a, 0xf37b, 0xf37c};
    paging = 1;
    for (page8k = 0; page8k < 2; ++page8k)
        for (bank = 0; bank < 2; ++bank)
            for (direct_ram = 0; direct_ram < 2; ++direct_ram)
                for (warm = 0; warm < 2; ++warm)
                    for (crossing = 0; crossing < 2; ++crossing)
                        for (unsigned h = 0; h < sizeof headers / sizeof *headers; ++h) {
                            frame_header = headers[h];
                            for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
                                compare_frame(examples[i], 1);
                        }
    direct_ram = warm = bank = page8k = 0;
    crossing = 1;
    for (invalid_page = 1; invalid_page <= 2; ++invalid_page)
        for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
            compare_frame(examples[i], 1);
    invalid_page = 0;
    protected_page = 1;
    for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
        compare_frame(examples[i], 1);
    protected_page = crossing = 0;
    for (unsigned edge = 0; edge < 2; ++edge) {
        frame_pc = edge ? 0x1ffe : 0x1ffc;
        for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
            compare_frame(examples[i], 1);
    }
    frame_pc = paging = 0;
    for (indexed_extension = 0x0121; indexed_extension <= 0x0123; ++indexed_extension) {
        compare_frame(0xf330, 1);
        compare_frame(0xf370, 1);
    }
    indexed_extension = 0x0108; // Reserved full-index extension.
    compare_frame(0xf330, 1);
    compare_frame(0xf370, 1);
    indexed_extension = 0;
    through_cpu = 1;
    for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode)
        for (fault_call = 0; fault_call < 5; ++fault_call)
            compare_frame(opcode, 1);
    through_cpu = fault_call = 0;
    wrap_pc = 1;
    for (unsigned i = 0; i < sizeof examples / sizeof *examples; ++i)
        compare_frame(examples[i], 1);
    wrap_pc = 0;
    frame_header = 0x41000000;
    frame_setup(0xf350, 1);
    CPUState unchanged = cpu_state;
    Syn68kFPUStep guard, before;
    memset(&guard, 0x5a, sizeof guard);
    before = guard;
    Syn68kFPUFrame valid = {
        .pc = start, .next = start + 2, .ea = 0x6000, .opcode = 0xf350, .header = frame_header, .entry = {0, 0}};
    for (unsigned bad = 0; bad < 13; ++bad) {
        Syn68kFPUFrame invalid = valid;
        switch (bad) {
        case 0:
            invalid.pc |= 1;
            break;
        case 1:
            invalid.next |= 1;
            break;
        case 2:
            invalid.next = invalid.pc;
            break;
        case 3:
            invalid.next = invalid.pc + 14;
            break;
        case 4:
            invalid.opcode = 0xf200;
            break;
        case 5:
            invalid.opcode = 0xf340;
            break;
        case 6:
            invalid.opcode = 0xf348;
            break;
        case 7:
            invalid.opcode = 0xf360;
            break;
        case 8:
            invalid.opcode = 0xf318;
            break;
        case 9:
            invalid.opcode = 0xf33a;
            break;
        case 10:
            invalid.opcode = 0xf37d;
            break;
        case 11:
            invalid.entry.original_null = 2;
            break;
        case 12:
            invalid.opcode = 0xf37c;
            break;
        }
        assert(!__real_syn68k_fpu_finish_frame(&invalid, &memory, &guard));
        assert(!memcmp(&unchanged, &cpu_state, sizeof unchanged) && !memcmp(&guard, &before, sizeof guard));
        assert(!transfer_count);
        ++cases;
    }
    printf("%u mapped FPU frame/null/idle/exception/fault/callback cases passed\n", cases);
    return 0;
}
