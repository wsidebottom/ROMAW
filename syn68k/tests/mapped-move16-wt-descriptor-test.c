/*+===================================================================
  File:      mapped-move16-wt-descriptor-test.c

  Summary:   Verify MOVE16 table updates and page-fault cleanup with write-through descriptors.

  Classes:   None.

  Functions: main

  Origin:    Original ROMAW regression for MC68040UM 3.2.1, 4.3.3, 8.4.6 and UMAD item 13.

  Copyright and Legal notices: Applicable license: MIT; see the SPDX notice below.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#define main wt_descriptor_parent_unused
#include "mapped-cache040-test.c"
#undef main

static unsigned model_calls, source_reads, destination_writes, locked_updates, plain_updates;
static unsigned destination_physical, source_mode;
static Syn68kCache040Bus physical_bus;

Syn68kMMU040Result __real_syn68k_mmu040_translate(Syn68kMMU040*, const Syn68kMMU040Bus*, uint32_t, unsigned, int);

Syn68kMMU040Result __wrap_syn68k_mmu040_translate(Syn68kMMU040* m, const Syn68kMMU040Bus* b, uint32_t a, unsigned f,
                                                  int w)
{
    ++model_calls;
    return __real_syn68k_mmu040_translate(m, b, a, f, w);
}

Syn68kCache040Access __real_syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned,
                                                     unsigned, int, uint32_t, uint32_t, uint32_t, int,
                                                     enum Syn68kCache040Traffic);

Syn68kCache040Access __wrap_syn68k_cache040_transfer(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                     unsigned f, unsigned n, int w, uint32_t v, uint32_t attr,
                                                     uint32_t cacr, int na, enum Syn68kCache040Traffic t)
{
    ++model_calls;
    return __real_syn68k_cache040_transfer(c, b, a, f, n, w, v, attr, cacr, na, t);
}

Syn68kCache040Access __real_syn68k_cache040_move16(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t, unsigned, int,
                                                   unsigned char[16], uint32_t, uint32_t);

Syn68kCache040Access __wrap_syn68k_cache040_move16(Syn68kCache040* c, const Syn68kCache040Bus* b, uint32_t a,
                                                   unsigned f, int w, unsigned char d[16], uint32_t attr, uint32_t cacr)
{
    ++model_calls;
    return __real_syn68k_cache040_move16(c, b, a, f, w, d, attr, cacr);
}

static int io_read(void* opaque, uint32_t a, unsigned char* d, unsigned n, uint32_t attr, unsigned fc)
{
    int result = physical_read(opaque, a, d, n, attr, fc);
    if (a >= 0xc000 && a < 0xc010) {
        assert(n == 4);
        ++source_reads;
        /* A second peripheral read would supply different data. Hard cleanup
           must consume captured PD and skip, rather than retouch this source. */
        if (source_mode >= 2)
            for (unsigned i = 0; i < n; ++i)
                ram[a + i] += 0x10;
    }
    return result;
}

static int io_write(void* opaque, uint32_t a, const unsigned char* d, unsigned n, uint32_t attr, unsigned fc)
{
    if (a >= destination_physical && a < destination_physical + 16) {
        assert(n == 4);
        ++destination_writes;
    }
    return physical_write(opaque, a, d, n, attr, fc);
}

static int bus_read(void* opaque, uint32_t a, unsigned char* d, unsigned n, const Syn68kCache040Cycle* c,
                    unsigned* inhibit)
{
    *inhibit = 0;
    return io_read(opaque, a, d, n, c->attributes, c->fc);
}

static int bus_write(void* opaque, uint32_t a, const unsigned char* d, unsigned n, const Syn68kCache040Cycle* c)
{
    assert(c->kind != CACHE040_PUSH && "WT descriptor setup must not generate a dirty descriptor push");
    return io_write(opaque, a, d, n, c->attributes, c->fc);
}

static int table_read(void* opaque, uint32_t a, uint32_t* value)
{
    assert(opaque == ram);
    *value = get(a, 4);
    return 1;
}

