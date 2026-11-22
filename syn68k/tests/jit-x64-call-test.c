/*+===================================================================
  File:      jit-x64-call-test.c

  Summary:   Implements jit x64 call test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: __wrap_trap_direct, main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Native subroutine targets, live extensions and checked stack-fault ordering. */
#include "jit-x64.h"
#include "hash.h"
#include "trap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases, faults, decoder_calls;

syn68k_addr_t __real_trap_direct(uint32 trap_number, uint32 exception_pc, uint32 exception_address);

syn68k_addr_t __wrap_trap_direct(uint32 trap_number, uint32 exception_pc, uint32 exception_address)
{
    ++decoder_calls;
    return __real_trap_direct(trap_number, exception_pc, exception_address);
}

static uint32 fault_pc;

static syn68k_addr_t fault_handler(syn68k_addr_t pc, void* argument)
{
    (void)argument;
    assert(pc == fault_pc);
    ++faults;
    return MAGIC_EXIT_EMULATOR_ADDRESS;
}

static void state(unsigned flags, unsigned bank, uint32 stack)
{
    memset(&cpu_state, 0, sizeof cpu_state);
    for (unsigned i = 0; i < sizeof cpu_state.jsr_stack / sizeof(jsr_stack_elt_t); ++i)
        cpu_state.jsr_stack[i].tag = UINT32_MAX;
    for (unsigned i = 0; i < 16; ++i)
        EM_DREG(i) = 0x12340000 + i * 0x100;
    cpu_state.ccc = flags & 1 ? 0x80000000 : 0;
    cpu_state.ccv = flags & 2 ? 0x12345678 : 0;
    cpu_state.ccnz = flags & 4 ? 0 : 0x80008000;
    cpu_state.ccn = flags & 8 ? 0x87654321 : 0;
    cpu_state.ccx = flags & 16 ? 0x11111111 : 0;
    cpu_state.sr = bank == 0 ? 0 : bank == 1 ? 0x2000 : 0x3000;
    cpu_state.isp = 0xF000;
    cpu_state.msp = 0xE000;
    cpu_state.usp = 0xD000;
    EM_A7 = stack;
}

static void* compile(uint32 pc)
{
    int8 lengths[] = {1};
    unsigned translated;
    int terminal;
    void* code = syn68k_x64_compile(SYN68K_TO_US(pc), lengths, 1, &translated, pc, &terminal);
    assert(code && translated == 1 && terminal);
    return code;
}

static void check(void* code, uint32 target, uint32 next_pc, uint32 stack)
{
    CPUState expected = cpu_state;
    expected.regs[15].ul.n = stack - 4;
    uint64 before = syn68k_native_execution_count();
    assert(syn68k_x64_execute(code) == target);
    assert(syn68k_native_execution_count() == before + 1);
    assert(READUL(stack - 4) == next_pc);
    assert(!memcmp(&expected, &cpu_state, sizeof expected));
    ++cases;
}

static void ordinary_calls(void)
{
    const int32 displacements[][6] = {
        {-128, -2, 2, 4, 124, 126}, {-32768, -2, 0, 2, 32766, 32767}, {INT32_MIN, -32768, -2, 0, 32766, INT32_MAX}};
    const uint32 stacks[] = {0x4004, 0x4005, 0x4006, 0x4007, 0x5002};
    for (unsigned width = 0; width < 3; ++width) {
        WRITEUW(0x1000, 0x6100 | (width == 0 ? 2 : width == 1 ? 0 : 255));
        WRITEUL(0x1002, 0);
        void* code = compile(0x1000);
        for (unsigned d = 0; d < 6; ++d) {
            int32 disp = displacements[width][d];
            if (width == 0) {
                syn68k_x64_release(code);
                WRITEUW(0x1000, 0x6100 | (uint8)disp);
                code = compile(0x1000);
            }
            else if (width == 1)
                WRITEUW(0x1002, (uint16)disp);
            else
                WRITEUL(0x1002, (uint32)disp);
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned flags = 0; flags < 32; ++flags)
                    for (unsigned s = 0; s < sizeof stacks / sizeof *stacks; ++s) {
                        state(flags, bank, stacks[s]);
                        check(code, 0x1002 + (uint32)disp, 0x1002 + width * 2, stacks[s]);
                    }
        }
        syn68k_x64_release(code);
    }
    const uint32 targets[] = {0, UINT32_MAX, 0x12345678, 0x80000000};
    for (unsigned form = 0; form < 19; ++form) {
        unsigned op = form < 8     ? 0x4E90 + form
                      : form < 16  ? 0x4EA8 + form - 8
                      : form == 16 ? 0x4EB8
                      : form == 17 ? 0x4EB9
                                   : 0x4EBA;
        WRITEUW(0x1000, op);
        WRITEUL(0x1002, 0);
        void* code = compile(0x1000);
        for (unsigned t = 0; t < sizeof targets / sizeof *targets; ++t)
            for (unsigned bank = 0; bank < 3; ++bank)
                for (unsigned flags = 0; flags < 32; ++flags) {
                    uint32 stack = 0x6004, target = targets[t];
                    state(flags, bank, stack);
                    if (form < 8) {
                        if (form == 7)
                            target = stack;
                        else
                            EM_AREG(form) = target;
                    }
                    else if (form < 16) {
                        if (form == 15)
                            target = stack + 0xFFFF8000;
                        else
                            EM_AREG(form - 8) = target - 0xFFFF8000;
                        WRITEUW(0x1002, 0x8000);
                    }
                    else if (form == 16) {
                        WRITEUW(0x1002, (uint16)target);
                        target = (uint32)(int32)(int16)target;
                    }
                    else if (form == 17)
                        WRITEUL(0x1002, target);
                    else {
                        WRITEUW(0x1002, (uint16)target);
                        target = 0x1002 + (uint32)(int32)(int16)target;
                    }
                    check(code, target, form < 8 ? 0x1002 : form == 17 ? 0x1006 : 0x1004, stack);
                }
        syn68k_x64_release(code);
    }
}

