/*+===================================================================
  File:      jit-x64-coverage-test.c

  Summary:   Implements jit x64 coverage test support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Structural emission coverage, not an ISA or extension correctness oracle. */
#include "jit-x64.h"
#include "mapping.h"
#include "blockinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(void)
{
    void* ram = calloc(1, 0x10000);
    assert(ram);
    ROMlib_offset = (uintptr_t)ram;
    syn68k_set_memory_window(0x10000, 0, 0);
    uint32 vectors[64] = {0};
    initialize_68k_emulator(NULL, 1, vectors, 0);
    unsigned valid = 0, lowered = 0, missing[16] = {0};

    for (unsigned op = 0; op < 65536; ++op) {
        unsigned map = opcode_map_index[op];
        if (map == opcode_map_index[0x4afc] && op != 0x4afc)
            continue;
        for (unsigned i = 0; i < 32; ++i)
            WRITEUW(0x1000 + i * 2, 0);
        WRITEUW(0x1000, op);
        int n = instruction_size(SYN68K_TO_US(0x1000), &opcode_map_info[map]);
        if (n <= 0 || n > 16)
            continue;
        WRITEUW(0x1000 + n * 2, 0x4E71);
        WRITEUW(0x1002 + n * 2, 0x4AFC);
        Block block = {0};
        TempBlockInfo temp = {0};
        compute_block_info(&block, SYN68K_TO_US(0x1000), &temp);
        int8 length = temp.next_instr_offset[0];
        unsigned translated;
        int terminal;
        void* code = syn68k_x64_compile(SYN68K_TO_US(0x1000), &length, 1, &translated, 0x1000, &terminal);
        ++valid;
        if (code) {
            assert(translated == 1 && terminal);
            ++lowered;
            syn68k_x64_release(code);
        }
        else {
            ++missing[op >> 12];
            fprintf(stderr, "Missing opcode=%04X length=%u map=%u\n", op, (unsigned)length, map);
        }
        /* Also require a native prefix in the real decoder-selected block.
         Single-instruction eligibility alone misses nonterminal fallback. */
        code = syn68k_x64_compile(SYN68K_TO_US(0x1000), temp.next_instr_offset, temp.num_68k_instrs, &translated,
                                  0x1000, &terminal);
        if (!code || !translated) {
            fprintf(stderr, "Missing block-entry opcode=%04X\n", op);
            abort();
        }
        syn68k_x64_release(code);
        free(temp.next_instr_offset);
    }

    printf("Decoder-table opcode samples lowered: %u/%u\n", lowered, valid);
    for (unsigned i = 0; i < 16; ++i)
        if (missing[i])
            printf("%X: %u missing\n", i, missing[i]);
    assert(valid && lowered == valid);
    free(ram);
    return 0;
}
