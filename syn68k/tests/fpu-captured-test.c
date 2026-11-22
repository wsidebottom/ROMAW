/*+===================================================================
  File:      fpu-captured-test.c

  Summary:   Validate checked FPU completion from native operands without repeated device reads or EA updates.

  Classes:   None.

  Functions: main.

  Origin:    Original ARDI Executor direct 68040 FPU regression fixture.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fpu-captured.h"
#include "native040-ea.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[65536];
static Syn68kMMU040 mmu;
static unsigned reads, forbid_reads, cases;
static Syn68kNative040Entry read_root, ea_root;

static int read_operand(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                        unsigned fc)
{
    assert(!opaque && !forbid_reads && !attributes && (fc == 1 || fc == 2 || fc == 5 || fc == 6));
    assert(address < sizeof ram && size <= sizeof ram - address);
    memcpy(bytes, ram + address, size);
    ++reads;
    return 1;
}

static Syn68kMMU040Memory memory = {.mmu = &mmu, .read = read_operand};

static void put(uint32_t address, uint32_t value, unsigned size)
{
    assert(address < sizeof ram && size <= sizeof ram - address);
    for (unsigned i = 0; i < size; ++i)
        ram[address + i] = value >> ((size - i - 1) * 8);
}

static unsigned format_size(unsigned format)
{
    const unsigned sizes[] = {4, 4, 12, 0, 2, 8, 1};
    return sizes[format];
}

static void operand(unsigned address, unsigned format, unsigned sample)
{
    const uint32_t singles[] = {0, 0x3f800000, 0xbf800000, 0x7f812345, 1, 0x7f7fffff, 0x7fc12345, 0x7f800000};
    const uint64_t doubles[] = {
        0, UINT64_C(0x3ff0000000000000), UINT64_C(0xbff0000000000000), UINT64_C(0x7ff0123456789abc),
        1, UINT64_C(0x7fefffffffffffff), UINT64_C(0x7ff8123456789abc), UINT64_C(0x7ff0000000000000)};
    const uint16_t high[] = {0, 0x3fff, 0xbfff, 0x7fff, 0, 0x7ffe, 0x7fff, 0x7fff};
    const uint64_t low[] = {
        0,          UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000000), UINT64_C(0x80123456789abcde), 1,
        UINT64_MAX, UINT64_C(0xc0123456789abcde), UINT64_C(0x8000000000000000)};
    const uint32_t integers[] = {0, 1, UINT32_MAX, 0x7fffffff, 0x80000000, 127, 32767, 42};
    if (format == 2) {
        put(address, (uint32_t)high[sample] << 16, 4);
        put(address + 4, low[sample] >> 32, 4);
        put(address + 8, low[sample], 4);
    }
    else if (format == 5) {
        put(address, doubles[sample] >> 32, 4);
        put(address + 4, doubles[sample], 4);
    }
    else
        put(address, format == 1 ? singles[sample] : integers[sample], format_size(format));
}

static void fixture(unsigned ea, unsigned format, unsigned operation, unsigned sample, unsigned mode)
{
    memset(&cpu_state, 0, sizeof cpu_state);
    memset(ram, 0, sizeof ram);
    syn68k_mmu040_init(&mmu);
    reads = forbid_reads = 0;
    cpu_state.sr = mode & 1 ? 0x2000 : 0;
    cpu_state.fpu.fpcr = mode == 1 ? 0xff00 : mode == 2 ? 0x50 : mode == 3 ? 0xb0 : 0;
    cpu_state.fpu.fpsr = 0x09ff12f8;
    cpu_state.fpu.fpiar = 0x98765432;
    cpu_state.fpu.null_state = sample & 1;
    for (unsigned reg = 0; reg < 8; ++reg)
        cpu_state.fpu.regs[reg] = (Syn68kFP80){0x3fff, UINT64_C(0x8000000000000000) + reg};
    if (sample == 5)
        cpu_state.fpu.regs[3] = (Syn68kFP80){0x7ffe, UINT64_MAX};
    cpu_state.regs[8].ul.n = 0x6000 + ((ea >> 3) == 4 ? format_size(format) : 0);
    put(0x1002, 0x4000 | format << 10 | 3 << 7 | operation, 2);
    operand(0x6000, format, sample);
    operand(0x6020, format, sample);
    put(0x6010, 0x6020, 4);
    if (ea == 0x28)
        put(0x1004, 0x0020, 2);
    else if (ea == 0x30) {
        put(0x1004, 0x0161, 2);
        put(0x1006, 0x0010, 2);
    }
    else if (ea == 0x39)
        put(0x1004, 0x6000, 4);
    else if (ea == 0x3a)
        put(0x1004, 0x4ffc, 2);
    else if (ea == 0x3c)
        operand(0x1004 + (format == 6), format, sample);
    else if (ea == 3) {
        uint32_t value = 0;
        for (unsigned i = 0; i < format_size(format); ++i)
            value = value << 8 | ram[0x6000 + i];
        cpu_state.regs[3].ul.n = value;
    }
}

static uint32_t read_value(Syn68kFPUCaptured* captured, Syn68kEA ea, uint32_t address, unsigned size)
{
    if (ea.reg >= 0) {
        uint32_t value = cpu_state.regs[ea.reg].ul.n;
        return size == 1 ? value & 255 : size == 2 ? value & 65535 : value;
    }
    Syn68kNative040MMU frame = {.memory = &memory,
                                .address = address,
                                .size = size,
                                .fc =
                                    ea.instruction ? (cpu_state.sr & 0x2000 ? 6 : 2) : (cpu_state.sr & 0x2000 ? 5 : 1)};
    read_root(&frame);
    assert(!frame.access.bus_error && frame.access.translation.fault == MMU040_OK);
    captured->access = frame.access;
    return frame.value;
}

static void compare(unsigned ea_code, unsigned format, unsigned operation, unsigned sample, unsigned mode)
{
    unsigned opcode = 0xf200 | ea_code;
    fixture(ea_code, format, operation, sample, mode);
    Syn68kFPUStep expected_step;
    assert(syn68k_fpu_step_decoded(0x1000, opcode, &memory, &expected_step));
    CPUState expected = cpu_state;
    unsigned expected_reads = reads;
    fixture(ea_code, format, operation, sample, mode);
    Syn68kFPUCaptured captured = {.pc = 0x1000, .opcode = opcode, .original_null = cpu_state.fpu.null_state};
    captured.command = read_value(&captured, (Syn68kEA){.reg = -1, .instruction = 1}, 0x1002, 2);
    cpu_state.fpu.null_state = 0;
    Syn68kNative040EA address = {.cpu = &cpu_state,
                                 .context = {.pc = 0x1004, .memory = &memory},
                                 .code = ea_code,
                                 .size = format_size(format),
                                 .registers = 1};
    ea_root(&address);
    assert(!address.context.error);
    if (address.context.access.size)
        captured.access = address.context.access;
    Syn68kEA ea = address.result;
    captured.next = address.context.pc;
    captured.ea = ea.reg < 0 && !ea.instruction ? ea.address : 0;
    if (format == 2) {
        captured.source.high = read_value(&captured, ea, ea.address, 4) >> 16;
        captured.source.low = (uint64_t)read_value(&captured, ea, ea.address + 4, 4) << 32;
        captured.source.low |= read_value(&captured, ea, ea.address + 8, 4);
    }
    else {
        uint64_t bits = read_value(&captured, ea, ea.address, format == 5 ? 4 : format_size(format));
        if (format == 5)
            bits = bits << 32 | read_value(&captured, ea, ea.address + 4, 4);
        else if (format == 4)
            bits = (int16_t)bits;
        else if (format == 6)
            bits = (int8_t)bits;
        Syn68kNative040FPU conversion = {.bits = bits, .precision = 80};
        Syn68kNative040Entry convert = syn68k_native040_entry(format == 1   ? NATIVE040_FROM_F32
                                                              : format == 5 ? NATIVE040_FROM_F64
                                                                            : NATIVE040_FROM_I32);
        assert(convert);
        convert(&conversion);
        captured.source = conversion.result;
        captured.unsupported = conversion.unsupported;
        captured.signaling = conversion.signaling;
        captured.source_tag = conversion.source_tag;
    }
    assert(reads == expected_reads);
    forbid_reads = 1;
    Syn68kFPUStep actual;
    assert(syn68k_fpu_finish_captured(&captured, &actual));
    if (memcmp(&actual, &expected_step, sizeof actual) || memcmp(&cpu_state, &expected, sizeof expected)) {
        fprintf(stderr, "Captured FPU mismatch: ea=%02x format=%u op=%02x sample=%u mode=%u\n", ea_code, format,
                operation, sample, mode);
        fprintf(stderr, "step vector=%u/%u next=%08x/%08x exception-pc=%08x/%08x format=%u/%u ea=%08x/%08x\n",
                actual.vector, expected_step.vector, actual.next, expected_step.next, actual.exception_pc,
                expected_step.exception_pc, actual.format, expected_step.format, actual.fault_address,
                expected_step.fault_address);
        for (unsigned i = 0; i < sizeof expected; ++i)
            if (((unsigned char*)&cpu_state)[i] != ((unsigned char*)&expected)[i]) {
                fprintf(stderr, "first CPU difference at byte %u: %02x/%02x\n", i, ((unsigned char*)&cpu_state)[i],
                        ((unsigned char*)&expected)[i]);
                break;
            }
    }
    assert(!memcmp(&actual, &expected_step, sizeof actual) && !memcmp(&cpu_state, &expected, sizeof expected));
    assert(reads == expected_reads);
    ++cases;
}

static void invalid_packets(void)
{
    Syn68kFPUCaptured valid = {.pc = 0x1000,
                               .next = 0x1004,
                               .opcode = 0xf210,
                               .command = 0x4000,
                               .source = {0x3fff, UINT64_C(0x8000000000000000)},
                               .access = {.size = 4, .completed = 4, .fc = 1}};
    forbid_reads = 1;
    for (unsigned bad = 0; bad < 29; ++bad) {
        Syn68kFPUCaptured packet = valid;
        memset(&cpu_state, 0, sizeof cpu_state);
        switch (bad) {
        case 0:
            packet.pc |= 1;
            break;
        case 1:
            packet.next |= 1;
            break;
        case 2:
            packet.next = packet.pc + 2;
            break;
        case 3:
            packet.next = packet.pc + 18;
            break;
        case 4:
            packet.opcode |= 0x10000;
            break;
        case 5:
            packet.opcode = 0xf248;
            break;
        case 6:
            packet.command = 0;
            break;
        case 7:
            packet.command |= 3 << 10;
            break;
        case 8:
            packet.command |= 7 << 10;
            break;
        case 9:
            packet.opcode = 0xf208;
            break;
        case 10:
            packet.opcode = 0xf200;
            packet.command |= 5 << 10;
            break;
        case 11:
            packet.opcode = 0xf23d;
            break;
        case 12:
            packet.command |= 127;
            break;
        case 13:
            packet.original_null = 2;
            break;
        case 14:
            packet.unsupported = 2;
            break;
        case 15:
            packet.signaling = 2;
            break;
        case 16:
            packet.source_tag = 1;
            break;
        case 17:
            cpu_state.fpu.frame_size = 52;
            break;
        case 18:
            packet.access.bus_error = 1;
            break;
        case 19:
            packet.access.translation.fault = 1;
            break;
        case 20:
            packet.access.writing = 1;
            break;
        case 21:
            packet.access.rmw = 1;
            break;
        case 22:
            packet.access.size = 3;
            break;
        case 23:
            packet.access.completed = 2;
            break;
        case 24:
            packet.access.fc = 7;
            break;
        case 25:
            packet.access.size = 0;
            packet.access.completed = 0;
            break;
        case 26:
            packet.command |= 0x10000;
            break;
        case 27:
            packet.has_entry = 2;
            break;
        case 28:
            packet.has_entry = 1;
            packet.entry.original_null = 1;
            break;
        }
        CPUState before = cpu_state;
        Syn68kFPUStep result;
        memset(&result, 0xa5, sizeof result);
        Syn68kFPUStep previous = result;
        assert(!syn68k_fpu_finish_captured(&packet, &result));
        assert(!memcmp(&before, &cpu_state, sizeof before) && !memcmp(&result, &previous, sizeof result));
        ++cases;
    }
    assert(!syn68k_fpu_finish_captured(NULL, NULL) && !syn68k_fpu_finish_captured(&valid, NULL));
    memset(&cpu_state, 0, sizeof cpu_state);
    valid.pc = 0xfffffffc;
    valid.next = 0;
    Syn68kFPUStep result;
    assert(syn68k_fpu_finish_captured(&valid, &result) && !result.vector && !result.next);
    assert(cpu_state.fpu.fpiar == valid.pc && cpu_state.fpu.regs[0].exponent == 0x3fff &&
           cpu_state.fpu.regs[0].significand == UINT64_C(0x8000000000000000));
    ++cases;
}

int main(void)
{
    read_root = syn68k_native040_entry(NATIVE040_READ);
    ea_root = syn68k_native040_entry(NATIVE040_EA);
    assert(read_root && ea_root);
    const unsigned operations[] = {0,    4,    0x18, 0x1a, 0x20, 0x22, 0x23, 0x28, 0x24, 0x27, 0x38, 0x3a, 0x40, 0x44,
                                   0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66, 0x63, 0x67, 0x68, 0x6c};
    const unsigned eas[] = {3, 0x10, 0x18, 0x20, 0x28, 0x30, 0x39, 0x3a, 0x3c};
    for (unsigned f = 0; f < 7; ++f) {
        if (f == 3)
            continue;
        for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op)
            for (unsigned e = 0; e < sizeof eas / sizeof *eas; ++e) {
                if (eas[e] == 3 && (f == 2 || f == 5))
                    continue;
                for (unsigned sample = 0; sample < 8; ++sample)
                    for (unsigned mode = 0; mode < 4; ++mode)
                        compare(eas[e], f, operations[op], sample, mode);
            }
    }
    invalid_packets();
    printf("%u captured FPU arithmetic/frame/device-read/EA/invalid-packet cases passed without operand replay\n",
           cases);
    return 0;
}
