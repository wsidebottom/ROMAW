/*+===================================================================
  File:      flat-fpu-native-test.c

  Summary:   Validate flat FPU component/dispatch execution, native memory bindings and instruction trace boundaries.

  Classes:   None.

  Functions: main, __wrap_syn68k_fpu_finish_destination, __wrap_syn68k_fpu_finish_frame, __wrap_trap_direct,
             __wrap_trap_exception; imports bounded FPU fixtures and checked-exit wrappers.

  Origin:    Original ARDI Executor direct flat-memory 68040 FPU regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
int flat_control_fixture(int argc, char** argv);
#define MAPPED_FPU_CONTROL_TEST_ENTRY flat_control_fixture
#include "mapped-fpu-control-test.c"
#undef MAPPED_FPU_CONTROL_TEST_ENTRY

extern int (*syn68k_bus_read)(uint32, unsigned, uint32*);
static unsigned flat_device_kind, flat_device_format, flat_device_fail;
static unsigned flat_device_count;
static unsigned flat_trace_live_sr;

typedef struct {
    uint32_t address, value;
    unsigned size, ok;
    Syn68kFPUState fpu;
    uint32_t regs[16];
    uint16_t sr;
} FlatDeviceEvent;

static FlatDeviceEvent flat_events[64], expected_flat_events[64];

static int flat_device_read(uint32 address, unsigned size, uint32* value)
{
    assert(flat_device_count < 64);
    FlatDeviceEvent* event = &flat_events[flat_device_count++];
    *event = (FlatDeviceEvent){.address = address, .size = size, .fpu = cpu_state.fpu, .sr = cpu_state.sr};
    for (unsigned i = 0; i < 16; ++i)
        event->regs[i] = cpu_state.regs[i].ul.n;
    if (flat_device_kind == 8 && flat_device_count == flat_device_fail)
        return 0;
    if (flat_device_kind == 9) {
        CPUState saved = cpu_state;
        Syn68kFPUStep child;
        assert(syn68k_x64_flat_fpu(0x1020, 0xf200, &child) == 1 && !child.vector && child.next == 0x1024);
        syn68k_x64_mapped_clear();
        cpu_state = saved;
    }
    if (flat_device_count == 1) {
        if (flat_device_kind == 15 || flat_device_kind == 16)
            cpu_state.sr = (cpu_state.sr & ~0xc000) | flat_trace_live_sr;
        if (flat_device_kind >= 2 && flat_device_kind <= 7) {
            cpu_state.fpu.fpcr ^= 0x50;
            cpu_state.fpu.null_state ^= 1;
            cpu_state.sr ^= 0x2000;
        }
        if (flat_device_kind >= 3 && flat_device_kind <= 7) {
            cpu_state.fpu.frame_size = 52;
            cpu_state.fpu.pending_vector = 55;
            for (unsigned i = 0; i < sizeof cpu_state.fpu.frame; ++i)
                cpu_state.fpu.frame[i] = i ^ 0xa5;
        }
        if (flat_device_kind == 6 || flat_device_kind == 7)
            cpu_state.fpu.regs[0] = (Syn68kFP80){0x4000, UINT64_C(0x8000000000000000)};
        if (flat_device_kind == 7)
            cpu_state.fpu.fpcr |= 0x200;
        if (flat_device_kind == 13 || flat_device_kind == 14) {
            syn68k_address_mask = 0x7fff;
            if (flat_device_kind == 14) {
                ROMlib_offset = (uintptr_t)ram + 0x1000;
                syn68k_direct_memory_size = sizeof ram - 0x1000;
            }
        }
    }
    if (flat_device_kind == 6 || flat_device_kind == 7)
        *value = 0x6000; // Full-index pointer, consumed once before move-out conversion.
    else if (flat_device_kind == 10 || flat_device_kind == 16)
        *value = 0x41000000;
    else if (flat_device_kind == 11)
        *value = 0;
    else if (flat_device_kind == 12)
        *value = 0x41300000;
    else {
        unsigned offset = address & 15;
        switch (flat_device_format) {
        case 1:
            *value = flat_device_kind == 4 ? 0x7f800001 : flat_device_kind == 5 ? 1 : 0x3eaaaaab;
            break;
        case 2:
            *value = !offset ? 0x40000000 : offset == 4 ? 0x80000000 : 0;
            break;
        case 5:
            *value = !offset ? 0x3ff00000 : 0;
            break;
        default:
            *value = 2;
            break;
        }
    }
    event->ok = 1;
    event->value = *value;
    return 1;
}

static unsigned char *flat_aliases, *expected_flat_aliases;
static void* flat_alias_allocation;
static uint32 flat_alias_address, flat_dynamic_mask;
static unsigned flat_alias_24;

static unsigned flat_compiled, flat_frame;
static Syn68kFPUStep flat_trap;

int __real_syn68k_fpu_finish_destination(const Syn68kFPUCaptured*, Syn68kMMU040Memory*, Syn68kFPUStep*);
int __real_syn68k_fpu_finish_frame(const Syn68kFPUFrame*, Syn68kMMU040Memory*, Syn68kFPUStep*);
uint32 __real_trap_direct(uint32, uint32, uint32);

int __wrap_syn68k_fpu_finish_destination(const Syn68kFPUCaptured* captured, Syn68kMMU040Memory* bus,
                                         Syn68kFPUStep* result)
{
    assert(native_active == 2);
    unsigned saved = native_active;
    native_active = 0;
    int ok = __real_syn68k_fpu_finish_destination(captured, bus, result);
    native_active = saved;
    return ok;
}

int __wrap_syn68k_fpu_finish_frame(const Syn68kFPUFrame* captured, Syn68kMMU040Memory* bus, Syn68kFPUStep* result)
{
    assert(native_active == 2);
    unsigned saved = native_active;
    native_active = 0;
    int ok = __real_syn68k_fpu_finish_frame(captured, bus, result);
    native_active = saved;
    return ok;
}

uint32 __wrap_trap_direct(uint32 vector, uint32 pc, uint32 address)
{
    assert(native_active != 1);
    unsigned saved = native_active;
    native_active = 0;
    uint32 next = __real_trap_direct(vector, pc, address);
    native_active = saved;
    return next;
}

uint32 __wrap_trap_exception(uint32 vector, uint32 pc, uint32 address, unsigned format)
{
    flat_trap = (Syn68kFPUStep){
        .next = start, .exception_pc = pc, .fault_address = address, .vector = vector, .format = format};
    return 0x7654;
}

static void flat_setup(unsigned opcode, unsigned command, unsigned mode)
{
    setup(opcode & 63, command, mode);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(sizeof ram, 0xe00000, 0x40000);
    syn68k_address_mask = UINT32_MAX;
    put(start, opcode, 2);
    if ((opcode & ~127u) == 0xf300) {
        /* Frame EAs start immediately after the opcode. */
        memmove(ram + start + 2, ram + start + 4, 12);
        cpu_state.sr |= 0x2000;
        put(0x6000, flat_frame == 1 ? 0x41300000 : flat_frame == 2 ? 0x41600000 : mode & 1 ? 0 : 0x41000000, 4);
        for (unsigned i = 4; i < 100; ++i)
            ram[0x6000 + i] = 0;
        if (flat_frame && !(opcode & 0x40)) {
            cpu_state.fpu.null_state = 0;
            cpu_state.fpu.frame_size = flat_frame == 1 ? 52 : 100;
            cpu_state.fpu.pending_vector = 49;
            for (unsigned i = 0; i < cpu_state.fpu.frame_size; ++i)
                cpu_state.fpu.frame[i] = 0x50 ^ i;
            cpu_state.fpu.frame[0] = 0x41;
            cpu_state.fpu.frame[1] = flat_frame == 1 ? 0x30 : 0x60;
        }
    }
    syn68k_bus_read = flat_device_kind ? flat_device_read : NULL;
    flat_device_format = command >> 10 & 7;
    flat_device_count = 0;
    memset(flat_events, 0, sizeof flat_events);
    if (flat_device_kind) {
        for (unsigned i = 8; i < 16; ++i)
            cpu_state.regs[i].ul.n = 0x8000;
        put(0x1020, 0xf200, 2);
        put(0x1022, 0x0022, 2);
        if (flat_device_kind == 6 || flat_device_kind == 7) {
            put(start + 4, 0x0121, 2);
            put(start + 6, 0x0010, 2);
            cpu_state.fpu.regs[0] = flat_device_kind == 6 ? (Syn68kFP80){0x401f, UINT64_C(0x8000000000000000)}
                                                          : (Syn68kFP80){0x3fff, UINT64_C(0xc000000000000000)};
        }
        if (flat_device_kind == 13 || flat_device_kind == 14) {
            unsigned base = flat_device_kind == 14 ? 0x1000 : 0;
            put(base + 4, 0x80000000, 4);
            put(base + 8, 0, 4);
        }
    }
    if (flat_alias_address) {
        syn68k_set_memory_window(sizeof ram, 0xf00000, 0x100000);
        syn68k_address_mask = flat_alias_24 ? 0xffffff : UINT32_MAX;
        memset(flat_aliases, 0xa5, 0x100000);
        for (unsigned i = 0; i < 8; ++i)
            assert(syn68k_host_to_guest((uintptr_t)flat_aliases + i * 0x20000) == 0xf00000 + i * 0x20000);
        unsigned size;
        if ((opcode & ~127u) == 0xf300)
            size = flat_frame == 1 ? 52 : flat_frame == 2 ? 100 : 4;
        else if (command >> 13 >= 6) {
            unsigned mask = command & 0x0800 ? flat_dynamic_mask : command & 255;
            if (command & 0x0800)
                cpu_state.regs[command >> 4 & 7].ul.n = mask;
            unsigned count = 0;
            for (unsigned i = 0; i < 8; ++i)
                count += !!(mask & (1u << i));
            size = count * 12;
        }
        else if (command >> 13 == 4 || command >> 13 == 5) {
            unsigned mask = command >> 10 & 7;
            size = 4 * (!!(mask & 4) + !!(mask & 2) + !!(mask & 1));
        }
        else {
            const unsigned sizes[] = {4, 4, 12, 12, 2, 8, 1, 12};
            size = sizes[command >> 10 & 7];
        }
        unsigned predecrement = (opcode >> 3 & 7) == 4;
        uint32 original = 0x6000 - (predecrement ? size : 0);
        uint32 target = flat_alias_address - (predecrement ? size : 0);
        for (unsigned i = 0; i < size; ++i)
            if (syn68k_memory_span(target + i, 1))
                *(unsigned char*)SYN68K_TO_US(target + i) = ram[original + i];
        for (unsigned i = 8; i < 16; ++i)
            cpu_state.regs[i].ul.n = flat_alias_address;
    }
    memset(&flat_trap, 0, sizeof flat_trap);
}

