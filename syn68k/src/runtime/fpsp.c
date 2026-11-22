/*+===================================================================
  File:      fpsp.c

  Summary:   Implements fpsp support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: syn68k_fpsp_install, syn68k_fpsp_size

  Origin:    Existing repository source in syn68k/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Bounded installation of Motorola's original guest software package. */
#include "syn68k_private.h"
#include "generated/fpsp-image.h"

uint32 syn68k_fpsp_size(void)
{
    return sizeof fpsp_image;
}

int syn68k_fpsp_install(syn68k_addr_t base)
{
    static const struct {
        unsigned vector, offset;
    } handlers[] = {{11, FPSP_FLINE}, {48, FPSP_BSUN}, {51, FPSP_UNFL},  {52, FPSP_OPERR},
                    {53, FPSP_OVFL},  {54, FPSP_SNAN}, {55, FPSP_UNSUPP}},
      forwards[] = {{14, FPSP_FMT_ERROR}, {48, FPSP_REAL_BSUN},  {11, FPSP_REAL_FLINE},
                    {49, FPSP_REAL_INEX}, {52, FPSP_REAL_OPERR}, {53, FPSP_REAL_OVFL},
                    {54, FPSP_REAL_SNAN}, {9, FPSP_REAL_TRACE},  {51, FPSP_REAL_UNFL}};

    uint32 previous[64];
    uint64 end = (uint64)base + sizeof fpsp_image;
    uint64 vector_end = (uint64)cpu_state.vbr + sizeof previous;
    if ((base & 1) || end > (uint64)syn68k_address_mask + 1 || vector_end > (uint64)syn68k_address_mask + 1 ||
        (base < vector_end && cpu_state.vbr < end) || !syn68k_memory_span(base, sizeof fpsp_image) ||
        !syn68k_memory_span(cpu_state.vbr, sizeof previous))
        return 0;
    for (unsigned i = 0; i < 64; ++i) {
        previous[i] = READUL(cpu_state.vbr + i * 4);
        /* Installing over an active copy would overwrite its forwarding
         targets and can turn an unhandled exception into a recursion. */
        if (previous[i] >= base && previous[i] < end)
            return 0;
    }
    destroy_blocks(base, sizeof fpsp_image);
    for (unsigned i = 0; i < sizeof fpsp_image; ++i)
        WRITEUB(base + i, fpsp_image[i]);
    for (unsigned i = 0; i < sizeof fpsp_relocations / sizeof *fpsp_relocations; ++i) {
        uint32 address = base + fpsp_relocations[i];
        WRITEUL(address, READUL(address) + base - 0x10000);
    }
    for (unsigned i = 0; i < sizeof forwards / sizeof *forwards; ++i)
        WRITEUL(base + forwards[i].offset + 2, previous[forwards[i].vector]);
    /* Publish vectors only after the entire executable image is ready. */
    for (unsigned i = 0; i < sizeof handlers / sizeof *handlers; ++i)
        WRITEUL(cpu_state.vbr + handlers[i].vector * 4, base + handlers[i].offset);
    return 1;
}
