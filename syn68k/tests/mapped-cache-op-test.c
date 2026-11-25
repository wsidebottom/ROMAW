/* SPDX-License-Identifier: MIT */
/* Decode, physical cache selection, trace and line-boundary IRQ regressions. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned irq_writes, irq_after, irq_level;

static int irq_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                     unsigned fc)
{
    int ok = physical_write(opaque, address, bytes, size, attributes, fc);
    if (++irq_writes == irq_after)
        cpu_state.interrupt_pending[irq_level] = 1;
    return ok;
}

static Syn68kCache040Line* seed(unsigned bank, unsigned address, unsigned dirty)
{
    unsigned set = (address >> 4) & 63, way = 0;
    while (cache.lines[bank][set][way].valid)
        ++way;
    Syn68kCache040Line* line = &cache.lines[bank][set][way];
    *line = (Syn68kCache040Line){.address = address, .valid = 1, .dirty = dirty};
    for (unsigned word = 0; word < 4; ++word) {
        uint32_t value = 0x11223344 + bank * 0x01010101 + word;
        for (unsigned byte = 0; byte < 4; ++byte)
            line->bytes[word * 4 + byte] = value >> (24 - byte * 8);
    }
    return line;
}

static void matrix(unsigned mode)
{
    for (unsigned low = 0; low < 256; ++low)
        for (unsigned model = 0; model < 2; ++model)
            for (unsigned supervisor = 0; supervisor < 2; ++supervisor)
                for (unsigned enabled = 0; enabled < 2; ++enabled)
                    for (unsigned page = 12; page <= 13; ++page)
                        for (unsigned trace = 0; trace < 3; ++trace) {
                            Syn68kMappedCPU cpu = setup(mode);
                            if (!model)
                                memory.cache = NULL;
                            unsigned opcode = 0xf400 | low, scope = (low >> 3) & 3, which = low >> 6;
                            cpu_state.sr = (supervisor ? 0x2000 : 0) | (trace == 1 ? 0x4000 : trace == 2 ? 0x8000 : 0);
                            cpu_state.cacr = enabled ? 0x80008000 : 0;
                            cpu_state.ccn = cpu_state.ccc = cpu_state.ccx = 1;
                            cpu_state.ccv = 0;
                            cpu_state.ccnz = 1;
                            mmu.tc = 0x8000 | (page == 13 ? 0x4000 : 0);
                            mmu.dtt[0] = 0; /* An is physical, never a translated data EA. */
                            for (unsigned reg = 0; reg < 8; ++reg)
                                EM_AREG(reg) = 0x9047;
                            put(0x8000, 2, opcode);
                            Syn68kCache040Line* data0 = seed(0, 0x8040, 1);
                            Syn68kCache040Line* data1 = seed(0, 0x9040, 1);
                            Syn68kCache040Line* instruction = seed(1, 0x9040, 0);
                            M68kReg registers[16];
                            memcpy(registers, cpu_state.regs, sizeof registers);
                            uint64_t before = syn68k_native_execution_count();
                            Syn68kMappedRun run;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            unsigned vector = !scope ? 4 : !supervisor ? 8 : trace ? 9 : 0;
                            if (scope && supervisor && !model) {
                                assert(run.reason == SYN68K_MAPPED_UNSUPPORTED && !run.completed && cpu.pc == 0x8000);
                            }
                            else {
                                assert(run.reason == (vector ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                                assert(run.step.vector == vector);
                                assert(run.completed == (scope && supervisor && !trace));
                                assert(cpu.pc == (scope && supervisor ? 0x8002 : 0x8000));
                                if (mode)
                                    assert(syn68k_native_execution_count() > before);
                            }
                            assert(!memcmp(registers, cpu_state.regs, sizeof registers));
                            assert(cpu_state.ccn && cpu_state.ccc && cpu_state.ccx && !cpu_state.ccv && cpu_state.ccnz);
                            unsigned executed = scope && supervisor && model;
                            unsigned match0 = scope == 3 || (scope == 2 && page == 13);
                            assert(data0->valid == !(executed && (which & 1) && match0));
                            assert(data1->valid == !(executed && (which & 1)));
                            assert(instruction->valid == !(executed && (which & 2)));
                            assert(get(0x9040, 4) == (executed && (which & 1) && (low & 0x20) ? 0x11223344 : 0));
                            assert(get(0x8040, 4) ==
                                   (executed && (which & 1) && match0 && (low & 0x20) ? 0x11223344 : 0));
                            ++cases;
                        }
}