static void flat_compare(unsigned opcode, unsigned command, unsigned mode, unsigned exceptional)
{
    flat_setup(opcode, command, mode);
    uint16 entry_sr = cpu_state.sr;
    Syn68kFPUStep expected, actual;
    assert(syn68k_fpu_step_decoded(start, opcode, NULL, &expected));
    if (flat_compiled && !expected.vector) {
        syn68k_fpu_trace_result(&expected, entry_sr, start);
        if (expected.vector)
            expected = (Syn68kFPUStep){.next = start,
                                       .exception_pc = expected.exception_pc,
                                       .fault_address = expected.fault_address,
                                       .vector = expected.vector,
                                       .format = expected.format};
    }
    CPUState expected_cpu = cpu_state;
    memcpy(expected_ram, ram, sizeof ram);
    if (flat_alias_address)
        memcpy(expected_flat_aliases, flat_aliases, 0x100000);
    unsigned expected_calls = flat_device_count;
    memcpy(expected_flat_events, flat_events, sizeof flat_events);
    uintptr_t expected_offset = ROMlib_offset;
    uint32 expected_mask = syn68k_address_mask, expected_size = syn68k_direct_memory_size;
    flat_setup(opcode, command, mode);
    native_active = expected.vector || exceptional ? 2 : 1;
    if (flat_compiled) {
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(start), &length, 1, &translated, start, &terminal);
        assert(code && translated == 1 && terminal);
        uint32 next = syn68k_x64_execute(code);
        actual = flat_trap.vector ? flat_trap : (Syn68kFPUStep){.next = next};
        /* The compiled frontend returns PC/trap data, not component-only
           T0 eligibility. Direct component calls still compare that field. */
        expected.trace_flow = 0;
        expected.prefetch_address = 0;
        if (flat_trap.vector)
            assert(next == 0x7654);
        syn68k_x64_release(code);
    }
    else
        assert(syn68k_x64_flat_fpu(start, opcode, &actual) == 1);
    native_active = 0;
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state) || memcmp(&expected, &actual, sizeof expected) ||
        memcmp(expected_ram, ram, sizeof ram))
        fprintf(stderr, "Flat FPU mismatch compiled=%u op=%x cmd=%x mode=%u vectors=%u/%u pc=%x/%x\n", flat_compiled,
                opcode, command, mode, expected.vector, actual.vector, expected.next, actual.next);
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(expected_ram, ram, sizeof ram));
    if (flat_alias_address)
        assert(!memcmp(expected_flat_aliases, flat_aliases, 0x100000));
    assert(expected_calls == flat_device_count);
    assert(!memcmp(expected_flat_events, flat_events, sizeof flat_events));
    assert(expected_offset == ROMlib_offset && expected_mask == syn68k_address_mask &&
           expected_size == syn68k_direct_memory_size);
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static void flat_trace_setup(unsigned opcode, unsigned command, unsigned counter, unsigned special, unsigned sr)
{
    flat_setup(opcode, command, 0);
    cpu_state.sr = (cpu_state.sr & ~0xc000) | sr;
    cpu_state.fpu.fpcr = cpu_state.fpu.fpsr = cpu_state.fpu.frame_size = cpu_state.fpu.pending_vector = 0;
    cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff, UINT64_C(0xa000000000000000)};
    cpu_state.fpu.regs[1] = (Syn68kFP80){0x4000, UINT64_C(0x8000000000000000)};
    if ((opcode & 0xfff8) == 0xf248) {
        cpu_state.regs[0].ul.n = counter;
        put(start + 4, 2, 2);
    }
    if (special == 3 || special == 4) {
        if ((opcode & 0xfff8) == 0xf248 || (opcode & 0xffc0) == 0xf2c0)
            put(start + 4, 1, 2);
    }
    if (special == 1 || special == 4) {
        cpu_state.fpu.fpcr = 0x8000;
        cpu_state.fpu.fpsr = 0x01000000;
    }
    else if (special == 2) {
        cpu_state.fpu.fpcr = 0x200;
        cpu_state.fpu.regs[0] = (Syn68kFP80){0x3fff, UINT64_C(0xa000000000000000)};
    }
}

