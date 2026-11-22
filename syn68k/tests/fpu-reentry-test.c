/*+===================================================================
  File:      fpu-reentry-test.c

  Summary:   Preserve checked FPU context and floating scratch across nested device callbacks.

  Classes:   None.

  Functions: main; includes fpu_command_matrix_unused and the bounded command-test fixture.

  Origin:    Original ARDI Executor mapped FPU callback regression.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the bounded memory, CPU fixtures and transfer records. */
int fpu_command_matrix_unused(void);
#define main fpu_command_matrix_unused
#include "fpu-command-test.c"
#undef main

extern int8_t float_rounding_mode, floatx80_rounding_precision, float_exception_flags, float_detect_tininess;
static unsigned depth, limit, child_exception, nested;
static unsigned outer_opcode;

static void callback(void)
{
    if (depth == limit)
        return;
    ++depth;
    ++nested;
    CPUState saved_cpu = cpu_state;
    Transfer saved_transfers[64];
    memcpy(saved_transfers, transfers, sizeof transfers);
    unsigned saved_count = transfer_count;
    uint32_t saved_failed = failed;
    failed = UINT32_MAX;
    cpu_state.fpu.fpcr = 0xb0 | (child_exception ? 0x200 : 0);
    cpu_state.fpu.frame_size = cpu_state.fpu.pending_vector = 0;
    cpu_state.fpu.regs[0] = (Syn68kFP80){0x4000, UINT64_C(0x8000000000000000)};
    cpu_state.regs[8].ul.n = 0x7000;
    unsigned pc = 0x2000 + depth * 0x100;
    int last = depth == limit;
    Syn68kFPUStep child;
    assert(syn68k_fpu_finish_command(pc, last ? 0xf200 : 0xf210, last ? 4 : 0x4422, &memory, &child));
    if (child.vector || cpu_state.fpu.fpiar != pc)
        fprintf(stderr,
                "Child mismatch depth=%u/%u exception=%u vector=%u fpiar=%x pc=%x outer=%x source=%02x%02x%02x%02x\n",
                depth, limit, child_exception, child.vector, cpu_state.fpu.fpiar, pc, outer_opcode, ram[0x6000],
                ram[0x6001], ram[0x6002], ram[0x6003]);
    assert(!child.vector && cpu_state.fpu.fpiar == pc && child.next == pc + 4);
    if (last && child_exception) {
        assert(cpu_state.fpu.frame_size && cpu_state.fpu.pending_vector == 49);
        unsigned count = transfer_count;
        Syn68kFPUStep pending;
        assert(syn68k_fpu_step_decoded(pc + 4, 0xf200, &memory, &pending));
        assert(pending.vector == 49 && transfer_count == count && cpu_state.fpu.fpiar == pc);
    }
    cpu_state = saved_cpu;
    memcpy(transfers, saved_transfers, sizeof transfers);
    transfer_count = saved_count;
    failed = saved_failed;
    --depth;
}

static int callback_read(void* opaque, uint32_t address, unsigned char* bytes, unsigned size, uint32_t attributes,
                         unsigned fc)
{
    if ((fc == 2 || fc == 6) && address == start) {
        assert(!latched && size == 2 && transfer_count < 64);
        bytes[0] = outer_opcode >> 8;
        bytes[1] = outer_opcode;
        transfers[transfer_count++] = (Transfer){.address = address, .value = outer_opcode, .size = size, .fc = fc};
        return 1;
    }
    if (fc == 1 || fc == 5)
        callback();
    return read_operand(opaque, address, bytes, size, attributes, fc);
}

static int callback_write(void* opaque, uint32_t address, const unsigned char* bytes, unsigned size,
                          uint32_t attributes, unsigned fc)
{
    callback();
    return write_operand(opaque, address, bytes, size, attributes, fc);
}

static int outer(unsigned opcode, unsigned command, unsigned entry, Syn68kFPUStep* result)
{
    outer_opcode = opcode;
    put(0x7000, 0x3f800000, 4);
    float_rounding_mode = 3;
    floatx80_rounding_precision = 32;
    float_exception_flags = 0x5a;
    float_detect_tininess = 1;
    latched = entry == 2;
    int handled = entry == 2 ? syn68k_fpu_finish_command(start, opcode, command, &memory, result)
                  : entry    ? syn68k_fpu_step_decoded(start, opcode, &memory, result)
                             : syn68k_fpu_step(start, &memory, result);
    assert(float_rounding_mode == 3 && floatx80_rounding_precision == 32 && float_exception_flags == 0x5a &&
           float_detect_tininess == 1 && !depth);
    return handled;
}

int main(void)
{
    memory.read = callback_read;
    memory.write = callback_write;
    Syn68kMMU040Native native;
    unsigned callbacks = 0;
    const unsigned eas[] = {0x10, 0x18, 0x20, 0x28, 0x30, 0x39, 0x3a};
    const unsigned commands[] = {0x4422, 0x4a22, 0x5422, 0x6080, 0x6480, 0x6880, 0x7480,
                                 0x7880, 0x9000, 0x9c00, 0xbc00, 0xc0ff, 0xe0ff};
    const uint32_t faults[] = {UINT32_MAX, 0x6000, 0x6004, 0x6010};
    for (unsigned bus = 0; bus < 2; ++bus) {
        if (bus)
            assert(syn68k_native040_memory(&memory, &native, (Syn68kNative040RAM){0}));
        for (unsigned entry = 0; entry < 3; ++entry)
            for (unsigned ea = 0; ea < sizeof eas / sizeof *eas; ++ea)
                for (unsigned cmd = 0; cmd < sizeof commands / sizeof *commands; ++cmd)
                    for (unsigned mode = 0; mode < 4; ++mode)
                        for (unsigned fault = 0; fault < sizeof faults / sizeof *faults; ++fault) {
                            unsigned opcode = 0xf200 | eas[ea], command = commands[cmd];
                            fixture(command, mode);
                            failed = faults[fault];
                            limit = 0;
                            Syn68kFPUStep expected;
                            assert(outer(opcode, command, entry, &expected));
                            CPUState expected_cpu = cpu_state;
                            memcpy(expected_ram, ram, sizeof ram);
                            unsigned expected_count = transfer_count;
                            memcpy(expected_transfers, transfers, sizeof transfers);
                            for (unsigned nesting = 1; nesting <= 3; ++nesting)
                                for (unsigned exception = 0; exception < 2; ++exception) {
                                    fixture(command, mode);
                                    failed = faults[fault];
                                    limit = nesting;
                                    child_exception = exception;
                                    nested = 0;
                                    Syn68kFPUStep actual;
                                    assert(outer(opcode, command, entry, &actual));
                                    assert(!memcmp(&expected, &actual, sizeof expected));
                                    assert(!memcmp(&expected_cpu, &cpu_state, sizeof cpu_state));
                                    assert(!memcmp(expected_ram, ram, sizeof ram));
                                    assert(transfer_count == expected_count);
                                    for (unsigned i = 0; i < transfer_count; ++i) {
                                        Transfer a = transfers[i], b = expected_transfers[i];
                                        assert(a.address == b.address && a.value == b.value && a.size == b.size &&
                                               a.fc == b.fc && a.writing == b.writing);
                                    }
                                    callbacks += nested;
                                    ++cases;
                                }
                        }
    }
    assert(callbacks);
    printf(
        "%u checked FPU context/fault/EA cases and %u nested callbacks preserved instruction and arithmetic scratch\n",
        cases, callbacks);
    return 0;
}
