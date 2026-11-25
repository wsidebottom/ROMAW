/* SPDX-License-Identifier: MIT */
/* Physical push frames and original-size read fills, with real guest cleanup/RTE. */
#define main cached_cpu_probe_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned failing_address, armed, mutation, push_writes, data_reads, callback_a0;

static void mutate(void)
{
    if (mutation == 1) {
        EM_A0 ^= 4;
        callback_a0 = EM_A0;
    }
    if (mutation == 2)
        EM_D0 ^= 1;
    if (mutation == 3)
        cpu_state.ccc ^= 1;
    if (mutation == 4)
        cpu_state.fpu.null_state ^= 1;
}

static int failing_push(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    if (address >= 0x9000 && address < 0xb000)
        ++push_writes;
    if (armed && address == failing_address) {
        armed = 0;
        mutate();
        return 0;
    }
    return physical_write(opaque, address, bytes, size, attributes, fc);
}

static int failing_fill(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    if (address >= 0x9000 && address < 0xb000)
        ++data_reads;
    if (armed && address == failing_address) {
        armed = 0;
        mutate();
        return 0;
    }
    return physical_read(opaque, address, bytes, size, attributes, fc);
}

static Syn68kCache040Line* line(unsigned address, unsigned dirty, unsigned way)
{
    Syn68kCache040Line* result = &cache.lines[0][(address >> 4) & 63][way];
    *result = (Syn68kCache040Line){.address = address, .valid = 1, .dirty = dirty};
    for (unsigned word = 0; word < 4; ++word) {
        uint32_t value = 0x11223344 + word * 0x11223344;
        for (unsigned byte = 0; byte < 4; ++byte)
            result->bytes[word * 4 + byte] = value >> (24 - byte * 8);
    }
    return result;
}

static void prepare(unsigned mode)
{
    armed = 1;
    mutation = push_writes = data_reads = 0;
    memory.write = failing_push;
    memory.read = failing_fill;
    if (mode == 2)
        native.ram.size = 0x9000;
    mmu.tc = 0x8000;
    cpu_state.dfc = 0; /* Guest cleanup uses alternate-space physical MOVES. */
    EM_D0 = 0xaabbccdd;
    EM_A0 = 0x9047;
    put(8, 4, 0x8100);
}

/* Save D0 in the otherwise unused EA slot, drain every PD word with physical
   MOVES, restore D0 and execute RTE. The CPU does not complete these writes. */
static void cleanup(unsigned physical)
{
    unsigned pc = 0x8100;
    put(pc, 2, 0x2f40);
    put(pc + 2, 2, 8);
    pc += 4; /* MOVE.L D0,8(A7) */
    for (unsigned word = 0; word < 4; ++word) {
        put(pc, 2, 0x202f);
        put(pc + 2, 2, 44 + word * 4);
        pc += 4;
        put(pc, 2, 0x0eb9);
        put(pc + 2, 2, 0x0800);
        put(pc + 4, 4, physical + word * 4);
        pc += 8;
    }
    put(pc, 2, 0x202f);
    put(pc + 2, 2, 8);
    pc += 4;
    put(pc, 2, 0x4e73);
}

static void check_frame(unsigned frame, unsigned sr, unsigned pc, unsigned ssw, unsigned address,
                        const unsigned char* data)
{
    assert(get(frame, 2) == sr && get(frame + 2, 4) == pc && get(frame + 6, 2) == 0x7008);
    assert(get(frame + 8, 4) == 0 && get(frame + 12, 2) == ssw);
    assert(get(frame + 14, 2) == 0 && get(frame + 16, 4) == 0);
    assert(get(frame + 20, 4) == address);
    for (unsigned offset = 24; offset < 40; offset += 4)
        assert(get(frame + offset, 4) == 0);
    assert(get(frame + 40, 4) == (data ? address : 0));
    for (unsigned byte = 0; byte < 16; ++byte)
        assert(ram[frame + 44 + byte] == (data ? data[byte] : 0));
}

