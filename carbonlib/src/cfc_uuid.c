/*+===================================================================
  File:      cfc_uuid.c

  Summary:   Native UUID entry control flow, text conversion and packed results; original interning and entropy workers
             remain.

  Classes:   None.

  Functions: ROMlib_cfc_CFUUIDCreate, ROMlib_cfc_CFUUIDCreateFromString, ROMlib_cfc_CFUUIDCreateFromUUIDBytes,
             ROMlib_cfc_CFUUIDCreateString, ROMlib_cfc_CFUUIDGetTypeID, ROMlib_cfc_CFUUIDGetUUIDBytes

  Origin:    C reconstruction of CoreFoundationLib 1.4 offsets 0x4b67c..0x4bfec and Universal Interfaces 3.4.2 CFUUID
             declarations.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "cfc_cohort.h"

static int uuid_nibble(unsigned character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static uint32 uuid_intern(const ROMlib_carbon_reference* reference, uint32 allocator, const uint32 words[4])
{
    uint32 arguments[] = {allocator, CL(words[0]), CL(words[1]), CL(words[2]), CL(words[3]), 0}, result = 0;
    cfp_private(reference, 0x4b684, arguments, 6, &result);
    return result;
}

uint32 ROMlib_cfc_CFUUIDGetTypeID(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (cfp_reference("CFUUIDGetTypeID", 0x4b67c, NULL, 0, &reference, &result))
        result = cfp_toc(&reference, 0x9f0);
    return result;
}

uint32 ROMlib_cfc_CFUUIDCreate(uint32 allocator)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (!cfp_reference("CFUUIDCreate", 0x4b74c, &allocator, 1, &reference, &result))
        return result;
    _Alignas(16) uint32 bytes[4] = {0};
    uint32 output = US_TO_SYN68K(bytes), status = 0;
    if (!cfp_private(&reference, 0x4c28c, &output, 1, &status) || status)
        return 0;
    return uuid_intern(&reference, allocator, bytes);
}

uint32 ROMlib_cfc_CFUUIDCreateFromUUIDBytes(uint32 allocator, uint32 first, uint32 second, uint32 third, uint32 fourth)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, first, second, third, fourth}, result = 0;
    if (!cfp_reference("CFUUIDCreateFromUUIDBytes", 0x4bfac, arguments, 5, &reference, &result))
        return result;
    uint32 bytes[] = {CL(first), CL(second), CL(third), CL(fourth)};
    return uuid_intern(&reference, allocator, bytes);
}

uint32 ROMlib_cfc_CFUUIDGetUUIDBytes(uint32 output, uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {output, object}, result = 0;
    if (!cfp_reference("CFUUIDGetUUIDBytes", 0x4bf88, arguments, 2, &reference, &result))
        return result;
    /* Preserve the two ordered eight-byte copies, including overlapping outputs. */
    for (unsigned i = 0; i < 2; ++i) {
        uint32 first, second;
        if (!cfp_get(object + 8 + i * 8, &first) || !cfp_get(object + 12 + i * 8, &second) ||
            !cfp_write(output + i * 8, first) || !cfp_write(output + 4 + i * 8, second))
            return 0;
    }
    return output;
}

uint32 ROMlib_cfc_CFUUIDCreateFromString(uint32 allocator, uint32 string)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, string}, result = 0;
    if (!cfp_reference("CFUUIDCreateFromString", 0x4b954, arguments, 2, &reference, &result))
        return result;
    if (!string)
        return 0;
    uint32 length = MIN(cfc_public("CFStringGetLength", &string, 1), 100);
    if (!length)
        return 0;
    _Alignas(256) uint16 characters[100];
    uint32 read[] = {string, 0, length, US_TO_SYN68K(characters)};
    if (!cfp_call("CFStringGetCharacters", read, 4, NULL))
        return 0;
    uint32 position = 0;
    while (position < length && uuid_nibble(CW(characters[position])) < 0)
        ++position;

    union {
        uint32 words[4];
        Byte bytes[16];
    } uuid = {{0}};

    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            ++position;
        /* 1.4 leaves missing bytes uninitialized; reject that unsupported input. */
        if (position >= length || length - position < 2)
            return 0;
        int high = uuid_nibble(CW(characters[position])), low = uuid_nibble(CW(characters[position + 1]));
        uuid.bytes[i] = high < 0 || low < 0 ? 0 : (high << 4) | low;
        position += 2;
    }
    return uuid_intern(&reference, allocator, uuid.words);
}

uint32 ROMlib_cfc_CFUUIDCreateString(uint32 allocator, uint32 object)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, object}, result = 0;
    if (!cfp_reference("CFUUIDCreateString", 0x4bd24, arguments, 2, &reference, &result))
        return result;
    uint32 create[] = {allocator, 0};
    result = cfc_public("CFStringCreateMutable", create, 2);
    if (!result)
        return 0;
    static const char digits[] = "0123456789ABCDEF";
    _Alignas(128) uint16 text[36];
    unsigned position = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const Byte* byte = ROMlib_carbon_guest_memory(object + 8 + i, 1, 0);
        if (!byte)
            return 0;
        text[position++] = CW((uint16)digits[*byte >> 4]);
        text[position++] = CW((uint16)digits[*byte & 15]);
        if (i == 3 || i == 5 || i == 7 || i == 9)
            text[position++] = CW('-');
    }
    static const unsigned lengths[] = {9, 5, 5, 5, 12};
    position = 0;
    for (unsigned i = 0; i < NELEM(lengths); ++i) {
        uint32 append[] = {result, US_TO_SYN68K(text + position), lengths[i]};
        if (!cfp_call("CFStringAppendCharacters", append, 3, NULL))
            return 0;
        position += lengths[i];
    }
    return result;
}