static void flat_trace_compare(unsigned opcode, unsigned command, unsigned counter, unsigned special, unsigned flow,
                               unsigned sr, unsigned compiled)
{
    flat_trace_setup(opcode, command, counter, special, sr);
    Syn68kFPUStep expected;
    assert(syn68k_fpu_step_decoded(start, opcode, NULL, &expected));
    unsigned vector = (special == 1 || special == 4)                                                   ? 48
                      : special == 2                                                                   ? 49
                      : opcode == 0xf27c                                                               ? 7
                      : (opcode == 0xf200 && command == 0x5c00) || (opcode == 0xf240 && command == 32) ? 11
                                                                                                       : 0;
    assert(expected.vector == vector);
    if (!expected.vector)
        assert(expected.trace_flow == flow);
    CPUState expected_cpu = cpu_state;
    memcpy(expected_ram, ram, sizeof ram);
    unsigned expected_calls = flat_device_count;
    memcpy(expected_flat_events, flat_events, sizeof flat_events);
    if (!expected.vector && special == 3) {
        unsigned reference = start + ((opcode & 0xfff8) == 0xf248 ? 5 : 3);
        assert(expected.prefetch_address == reference);
        expected = (Syn68kFPUStep){
            .next = start, .exception_pc = start, .fault_address = reference & ~1u, .vector = 3, .format = 2};
    }
    if (!expected.vector && ((sr & 0x8000) || ((sr & 0x4000) && flow)))
        expected = (Syn68kFPUStep){
            .next = start, .exception_pc = expected.next, .fault_address = start, .vector = 9, .format = 2};
    /* Both dispatch frontends return a PC or the architectural trap record. */
    expected.trace_flow = 0;
    expected.prefetch_address = 0;
    flat_trace_setup(opcode, command, counter, special, sr);
    native_active = compiled ? expected.vector ? 2 : 1 : 0;
    uint32 next;
    if (compiled) {
        int8 length = 1;
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(start), &length, 1, &translated, start, &terminal);
        assert(code && translated == 1 && terminal);
        next = syn68k_x64_execute(code);
        syn68k_x64_release(code);
    }
    else
        assert(syn68k_fpu_execute(start, &next));
    native_active = 0;
    Syn68kFPUStep actual = flat_trap.vector ? flat_trap : (Syn68kFPUStep){.next = next};
    if (flat_trap.vector)
        assert(next == 0x7654);
    if (memcmp(&expected, &actual, sizeof expected))
        fprintf(stderr, "Flat trace compiled=%u opcode=%x command=%x SR=%x vectors=%u/%u PC=%x/%x\n", compiled, opcode,
                command, sr, expected.vector, actual.vector, expected.exception_pc, actual.exception_pc);
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(expected_ram, ram, sizeof ram));
    assert(expected_calls == flat_device_count && !memcmp(expected_flat_events, flat_events, sizeof flat_events));
    assert(!syn68k_native_checkpoint());
    ++cases;
}