static void maintenance(unsigned mode)
{
    for (unsigned dirty = 1; dirty < 16; ++dirty)
        for (unsigned word = 0; word < 4; ++word) {
            unsigned single = !(dirty & (dirty - 1));
            if (single && !(dirty & (1u << word)))
                continue;
            for (unsigned scope = 1; scope <= 3; ++scope)
                for (unsigned both = 0; both < 2; ++both)
                    for (unsigned trace = 0; trace < 4; ++trace)
                        for (unsigned master = 0; master < 2; ++master)
                            for (unsigned enabled = 0; enabled < 2; ++enabled) {
                                Syn68kMappedCPU cpu = setup(mode);
                                prepare(mode);
                                unsigned sr = 0x2000 | master << 12 | trace << 14;
                                cpu_state.sr = sr;
                                cpu_state.cacr = enabled ? 0x80008000 : 0;
                                mmu.dtt[0] = 0xc040;
                                unsigned opcode = 0xf420 | (both ? 3 : 1) << 6 | scope << 3;
                                put(0x8000, 2, opcode);
                                Syn68kCache040Line* victim = line(0x9040, dirty, 0);
                                unsigned char expected[16];
                                memcpy(expected, victim->bytes, sizeof expected);
                                failing_address = 0x9040 + word * 4;
                                cleanup(0x9040);
                                uint64_t before = syn68k_native_execution_count();
                                Syn68kMappedRun run;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                if (mode)
                                    assert(syn68k_native_execution_count() > before);
                                assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                       run.step.cache_restart && !run.step.restartable_read && !run.completed &&
                                       cpu.pc == 0x8000 && !victim->valid);
                                uint16_t ssw;
                                assert(syn68k_mmu040_cache_push_ssw(&run.step.access, &ssw) &&
                                       ssw == (single ? 0 : 0x60));
                                Syn68kMappedDelivery delivery;
                                assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                                assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100 &&
                                       EM_A7 == 0x9fc4);
                                check_frame(EM_A7, sr, 0x8000, single ? 0 : 0x60, 0x9040 + (single ? word * 4 : 0),
                                            expected);
                                assert(syn68k_mapped_cpu_run(&cpu, 11, &run));
                                assert(run.reason == SYN68K_MAPPED_BUDGET && run.completed == 11 && cpu.pc == 0x8000 &&
                                       EM_A7 == 0xa000 && EM_D0 == 0xaabbccdd);
                                assert(!memcmp(ram + 0x9040, expected, sizeof expected));
                                unsigned writes = push_writes;
                                assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                                assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) &&
                                       run.step.vector == (trace ? 9u : 0u) && cpu.pc == 0x8002 &&
                                       push_writes == writes && EM_A0 == 0x9047);
                                ++cases;
                            }
        }
}

static void read_faults(unsigned mode, unsigned push, unsigned second)
{
    const unsigned addresses[] = {0x903f, 0x9040, 0x9041, 0x9042, 0x9043, 0x9047, 0x904f};
    for (unsigned index = 0; index < sizeof addresses / sizeof *addresses; ++index)
        for (unsigned size = 1; size <= 4; size *= 2)
            for (unsigned trace = 0; trace < 2; ++trace)
                for (unsigned user = 0; user < 2; ++user) {
                    Syn68kMappedCPU cpu = setup(mode);
                    prepare(mode);
                    unsigned operand = addresses[index];
                    if (second && (operand & 3) + size <= 4)
                        continue;
                    if (push && (operand & 15) + size > 16)
                        continue;
                    unsigned sr = (user ? 0 : 0x2000) | (trace ? 0x8000 : 0);
                    cpu_state.sr = sr;
                    cpu_state.isp = 0xb000;
                    EM_A0 = operand;
                    for (unsigned byte = 0; byte < 32; ++byte)
                        ram[0x9030 + byte] = byte + 1;
                    put(0x8000, 2, (size == 1 ? 0x1000 : size == 2 ? 0x3000 : 0x2000) | 0x18);
                    unsigned char expected[16];
                    if (push) {
                        unsigned base = (operand & ~15u) + 0x1000;
                        for (unsigned way = 0; way < 4; ++way)
                            line(base + way * 0x400, way ? 0 : 15, way);
                        memcpy(expected, cache.lines[0][(operand >> 4) & 63][0].bytes, sizeof expected);
                        failing_address = base + 8;
                        cleanup(base);
                    }
                    else {
                        failing_address = (operand & ~3u) + (second ? 4 : 0);
                        put(0x8100, 2, 0x4e73);
                    }
                    Syn68kMappedRun run;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && !run.completed &&
                           cpu.pc == 0x8000 && EM_A0 == operand && EM_D0 == 0xaabbccdd);
                    assert(run.step.cache_restart == push && run.step.restartable_read == !push);
                    unsigned frame = (user ? 0xb000 : 0xa000) - 60;
                    Syn68kMappedDelivery delivery;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                    assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100 && EM_A7 == frame);
                    unsigned ssw = 0x100 | (user ? 1 : 5) | (size == 1 ? 0x20 : size == 2 ? 0x40 : 0);
                    check_frame(frame, sr, 0x8000, push ? 0x60 : ssw, push ? failing_address - 8 : operand,
                                push ? expected : NULL);
                    assert(syn68k_mapped_cpu_run(&cpu, push ? 11 : 1, &run));
                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000 && EM_A7 == 0xa000);
                    unsigned expected_value = 0;
                    for (unsigned byte = 0; byte < size; ++byte)
                        expected_value = expected_value << 8 | ram[operand + byte];
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == (trace ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET) && cpu.pc == 0x8002 &&
                           EM_A0 == operand + size &&
                           (EM_D0 & (size == 1   ? 255u
                                     : size == 2 ? 65535u
                                                 : UINT32_MAX)) == expected_value);
                    ++cases;
                }
}

