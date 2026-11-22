/*+===================================================================
  File:      ppc_calls.c

  Summary:   Transport full PPC argument frames, read checked stack words and preserve date-getter fallback.

  Classes:   None.

  Functions: ROMlib_carbon_cf_date_frame, ROMlib_carbon_dispatch_frame, ROMlib_carbon_dispatch_mapped_frame,
             ROMlib_carbon_frame_word

  Origin:    Original Executor adapter using the existing classic PPC parameter-area layout.
             Date getter reconstructed from CoreFoundationLib 1.4 offset 0x23474.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "carbonlib.h"
#include <string.h>

_Static_assert(sizeof(double) == sizeof(uint64_t), "PPC floating register slots require 64-bit doubles");
_Static_assert(offsetof(ROMlib_carbon_ppc_frame, floating) == 32, "Carbon PPC frame floating register layout");
_Static_assert(offsetof(ROMlib_carbon_ppc_frame, stack) == 136, "Carbon PPC frame stack layout");

int ROMlib_carbon_dispatch_mapped_frame(ROMlib_carbon_frame_entry entry, uint32_t words[8], void* floating,
                                        uint32_t stack, uint32_t* fpscr, ROMlib_carbon_guest_words_fn guest_words,
                                        void* guest_opaque)
{
    if (!entry || !words || !floating || !fpscr)
        return ROMlib_carbon_report_failure("Invalid native Carbon PPC frame dispatch");
    ROMlib_carbon_ppc_frame call_frame;
    memcpy(call_frame.words, words, sizeof call_frame.words);
    memcpy(call_frame.floating, floating, sizeof call_frame.floating);
    call_frame.stack = stack;
    call_frame.fpscr = *fpscr;
    call_frame.guest_words = guest_words;
    call_frame.guest_opaque = guest_opaque;
    if (!entry(&call_frame))
        return 0;
    if (call_frame.stack != stack)
        return ROMlib_carbon_report_failure("Native Carbon PPC entry changed its caller stack");
    memcpy(words, call_frame.words, sizeof call_frame.words);
    memcpy(floating, call_frame.floating, sizeof call_frame.floating);
    *fpscr = call_frame.fpscr;
    return 1;
}

int ROMlib_carbon_dispatch_frame(ROMlib_carbon_frame_entry entry, uint32_t words[8], void* floating, uint32_t stack,
                                 uint32_t* fpscr)
{
    return ROMlib_carbon_dispatch_mapped_frame(entry, words, floating, stack, fpscr, NULL, NULL);
}

int ROMlib_carbon_frame_word(const ROMlib_carbon_ppc_frame* call_frame, unsigned index, uint32_t* result)
{
    if (!call_frame || !result)
        return ROMlib_carbon_report_failure("Invalid native Carbon PPC argument output");
    if (index < 8) {
        *result = call_frame->words[index];
        return 1;
    }
    uint64_t address = (uint64_t)call_frame->stack + 24 + (uint64_t)index * 4;
    if (address > UINT32_MAX - 3)
        return ROMlib_carbon_report_failure("Native Carbon PPC argument address overflow");
    const unsigned char* bytes = ROMlib_carbon_guest_memory((uint32_t)address, 4, 0);
    if (!bytes)
        return 0;
    *result = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    return 1;
}

int ROMlib_carbon_cf_date_frame(ROMlib_carbon_ppc_frame* call_frame)
{
    if (!call_frame)
        return ROMlib_carbon_report_failure("Invalid native Carbon date call frame");
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFDateGetAbsoluteTime", &reference))
        return 0;
    /* CoreFoundationLib 1.4, getter at 0x23474: lfd f1,8(r3); blr.
       Preserve the saved implementation for every other object layout/ABI. */
    if (reference.code_offset != 0x23474 || reference.code_size != 0x5dee0 || reference.toc_offset || !reference.toc ||
        reference.toc_size != 0x72a8)
        return ROMlib_carbon_invoke_frame(reference.function, call_frame);
    /* Keep the established native getter's null result. Non-null guest object
       extents are validated before copying raw bits, including NaN payloads. */
    if (!call_frame->words[0]) {
        call_frame->floating[0] = 0;
        return 1;
    }
    uint64_t address = (uint64_t)call_frame->words[0] + 8;
    if (address > UINT32_MAX - 7)
        return ROMlib_carbon_report_failure("Native Carbon date payload address overflow");
    const unsigned char* bytes = ROMlib_carbon_guest_memory((uint32_t)address, 8, 0);
    if (!bytes)
        return 0;
    uint64_t bits = 0;
    for (unsigned i = 0; i < 8; ++i)
        bits = (bits << 8) | bytes[i];
    call_frame->floating[0] = bits;
    return 1;
}