/* Kinds 0/1 are JMP/JSR, 2..9 are LEA to A0..A7, and 10 is PEA. */
static unsigned address_opcode(unsigned kind)
{
    return kind < 2 ? (kind ? 0x4E80 : 0x4EC0) : kind == 10 ? 0x4840 : 0x41C0 | (kind - 2) << 9;
}

static void check_address(void* code, unsigned kind, uint32 address, uint32 next_pc, uint32 stack)
{
    CPUState expected = cpu_state;
    WRITEUL(stack - 4, 0xCAFEBABE);
    if (kind == 10)
        expected.regs[15].ul.n -= 4;
    else
        expected.regs[kind + 6].ul.n = address;
    uint64 before = syn68k_native_execution_count();
    assert(syn68k_x64_execute(code) == next_pc);
    assert(syn68k_native_execution_count() == before + 1);
    assert(!memcmp(&expected, &cpu_state, sizeof expected));
    assert(READUL(stack - 4) == (kind == 10 ? address : 0xCAFEBABE));
    ++cases;
}

static void ordinary_addresses(void)
{
    unsigned before = decoder_calls;
    const uint32 values[] = {0, 0x7FFF, 0x8000, UINT32_MAX};
    for (unsigned kind = 2; kind <= 10; ++kind)
        for (unsigned form = 0; form < 19; ++form) {
            unsigned ea = form < 8 ? 16 + form : form < 16 ? 32 + form : 40 + form;
            WRITEUW(0x1000, address_opcode(kind) | ea);
            WRITEUL(0x1002, 0);
            void* code = compile(0x1000);
            for (unsigned sample = 0; sample < 4; ++sample)
                for (unsigned flags = 0; flags < 32; ++flags) {
                    uint32 stack = 0x6004 + sample, value = values[sample];
                    state(flags, flags % 3, stack);
                    uint32 address;
                    if (ea < 24)
                        address = EM_AREG(ea & 7);
                    else if (ea < 48)
                        address = EM_AREG(ea & 7) + (uint32)(int32)(int16)value;
                    else if (ea == 57)
                        address = value;
                    else
                        address = (uint32)(int32)(int16)value + (ea == 58 ? 0x1002 : 0);
                    if (ea == 57)
                        WRITEUL(0x1002, value);
                    else
                        WRITEUW(0x1002, (uint16)value);
                    check_address(code, kind, address, ea < 24 ? 0x1002 : ea == 57 ? 0x1006 : 0x1004, stack);
                }
            syn68k_x64_release(code);
        }
    assert(decoder_calls == before);
}

