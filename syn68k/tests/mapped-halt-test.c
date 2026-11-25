/* SPDX-License-Identifier: MIT */
/* Descriptor lockup and double-fault halts survive IRQs until embedding reset. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned callbacks, fail_at;
static unsigned char saved_ram[sizeof ram];

static int read_bus(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                    unsigned fc)
{
    ++callbacks;
    return address != fail_at && physical_read(opaque, address, bytes, size, attributes, fc);
}

static int write_bus(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                     unsigned fc)
{
    ++callbacks;
    return address != fail_at && physical_write(opaque, address, bytes, size, attributes, fc);
}

static int read_table(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    ++callbacks;
    *value = get(address, 4);
    return 1;
}

static int update_table(void* opaque, uint32_t address, uint32_t value, int locked)
{
    assert(opaque == ram);
    ++callbacks;
    put(address, 4, locked ? get(address, 4) | value : value);
    return 1;
}

static Syn68kMappedCPU prepare(unsigned mode, unsigned level, unsigned way, unsigned dirty)
{
    Syn68kMappedCPU cpu = setup(mode);
    memory.read = read_bus;
    memory.write = write_bus;
    memory.tables = (Syn68kMMU040Bus){ram, read_table, update_table};
    callbacks = 0;
    fail_at = ~0u;
    mmu.tc = 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    put(0x1008, 4, 0x200a);
    put(0x2000, 4, 0x300a);
    put(0x3000, 4, 0x9009);
    put(0x9040, 4, 0x12345678);
    unsigned address = 0x1000 * (level + 1);
    Syn68kCache040Line* line = &cache.lines[0][0][way];
    *line = (Syn68kCache040Line){.address = address, .valid = 1, .dirty = dirty};
    memcpy(line->bytes, ram + address, 16);
    cpu_state.cacr = 0x8000; /* Disable data cache without invalidating its tags. */
    return cpu;
}

static void frozen(Syn68kMappedCPU* cpu)
{
    /* Repairing the original cause must not secretly resume a halted CPU. */
    memset(&cache, 0, sizeof cache);
    mmu.tc = 0;
    cpu->pc = 0x8001; /* Halt precedes the odd-address exception too. */
    cpu->stopped = 1;
    cpu_state.interrupt_pending[7] = 1;
    CPUState state = cpu_state;
    Syn68kMappedCPU before = *cpu;
    Syn68kMMU040 saved_mmu = mmu;
    Syn68kCache040 saved_cache = cache;
    memcpy(saved_ram, ram, sizeof ram);
    unsigned count = callbacks;
    uint64_t native_count = syn68k_native_execution_count();
    uint64_t compiled = syn68k_native_compile_count();
    for (unsigned budget = 0; budget < 3; ++budget) {
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(cpu, budget, &run));
        assert(run.reason == (budget ? SYN68K_MAPPED_HALTED : SYN68K_MAPPED_BUDGET));
        assert(!run.completed && !run.opcode && !run.interrupt_level && !run.step.vector);
        assert(run.step.next == cpu->pc);
    }
    Syn68kIntegerStep record = {.vector = 31, .format = 0, .exception_pc = 0x8000};
    Syn68kMappedDelivery delivery;
    assert(syn68k_mapped_exception_enter(cpu, &record, 7, &delivery));
    assert(delivery.reason == SYN68K_DELIVERY_HALTED);
    assert(!memcmp(&before, cpu, sizeof before) && !memcmp(&state, &cpu_state, sizeof state));
    assert(!memcmp(&saved_mmu, &mmu, sizeof mmu) && !memcmp(&saved_cache, &cache, sizeof cache));
    assert(!memcmp(saved_ram, ram, sizeof ram) && callbacks == count);
    assert(syn68k_native_execution_count() == native_count && syn68k_native_compile_count() == compiled);
    for (unsigned invalid = 0; invalid < 2; ++invalid) {
        cpu->halted = invalid ? (enum Syn68kMappedHalt) - 1 : (enum Syn68kMappedHalt)3;
        Syn68kMappedRun run;
        assert(!syn68k_mapped_cpu_run(cpu, 1, &run) && run.reason == SYN68K_MAPPED_BAD_REQUEST);
        assert(!syn68k_mapped_exception_enter(cpu, &record, 7, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_BAD_REQUEST && callbacks == count);
    }
    *cpu = before;
}

static void reset_owner(Syn68kMappedCPU* cpu, unsigned mode)
{
    *cpu = setup(mode); /* The embedding resets CPU and mapped state together. */
    put(0x8000, 2, 0x4e71);
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(cpu, 1, &run));
    assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && cpu->pc == 0x8002 && !cpu->halted);
}