static int table_update(void* opaque, uint32_t a, uint32_t value, int locked)
{
    assert(opaque == ram && (a == 0x1008 || a == 0x2000 || a == 0x2100 || a == 0x3000));
    locked_updates += !!locked;
    plain_updates += !locked;
    put(a, 4, locked ? get(a, 4) | value : value);
    return 1;
}

static void table_store(unsigned address, unsigned value)
{
    Syn68kMMU040Access a = syn68k_mmu040_write(&memory, address, 5, 4, value);
    assert(a.translation.fault == MMU040_OK && a.translation.physical == address &&
           !(a.translation.attributes & 0x60) && !a.bus_error && a.completed == 4);
    assert(get(address, 4) == value);
}

static void clean_descriptors(void)
{
    for (unsigned set = 0; set < 64; ++set)
        for (unsigned way = 0; way < 4; ++way) {
            Syn68kCache040Line* line = &cache.lines[0][set][way];
            if (line->valid && line->address >= 0x1000 && line->address < 0x3500) {
                assert(!line->dirty);
                assert(!memcmp(line->bytes, ram + line->address, 16));
            }
        }
}

/* Hardware's hard-cleanup path for a MOVE16 write page fault: drain PD,
   advance the stacked PC and captured postincrements, then RTE. The source
   can be a peripheral, so retrying the whole instruction is not equivalent. */
static unsigned cleanup(unsigned form)
{
    unsigned pc = 0x8100;
    put(pc, 2, 0x2f02);
    pc += 2; /* MOVE.L D2,-(SP) */
    for (unsigned i = 0; i < 4; ++i) {
        put(pc, 2, 0x242f);
        put(pc + 2, 2, 48 + i * 4);
        pc += 4;
        put(pc, 2, 0x0eb9);
        put(pc + 2, 2, 0x2800);
        put(pc + 4, 4, destination_physical + i * 4);
        pc += 8;
    }
    put(pc, 2, form == 4 ? 0x58af : 0x5caf);
    put(pc + 2, 2, 6);
    pc += 4; /* ADDQ.L length,6(SP) */
    unsigned increments = 0;
    if (form < 2 || form == 4) {
        put(pc, 2, 0x41e8);
        put(pc + 2, 2, 16);
        pc += 4; /* LEA 16(A0),A0 */
        ++increments;
    }
    if (form == 4) {
        put(pc, 2, 0x43e9);
        put(pc + 2, 2, 16);
        pc += 4;
        ++increments;
    }
    put(pc, 2, 0x241f);
    put(pc + 2, 2, 0x4e73);
    return 12 + increments;
}

