/*+===================================================================
  File:      cfx_charsets.c

  Summary:   Implements CoreFoundation charsets service entries for the PowerPC bridge.

  Classes:   None.

  Functions: ROMlib_cfx_CFCharacterSetCompact, ROMlib_cfx_CFCharacterSetFast

  Origin:    C reconstruction of the local CoreFoundationLib 1.4 disassembly; each entry verifies its reference offset.
             Public contracts use Universal Interfaces 3.4.2; private ABI notes are in cf-services-batch.json.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cfx_internal.h"

static unsigned service_charset_bit(const Byte* bitmap, unsigned character)
{
    return !!(bitmap[character >> 3] & (1U << (character & 7)));
}

static int service_charset_summary(const ROMlib_carbon_reference* reference, uint32 compact, unsigned character)
{
    uint32 slot = cfx_address(compact, 4 + ((character >> 15) * 4), 4);
    uint32 mask = cfp_read(cfx_address(US_TO_SYN68K(reference->toc), 0x3964 + ((character >> 8) & 0x7c), 4));
    return cfp_write(slot, cfp_read(slot) | mask);
}

int ROMlib_cfx_CFCharacterSetCompact(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFCharacterSetCompact", 0x35094, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0), flags = cfp_read(cfx_address(object, 4, 4));
    if (!ROMlib_carbon_guest_memory(cfx_address(object, 4, 12), 12, 1))
        return 0;
    uint32 bitmap_address = cfp_read(cfx_address(object, 12, 4));
    if ((flags & 0x70) != 0x30 || !bitmap_address)
        return 1;
    Byte* snapshot = malloc(8192);
    if (!snapshot)
        return cfp_failure("Native Carbon character-set snapshot allocation failed");
    const Byte* input = ROMlib_carbon_guest_memory(bitmap_address, 8192, 0);
    if (!input) {
        free(snapshot);
        return 0;
    }
    memcpy(snapshot, input, 8192);
    unsigned singles = 0, ranges = 0;
    for (unsigned character = 0; character < 65536; ++character) {
        if (!service_charset_bit(snapshot, character))
            continue;
        unsigned start = character;
        while (character + 1 < 65536 && service_charset_bit(snapshot, character + 1))
            ++character;
        if (start == character)
            ++singles;
        else
            ++ranges;
    }
    uint32 size = 40 + singles * 4 + ranges * 8;
    if (size > 8192) {
        free(snapshot);
        return 1;
    }
    uint32 allocator = CFW_CALL("CFGetAllocator", object);
    uint32 compact = CFW_CALL("CFAllocatorAllocate", allocator, size, 0);
    if (!compact) {
        free(snapshot);
        return cfp_failure("Native Carbon compact character-set allocation failed");
    }
    unsigned success = cfp_clear(compact, size) && cfp_write(cfx_address(compact, 28, 4), 0x1ffff);
    uint32 single_offset = 36, range_offset = 36 + singles * 4;
    for (unsigned character = 0; success && character < 65536; ++character) {
        if (!service_charset_bit(snapshot, character))
            continue;
        unsigned start = character;
        while (character + 1 < 65536 && service_charset_bit(snapshot, character + 1))
            ++character;
        if (start == character) {
            success = cfp_write(cfx_address(compact, single_offset, 4), start);
            single_offset += 4;
            if (start < cfp_read(cfx_address(compact, 28, 4)))
                success = success && cfp_write(cfx_address(compact, 28, 4), start);
            if (start > cfp_read(cfx_address(compact, 32, 4)))
                success = success && cfp_write(cfx_address(compact, 32, 4), start);
        }
        else {
            success = cfp_write(cfx_address(compact, range_offset, 4), start) &&
                      cfp_write(cfx_address(compact, range_offset + 4, 4), character);
            range_offset += 8;
        }
        for (unsigned member = start; success && member <= character; ++member)
            success = service_charset_summary(&reference, compact, member);
    }
    free(snapshot);
    success = success && cfp_write(compact, 1) && cfp_write(cfx_address(compact, 20, 4), singles) &&
              cfp_write(cfx_address(compact, 24, 4), ranges);
    if (!success) {
        CFW_CALL("CFAllocatorDeallocate", allocator, compact);
        return 0;
    }
    if (!cfp_write(cfx_address(object, 12, 4), compact) ||
        !cfp_write(cfx_address(object, 4, 4), (flags & ~0x70U) | 0x40)) {
        CFW_CALL("CFAllocatorDeallocate", allocator, compact);
        return 0;
    }
    CFW_CALL("CFAllocatorDeallocate", allocator, bitmap_address);
    return 1;
}

int ROMlib_cfx_CFCharacterSetFast(ROMlib_carbon_ppc_frame* call_frame)
{
    ROMlib_carbon_reference reference;
    int disposition = cfx_reference("CFCharacterSetFast", 0x352e8, call_frame, &reference);
    if (disposition != 1)
        return disposition == 2;
    uint32 object = CFX_ARG(0), flags = cfp_read(cfx_address(object, 4, 4));
    if (!ROMlib_carbon_guest_memory(cfx_address(object, 4, 12), 12, 1))
        return 0;
    uint32 old = cfp_read(cfx_address(object, 12, 4));
    if ((flags & 0x70) != 0x40 || !old)
        return 1;
    uint32 allocator = CFW_CALL("CFGetAllocator", object);
    uint32 bitmap = CFW_CALL("CFAllocatorAllocate", allocator, 8192, 0);
    if (!bitmap)
        return cfp_failure("Native Carbon fast character-set allocation failed");
    uint32 arguments[] = {object, bitmap}, ignored = 0;
    if (!cfp_private(&reference, 0x2fbf4, arguments, 2, &ignored)) {
        CFW_CALL("CFAllocatorDeallocate", allocator, bitmap);
        return 0;
    }
    /* The conversion incorporates inversion into the materialized bitmap. */
    flags = cfp_read(cfx_address(object, 4, 4));
    old = cfp_read(cfx_address(object, 12, 4));
    uint32 kind = flags & 0x70;
    if (!cfp_write(cfx_address(object, 12, 4), bitmap) ||
        !cfp_write(cfx_address(object, 4, 4), ((flags & ~0x70U) | 0x30) & ~8U)) {
        CFW_CALL("CFAllocatorDeallocate", allocator, bitmap);
        return 0;
    }
    if (old && (kind == 0x20 || kind == 0x40))
        CFW_CALL("CFAllocatorDeallocate", allocator, old);
    return 1;
}