static void instruction_lockups(unsigned mode)
{
    for (unsigned level = 0; level < 3; ++level)
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned dirty = 0; dirty < 2; ++dirty)
                for (unsigned trace = 0; trace < 3; ++trace)
                    for (unsigned bank = 0; bank < 3; ++bank)
                        for (unsigned family = 0; family < 11; ++family) {
                            if (!bank && family >= 7)
                                continue; /* PTEST/FSAVE/FRESTORE/RTE require supervisor. */
                            Syn68kMappedCPU cpu = prepare(mode, level, way, dirty ? 15 : 0);
                            cpu_state.sr = (bank ? 0x2000 | (bank == 2 ? 0x1000 : 0) : 0) | trace << 14;
                            EM_A0 = 0x04000040;
                            EM_D0 = 0x87654321;
                            const unsigned opcodes[] = {0x4e71, 0x2010, 0x2080, 0xf210, 0xf210, 0x4cd0,
                                                        0x48d0, 0xf568, 0xf310, 0xf350, 0x4e73};
                            put(0x8000, 2, opcodes[family]);
                            put(0x8002, 2, family == 3 ? 0x4400 : family == 4 ? 0x6400 : 3);
                            if (!family)
                                cpu.pc = 0x04000040;
                            if (family == 10)
                                EM_A7 = 0x04000040;
                            unsigned pc = cpu.pc;
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            if (run.reason != SYN68K_MAPPED_HALTED)
                                fprintf(stderr, "mode=%u level=%u family=%u bank=%u vector=%u reason=%u cache=%u\n",
                                        mode, level, family, bank, run.step.vector, run.reason,
                                        run.step.access.cache.fault);
                            assert(run.reason == SYN68K_MAPPED_HALTED && cpu.halted == SYN68K_HALT_DESCRIPTOR_LOCKUP);
                            assert(cpu.pc == pc && !run.completed && !run.interrupt_level && !cpu.stopped);
                            assert(run.step.access.cache_table && run.step.access.cache_exception &&
                                   run.step.access.cache.fault == CACHE040_DESCRIPTOR_LOCKUP &&
                                   run.step.access.translation.fault == MMU040_PHYSICAL_ABORT);
                            assert(!mmu.mmusr && !run.step.forced_trace);
                            frozen(&cpu);
                            reset_owner(&cpu, mode);
                            ++cases;
                        }
}

static void delivery_lockups(unsigned mode)
{
    for (unsigned level = 0; level < 3; ++level)
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned dirty = 0; dirty < 2; ++dirty)
                for (unsigned bank = 0; bank < 3; ++bank)
                    for (unsigned stage = 0; stage < 2; ++stage)
                        for (unsigned which = 0; which < 4; ++which) {
                            Syn68kMappedCPU cpu = prepare(mode, level, way, dirty ? 15 : 0);
                            cpu_state.sr = bank ? 0x2000 | (bank == 2 ? 0x1000 : 0) : 0;
                            EM_A7 = cpu_state.isp = cpu_state.msp = stage ? 0xa000 : 0x04000080;
                            if (stage)
                                cpu_state.vbr = 0x04000000;
                            unsigned vector = (unsigned[]){4, 9, 31, 32}[which];
                            Syn68kIntegerStep record = {.vector = vector, .format = 0, .exception_pc = cpu.pc};
                            Syn68kMappedDelivery delivery;
                            assert(syn68k_mapped_exception_enter(&cpu, &record, which == 2 ? 7 : 0, &delivery));
                            assert(delivery.reason == SYN68K_DELIVERY_HALTED &&
                                   cpu.halted == SYN68K_HALT_DESCRIPTOR_LOCKUP && cpu.pc == 0x8000);
                            assert(delivery.stacking == !stage && delivery.access.cache_table &&
                                   delivery.access.cache.fault == CACHE040_DESCRIPTOR_LOCKUP);
                            frozen(&cpu);
                            reset_owner(&cpu, mode);
                            ++cases;
                        }
}

static void double_faults(unsigned mode)
{
    for (unsigned vector = 2; vector <= 3; ++vector)
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned failed = 0; failed < 5; ++failed) {
                Syn68kMappedCPU cpu = setup(mode);
                cpu_state.sr = bank ? 0x2000 | (bank == 2 ? 0x1000 : 0) : 0;
                EM_A7 = cpu_state.isp = cpu_state.msp = 0xa000;
                memory.read = read_bus;
                memory.write = write_bus;
                cpu_state.cacr = 0;
                if (mode == 2)
                    native.ram = (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
                callbacks = 0;
                fail_at = failed == 4 ? vector * 4 : 0x9ff4 + (unsigned[]){0, 2, 6, 8}[failed];
                Syn68kIntegerStep record = {.vector = vector, .format = 2, .exception_pc = cpu.pc};
                Syn68kMappedDelivery delivery;
                assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
                assert(delivery.reason == SYN68K_DELIVERY_DOUBLE_FAULT && cpu.halted == SYN68K_HALT_DOUBLE_FAULT);
                assert(delivery.stacking == (failed != 4) && delivery.access.bus_error);
                assert(EM_A7 == 0x9ff4 && cpu.pc == 0x8000);
                frozen(&cpu);
                reset_owner(&cpu, mode);
                ++cases;
            }
}

static void normal_controls(unsigned mode)
{
    for (unsigned level = 0; level < 3; ++level)
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned enabled = 0; enabled < 2; ++enabled) {
                Syn68kMappedCPU cpu = prepare(mode, level, way, 0);
                cpu_state.cacr |= enabled ? 0x80000000 : 0;
                cache.lines[0][0][way].valid = enabled;
                put(0x8000, 2, 0x2010);
                EM_A0 = 0x04000040;
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 1 && EM_D0 == 0x12345678 && !cpu.halted &&
                       cpu.pc == 0x8002);
                ++cases;
            }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        instruction_lockups(mode);
        delivery_lockups(mode);
        double_faults(mode);
        normal_controls(mode);
    }
    printf("%u descriptor-lockup/double-fault/halt/IRQ/reset cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
}