static void exercise(unsigned config, unsigned fast_tables, unsigned page, unsigned cm, unsigned hit, unsigned warm,
                     unsigned um, unsigned used, unsigned form, unsigned user, unsigned trace, unsigned quarter,
                     unsigned protected)
{
    Syn68kMappedCPU cpu = setup(config & 2 ? 2 : config & 1);
    cpu.native = config & 1;
    if (memory.native)
        native.ram =
            fast_tables ? (Syn68kNative040RAM){ram, 0, 0x9000} : (Syn68kNative040RAM){ram + 0x8000, 0x8000, 0x1000};
    /* Source/destination stay observable. Native tables either use their live
       callbacks or lower RAM reads/writes/locked OR directly in the image. */
    memory.read = io_read;
    memory.write = io_write;
    physical_bus = (Syn68kCache040Bus){.opaque = ram, .read_cycle = bus_read, .write_cycle = bus_write};
    binding.bus = &physical_bus;
    mmu.dtt[0] = 0xc000; /* Descriptor region is WT, never CB (UMAD item 13). */
    mmu.tc = page ? 0xc000 : 0x8000;
    mmu.srp = mmu.urp = 0x1000;
    memory.tables = (Syn68kMMU040Bus){ram, table_read, table_update};
    source_mode = cm;
    destination_physical = page ? 0xa040 : 0x9040;
    source_reads = destination_writes = locked_updates = plain_updates = 0;
    unsigned models = model_calls;
    table_store(0x1008, 0x2002 | (used ? 8 : 0));
    table_store(0x2000, 0x3002 | (used ? 8 : 0));
    table_store(0x2100, 0x3402 | (used ? 8 : 0));
    table_store(0x3400, 0xc019 | cm << 5);
    unsigned leaf = (page ? 0xa000 : 0x9000) | 1 | (um == 0 ? 0 : um == 1 ? 8 : 24) | (protected ? 4 : 0);
    table_store(0x3000, leaf);
    if (warm) {
        const unsigned addresses[] = {0x1008, 0x2000, 0x2100, 0x3000, 0x3400};
        for (unsigned i = 0; i < 5; ++i) {
            uint32_t value;
            Syn68kMMU040Access a = syn68k_mmu040_read(&memory, addresses[i], 5, 4, 0, &value);
            assert(!a.translation.fault && !a.bus_error && value == get(addresses[i], 4));
            assert(!(a.translation.attributes & 0x60));
        }
    }
    clean_descriptors();
    for (unsigned i = 0; i < 16; ++i) {
        ram[0xc000 + i] = i + 1;
        ram[destination_physical + i] = 0xaa;
    }
    unsigned source = 0x05000000 + quarter * 4 + (form & 3);
    unsigned destination = 0x04000040 + ((quarter + form) & 3) * 4 + ((form + 1) & 3);
    if (hit == 1) {
        uint32_t value;
        Syn68kMMU040Access a = syn68k_mmu040_read(&memory, source & ~3u, user ? 1 : 5, 4, 0, &value);
        assert(!a.translation.fault && !a.bus_error);
    }
    if (hit == 2)
        for (unsigned i = 0; i < 4; ++i) {
            Syn68kMMU040Access a =
                syn68k_mmu040_write(&memory, 0x05000000 + i * 4, user ? 1 : 5, 4, 0x41424344u + i * 0x04040404u);
            assert(!a.translation.fault && !a.bus_error);
        }
    if (hit)
        assert(resident(0, 0xc000) && resident(0, 0xc000)->dirty == (hit == 2 ? 15u : 0u));
    source_reads = destination_writes = 0;
    cpu_state.sr = (user ? 0 : 0x2000) | trace << 14;
    cpu_state.isp = 0xd000;
    EM_A7 = user ? 0xe000 : 0xd000;
    cpu_state.dfc = 0;
    EM_D2 = 0xdeadbeef;
    EM_A0 = form & 1 ? destination : source;
    EM_A1 = destination;
    unsigned initial_a0 = EM_A0;
    put(8, 4, 0x8100);
    put(0x8000, 2, 0xf600 | form << 3);
    put(0x8002, form == 4 ? 2 : 4, form == 4 ? 0x9000 : form & 1 ? source : destination);
    unsigned steps = cleanup(form);
    Syn68kMappedRun run;
    assert(syn68k_mapped_cpu_run(&cpu, 1, &run));
    assert(source_reads == (hit ? 0u : 4u));
    /* Figure 3-10 writes ancestor U bits without a locked RMW. Table
       3-1 uses locked RMW for a protected page's U-only transition. */
    assert(locked_updates == ((config & 2) && fast_tables ? 0u : protected && !um));
    assert(plain_updates == ((config & 2) && fast_tables ? 0u : (used ? 0u : 3u) + (!protected && um != 2)));
    assert(get(0x1008, 4) == 0x200a && get(0x2000, 4) == 0x300a && get(0x2100, 4) == 0x340a);
    assert(get(0x3000, 4) == (leaf | (protected ? 8 : 24)));
    clean_descriptors();
    if (protected) {
        assert(!trace && run.reason == SYN68K_MAPPED_EXCEPTION && run.step.vector == 2 && run.step.move16.state == 4 &&
               run.step.access.translation.fault == MMU040_WRITE_PROTECTED);
        assert(cpu.pc == 0x8000 && EM_A0 == initial_a0 && EM_A1 == destination && !destination_writes);
        Syn68kMappedDelivery delivery;
        assert(syn68k_mapped_exception_enter(&cpu, &run.step, 0, &delivery));
        assert(delivery.reason == SYN68K_DELIVERY_COMPLETE && cpu.pc == 0x8100);
        unsigned sp = EM_A7;
        assert(get(sp + 6, 2) == 0x7008 && get(sp + 16, 2) == (0xe8 | (user ? 1 : 5)));
        for (unsigned i = 0; i < 16; ++i)
            assert(ram[sp + 44 + i] == (hit == 2 ? 0x41u : 1u) + i);
        assert(syn68k_mapped_cpu_run(&cpu, steps, &run));
        assert(run.reason == SYN68K_MAPPED_BUDGET);
    }
    else {
        assert(run.reason == (trace & 2 ? SYN68K_MAPPED_EXCEPTION : SYN68K_MAPPED_BUDGET));
        if (trace & 2)
            assert(run.step.vector == 9);
    }
    assert(cpu.pc == 0x8000 + (form == 4 ? 4u : 6u) && destination_writes == 4 && source_reads == (hit ? 0u : 4u));
    assert(EM_A0 == (form == 4  ? source + 16
                     : form < 2 ? initial_a0 + 16
                                : initial_a0) &&
           EM_A1 == (form == 4 ? destination + 16 : destination) && EM_D2 == 0xdeadbeef &&
           EM_A7 == (user ? 0xe000u : 0xd000u));
    for (unsigned i = 0; i < 16; ++i)
        assert(ram[destination_physical + i] == (hit == 2 ? 0x41u : 1u) + i);
    clean_descriptors();
    if (config & 2)
        assert(model_calls == models);
    ++cases;
}