static void indexed_calls_and_jumps(void)
{
    unsigned before = decoder_calls;
    const uint32 values[] = {0, 0x7FFF, 0x8000, 0x80000001};
    const int displacements[] = {-128, -1, 0, 127};
    const uint32 stacks[] = {0x6004, 0x8004, 0x9004, 0xF004};
    for (unsigned kind = 0; kind <= 10; ++kind)
        for (unsigned form = 0; form < 9; ++form) {
            WRITEUW(0x1000, address_opcode(kind) | (form == 8 ? 59 : 48 + form));
            WRITEUW(0x1002, 0);
            void* code = compile(0x1000);
            for (unsigned reg = 0; reg < 16; ++reg)
                for (unsigned wide = 0; wide < 2; ++wide)
                    for (unsigned scale = 0; scale < 4; ++scale)
                        for (unsigned d = 0; d < 4; ++d)
                            for (unsigned sample = 0; sample < 4; ++sample) {
                                uint32 stack = stacks[sample];
                                state((reg + sample + scale * 7) & 31, sample % 3, stack);
                                if (reg != 15)
                                    EM_DREG(reg) = values[sample];
                                uint32 index = EM_DREG(reg);
                                if (!wide)
                                    index = (uint32)(int32)(int16)index;
                                uint32 base = form == 8 ? 0x1002 : EM_AREG(form);
                                uint32 target = base + (index << scale) + (uint32)displacements[d];
                                WRITEUW(0x1002, reg << 12 | wide << 11 | scale << 9 | (uint8)displacements[d]);
                                WRITEUL(stack - 4, 0xCAFEBABE);
                                if (kind == 1)
                                    check(code, target, 0x1004, stack);
                                else if (kind >= 2)
                                    check_address(code, kind, target, 0x1004, stack);
                                else {
                                    CPUState expected = cpu_state;
                                    assert(syn68k_x64_execute(code) == target);
                                    assert(!memcmp(&expected, &cpu_state, sizeof expected));
                                    assert(READUL(stack - 4) == 0xCAFEBABE);
                                    ++cases;
                                }
                            }
            syn68k_x64_release(code);
        }
    assert(decoder_calls == before); /* Successful brief EAs are native. */
}

static void changed_full_format(void)
{
    const unsigned extensions[] = {0x0150, 0x01F0, 0x0130, 0x01F3, 0x0100};
    fault_pc = 0x1000;
    for (unsigned kind = 0; kind <= 10; ++kind)
        for (unsigned relative = 0; relative < 2; ++relative) {
            WRITEUW(0x1000, address_opcode(kind) | (relative ? 59 : 55));
            WRITEUW(0x1002, 0);
            void* code = compile(0x1000);
            for (unsigned i = 0; i < sizeof extensions / sizeof *extensions; ++i) {
                WRITEUW(0x1002, extensions[i]);
                WRITEUL(0x1004, 0x3000);
                WRITEUL(0x1008, 0x12345678);
                WRITEUL(0x3000, 0x4000);
                state(31, 0, 0x6004);
                EM_D0 = 0;
                CPUState saved = cpu_state;
                unsigned before = decoder_calls;
                WRITEUL(0x6000, 0xCAFEBABE);
                uint32 target = trap_direct(4, 0x1000, 0);
                CPUState expected = cpu_state;
                uint32 return_pc = READUL(0x6000);
                cpu_state = saved;
                WRITEUL(0x6000, 0xCAFEBABE);
                assert(syn68k_x64_execute(code) == target);
                /* The reference enters the decoder once. Valid full-format EAs
               now use the bounded address helper and native instruction body;
               the reserved final extension must still enter the decoder. */
                assert(decoder_calls == before + 1 + (extensions[i] == 0x0100));
                assert(!memcmp(&expected, &cpu_state, sizeof expected));
                if (kind == 1 || kind == 10)
                    assert(READUL(0x6000) == return_pc);
                else
                    assert(READUL(0x6000) == 0xCAFEBABE);
                ++cases;
            }
            syn68k_x64_release(code);
        }
}

static void checked_paths(void* memory)
{
    unsigned char *initial = malloc(0x10000), *expected = malloc(0x10000);
    assert(initial && expected);
    /* Compare full CPU/RAM state, including exception frames. Extension faults
     must precede predecrement; stack faults must retain the decremented A7. */
    const unsigned ops[] = {0x6102, 0x6100, 0x61FF, 0x4E90, 0x4E97, 0x4EB9, 0x4EAF, 0x4EB7, 0x4EBB,
                            0x4EF7, 0x4EFB, 0x41EF, 0x4FF7, 0x4857, 0x486F, 0x4877, 0x487B, 0x4879};
    for (unsigned i = 0; i < sizeof ops / sizeof *ops; ++i)
        for (unsigned kind = 0; kind < 4; ++kind) {
            uint32 pc = kind == 0 && (ops[i] == 0x6100 || ops[i] == 0x61FF || (ops[i] & 63) >= 40) ? 0xFFFE : 0x1000;
            WRITEUW(pc, ops[i]);
            if (pc == 0x1000)
                WRITEUL(pc + 2, 0x12345678);
            void* code = compile(pc);
            state(31, 0, kind == 1 ? 2 : kind == 2 ? 0x10002 : 0x4004);
            EM_A0 = 0x2000;
            if (kind == 3)
                hash_lookup_code_and_create_if_needed(0x4000);
            memcpy(initial, memory, 0x10000);
            CPUState saved = cpu_state;
            fault_pc = pc;
            unsigned before = faults;
            uint32 target = trap_direct(4, pc, 0);
            CPUState result = cpu_state;
            memcpy(expected, memory, 0x10000);
            unsigned expected_faults = faults - before;
            cpu_state = saved;
            memcpy(memory, initial, 0x10000);
            if (kind == 3)
                hash_lookup_code_and_create_if_needed(0x4000);
            before = faults;
            assert(syn68k_x64_execute(code) == target);
            assert(faults - before == expected_faults);
            assert(!memcmp(&result, &cpu_state, sizeof result));
            assert(!memcmp(expected, memory, 0x10000));
            syn68k_x64_release(code);
            destroy_blocks(0x4000, 16);
            ++cases;
        }
    free(expected);
    free(initial);
}