static void other_reads(unsigned mode)
{
    for (unsigned rte = 0; rte < 2; ++rte) {
        Syn68kMappedCPU cpu = setup(mode);
        prepare(mode);
        put(0x8100, 2, 0x4e73);
        failing_address = 0x9040;
        if (rte) {
            EM_A7 = 0x9040;
            put(0x8000, 2, 0x4e73);
            put(0x9040, 2, 0x2000);
            put(0x9042, 4, 0x8200);
            put(0x9046, 2, 0);
        }
        else {
            EM_A0 = 0x903c;
            EM_D1 = 0xdeadbeef;
            put(0x8000, 2, 0x4cd8);
            put(0x8002, 2, 3); /* MOVEM.L (A0)+,D0-D1 */
            put(0x903c, 4, 0x12345678);
            put(0x9040, 4, 0x87654321);
        }
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.restartable_read == rte &&
               run.step.movem_state == !rte && !run.step.cache_restart);
        if (!rte)
            assert(EM_D0 == 0x12345678 && EM_D1 == 0xdeadbeef);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
        unsigned frame = EM_A7;
        assert(get(frame + 8, 4) == (rte ? 0 : 0x903c) && get(frame + 12, 2) == (rte ? 0x145 : 0x1105));
        assert(get(frame + 20, 4) == 0x9040 && get(frame + 14, 2) == 0 && get(frame + 16, 4) == 0);
        for (unsigned offset = 24; offset < 60; offset += 4)
            assert(get(frame + offset, 4) == 0);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == (rte ? 0x8200 : 0x8004));
        if (rte)
            assert(EM_A7 == 0x9048);
        else
            assert(EM_A0 == 0x9044 && EM_D0 == 0x12345678 && EM_D1 == 0x87654321);
        ++cases;
    }
}

static void callback_guards(unsigned mode)
{
    for (unsigned push = 0; push < 2; ++push)
        for (unsigned change = 1; change <= 4; ++change) {
            Syn68kMappedCPU cpu = setup(mode);
            prepare(mode);
            mutation = change;
            EM_A0 = 0x9040;
            if (push) {
                put(0x8000, 2, 0xf468); /* CPUSHL DC,(A0) */
                line(0x9040, 15, 0);
            }
            else
                put(0x8000, 2, 0x2018);
            failing_address = 0x9040;
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && !run.step.cache_restart && !run.step.restartable_read);
            if (change == 1)
                assert(EM_A0 == callback_a0);
            if (change == 2)
                assert(EM_D0 != 0xaabbccdd);
            if (change == 3)
                assert(cpu_state.ccc);
            if (change == 4)
                assert(!cpu_state.fpu.null_state);
            Syn68kMappedDelivery delivery;
            unsigned stack = EM_A7;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && EM_A7 == stack && cpu.pc == 0x8000);
            ++cases;
        }
}

