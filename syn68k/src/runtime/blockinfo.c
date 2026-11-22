/*+===================================================================
  File:      blockinfo.c

  Summary:   Implements blockinfo support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: amode_size, compute_block_info, instruction_size, syn68k_indexed_extension_valid

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "syn68k_private.h"
#include "block.h"
#include "mapping.h"
#include "rangetree.h"
#include "blockinfo.h"
#include "alloc.h"
#include "jit-x64.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static void determine_next_block_addresses(const uint16* code, TempBlockInfo* temp, const OpcodeMappingInfo* map);

/* This function takes a block and a pointer to some m68k code and computes
 * the length of the block and various cc bit information about the block.
 * The info is stored in the block struct.
 */
void compute_block_info(Block* b, const uint16* code, TempBlockInfo* temp)
{
    const uint16 *start_code = code, *old_code;
    int clobbered = 0, may_set = 0, may_not_set = ALL_CCS, needed = 0;
    int next_array_size;
    const OpcodeMappingInfo* map = NULL;
#ifdef GENERATE_X64_CODE
    OpcodeMappingInfo fast_memory_map = opcode_map_info[opcode_map_index[0x4AFC]];
    fast_memory_map.ends_block = 0;
#endif

    /* Initialize the next offset array.  This lets us step through this
   * code forwards when we actually get around to compiling it.
   */
    temp->num_68k_instrs = 0;
    next_array_size = 16;
    temp->next_instr_offset = (int8*)xmalloc(next_array_size * sizeof(int8));

    /* Loop over all instructions in the block and determine information
   * about how this block deals with CC bits.
   */
    old_code = code;
    do {
        int insn_size;
        unsigned m68k_op;
        int checked_memory = 0;

        m68k_op = READUW(US_TO_SYN68K(code));
        map = &opcode_map_info[opcode_map_index[m68k_op]];
#ifdef GENERATE_X64_CODE
        if (syn68k_x64_enabled && syn68k_x64_memory_opcode(m68k_op)) {
            checked_memory = 1;
            unsigned words = syn68k_x64_fast_memory_opcode(m68k_op, US_TO_SYN68K(code));
            fast_memory_map.instruction_words = words;
            map = words ? &fast_memory_map : &opcode_map_info[opcode_map_index[0x4AFC]];
        }
#endif

#if 0
      if (opcode_map_index[m68k_op] == 0)
	{
	  fprintf (stderr, "m68kop 0x%04X unimplemented!\n", m68k_op);
	  abort ();
	}
#endif

        insn_size = instruction_size(code, map);
        if (insn_size <= 0) {
            map = &opcode_map_info[opcode_map_index[0x4AFC]]; /* illegal */
            /* Include examined extension words in the block's invalidation span. */
            insn_size = insn_size < 0 ? -insn_size : 1;
        }

        /* Update cc bit info for this block. */
        if (checked_memory) {
            /* A successful access may set flags; a fault observes all old
             flags. The ILLEGAL synthetic operand layout alone cannot express
             these live decoder semantics. */
            needed |= ALL_CCS & may_not_set;
            may_set |= ALL_CCS;
        }
        else {
            clobbered |= (map->cc_may_set & ~(map->cc_may_not_set | map->cc_needed | needed));
            needed |= map->cc_needed & may_not_set;
            may_set |= map->cc_may_set;
            may_not_set &= map->cc_may_not_set;
        }

        /* Grow next instruction offset array iff necessary.  We'll
       * leave one extra element for after this loop ends.
       */
        if (temp->num_68k_instrs >= next_array_size - 1) {
            next_array_size *= 2;
            temp->next_instr_offset = (int8*)xrealloc(temp->next_instr_offset, next_array_size * sizeof(int8));
        }

        /* Remember offset to next instruction. */
        temp->next_instr_offset[temp->num_68k_instrs++] = insn_size;

        /* Move on to the next instruction. */
        old_code = code;
        code += insn_size;
    } while (!map->ends_block);

    /* Terminate the array with a 0 offset. */
    temp->next_instr_offset[temp->num_68k_instrs] = 0;

    /* Figure out where this block goes (if possible). */
    determine_next_block_addresses(old_code, temp, map);

    /* Record the block information we've computed. */
    b->cc_clobbered = clobbered;
    b->cc_may_not_set = may_not_set;
    b->cc_needed = needed;
    b->m68k_start_address = US_TO_SYN68K(start_code);
    b->m68k_code_length = (code - start_code) * sizeof(uint16);
}

