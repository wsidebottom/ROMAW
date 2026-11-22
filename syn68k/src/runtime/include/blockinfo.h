/*+===================================================================
  File:      blockinfo.h

  Summary:   ifndef _blockinfo_h_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in syn68k/src/runtime/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#ifndef _blockinfo_h_
#define _blockinfo_h_

#include "block.h"

typedef struct {
    uint32 child[2];        /* m68k addresses of m68k code following this blk. */
    int16 num_child_blocks; /* # of child addrs we can know at translate time. */
    uint16 num_68k_instrs;
    int8* next_instr_offset; /* word offset to next instr; 0 iff last instr.  */
} TempBlockInfo;

extern void compute_block_info(Block* b, const uint16* code, TempBlockInfo* temp);
extern int amode_size(int amode, const uint16* code, int ref_size);
extern int instruction_size(const uint16* code, const OpcodeMappingInfo* map);
extern int syn68k_indexed_extension_valid(unsigned extension);

/* Share the exact encoding rule with the closed native EA specialization. */
static inline int syn68k_indexed_extension_encoding_valid(unsigned extension)
{
    /* M68000PRM, Figure 2-2 and Table 2-2. IS=1 reserves I/IS=100..111. */
    if (!(extension & 0x100))
        return 1;
    unsigned indirect = extension & 7;
    return !(extension & 8) && (extension & 0x30) && indirect != 4 && (!(extension & 0x40) || indirect < 4);
}

#endif /* Not _blockinfo_h_ */
