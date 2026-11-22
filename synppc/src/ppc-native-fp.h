/*+===================================================================
  File:      ppc-native-fp.h

  Summary:   Emit direct floating arithmetic with checked exceptional exits on x86 hosts.

  Classes:   Uses memory_emitter, native_block and powerpc_cpu from ppc-jit.cpp.

  Functions: Internal arithmetic/estimate decoding and emit_native_fp_arithmetic, emit_native_fp_estimate.

  Origin:    Original ARDI Executor native PowerPC instruction lowering.

  Copyright and Legal notices: Applicable license: synppc/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ROMAW_PPC_NATIVE_FP_H
#define ROMAW_PPC_NATIVE_FP_H

unsigned native_fp_arithmetic_operation(uint32 opcode)
{
    unsigned primary = opcode >> 26, operation = (opcode >> 1) & 31;
    if (primary != 59 && primary != 63)
        return 0;
    if (operation == 18 || operation == 20 || operation == 21)
        return opcode & 0x000007C0 ? 0 : operation;
    if (operation == 25)
        return opcode & 0x0000F800 ? 0 : operation;
    if (operation == 22 || (primary == 63 && operation == 12))
        return opcode & 0x001F07C0 ? 0 : operation;
    return 0;
}

unsigned native_fp_estimate_operation(uint32 opcode)
{
    unsigned primary = opcode >> 26, operation = (opcode >> 1) & 31;
    if ((primary == 59 && operation == 24) || (primary == 63 && operation == 26))
        return opcode & 0x001F07C0 ? 0 : operation;
    return 0;
}

std::vector<size_t> emit_native_fp_estimate(std::vector<unsigned char>& bytes, const memory_emitter& emit,
                                            powerpc_cpu& cpu, uint32 opcode)
{
    std::vector<size_t> exceptional;
    bool reciprocal = native_fp_estimate_operation(opcode) == 24;
    auto source = reinterpret_cast<const unsigned char*>(&cpu.fpr_dw((opcode >> 11) & 31));
    auto result = reinterpret_cast<const unsigned char*>(&emit.block->fp_result);
    emit(bytes, 0x8b, source + 4);
    bytes.push_back(0x25);
    emit_word(bytes, reciprocal ? 0x7fffffff : 0xffffffff);
    bytes.push_back(0x2d);
    emit_word(bytes, 0x00100000);
    bytes.push_back(0x3d);
    emit_word(bytes, 0x7fe00000);
    exceptional.push_back(emit_local_jump(bytes, 0x83)); // Zero, denormal, NaN, infinity, or negative sqrt.
    if (reciprocal) {
        // FRES specifies an extended reciprocal followed by binary32 rounding.
        // Save every x87 register and control/status bit, including a nonempty
        // caller stack, before using 64-bit significand precision for division.
        emit(bytes, 0xd9, &emit.block->fp_control, 7); // FNSTCW.
        emit(bytes, 0x8b, &emit.block->fp_control);
        bytes.push_back(0x25);
        emit_word(bytes, 0x0c00);
        bytes.push_back(0x0d);
        emit_word(bytes, 0x033f);
        emit(bytes, 0x89, &emit.block->fp_control);
        auto environment = [&](bool restore) {
#if defined(__x86_64__)
            emit(bytes, 0x8d, emit.block->fp_x87_environment, 0, false, true);
            bytes.insert(bytes.end(), {0x48, 0x83, 0xc0, 15, 0x48, 0x83, 0xe0, 0xf0, 0x48});
#else
            emit(bytes, 0x8d, emit.block->fp_x87_environment);
            bytes.insert(bytes.end(), {0x83, 0xc0, 15, 0x83, 0xe0, 0xf0});
#endif
            bytes.insert(bytes.end(), {0x0f, 0xae, (unsigned char)(restore ? 0x08 : 0x00)});
        };
        environment(false);                            // FXSAVE[64] preserves complete instruction/data pointers too.
        bytes.insert(bytes.end(), {0xdb, 0xe3});       // FNINIT establishes a private empty stack.
        emit(bytes, 0xd9, &emit.block->fp_control, 5); // FLDCW.
        bytes.insert(bytes.end(), {0xd9, 0xe8});       // FLD1.
        emit(bytes, 0xdc, source, 6);                  // FDIV binary64.
        emit(bytes, 0xd9, &emit.block->fp_single_first, 3); // FSTP binary32.
        bytes.insert(bytes.end(), {0xdf, 0xe0});            // FNSTSW AX.
        emit(bytes, 0x89, &emit.block->fp_status);
        environment(true); // FXRSTOR[64], before any checked exit.
        emit(bytes, 0xf7, &emit.block->fp_status);
        emit_word(bytes, 0x1f);
        exceptional.push_back(emit_local_jump(bytes, 0x85));
        emit(bytes, 0x8b, &emit.block->fp_single_first);
        bytes.push_back(0x25);
        emit_word(bytes, 0x7fffffff);
        bytes.push_back(0x2d);
        emit_word(bytes, 0x00800000);
        bytes.push_back(0x3d);
        emit_word(bytes, 0x7f000000);
        exceptional.push_back(emit_local_jump(bytes, 0x83)); // Range exceptions retain scaled-result handling.
        bytes.push_back(0x3d);
        emit_word(bytes, 0);
        auto above_minimum = emit_local_jump(bytes, 0x85);
        emit(bytes, 0x8b, source + 4);
        bytes.push_back(0x25);
        emit_word(bytes, 0x7fffffff);
        bytes.push_back(0x3d);
        emit_word(bytes, 0x47d00000); // |source| > 2^126 is tiny before reciprocal rounding.
        exceptional.push_back(emit_local_jump(bytes, 0x87));
        auto normal_boundary = emit_local_jump(bytes, 0x82);
        emit(bytes, 0x83, source, 7);
        bytes.push_back(0);
        exceptional.push_back(emit_local_jump(bytes, 0x85));
        point_jump_here(bytes, normal_boundary);
        point_jump_here(bytes, above_minimum);
        emit_sse(bytes, emit, 0xf3, 0x10, &emit.block->fp_single_first);
        bytes.insert(bytes.end(), {0xf3, 0x0f, 0x5a, 0xc0}); // Exact widening of a normal binary32.
    }
    else {
        // Deterministic nearest-even sqrt followed by reciprocal, matching the
        // interpreter's 750 estimate policy independently of guest RN.
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_saved_control, 3);
        emit(bytes, 0x8b, &emit.block->fp_saved_control);
        bytes.push_back(0x25);
        emit_word(bytes, ~0xe07fu);
        bytes.push_back(0x0d);
        emit_word(bytes, 0x1f80);
        emit(bytes, 0x89, &emit.block->fp_control);
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_control, 2);
        emit_sse(bytes, emit, 0xf2, 0x51, source, 1);
        emit(bytes, 0xc7, &emit.block->fp_reference);
        emit_word(bytes, 0);
        emit(bytes, 0xc7, reinterpret_cast<const unsigned char*>(&emit.block->fp_reference) + 4);
        emit_word(bytes, 0x3ff00000);
        emit_sse(bytes, emit, 0xf2, 0x10, &emit.block->fp_reference);
        bytes.insert(bytes.end(), {0xf2, 0x0f, 0x5e, 0xc1});
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_saved_control, 2);
    }
    emit_sse(bytes, emit, 0xf2, 0x11, result);
    emit(bytes, 0xc7, &emit.block->fp_flags);
    emit_word(bytes, 0);
    emit(bytes, 0xc7, &emit.block->fp_raised);
    emit_word(bytes, 0);
    emit(bytes, 0x8b, result + 4);
    bytes.insert(bytes.end(), {0xc1, 0xf8, 31});
    bytes.push_back(0x25);
    emit_word(bytes, 0x4000);
    bytes.push_back(0x05);
    emit_word(bytes, 0x4000);
    emit(bytes, 0x89, &emit.block->fp_class);
    return exceptional;
}

std::vector<size_t> emit_native_fp_arithmetic(std::vector<unsigned char>& bytes, const memory_emitter& emit,
                                              powerpc_cpu& cpu, uint32 opcode)
{
    std::vector<size_t> exceptional;
    unsigned operation = native_fp_arithmetic_operation(opcode);
    bool single = opcode >> 26 == 59 || operation == 12;
    bool unary = operation == 12 || operation == 22;
    auto first = reinterpret_cast<const unsigned char*>(&cpu.fpr_dw((opcode >> (unary ? 11 : 16)) & 31));
    auto second = reinterpret_cast<const unsigned char*>(&cpu.fpr_dw((opcode >> (operation == 25 ? 6 : 11)) & 31));
    auto result = reinterpret_cast<const unsigned char*>(&emit.block->fp_result);
    auto truncated = reinterpret_cast<const unsigned char*>(&emit.block->fp_reference);
    auto immediate = [&](unsigned instruction, uint32 value) {
        bytes.push_back(instruction);
        emit_word(bytes, value);
    };
    auto guard_operand = [&](const unsigned char* source) {
        emit(bytes, 0x8b, source + 4);
        immediate(0x25, 0x7FFFFFFF);
        immediate(0x3d, 0x7FF00000);
        exceptional.push_back(emit_local_jump(bytes, 0x83));
        immediate(0x3d, 0x00100000);
        auto normal = emit_local_jump(bytes, 0x83);
        emit(bytes, 0x0b, source);
        exceptional.push_back(emit_local_jump(bytes, 0x85));
        auto zero = emit_local_jump(bytes, 0);
        point_jump_here(bytes, normal);
        if (single && operation != 12) {
            emit(bytes, 0xf7, &emit.block->fpscr);
            emit_word(bytes, 4);
            auto ieee = emit_local_jump(bytes, 0x84);
            immediate(0x3d, 0x38100000);
            exceptional.push_back(emit_local_jump(bytes, 0x82));
            point_jump_here(bytes, ieee);
        }
        point_jump_here(bytes, zero);
    };
    guard_operand(first);
    if (!unary)
        guard_operand(second);

    // Every host floating exception is masked, and each evaluation starts
    // without sticky flags. Original status/control is restored before exits.
    emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_saved_control, 3);
    emit(bytes, 0x8b, &emit.block->fp_saved_control);
    immediate(0x25, ~0x807Fu);
    immediate(0x0d, 0x1F80);
    emit(bytes, 0x89, &emit.block->fp_control);
    auto evaluate = [&](const void* output, bool truncate) {
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_control, 2);
        size_t cancellation = 0;
        if (single && operation == 20) {
            // Preserve the classic integer-bias subtraction path: equal
            // sign/exponent inputs cancel exactly before single rounding.
            emit(bytes, 0x8b, first + 4);
            emit(bytes, 0x33, second + 4);
            immediate(0x25, 0xFFF00000);
            auto different_exponents = emit_local_jump(bytes, 0x85);
            emit_sse(bytes, emit, 0xf2, 0x10, first);
            emit_sse(bytes, emit, 0xf2, 0x10, second, 1);
            bytes.insert(bytes.end(), {0xf2, 0x0f, 0x5c, 0xc1, 0xf2, 0x0f, 0x5a, 0xc0, 0xf3, 0x0f, 0x5a, 0xc0});
            cancellation = emit_local_jump(bytes, 0);
            point_jump_here(bytes, different_exponents);
        }
        if (single && operation != 12 && truncate) {
            emit_sse(bytes, emit, 0xf3, 0x10, &emit.block->fp_single_first);
            if (!unary)
                emit_sse(bytes, emit, 0xf3, 0x10, &emit.block->fp_single_second, 1);
        }
        else {
            emit_sse(bytes, emit, 0xf2, 0x10, first);
            if (!unary)
                emit_sse(bytes, emit, 0xf2, 0x10, second, 1);
            if (single) {
                bytes.insert(bytes.end(), {0xf2, 0x0f, 0x5a, 0xc0});
                if (!unary)
                    bytes.insert(bytes.end(), {0xf2, 0x0f, 0x5a, 0xc9});
                if (operation != 12) {
                    emit_sse(bytes, emit, 0xf3, 0x11, &emit.block->fp_single_first);
                    if (!unary)
                        emit_sse(bytes, emit, 0xf3, 0x11, &emit.block->fp_single_second, 1);
                    // Operand narrowing is separate from operation status.
                    emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_control, 2);
                }
            }
        }
        if (operation != 12) {
            unsigned instruction = operation == 18   ? 0x5e
                                   : operation == 20 ? 0x5c
                                   : operation == 21 ? 0x58
                                   : operation == 25 ? 0x59
                                                     : 0x51;
            bytes.insert(bytes.end(), {(unsigned char)(single ? 0xf3 : 0xf2), 0x0f, (unsigned char)instruction,
                                       (unsigned char)(unary ? 0xc0 : 0xc1)});
        }
        if (single)
            bytes.insert(bytes.end(), {0xf3, 0x0f, 0x5a, 0xc0});
        if (cancellation)
            point_jump_here(bytes, cancellation);
        emit_sse(bytes, emit, 0xf2, 0x11, output);
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_status, 3);
        emit_sse(bytes, emit, 0, 0xae, &emit.block->fp_saved_control, 2);
    };
    evaluate(result, false);
    emit(bytes, 0xf7, &emit.block->fp_status);
    emit_word(bytes, 0x1F); // Invalid, denormal, divide-by-zero and range exceptions.
    exceptional.push_back(emit_local_jump(bytes, 0x85));
    emit(bytes, 0x8b, result + 4);
    immediate(0x25, 0x7FFFFFFF);
    immediate(0x3d, 0x7FF00000);
    exceptional.push_back(emit_local_jump(bytes, 0x83));
    const uint32 minimum = single ? 0x38100000 : 0x00100000;
    immediate(0x3d, minimum);
    auto normal_result = emit_local_jump(bytes, 0x83);
    emit(bytes, 0x0b, result);
    exceptional.push_back(emit_local_jump(bytes, 0x85));
    auto zero_result = emit_local_jump(bytes, 0);
    point_jump_here(bytes, normal_result);
    // SSE detects tininess after rounding; PPC detects it before rounding.
    // An inexact minimum-normal result needs the checked range path.
    immediate(0x3d, minimum);
    auto above_minimum = emit_local_jump(bytes, 0x85);
    emit(bytes, 0x83, result, 7);
    bytes.push_back(0);
    auto fraction = emit_local_jump(bytes, 0x85);
    emit(bytes, 0xf7, &emit.block->fp_status);
    emit_word(bytes, 0x20);
    exceptional.push_back(emit_local_jump(bytes, 0x85));
    point_jump_here(bytes, above_minimum);
    point_jump_here(bytes, fraction);
    point_jump_here(bytes, zero_result);

    emit(bytes, 0x8b, &emit.block->fp_status);
    bytes.insert(bytes.end(), {0x83, 0xe0, 0x20, 0xc1, 0xe0, 12});
    emit(bytes, 0x89, &emit.block->fp_flags);
    bytes.insert(bytes.end(), {0xc1, 0xe0, 8});
    emit(bytes, 0x89, &emit.block->fp_raised);
    auto exact = emit_local_jump(bytes, 0x84);
    emit(bytes, 0x81, &emit.block->fp_control, 1);
    emit_word(bytes, 0x6000); // Evaluate the same operation toward zero for FR.
    evaluate(truncated, true);
    emit(bytes, 0x8b, result + 4);
    emit(bytes, 0x3b, truncated + 4);
    auto incremented = emit_local_jump(bytes, 0x87);
    auto smaller = emit_local_jump(bytes, 0x82);
    emit(bytes, 0x8b, result);
    emit(bytes, 0x3b, truncated);
    auto unchanged = emit_local_jump(bytes, 0x86);
    point_jump_here(bytes, incremented);
    emit(bytes, 0x81, &emit.block->fp_flags, 1);
    emit_word(bytes, 0x00040000);
    point_jump_here(bytes, smaller);
    point_jump_here(bytes, unchanged);
    point_jump_here(bytes, exact);

    emit(bytes, 0x8b, result + 4);
    immediate(0x25, 0x7FFFFFFF);
    emit(bytes, 0x0b, result);
    auto zero_class = emit_local_jump(bytes, 0x84);
    emit(bytes, 0x8b, result + 4);
    bytes.insert(bytes.end(), {0xc1, 0xf8, 31});
    immediate(0x25, 0x4000);
    immediate(0x05, 0x4000);
    auto classified = emit_local_jump(bytes, 0);
    point_jump_here(bytes, zero_class);
    emit(bytes, 0x8b, result + 4);
    bytes.insert(bytes.end(), {0xc1, 0xe8, 15});
    immediate(0x25, 0x10000);
    immediate(0x0d, 0x2000);
    point_jump_here(bytes, classified);
    emit(bytes, 0x89, &emit.block->fp_class);
    return exceptional;
}

#endif