static void rte_push(unsigned mode)
{
    for (unsigned prefix = 0; prefix < 2; ++prefix)
        for (unsigned change = 0; change <= 4; ++change) {
            unsigned throwaway = !prefix;
            Syn68kMappedCPU cpu = setup(mode);
            prepare(mode);
            mutation = change;
            EM_A7 = throwaway ? 0x9048 : 0x9050;
            put(0x8000, 2, 0x4e73);
            if (throwaway) {
                put(0x9048, 2, 0x2000);
                put(0x904a, 4, 0x8200);
                put(0x904e, 2, 0x1000);
            }
            put(0x9050, 2, 0x2000);
            put(0x9052, 4, 0x8300);
            put(0x9056, 2, 0);
            for (unsigned way = 0; way < 4; ++way)
                line(0xa050 + way * 0x400, way ? 0 : 15, way);
            cache.replacement[0] = throwaway ? 1 : 0; /* Three first-frame cache hits precede the miss. */
            unsigned char expected[16];
            memcpy(expected, cache.lines[0][5][0].bytes, sizeof expected);
            failing_address = 0xa054;
            cleanup(0xa050);
            Syn68kMappedRun run;
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            if (run.step.cache_restart != 2 || EM_A7 != 0x9050)
                fprintf(stderr, "RTE push restart lost live SP: mode=%u throwaway=%u marker=%u sp=%x\n", mode,
                        throwaway, run.step.cache_restart, EM_A7);
            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.cache_restart == 2 &&
                   !run.step.restartable_read && EM_A7 == 0x9050);
            uint32_t d0 = EM_D0, a0 = EM_A0;
            unsigned null_state = cpu_state.fpu.null_state;
            Syn68kMappedDelivery delivery;
            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100 && EM_A7 == 0x9014);
            assert(get(EM_A7 + 12, 2) == 0x60 && get(EM_A7 + 20, 4) == 0xa050);
            for (unsigned word = 0; word < 4; ++word) {
                uint32_t value = 0, want = 0;
                Syn68kMMU040Access access = syn68k_mmu040_read(&memory, EM_A7 + 44 + word * 4, 5, 4, 0, &value);
                for (unsigned byte = 0; byte < 4; ++byte)
                    want = want << 8 | expected[word * 4 + byte];
                assert(!access.translation.fault && !access.bus_error && value == want);
            }
            assert(syn68k_mapped_cpu_run(&cpu, 11, &run));
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000 && EM_A7 == 0x9050);
            assert(!memcmp(ram + 0xa050, expected, sizeof expected));
            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8300 && EM_A7 == 0x9058 && EM_D0 == d0 &&
                   EM_A0 == a0 && cpu_state.fpu.null_state == null_state);
            ++cases;
        }
}

static int table_read(void* opaque, uint32_t address, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(address, 4);
    return 1;
}

static int table_update(void* opaque, uint32_t address, uint32_t bits, int locked)
{
    assert(opaque == ram);
    (void)locked;
    put(address, 4, get(address, 4) | bits);
    return 1;
}

static void fetch_faults(unsigned mode)
{
    for (unsigned table = 0; table < 2; ++table) {
        Syn68kMappedCPU cpu = setup(mode);
        prepare(mode);
        put(0x8000, 2, 0x4e71);
        unsigned char expected[16];
        if (table) {
            mmu.itt[0] = 0;
            mmu.srp = 0x1000;
            put(0x1000, 4, 0x200a);
            put(0x2000, 4, 0x300a);
            put(0x3020, 4, 0x8001);
            memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
            Syn68kCache040Line* descriptor = line(0x3020, 1, 0);
            descriptor->bytes[0] = descriptor->bytes[1] = 0;
            descriptor->bytes[2] = 0x80;
            descriptor->bytes[3] = 1;
            memcpy(expected, descriptor->bytes, sizeof expected);
            failing_address = 0x3020;
            cleanup(0x3020);
            /* Handler instruction fetch can set U before the captured old
               descriptor is drained. Flush that cached translation in guest
               code so the next fetch republishes its hardware history. */
            put(0x8138, 2, 0xf518); /* PFLUSHA */
            put(0x813a, 2, 0x4e73);
            if (mode == 2)
                native.ram = (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
        }
        else {
            failing_address = 0x8000;
            put(0x8100, 2, 0x4e73);
            if (mode == 2)
                native.ram.size = 0x8000;
        }
        Syn68kMappedRun run;
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && cpu.pc == 0x8000 &&
               run.step.cache_restart == table);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
        check_frame(EM_A7, 0x2000, 0x8000, table ? 0x200 : 0x146, table ? 0x3020 : 0x8000, table ? expected : NULL);
        assert(syn68k_mapped_cpu_run(&cpu, table ? 12 : 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8000 && EM_A7 == 0xa000);
        assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x8002);
        if (table)
            assert(get(0x3020, 4) & 8);
        ++cases;
    }
}