static void boundary_and_invalidation(void)
{
    WRITEUW(0x1FFC, 0x61FF);
    WRITEUL(0x1FFE, 0);
    void* code = compile(0x1FFC);
    WRITEUL(0x1FFE, 0xFFFF8000);
    state(31, 0, 0x6004);
    check(code, 0xFFFF9FFE, 0x2002, 0x6004);
    syn68k_x64_release(code);
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        syn68k_x64_enabled = enabled;
        destroy_blocks(0x1000, 0x2000);
        WRITEUW(0x1000, 0x6100);
        WRITEUW(0x1002, 0xFFE);
        WRITEUW(0x2000, 0x702A);
        WRITEUW(0x2002, 0x4EF9);
        WRITEUL(0x2004, MAGIC_EXIT_EMULATOR_ADDRESS);
        state(0, 0, 0x1004);
        uint64 before = syn68k_native_execution_count();
        /* The return write overwrites the executing call. Its guard must tail
         out before the checked write releases that native allocation. */
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_D0 == 42 && EM_A7 == 0x1000);
        assert(READUL(0x1000) == 0x1004);
        assert(!hash_lookup(0x1000));
        assert(syn68k_native_execution_count() == before + enabled * 2);
        ++cases;
    }
}

static void pea_invalidation(void)
{
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        syn68k_x64_enabled = enabled;
        destroy_blocks(0x1000, 0x2000);
        WRITEUW(0x1000, 0x4850); /* PEA (A0). */
        WRITEUW(0x1002, 0x4EF9);
        WRITEUL(0x1004, MAGIC_EXIT_EMULATOR_ADDRESS);
        state(31, 0, 0x1004);
        /* Overwrite the executing PEA, keeping the next JMP opcode valid. */
        EM_A0 = 0x12344EF9;
        uint64 before = syn68k_native_execution_count();
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_A7 == 0x1000 && READUL(0x1000) == 0x12344EF9);
        assert(!hash_lookup(0x1000));
        assert(syn68k_native_execution_count() == before + enabled * 2);
        ++cases;
    }
}

static void call_return_dispatch(void)
{
    for (unsigned enabled = 0; enabled < 2; ++enabled) {
        syn68k_x64_enabled = enabled;
        destroy_blocks(0x1000, 0x2000);
        WRITEUW(0x1000, 0x6100);
        WRITEUW(0x1002, 0xFE);
        WRITEUW(0x1004, 0x4EF9);
        WRITEUL(0x1006, MAGIC_EXIT_EMULATOR_ADDRESS);
        WRITEUW(0x1100, 0x5280); /* ADDQ.L #1,D0 */
        WRITEUW(0x1102, 0x4E75);
        state(0, 0, 0x6004);
        EM_D0 = 0;
        uint64 before = syn68k_native_execution_count();
        interpret_code(hash_lookup_code_and_create_if_needed(0x1000));
        assert(EM_D0 == 1 && EM_A7 == 0x6004);
        assert(syn68k_native_execution_count() == before + enabled * 3);
        ++cases;
    }
}

int main(void)
{
    void* memory = calloc(1, 0x10000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    trap_install_handler(2, fault_handler, NULL);
    trap_install_handler(4, fault_handler, NULL);
    ordinary_calls();
    ordinary_addresses();
    indexed_calls_and_jumps();
    changed_full_format();
    checked_paths(memory);
    boundary_and_invalidation();
    pea_invalidation();
    call_return_dispatch();
    printf("%u native BSR/JSR/JMP/LEA/PEA address, stack, fault and dispatch cases passed.\n", cases);
    return 0;
}