static void flat_trace_cases(void)
{
    const struct {
        unsigned opcode, command, counter, flow, special;
    } instructions[] = {
        {0xf200, 0x0022, 0, 0, 0}, {0xf210, 0x4022, 0, 0, 0}, {0xf210, 0x6000, 0, 0, 0}, {0xf240, 15, 0, 0, 0},
        {0xf250, 15, 0, 0, 0},     {0xf280, 2, 0, 0, 0},      {0xf28f, 2, 0, 1, 0}, // Taken branch targets fallthrough.
        {0xf248, 15, 0, 1, 0},     {0xf248, 0, 0, 1, 0},      {0xf248, 0, 1, 1, 0},      {0xf200, 0xb000, 0, 1, 0},
        {0xf200, 0x9000, 0, 1, 0}, {0xf210, 0xbc00, 0, 1, 0}, {0xf210, 0x9c00, 0, 1, 0}, {0xf210, 0xf080, 0, 1, 0},
        {0xf210, 0xd080, 0, 1, 0}, {0xf310, 0, 0, 1, 0},      {0xf350, 0, 0, 1, 0},      {0xf27c, 15, 0, 0, 0},
        {0xf200, 0x5c00, 0, 0, 0}, {0xf240, 32, 0, 0, 0},     {0xf240, 16, 0, 0, 1},     {0xf210, 0x6000, 0, 0, 2},
        {0xf280, 1, 0, 0, 3},      {0xf28f, 1, 0, 1, 3},      {0xf2c0, 0, 0, 0, 3},      {0xf2cf, 0, 0, 1, 3},
        {0xf248, 0, 0, 1, 3},      {0xf248, 0, 1, 1, 3},      {0xf248, 15, 1, 1, 3},     {0xf290, 1, 0, 0, 4},
        {0xf248, 16, 1, 1, 4},
    };

    for (unsigned compiled = 0; compiled < 2; ++compiled)
        for (unsigned trace = 0; trace < 4; ++trace) {
            for (unsigned i = 0; i < sizeof instructions / sizeof *instructions; ++i)
                flat_trace_compare(instructions[i].opcode, instructions[i].command, instructions[i].counter,
                                   instructions[i].special, instructions[i].flow, trace << 14, compiled);
            for (unsigned live = 0; live < 3; ++live) {
                flat_trace_live_sr = live << 14;
                flat_device_kind = 15;
                flat_trace_compare(0xf210, 0x4022, 0, 0, 0, trace << 14, compiled);
                flat_trace_compare(0xf210, 0x9c00, 0, 0, 1, trace << 14, compiled);
                flat_trace_compare(0xf210, 0xd080, 0, 0, 1, trace << 14, compiled);
                flat_device_kind = 16;
                flat_trace_compare(0xf350, 0, 0, 0, 1, trace << 14, compiled);
                flat_device_kind = 0;
            }
        }
    syn68k_bus_read = NULL;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 4);
    assert(!_putenv_s("SYN68K_AOT_CACHE", ""));
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(sizeof ram, 0, 0);
    flat_alias_allocation = malloc(0x120000);
    expected_flat_aliases = malloc(0x100000);
    assert(flat_alias_allocation && expected_flat_aliases);
    flat_aliases = (unsigned char*)(((uintptr_t)flat_alias_allocation + 0x1ffff) & ~(uintptr_t)0x1ffff);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    if (argc == 4) {
        assert(!_putenv_s("SYN68K_AOT_CACHE", argv[1]));
        unsigned warm = atoi(argv[2]);
        if (atoi(argv[3]) == 2) {
            uint64 emitted_before = syn68k_native_compile_count(), hits_before = syn68k_aot_hit_count();
            flat_trace_cases();
            for (flat_frame = 1; flat_frame <= 2; ++flat_frame) {
            flat_compare(0xf310, 0, 0, 0);
            flat_compare(0xf320, 0, 0, 0);
            flat_compare(0xf350, 0, 0, 0);
            flat_compare(0xf358, 0, 0, 0);
        }
        flat_frame = 0;
        uint64 emissions = syn68k_native_compile_count() - emitted_before;
            uint64 hits = syn68k_aot_hit_count() - hits_before;
            assert(warm ? !emissions && hits : emissions);
            printf("%u flat frontend trace cases passed; emitted=%llu hits=%llu\n", cases,
                   (unsigned long long)emissions, (unsigned long long)hits);
            syn68k_x64_mapped_clear();
            free(flat_alias_allocation);
            free(expected_flat_aliases);
            return 0;
        }
        flat_compiled = atoi(argv[3]);
        uint64 emitted_before = syn68k_native_compile_count(), hits_before = syn68k_aot_hit_count();
        const unsigned formats[] = {0, 1, 2, 4, 5, 6};
        const unsigned operations[] = {0, 4, 0x20, 0x22, 0x38, 0x3a};
        for (unsigned sample = 0; sample < 16; ++sample) {
            unsigned format = formats[sample % 6];
            flat_compare(0xf210, 0x4000 | format << 10 | operations[sample % 6], sample, 0);
            flat_compare(0xf211, 0x6000 | format << 10 | (sample & 7) << 7, sample, 0);
            flat_compare(0xf212, 0x8000 | (sample & 1) << 13 | (sample % 7 + 1) << 10, sample, 0);
            flat_compare(0xf213, 0xd000 | ((sample & 1) ? 0xa5 : 0xff), sample, 0);
            flat_compare(0xf254, sample | 16, sample, 0);
            flat_compare(0xf356, 0, sample, 0);
        }
        for (flat_frame = 1; flat_frame <= 2; ++flat_frame) {
            flat_compare(0xf310, 0, 0, 0);
            flat_compare(0xf320, 0, 0, 0);
            flat_compare(0xf350, 0, 0, 0);
            flat_compare(0xf358, 0, 0, 0);
        }
        flat_frame = 0;
        uint64 emissions = syn68k_native_compile_count() - emitted_before;
        uint64 hits = syn68k_aot_hit_count() - hits_before;
        unsigned expected_emissions = warm ? 0 : flat_compiled ? 20 : 10;
        /* Three frame opcodes share a mapped-code slot and reload on the second pass. */
        unsigned expected_hits = flat_compiled ? warm ? 117 : 97 : warm ? 13 : 3;
        if (emissions != expected_emissions || hits != expected_hits)
            fprintf(stderr, "Flat AOT compiled=%u warm=%u emissions=%llu/%u hits=%llu/%u\n", flat_compiled, warm,
                    (unsigned long long)emissions, expected_emissions, (unsigned long long)hits, expected_hits);
        assert(emissions == expected_emissions && hits == expected_hits);
        syn68k_x64_mapped_clear();
        free(flat_alias_allocation);
        free(expected_flat_aliases);
        puts("Flat FPU AOT replayed live formats, masks, values, conditions and null/busy frames");
        return 0;
    }
    const unsigned formats[] = {0, 1, 2, 4, 5, 6};
    const unsigned arithmetic[] = {0, 4, 0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x38, 0x3a};
    for (flat_compiled = 0; flat_compiled < 2; ++flat_compiled)
        for (unsigned ea = 16; ea < 64; ++ea) {
            for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f)
                for (unsigned op = 0; op < sizeof arithmetic / sizeof *arithmetic; ++op)
                    flat_compare(0xf200 | ea, 0x4000 | formats[f] << 10 | arithmetic[op], f, 0);
            for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f)
                flat_compare(0xf200 | ea, 0x6000 | formats[f] << 10, f, 0);
            for (unsigned mask = 1; mask < 8; ++mask) {
                flat_compare(0xf200 | ea, 0x8000 | mask << 10, mask, 0);
                flat_compare(0xf200 | ea, 0xa000 | mask << 10, mask, 0);
            }
            const unsigned masks[] = {0, 1, 0x80, 0x55, 0xff};
            for (unsigned i = 0; i < sizeof masks / sizeof *masks; ++i) {
                flat_compare(0xf200 | ea, 0xc000 | masks[i], i, 0);
                flat_compare(0xf200 | ea, 0xe000 | masks[i], i, 0);
                flat_compare(0xf200 | ea, 0xd000 | masks[i], i, 0);
                flat_compare(0xf200 | ea, 0xf000 | masks[i], i, 0);
            }
        }
    for (flat_compiled = 0; flat_compiled < 2; ++flat_compiled) {
        for (unsigned opcode = 0xf240; opcode < 0xf300; ++opcode)
            for (unsigned condition = 0; condition < 32; ++condition)
                flat_compare(opcode, condition, condition, 0);
        for (unsigned opcode = 0xf300; opcode < 0xf380; ++opcode) {
            flat_compare(opcode, 0, 0, 0);
            flat_compare(opcode, 0, 1, 0);
        }
    }
    for (flat_compiled = 0; flat_compiled < 2; ++flat_compiled) {
        for (flat_device_kind = 1; flat_device_kind <= 5; ++flat_device_kind)
            for (unsigned i = 0; i < sizeof formats / sizeof *formats; ++i) {
                if ((flat_device_kind == 4 || flat_device_kind == 5) && formats[i] != 1)
                    continue;
                flat_compare(0xf210, 0x4000 | formats[i] << 10 | 0x22, 0, flat_device_kind >= 4);
                flat_compare(0xf218, 0x4000 | formats[i] << 10 | 0x22, 0, flat_device_kind >= 4);
            }
        for (flat_device_kind = 6; flat_device_kind <= 7; ++flat_device_kind)
            flat_compare(0xf230, 0x6000, 0, 1);
        flat_device_kind = 8;
        for (flat_device_fail = 1; flat_device_fail <= 3; ++flat_device_fail) {
            flat_compare(0xf210, 0x4822, 0, 0);
            flat_compare(0xf218, 0x4822, 0, 0);
        }
        flat_device_fail = 0;
        flat_device_kind = 9;
        flat_compare(0xf210, 0x4822, 0, 0);
        flat_compare(0xf218, 0x4822, 0, 0);
        for (flat_device_kind = 10; flat_device_kind <= 12; ++flat_device_kind) {
            flat_compare(0xf350, 0, 0, flat_device_kind == 12);
            flat_compare(0xf358, 0, 0, flat_device_kind == 12);
        }
        for (flat_device_kind = 13; flat_device_kind <= 14; ++flat_device_kind) {
            flat_compare(0xf210, 0x4800, 0, 0);
            flat_compare(0xf218, 0x4822, 0, 0);
        }
    }
    flat_device_kind = 0;
    const uint32 alias_addresses[] = {0xf1fff9, 0xfffffa, 0xffffee};
    const unsigned alias_eas[] = {16, 24, 32};
    flat_dynamic_mask = 0xa5;
    for (flat_compiled = 0; flat_compiled < 2; ++flat_compiled)
        for (flat_alias_24 = 0; flat_alias_24 < 2; ++flat_alias_24)
            for (unsigned at = 0; at < sizeof alias_addresses / sizeof *alias_addresses; ++at) {
                flat_alias_address = alias_addresses[at];
                for (unsigned e = 0; e < sizeof alias_eas / sizeof *alias_eas; ++e) {
                    unsigned ea = alias_eas[e];
                    for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f) {
                        flat_compare(0xf200 | ea, 0x4000 | formats[f] << 10 | 0x22, f, 0);
                        flat_compare(0xf200 | ea, 0x6000 | formats[f] << 10, f, 0);
                    }
                    flat_compare(0xf200 | ea, 0x9c00, 0, 0);
                    flat_compare(0xf200 | ea, 0xbc00, 0, 0);
                    flat_compare(0xf200 | ea, 0xd0ff, 0, 0);
                    flat_compare(0xf200 | ea, 0xf0ff, 0, 0);
                    flat_compare(0xf200 | ea, 0xd800, 0, 0);
                    flat_compare(0xf200 | ea, 0xf800, 0, 0);
                    flat_compare(0xf200 | ea, 0xe0ff, 0, 0);
                    flat_compare(0xf240 | ea, 1, 0, 0);
                    flat_compare(0xf300 | ea, 0, 0, 0);
                    flat_compare(0xf340 | ea, 0, 1, 0);
                    for (flat_frame = 1; flat_frame <= 2; ++flat_frame) {
                        flat_compare(0xf300 | ea, 0, 0, 0);
                        flat_compare(0xf340 | ea, 0, 0, 0);
                    }
                    flat_frame = 0;
                }
            }
    flat_alias_address = flat_alias_24 = flat_dynamic_mask = 0;
    for (flat_compiled = 0; flat_compiled < 2; ++flat_compiled)
        for (flat_frame = 1; flat_frame <= 2; ++flat_frame) {
            flat_compare(0xf310, 0, 0, 0);
            flat_compare(0xf320, 0, 0, 0);
            flat_compare(0xf350, 0, 0, 0);
            flat_compare(0xf358, 0, 0, 0);
        }
    flat_frame = 0;
    flat_device_kind = 0;
    flat_trace_cases();
    syn68k_bus_read = NULL;
    syn68k_x64_mapped_clear();
    free(flat_alias_allocation);
    free(expected_flat_aliases);
    printf("%u flat decoded/compiled FPU memory/control/list/predicate/frame cases passed\n", cases);
    return 0;
}
