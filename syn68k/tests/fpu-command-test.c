/*+===================================================================
  File:      fpu-command-test.c

  Summary:   Compare checked FPU exits with consumed commands without rereading instruction devices.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor mapped native FPU exit regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fpu-captured.h"
#include "syn68k_public.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t address, value;
    unsigned size, fc, writing;
} Transfer;

static unsigned char ram[32768], expected_ram[sizeof ram];
static Syn68kMMU040 mmu;
static Transfer transfers[64], expected_transfers[64];
static unsigned transfer_count, latched, cases;
static uint32_t start, failed;

static void put(unsigned address, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        ram[address + i] = value >> ((size - i - 1) * 8);
}

static int read_operand(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    (void)opaque;
    assert(!attributes && transfer_count < 64);
    assert(!latched || (fc != 2 && fc != 6) || (address != start && address != start + 2));
    Transfer* transfer = &transfers[transfer_count++];
    *transfer = (Transfer){.address = address, .size = size, .fc = fc};
    if (address == failed || address >= sizeof ram || size > sizeof ram - address)
        return 0;
    memcpy(bytes, ram + address, size);
    for (unsigned i = 0; i < size; ++i)
        transfer->value = transfer->value << 8 | bytes[i];
    return 1;
}

static int write_operand(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    (void)opaque;
    assert(!attributes && transfer_count < 64);
    Transfer* transfer = &transfers[transfer_count++];
    *transfer = (Transfer){.address = address, .size = size, .fc = fc, .writing = 1};
    for (unsigned i = 0; i < size; ++i)
        transfer->value = transfer->value << 8 | bytes[i];
    if (address == failed || address >= sizeof ram || size > sizeof ram - address)
        return 0;
    memcpy(ram + address, bytes, size);
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu, .read = read_operand, .write = write_operand};

static void fixture(unsigned command, unsigned mode)
{
    memset(&cpu_state, 0, sizeof cpu_state);
    memset(ram, 0, sizeof ram);
    memset(transfers, 0, sizeof transfers);
    syn68k_mmu040_init(&mmu);
    latched = transfer_count = 0;
    start = mode == 3 ? UINT32_MAX - 1 : 0x1000;
    failed = mode == 1 ? start + 4 : mode == 2 ? 0x6000 : UINT32_MAX;
    cpu_state.sr = mode & 1 ? 0x2000 : 0;
    cpu_state.fpu.fpcr = command & 1 ? 0xff00 : 0x50;
    cpu_state.fpu.fpsr = 0x0bff18f8;
    cpu_state.fpu.fpiar = 0x98765432;
    cpu_state.fpu.null_state = command & 1;
    for (unsigned i = 0; i < 8; ++i) {
        cpu_state.regs[i].ul.n = 0x12340000 + i;
        cpu_state.regs[8 + i].ul.n = 0x6000 + i * 64;
        cpu_state.fpu.regs[i] = (Syn68kFP80){0x3fff, UINT64_C(0x8000000000000000) + i};
    }
    put(start + 2, command, 2);
    put(start + 4, 0x6010, 4); // Absolute long or full/brief extension input.
    put(0x6000, 0x3f800000, 4);
    put(0x6010, 0x6020, 4);
    put(0x6020, 0x3fff0000, 4);
    put(0x6024, 0x80000000, 4);
}

static void compare(unsigned ea, unsigned command, unsigned mode)
{
    fixture(command, mode);
    Syn68kFPUStep expected, actual;
    assert(syn68k_fpu_step_decoded(start, 0xf200 | ea, &memory, &expected));
    CPUState expected_cpu = cpu_state;
    Syn68kMMU040 expected_mmu = mmu;
    memcpy(expected_ram, ram, sizeof ram);
    assert(transfer_count && transfers[0].address == start + 2 && transfers[0].size == 2);
    unsigned expected_count = transfer_count - 1;
    memcpy(expected_transfers, transfers + 1, expected_count * sizeof transfers[0]);
    fixture(command, mode);
    latched = 1;
    /* Reject repeated instruction transfers at the bus boundary. Data reads
       may legally alias the command address, including across PC wrap. */
    assert(syn68k_fpu_finish_command(start, 0xf200 | ea, command, &memory, &actual));
    if (memcmp(&expected_cpu, &cpu_state, sizeof cpu_state)) {
        fprintf(stderr, "FPU command mismatch ea=%x command=%x mode=%u vectors=%u/%u next=%x/%x\n", ea, command, mode,
                expected.vector, actual.vector, expected.next, actual.next);
        for (unsigned i = 0; i < sizeof cpu_state; ++i)
            if (((unsigned char*)&expected_cpu)[i] != ((unsigned char*)&cpu_state)[i])
                fprintf(stderr, "CPU byte %u: %02x/%02x\n", i, ((unsigned char*)&expected_cpu)[i],
                        ((unsigned char*)&cpu_state)[i]);
    }
    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
    assert(!memcmp(&expected_mmu, &mmu, sizeof mmu));
    assert(!memcmp(&expected, &actual, sizeof expected));
    assert(transfer_count == expected_count);
    for (unsigned i = 0; i < transfer_count; ++i) {
        Transfer a = expected_transfers[i], b = transfers[i];
        assert(a.address == b.address && a.value == b.value && a.size == b.size && a.fc == b.fc &&
               a.writing == b.writing);
    }
    assert(!memcmp(expected_ram, ram, sizeof ram));
    ++cases;
}

int main(void)
{
    /* Every command bit pattern, including reserved classes and fields. */
    for (unsigned command = 0; command <= 65535; ++command)
        compare(0, command, 0);
    const unsigned commands[] = {0,      0x0022, 0x0420, 0x4400, 0x4a22, 0x5484, 0x5b38, 0x6400,
                                 0x6c00, 0x7000, 0x7c00, 0x9000, 0x9c00, 0xbc00, 0xc0ff, 0xe0ff};
    for (unsigned ea = 0; ea < 64; ++ea)
        for (unsigned i = 0; i < sizeof commands / sizeof *commands; ++i)
            for (unsigned mode = 0; mode < 4; ++mode)
                compare(ea, commands[i], mode);
    fixture(0, 0);
    CPUState before = cpu_state;
    Syn68kFPUStep result, saved;
    memset(&result, 0xa5, sizeof result);
    saved = result;
    Syn68kMMU040Memory broken = {0};
    Syn68kFPUEntry invalid_entry = {.original_null = 2};
    assert(!syn68k_fpu_finish_command_at(start, 0xf200, 0, &memory, &invalid_entry, &result));
    assert(!syn68k_fpu_finish_command(start | 1, 0xf200, 0, &memory, &result));
    assert(!syn68k_fpu_finish_command(start, 0xf240, 0, &memory, &result));
    assert(!syn68k_fpu_finish_command(start, 0xf200, 65536, &memory, &result));
    assert(!syn68k_fpu_finish_command(start, 0xf200, 0, NULL, &result));
    assert(!syn68k_fpu_finish_command(start, 0xf200, 0, &broken, &result));
    assert(!syn68k_fpu_finish_command(start, 0xf200, 0, &memory, NULL));
    assert(!memcmp(&before, &cpu_state, sizeof before) && !memcmp(&saved, &result, sizeof saved) && !transfer_count);
    cpu_state.fpu.frame_size = 4;
    assert(!syn68k_fpu_finish_command(start, 0xf200, 0, &memory, &result));
    assert(cpu_state.fpu.frame_size == 4 && !memcmp(&saved, &result, sizeof saved) && !transfer_count);
    printf("%u latched FPU command/EA/bus/fault/wrap cases passed without instruction-device replay\n", cases);
    return 0;
}
