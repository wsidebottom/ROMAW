/*+===================================================================
  File:      floatconv.h

  Summary:   Declares floatconv support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_floatconv_h_)
#define _floatconv_h_

/* This file contains static inline routines to convert between ieee_t's and
 * SANE representations for various numbers.  The exact type of conversion
 * should be obvious from the function name.
 *
 * If these functions are not inlined, and ieee_t's are converted to 8 byte
 * doubles, precision will be lost.  Therefore all of these are defined
 * static inline.
 *
 * This file contains:
 *
 *     x80_to_ieee           ieee_to_x80
 *     f64_to_ieee           ieee_to_f64
 *     f32_to_ieee           ieee_to_f32
 *     comp_to_ieee          ieee_to_comp
 *
 * These routines will only work on machines that support IEEE floating point.
 *
 */

#include <string.h>
#include "rsys/float.h"

EXTERN_INLINE ieee_t x80_to_ieee(const x80_t* x) ALWAYS_INLINE;

EXTERN_INLINE ieee_t x80_to_ieee(const x80_t* x)
{
    register ieee_t retval;

    volatile x87_x80_t temp80;

    /* Swap x's byte order. */
    temp80.man_lo = CL(x->man.hilo.man_lo);
    temp80.man_hi = CL(x->man.hilo.man_hi);
    temp80.sgn_and_exp = CW(x->se.sgn_and_exp);

    /* If it's an Inf or NaN, be sure to set the "normalized" bit.
   * 80387 requires this bit to be set (else Inf's become NaN's)
   * but SANE doesn't care about it either way.
   */
    if ((temp80.sgn_and_exp & 0x7FFF) == 0x7FFF)
        temp80.man_hi |= 0x80000000;

    /* Load the 10 byte value into our local variable. */
    asm("fldt %1" : "=t"(retval) : "m"(*&temp80));

    return retval;
}

EXTERN_INLINE ieee_t f64_to_ieee(const f64_t* f) ALWAYS_INLINE;

EXTERN_INLINE ieee_t f64_to_ieee(const f64_t* f)
{
#if defined(QUADALIGN)
#ifdef LITTLEENDIAN
    volatile f64_t temp1 __attribute__((aligned(8)));
    volatile native_f64_t temp2 __attribute__((aligned(8)));
    memcpy(&temp1, f, sizeof temp1);
    temp2.hilo.hi = CL(temp1.hilo.lo);
    temp2.hilo.lo = CL(temp1.hilo.hi);
    return *(double*)&temp2;
#else  /* !LITTLEENDIAN */
    double d;
    memcpy(&d, f, sizeof d);
    return d;
#endif /* !LITTLEENDIAN */
#else  /* !QUADALIGN */
#ifdef LITTLEENDIAN
    volatile native_f64_t temp;
    temp.hilo.hi = CL(f->hilo.lo);
    temp.hilo.lo = CL(f->hilo.hi);
    return temp.d;
#else  /* !LITTLEENDIAN */
    return f->d;
#endif /* !LITTLEENDIAN */
#endif /* !QUADALIGN */
}

EXTERN_INLINE ieee_t f32_to_ieee(const f32_t* f) ALWAYS_INLINE;

EXTERN_INLINE ieee_t f32_to_ieee(const f32_t* f)
{
#if !defined(LITTLEENDIAN) && !defined(QUADALIGN)
    return *(float*)f;
#else
    volatile native_f32_t temp;
    temp.n = CL(*(uint32*)f); /* This byte swaps on LITTLEENDIAN machines. */
    return temp.f;
#endif /* !QUADALIGN */
}

EXTERN_INLINE ieee_t comp_to_ieee(const comp_t* cp) ALWAYS_INLINE;

