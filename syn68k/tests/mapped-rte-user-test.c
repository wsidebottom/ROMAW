/* SPDX-License-Identifier: MIT */
/* RTE format-1 progress across user/supervisor stacks and later access faults.
 */
#define main rte_user_stream_unused
#include "mmu040-stream-test.c"
#undef main

static unsigned header_faults(void)
{
    const struct {
        unsigned offset, size, partial;
    } fields[] = {{0, 2, 0}, {2, 4, 0}, {6, 2, 0}, {8, 4, 0}, {12, 2, 0}, {2, 4, 2}, {8, 4, 2}};

    unsigned passed = 0;
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned page = 0; page < 2; ++page)
            for (unsigned master = 0; master < 2; ++master)
                for (unsigned field = 0; field < sizeof fields / sizeof *fields; ++field)
                    for (unsigned bus = 0; bus < 2; ++bus)
                        for (unsigned ccr = 0; ccr < 32; ++ccr) {
                            memory.native = NULL;
                            memory.cache = NULL;
                            memory.aligned_transfers = 1;
                            fixture(page, 1);
                            syn68k_fpu_reset();
                            unsigned boundary = 0x20000 + page_size,
                                     next = boundary - fields[field].offset - fields[field].partial;
                            unsigned first = 0x20080, prefix_sr = ccr | (master ? 0x1000 : 0);
                            supervisor = 0;
                            put32(pte(0x10000), 0xc001);
                            put32(pte(0x20000), 0x10019);
                            put32(pte(boundary), bus ? 0x14019 : 0);
                            unsigned fault_pte = pte(boundary), watch = physical(next);
                            code_word(next, (ccr + 1) & 31);
                            put32(physical(next + 2), 0x10080);
                            code_word(next + 6, 0x7008);
                            put32(physical(next + 8), 0x12345678);
                            code_word(next + 12, 0);
                            unsigned char original[14];
                            for (unsigned i = 0; i < 14; ++i)
                                original[i] = ram[physical(next + i)];
                            supervisor = 1;
                            set_status(0x2000 | ((ccr + 2) & 31));
                            EM_A7 = cpu_state.isp = first;
                            cpu_state.msp = 0x201c0;
                            cpu_state.usp = next;
                            code_word(first, prefix_sr);
                            put32(physical(first + 2), 0x10080);
                            code_word(first + 6, 0x1000);
                            code_word(0x10000, 0x4e73);
                            cpu_state.vbr = 0x20000;
                            put32(physical(cpu_state.vbr + 8), 0x10040);
                            unsigned table_page = 0x3000 & ~(page_size - 1), target = 0x30000 + fault_pte - table_page;
                            put32(pte(0x30000), table_page | 1);
                            const unsigned program[] = {0x23fc,         1,      0x4019, target >> 16,
                                                        target & 65535, 0xf518, 0x4e73};
                            for (unsigned i = 0; i < sizeof program / sizeof *program; ++i)
                                code_word(0x10040 + 2 * i, program[i]);
                            if (bus)
                                fail_physical = 0x14000;
                            Syn68kMMU040Native native;
                            if (config & 2)
                                assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
                            Syn68kFPUState fpu = cpu_state.fpu;
                            Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = config & 1};
                            Syn68kMappedRun run;
                            Syn68kMappedDelivery delivery;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 &&
                                   run.step.restartable_read == 1 && !run.step.cache_restart);
                            assert(cpu.pc == 0x10000 && EM_A7 == next && status() == prefix_sr &&
                                   cpu_state.isp == first + 8);
                            assert(run.step.access.request == next + fields[field].offset &&
                                   run.step.access.logical == boundary &&
                                   run.step.access.completed == fields[field].partial);
                            assert(run.step.access.size == fields[field].size && run.step.access.fc == 1 &&
                                   !run.step.access.writing && !run.step.access.rmw &&
                                   run.step.access.bus_error == bus);
                            assert(!memcmp(&fpu, &cpu_state.fpu, sizeof fpu));
                            fail_physical = UINT32_MAX;
                            assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
                            assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x10040);
                            unsigned frame = (master ? 0x201c0 : first + 8) - 60;
                            assert(EM_A7 == frame);
                            assert(mapped_value(frame, 2) == prefix_sr && mapped_value(frame + 2, 4) == 0x10000 &&
                                   mapped_value(frame + 6, 2) == 0x7008);
                            unsigned expected_ssw = 0x101 | (fields[field].size == 2 ? 0x40 : 0) |
                                                    (!bus ? (fields[field].partial ? 0xc00 : 0x400) : 0);
                            assert(mapped_value(frame + 12, 2) == expected_ssw &&
                                   mapped_value(frame + 20, 4) == next + fields[field].offset);
                            for (unsigned i = 14; i <= 18; i += 2)
                                assert(!mapped_value(frame + i, 2));
                            assert(syn68k_mapped_cpu_run(&cpu, 3, &run));
                            assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_A7 == next &&
                                   status() == prefix_sr);
                            assert(!memcmp(&fpu, &cpu_state.fpu, sizeof fpu));
                            watched_physical = watch;
                            watched_read_attempts = 0;
                            assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                            assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 8 && EM_A7 == next &&
                                   !watched_read_attempts);
                            supervisor = 0;
                            for (unsigned i = 0; i < 14; ++i)
                                assert(ram[physical(next + i)] == original[i]);
                            supervisor = 1;
                            ++passed;
                        }
    memory.native = NULL;
    memory.cache = NULL;
    memory.aligned_transfers = 0;
    return passed;
}

