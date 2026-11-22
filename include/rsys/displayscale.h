/*+===================================================================
  File:      displayscale.h

  Summary:   Host presentation scaling; guest screen geometry remains unchanged. ifndef RSYS_DISPLAYSCALE_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Host presentation scaling; guest screen geometry remains unchanged. */
#ifndef RSYS_DISPLAYSCALE_H
#define RSYS_DISPLAYSCALE_H

/* Decimal multiplier, optional x suffix, in hundredths (0.25x..16x).
   Parse without locale, floating-point rounding or unbounded accumulation. */
static inline int ROMlib_scale_parse(const char* text, int* percent)
{
    unsigned whole = 0, fraction = 0, digits = 0;
    const char* p = text;
    if (!p || *p < '0' || *p > '9')
        return 0;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 2)
            return 0;
        whole = whole * 10 + *p++ - '0';
    }
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9')
            return 0;
        fraction = (*p++ - '0') * 10;
        if (*p >= '0' && *p <= '9')
            fraction += *p++ - '0';
    }
    if (*p == 'x' || *p == 'X')
        ++p;
    unsigned value = whole * 100 + fraction;
    if (*p || value < 25 || value > 1600)
        return 0;
    *percent = value;
    return 1;
}

static inline int ROMlib_scale_edge(int coordinate, int percent)
{
    return (coordinate * percent + 99) / 100;
}

static inline int ROMlib_scale_guest(int coordinate, int percent)
{
    return coordinate * 100 / percent;
}
#endif