EXTERN_INLINE ieee_t comp_to_ieee(const comp_t* cp)
{
    register ieee_t retval;
    native_comp_t c;

#if defined(LITTLEENDIAN)
    c.hilo.hi = CL(*(uint32*)(&cp->hilo.hi));
    c.hilo.lo = CL(*(uint32*)(&cp->hilo.lo));
#else /* Not LITTLEENDIAN */
#ifndef QUADALIGN
    c = *cp;
#else  /* QUADALIGN */
    memcpy(&c, cp, sizeof c);
#endif /* QUADALIGN */
#endif /* Not LITTLEENDIAN */

    if (c.val == 0) /* zero maps to zero. */
        retval = 0.0;
    else if (NATIVE_COMP_IS_NAN(c)) /* NaN maps to special NaN value. */
    {
        /* Magical value for SANE NaN derived from comp NaN. */
        static const unsigned char f64_nan_comp[]
            __attribute__((aligned(2))) = {0x7F, 0xFF, 0x40, 0x00, 0x00, 0x00, 0x00, 0x14};
        retval = f64_to_ieee((const f64_t*)&f64_nan_comp);
    }
    else {
        retval = c.val;
    }

    return retval;
}

EXTERN_INLINE void ieee_to_x80(ieee_t n, x80_t* x) ALWAYS_INLINE;

EXTERN_INLINE void ieee_to_x80(ieee_t n, x80_t* x)
{
    volatile x87_x80_t temp80;

    /* Move the 80 bit representation to memory. */
    asm("fstpt %0\n\t"
        "fwait"
        : "=m"(*&temp80)
        : "t"(n)
        : "st"); /* FIXME - st??? */

    /* Byte swap the memory representation and store it in x. */
    x->se.sgn_and_exp = CW(temp80.sgn_and_exp);
    x->man.hilo.man_lo = CL(temp80.man_lo);
    x->man.hilo.man_hi = CL(temp80.man_hi);
}

EXTERN_INLINE void ieee_to_f64(ieee_t val, f64_t* dest) ALWAYS_INLINE;

EXTERN_INLINE void ieee_to_f64(ieee_t val, f64_t* dest)
{
#if !defined(LITTLEENDIAN)
#if !defined(QUADALIGN)
    dest->d = (double)val;
#else  /* QUADALIGN */
    volatile f64_t temp;
    temp.d = (double)val;
    *(uint32*)&dest->hi = CL(temp.hilo.hi);
    *(uint32*)&dest->lo = CL(temp.hilo.lo);
#endif /* QUADALIGN */
#else  /* LITTLEENDIAN */
    volatile f64_t temp;
    temp.d = (double)val;
    *(uint32*)&dest->hilo.lo = CL(temp.hilo.hi);
    *(uint32*)&dest->hilo.hi = CL(temp.hilo.lo);
#endif /* LITTLEENDIAN */
}

EXTERN_INLINE void ieee_to_f32(ieee_t val, f32_t* dest) ALWAYS_INLINE;

EXTERN_INLINE void ieee_to_f32(ieee_t val, f32_t* dest)
{
#if !defined(QUADALIGN)
    dest->f = (float)val;
#ifdef LITTLEENDIAN
    dest->n = CL(dest->n);
#endif
#else /* QUADALIGN */
    volatile f32_t temp;
    temp.f = (float)val;
    *(uint32*)dest = CL(temp.n);
#endif
}

EXTERN_INLINE void ieee_to_comp(ieee_t val, comp_t* dest) ALWAYS_INLINE;

EXTERN_INLINE void ieee_to_comp(ieee_t val, comp_t* dest)
{
    /* FIXME - does not handle range errors! */

    /* Check to see if val is NaN. */
    if (val != val) {
        *(uint32*)&dest->hilo.hi = CL(0x80000000); /* NaN high 32 bits. */
        *(uint32*)&dest->hilo.lo = CL(0x00000000); /* NaN low 32 bits. */
    }
    else /* Not NaN */
    {
#if !defined(LITTLEENDIAN) && !defined(QUADALIGN)
        dest->val = val; /* Let C compiler cast the ieee_t to a LONGINT LONGINT. */
#else
        volatile native_comp_t temp;

        temp.val = val; /* Let C compiler cast the ieee_t to a LONGINT LONGINT. */

        /* Write the value out. */
        *(uint32*)&dest->hilo.hi = CL(temp.hilo.hi);
        *(uint32*)&dest->hilo.lo = CL(temp.hilo.lo);
#endif
    }
}

#endif