static unsigned cache_faults(void)
{
    unsigned passed = 0;
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned p = 0; p < 2; ++p)
            for (unsigned m = 0; m < 2; ++m)
                for (unsigned kind = 0; kind < 4; ++kind) {
                    memory.native = NULL;
                    memory.cache = NULL;
                    fixture(p, 1);
                    syn68k_fpu_reset();
                    set_status(0x2015);
                    unsigned frame = 0x20080, next = 0x28000;
                    EM_A7 = cpu_state.isp = frame;
                    cpu_state.usp = next;
                    cpu_state.msp = 0x201c0;
                    code_word(0x10000, 0x4e73);
                    code_word(0x10040, 0x4e73);
                    code_word(frame, m ? 0x1015 : 0x0015);
                    put32(physical(frame + 2), 0x10080);
                    code_word(frame + 6, 0x1000);
                    cpu_state.vbr = 0x20000;
                    put32(physical(cpu_state.vbr + 8), 0x10040);
                    supervisor = 0;
                    put32(pte(0x10000), 0xc001);
                    if (kind)
                        put32(pte(next), kind == 3 ? 0x10001 : 0x10019);
                    supervisor = 1;
                    Syn68kCache040 cache;
                    Syn68kMMU040Cache binding;
                    uint32_t cacr = 0x80000000;
                    if (kind >= 2) {
                        syn68k_cache040_init(&cache);
                        binding = (Syn68kMMU040Cache){.state = &cache, .cacr = &cacr};
                        memory.cache = &binding;
                    }
                    if (kind >= 2) {
                        supervisor = 0;
                        unsigned physical_push = kind == 2 ? 0xb000 : (pte(next) & ~15u);
                        supervisor = 1;
                        cpu_state.dfc = 0;
                        unsigned handler = 0x10040;
                        code_word(handler, 0x2f40);
                        code_word(handler + 2, 8);
                        handler += 4;
                        for (unsigned word = 0; word < 4; ++word) {
                            code_word(handler, 0x202f);
                            code_word(handler + 2, 44 + word * 4);
                            handler += 4;
                            code_word(handler, 0x0eb9);
                            code_word(handler + 2, 0x0800);
                            put32(physical(handler + 4), physical_push + word * 4);
                            handler += 8;
                        }
                        code_word(handler, 0x202f);
                        code_word(handler + 2, 8);
                        code_word(handler + 4, 0x4e73);
                    }
                    Syn68kMMU040Native native;
                    if (config & 2)
                        assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
                    if (kind == 1)
                        fail_physical = 0x10000;
                    if (kind >= 2) {
                        uint32_t v;
                        Syn68kMMU040Access a = syn68k_mmu040_read(&memory, 0x10000, 6, 2, 0, &v);
                        assert(!a.translation.fault && !a.bus_error);
                        a = syn68k_mmu040_read(&memory, frame, 5, 2, 0, &v);
                        assert(!a.translation.fault && !a.bus_error);
                        if (kind == 2) {
                            a = syn68k_mmu040_memory_translate(&memory, next, 1, 0, 0);
                            assert(!a.translation.fault);
                            for (unsigned way = 0; way < 4; ++way) {
                                Syn68kCache040Line* l = &cache.lines[0][0][way];
                                *l = (Syn68kCache040Line){.address = 0x8000 + way * 0x1000, .valid = 1, .dirty = 1};
                                memset(l->bytes, 0x50 + way, 16);
                            }
                            cache.replacement[0] = 0;
                            fail_write_physical = 0xb000;
                        }
                        else {
                            supervisor = 0;
                            unsigned address = pte(next);
                            supervisor = 1;
                            Syn68kCache040Line* l = &cache.lines[0][(address >> 4) & 63][0];
                            *l = (Syn68kCache040Line){
                                .address = address & ~15u, .valid = 1, .dirty = 1u << ((address >> 2) & 3)};
                            memcpy(l->bytes, ram + (address & ~15u), 16);
                            fail_write_physical = address;
                        }
                    }
                    Syn68kMappedCPU cpu = {.pc = 0x10000, .memory = &memory, .native = config & 1};
                    Syn68kMappedRun run;
                    Syn68kMappedDelivery d;
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    if (run.step.vector != 2 || EM_A7 != next || run.step.access.fc != 1 ||
                        (kind >= 2 ? run.step.cache_restart != 2 : !run.step.restartable_read))
                        fprintf(stderr,
                                "candidate config=%u page=%u M=%u kind=%u vector=%u sp=%x "
                                "fc=%u cert=%u push=%u fault=%u\n",
                                config, page_size, m, kind, run.step.vector, EM_A7, run.step.access.fc,
                                run.step.restartable_read, run.step.cache_restart, run.step.access.cache.fault);
                    assert(run.step.vector == 2 && EM_A7 == next && run.step.access.fc == 1);
                    assert(kind >= 2 ? run.step.cache_restart == 2 : run.step.restartable_read == 1);
                    unsigned push_address = run.step.access.cache.push_address;
                    unsigned char push_data[16];
                    memcpy(push_data, run.step.access.cache.push_data, 16);
                    unsigned saved_d0 = EM_D0;
                    fail_physical = fail_write_physical = UINT32_MAX;
                    assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &d));
                    assert(d.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x10040);
                    unsigned expected_sp = (m ? 0x201c0 : frame + 8) - 60;
                    assert(EM_A7 == expected_sp);
                    assert(syn68k_mapped_cpu_run(&cpu, kind >= 2 ? 11 : 1, &run));
                    if (kind >= 2)
                        assert(!memcmp(ram + push_address, push_data, 16) && EM_D0 == saved_d0);
                    assert(run.reason == SYN68K_MAPPED_BUDGET && cpu.pc == 0x10000 && EM_A7 == next &&
                           status() == (m ? 0x1015 : 0x0015));
                    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
                    assert(run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 8 && EM_A7 == next);
                    ++passed;
                }
    memory.native = NULL;
    memory.cache = NULL;
    return passed;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 2 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc >= 2 ? argv[1] : ""));
    uint64_t compiled = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    unsigned count = cache_faults();
    count += header_faults();
    uint64_t emitted = syn68k_native_compile_count() - compiled, loaded = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && loaded : emitted);
    printf("%u RTE user-prefix/header/bank/CCR/fault/guest-cleanup/privilege "
           "cases passed; emitted=%llu hits=%llu\n",
           count, (unsigned long long)emitted, (unsigned long long)loaded);
    return 0;
}