static void interrupts(unsigned mode)
{
    for (unsigned mask = 0; mask <= 7; ++mask)
        for (unsigned level = 1; level <= 7; ++level)
            for (unsigned trace = 0; trace < 3; ++trace) {
                Syn68kMappedCPU cpu = setup(mode);
                if (mode == 2)
                    native.ram.size = 0x9000; /* Pushes use callbacks; fetch remains direct RAM. */
                memory.write = irq_write;
                irq_writes = 0;
                irq_after = 1;
                irq_level = level;
                cpu_state.sr = 0x2000 | mask << 8 | (trace == 1 ? 0x4000 : trace == 2 ? 0x8000 : 0);
                put(0x8000, 2, 0xf478); /* CPUSHA DC */
                Syn68kCache040Line* first = seed(0, 0x9040, 1);
                Syn68kCache040Line* second = seed(0, 0x9050, 1);
                Syn68kMappedRun run;
                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                unsigned eligible = level == 7 || level > mask;
                if (eligible) {
                    assert(run.reason == SYN68K_MAPPED_INTERRUPT && run.interrupt_level == level && !run.completed &&
                           !run.step.vector && cpu.pc == 0x8000 && !first->valid && second->valid);
                    assert(irq_writes == 1 && cpu_state.interrupt_pending[level]);
                    /* RTE would return to the unretired instruction. Clear the
                       request as accepted entry does, then finish the rest. */
                    cpu_state.interrupt_pending[level] = 0;
                    irq_after = 99;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                }
                assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
                assert(run.completed == !trace && cpu.pc == 0x8002 && run.step.vector == (trace ? 9u : 0u));
                assert(!first->valid && !second->valid && irq_writes == 2);
                ++cases;
            }
}

static unsigned fault_address, fault_writes;

static int failing_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    ++fault_writes;
    if (address == fault_address) {
        cpu_state.interrupt_pending[7] = 1; /* The failed push wins over this IRQ. */
        return 0;
    }
    return physical_write(opaque, address, bytes, size, attributes, fc);
}

static void push_faults(unsigned mode)
{
    for (unsigned word = 0; word < 4; ++word)
        for (unsigned trace = 0; trace < 3; ++trace) {
            Syn68kMappedCPU cpu = setup(mode);
            if (mode == 2)
                native.ram.size = 0x9000;
            memory.write = failing_write;
            fault_address = 0x9050 + word * 4;
            fault_writes = 0;
            cpu_state.sr = 0x2000 | (trace == 1 ? 0x4000 : trace == 2 ? 0x8000 : 0);
            put(0x8000, 2, 0xf478); /* CPUSHA DC */
            Syn68kCache040Line* first = seed(0, 0x9040, 1);
            Syn68kCache040Line* failing = seed(0, 0x9050, 15);
            Syn68kCache040Line* last = seed(0, 0x9060, 1);
            unsigned char expected[16];
            memcpy(expected, failing->bytes, sizeof expected);
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.completed && !run.interrupt_level);
            assert(run.step.vector == 2 && run.step.format == 7 && cpu.pc == 0x8000);
            const Syn68kMMU040Access* access = &run.step.access;
            assert(access->cache_exception && access->bus_error && access->translation.fault == MMU040_PHYSICAL_ABORT);
            assert(access->cache.fault == CACHE040_PUSH_FAULT && access->cache.bus_address == fault_address &&
                   access->cache.push_address == 0x9050 && access->cache.push_size == 16 &&
                   access->cache.push_word == word && !access->cache.interrupt_level);
            assert(!memcmp(expected, access->cache.push_data, sizeof expected));
            assert(access->cache.cycle.kind == CACHE040_PUSH && access->cache.cycle.fc == 0 &&
                   access->cache.cycle.burst == 16 && access->cache.cycle.beat == word);
            uint16_t ssw;
            assert(!syn68k_mmu040_access_ssw(access, &ssw)); /* Requires the physical push frame. */
            /* The failed line is invalidated; its captured data now belongs to
               the access-error recipient rather than a replayable cache tag. */
            assert(!first->valid && !failing->valid && last->valid);
            assert(fault_writes == word + 2 && cpu_state.interrupt_pending[7]);
            assert(!cache.active && cache.traffic == CACHE040_NORMAL);
            ++cases;
        }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        matrix(mode);
        interrupts(mode);
        push_faults(mode);
    }
    printf("%u cache opcode/privilege/physical-scope/trace/IRQ cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