int main(int argc, char** argv)
{
    assert(argc == 1 || argc == 3);
    assert(!_putenv_s("SYN68K_AOT_CACHE", argc == 3 ? argv[1] : ""));
    uint64_t emitted = syn68k_native_compile_count(), hits = syn68k_aot_hit_count();
    for (unsigned config = 0; config < 4; ++config)
        for (unsigned fast_tables = 0; fast_tables < (config & 2 ? 2u : 1u); ++fast_tables)
            for (unsigned page = 0; page < 2; ++page)
                for (unsigned cm = 0; cm < 4; ++cm)
                    for (unsigned hit = 0; hit < 3; ++hit) {
                        if ((hit == 1 && cm >= 2) || (hit == 2 && cm != 1))
                            continue;
                        for (unsigned warm = 0; warm < 2; ++warm)
                            for (unsigned um = 0; um < 3; ++um)
                                for (unsigned used = 0; used < 2; ++used)
                                    for (unsigned form = 0; form < 5; ++form)
                                        for (unsigned user = 0; user < 2; ++user)
                                            for (unsigned trace = 0; trace < 4; ++trace)
                                                for (unsigned quarter = 0; quarter < 4; ++quarter) {
                                                    exercise(config, fast_tables, page, cm, hit, warm, um, used, form,
                                                             user, trace, quarter, 0);
                                                    if (!trace)
                                                        exercise(config, fast_tables, page, cm, hit, warm, um, used,
                                                                 form, user, trace, quarter, 1);
                                                }
                    }
    for (unsigned mode = 0; mode < 3; ++mode)
        instruction_view(mode);
    emitted = syn68k_native_compile_count() - emitted;
    hits = syn68k_aot_hit_count() - hits;
    if (argc == 3)
        assert(atoi(argv[2]) ? !emitted && hits : emitted);
    printf("%u WT-descriptor/MOVE16/UM/cache/peripheral/PD/skip/guest-RTE cases passed; emitted=%llu hits=%llu\n",
           cases, (unsigned long long)emitted, (unsigned long long)hits);
    return 0;
}