static void determine_next_block_addresses(const uint16* code, TempBlockInfo* temp, const OpcodeMappingInfo* map)
{
    uint16 m68kop = READUW(US_TO_SYN68K(code));

#ifdef DEBUG
    temp->child[0] = temp->child[1] = 0;
#endif

    /* First see if we can even tell where the child block is. */
    if (map->next_block_dynamic) {
        temp->num_child_blocks = 0;
        return;
    }

    /* Is it a Bcc? */
    if ((m68kop >> 12) == 6) {
        uint32 t1, t2;

        t1 = US_TO_SYN68K(code + 1);
        /* Compute branch target. */
        if ((m68kop & 0xFF) == 0) {
            t1 += READSW(US_TO_SYN68K(code + 1));
            t2 = US_TO_SYN68K(code + 2);
        }
        else if ((m68kop & 0xFF) == 0xFF) {
            t1 += READSL(US_TO_SYN68K(code + 1));
            t2 = US_TO_SYN68K(code + 3);
        }
        else {
            t1 += ((int8*)code)[1];
            t2 = US_TO_SYN68K(code + 1);
        }

        /* BRA has one destination; BSR already returned as dynamic. */
        if ((m68kop & 0xFE00) == 0x6000) {
            temp->child[0] = t1;
            temp->num_child_blocks = 1;
            return;
        }

        temp->child[0] = t2;
        temp->child[1] = t1;
        temp->num_child_blocks = 2;
        return;
    }

    /* Strange, unknown block ender.  Probably something capable of trapping.
   * Assume that the subsequent instruction is the target.
   */
    temp->child[0] = US_TO_SYN68K(code + map->instruction_words);
    temp->num_child_blocks = 1;
}

int syn68k_indexed_extension_valid(unsigned extension)
{
    return syn68k_indexed_extension_encoding_valid(extension);
}

int amode_size(int amode, const uint16* code, int ref_size)
{
    uint16 c = READUW(US_TO_SYN68K(code));

    switch (amode) {
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B: /* Addressing mode 5 */
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
        return 1;
    case 0x38: /* 111/000 (xxx).W */
        return 1;
    case 0x39: /* 111/001 (xxx).L */
        return 2;
    case 0x3C:                        /* 111/100 #<data> */
        return ref_size == 4 ? 2 : 1; /* Bytes still take 1 full word. */
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33: /* Addressing mode 6. */
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x3B: /* Addressing mode 111/011 */
        if (!syn68k_indexed_extension_valid(c))
            return -1;
        if (!(c & 0x100)) /* 8 bit displacement. */
            return 1;
        else if ((c & 0xF) == 0x0)
            return ((c >> 4) & 3);
        return ((c >> 4) & 3) + (c & 3) - 1;
    case 0x3A: /* 111/010 (d16,PC) */
        return 1;
    default:
        return 0;
    }
}

/* Given a pointer to an m68k opcode, returns the total number of words
 * that instruction occupies, including operands and memory taken by
 * complex addressing modes. For a reserved extension, returns the negative
 * number of words examined, so invalidation still covers that extension.
 */
int instruction_size(const uint16* code, const OpcodeMappingInfo* map)
{
    int size;
    int m68kop = READUW(US_TO_SYN68K(code));

    /* See if we have a conditional branch format operand instruction. */
    if ((m68kop >> 12) == 6 && !map->next_block_dynamic) {
        if ((m68kop & 0xFF) == 0)
            size = 2;
        else if ((m68kop & 0xFF) == 0xFF)
            size = 3;
        else
            size = 1;
    }
    else {
        size = map->instruction_words;
        /* If we have an addressing mode, compute how many words it takes. */
        if (map->amode_size != 0) {
            int words = amode_size(m68kop & 0x3F, code + size, map->amode_size);
            if (words < 0)
                return -(size + 1);
            size += words;
        }

        /* If we have a rev. addressing mode, compute how many words it takes. */
        if (map->reversed_amode_size != 0) /* Is there a rev. addressing mode? */
        {
            int words =
                amode_size(((m68kop >> 9) & 0x7) | ((m68kop >> 3) & 0x38), code + size, map->reversed_amode_size);
            if (words < 0)
                return -(size + 1);
            size += words;
        }
    }

    return size;
}
