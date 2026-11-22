/*+===================================================================
  File:      itl2.h

  Summary:   System 7 international sorting ABI, from Script.a/RomanITL2.a and the InternationalPACK descriptions in the
             user's SuperMario source reference. ifndef RSYS_ITL2_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* System 7 international sorting ABI, from Script.a/RomanITL2.a and the
   InternationalPACK descriptions in the user's SuperMario source reference. */
#ifndef RSYS_ITL2_H
#define RSYS_ITL2_H
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t current, mapped, decision;
    uint8_t buffered, just_after, ignored, no_fetch;
    uint16_t remaining;
    uint32_t text;
} romaw_itl2_string;

typedef struct __attribute__((packed)) {
    uint32_t resource, weak_b, weak_a;
    uint8_t scratch[18], weak_magnitude, lock, weak_equal, want_magnitude;
    romaw_itl2_string b, a;
    uint32_t old_a6, return_address;
    uint16_t b_length, a_length;
    uint32_t b_text, a_text;
    int16_t result;
} romaw_itl2_frame;

_Static_assert(sizeof(romaw_itl2_string) == 16, "IUStrData ABI");
_Static_assert(sizeof(romaw_itl2_frame) == 88, "IUNSortFrame ABI");
_Static_assert(offsetof(romaw_itl2_frame, old_a6) == 66, "A6 frame offset");
_Static_assert(offsetof(romaw_itl2_frame, a) == 50, "AInfo at A6-16");
_Static_assert(offsetof(romaw_itl2_frame, result) == 86, "result at A6+20");

static int itl2_vectors(const unsigned char* data, size_t size, uint16_t offsets[5])
{
    const unsigned fields[] = {0, 2, 4, 6, 10};
    uint16_t checked[5];
    if (!data || size < 12)
        return 0;
    for (unsigned i = 0; i < 5; ++i) {
        unsigned n = (unsigned)data[fields[i]] * 256 + data[fields[i] + 1];
        if (n < 12 || n >= 32768 || (n & 1) || n > size - 2)
            return 0;
        checked[i] = n;
    }
    for (unsigned i = 0; i < 5; ++i)
        offsets[i] = checked[i];
    return 1;
}

static unsigned itl2_projection(unsigned c)
{
    static const unsigned char latin[32] = {'A', 'A', 'C', 'E', 'N', 'O', 'U', 'A', 'A', 'A', 'A',
                                            'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
                                            'N', 'O', 'O', 'O', 'O', 'O', 'U', 'U', 'U', 'U'};
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    if (c >= 0x80 && c <= 0x9f)
        return latin[c - 0x80];
    switch (c) {
    case 0xaf:
    case 0xbf:
    case 0xcd:
    case 0xee:
    case 0xef:
    case 0xf1:
        return 'O';
    case 0xc7:
    case 0xc8:
    case 0xd2:
    case 0xd3:
        return '"';
    case 0xd4:
    case 0xd5:
        return '\'';
    case 0xca:
        return ' ';
    case 0xcb:
    case 0xcc:
    case 0xe5:
    case 0xe7:
        return 'A';
    case 0xd8:
    case 0xd9:
        return 'Y';
    case 0xe6:
    case 0xe8:
    case 0xe9:
        return 'E';
    case 0xea:
    case 0xeb:
    case 0xec:
    case 0xed:
    case 0xf5:
        return 'I';
    case 0xf2:
    case 0xf3:
    case 0xf4:
        return 'U';
    default:
        return c;
    }
}

static unsigned itl2_vernier(unsigned high, unsigned low)
{
    static const uint16_t substitutions[][2] = {
        {0xaf, 0x8700}, {0xcb, 0x7e00}, {0xcc, 0x8001}, {0xcd, 0x8600}, {0x83, 0x7000}, {0xe5, 0x7f00}, {0xe6, 0x7200},
        {0xe7, 0x7d00}, {0xe8, 0x7300}, {0xe9, 0x7100}, {0xea, 0x8000}, {0xeb, 0x8200}, {0xec, 0x8300}, {0xed, 0x8100},
        {0xee, 0x8000}, {0xef, 0x8200}, {0xf1, 0x8100}, {0xf2, 0x8000}, {0xf3, 0x8200}, {0xf4, 0x8100}, {0xf5, 0x8b00}};
    if (!low) {
        if (high >= 'a' && high <= 'z')
            high += 0x22;
        else
            for (unsigned i = 0; i < sizeof substitutions / sizeof substitutions[0]; ++i)
                if (substitutions[i][0] == high)
                    return substitutions[i][1];
    }
    return high * 256 + low;
}
#endif