static void malformed(unsigned mode)
{
    Syn68kMappedCPU cpu = setup(mode);
    prepare(mode);
    put(0x8000, 2, 0xf468);
    line(0x9040, 15, 0);
    failing_address = 0x9044;
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run) && run.step.cache_restart);
    const Syn68kIntegerStep original = run.step;
    for (unsigned bad = 0; bad < 31; ++bad) {
        Syn68kIntegerStep record = original;
        switch (bad) {
        case 0:
            record.cache_restart = 2;
            break;
        case 1:
            record.cache_restart = 0;
            break;
        case 2:
            record.access.cache.fault = CACHE040_FILL_FAULT;
            break;
        case 3:
            record.access.cache.push_size = 8;
            break;
        case 4:
            record.access.cache.push_address++;
            break;
        case 5:
            record.access.cache.cycle.kind = CACHE040_OPERAND;
            break;
        case 6:
            record.access.cache.cycle.fc = 5;
            break;
        case 7:
            record.access.cache.cycle.attributes = 0x20;
            break;
        case 8:
            record.access.cache.cycle.way = 4;
            break;
        case 9:
            record.access.cache.cycle.traffic = (enum Syn68kCache040Traffic)(CACHE040_MOVE16 + 1);
            break;
        case 10:
            record.access.cache.cycle.lock_end = 1;
            break;
        case 11:
            record.access.cache.cycle.locked = 1;
            break;
        case 12:
            record.access.cache.cycle.burst = 0;
            break;
        case 13:
            record.access.cache.cycle.beat = 4;
            break;
        case 14:
            record.access.cache.push_word ^= 1;
            break;
        case 15:
            record.access.cache.bus_address += 4;
            break;
        case 16:
            record.access.cache.interrupt_level = 7;
            break;
        case 17:
            record.access.translation.fault = MMU040_INVALID;
            break;
        case 18:
            record.access.cache_exception = 0;
            break;
        case 19:
            record.access.bus_error = 0;
            record.access.translation.fault = MMU040_OK;
            break;
        case 20:
            record.store_state = 1;
            break;
        case 21:
            record.restartable_read = 1;
            break;
        case 22:
            record.prefetch_address = 0x9001;
            break;
        case 23:
            record.flow_changed = 1;
            break;
        case 24:
            record.fpu_vector = 48;
            break;
        case 25:
            record.movem_state = 1;
            break;
        case 26:
            record.next += 2;
            break;
        case 27:
            record.vector = 3;
            break;
        case 28:
            record.format = 0;
            break;
        case 29:
            record.exception_pc++;
            break;
        case 30:
            record.fault_address++;
            break;
        }
        unsigned sr = cpu_state.sr, stack = EM_A7, writes = push_writes, reads = data_reads;
        cpu.stopped = 1;
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &record, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_UNSUPPORTED && cpu.stopped && cpu.pc == 0x8000 &&
               cpu_state.sr == sr && EM_A7 == stack && push_writes == writes && data_reads == reads);
        ++cases;
    }
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned mode = 0; mode < 3; ++mode) {
        rte_push(mode);
        maintenance(mode);
        read_faults(mode, 0, 0);
        read_faults(mode, 0, 1);
        read_faults(mode, 1, 0);
        callback_guards(mode);
        other_reads(mode);
        fetch_faults(mode);
        malformed(mode);
    }
    printf("%u cache push/fill frame/guest-cleanup/RTE/trace/callback cases passed; emitted=%llu hits=%llu\n", cases,
           (unsigned long long)(syn68k_native_compile_count() - compiled),
           (unsigned long long)(syn68k_aot_hit_count() - hits));
    return 0;
}
