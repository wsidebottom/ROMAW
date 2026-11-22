/*+===================================================================
  File:      emutrap.c

  Summary:   Implements emutrap support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: CToPascalCall, PascalToCCall, ROMlib_CToPascalCall64

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include <stdarg.h>
#include "rsys/trapglue.h"
#include "rsys/mixed_mode.h"

/*
 * Scalar Toolbox arguments occupy integer calling-convention slots. Keep
 * host slots pointer-sized while Macintosh stack values stay 32 bits.
 * Floating-point and wider aggregate arguments need separate conventions.
 */

#define SYN68K_TO_US_CHECK0_CHECKNEG1(addr)                             \
    ({                                                                  \
        typeof(addr) __t;                                               \
                                                                        \
        __t = addr;                                                     \
        ((intptr_t)__t == -1) ? (uint16*)-1 : SYN68K_TO_US_CHECK0(__t); \
    })

#define US_TO_SYN68K_CHECK0_CHECKNEG1(addr)                             \
    ({                                                                  \
        typeof(addr) __t;                                               \
                                                                        \
        __t = addr;                                                     \
        ((intptr_t)__t == -1) ? (int32) - 1 : US_TO_SYN68K_CHECK0(__t); \
    })

PUBLIC syn68k_addr_t PascalToCCall(syn68k_addr_t ignoreme, ptocblock_t* infop)
{
    unsigned short pth, ptv;
    intptr_t args[11], retval;
    unsigned long long magic;
    int count, rettype;
    void* funcp;
    int sizeflag;
    typedef intptr_t (*func0argsp_t)(void);
    typedef intptr_t (*func1argsp_t)(intptr_t);
    typedef intptr_t (*func2argsp_t)(intptr_t, intptr_t);
    typedef intptr_t (*func3argsp_t)(intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func4argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func5argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func6argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func7argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func8argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
    typedef intptr_t (*func9argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                                     intptr_t);
    typedef intptr_t (*func10argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                                      intptr_t, intptr_t);
    typedef intptr_t (*func11argsp_t)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                                      intptr_t, intptr_t, intptr_t);
    syn68k_addr_t retaddr;

    count = 0;

    retaddr = POPADDR();
    switch (infop->magic) {
    case PTOC_A10_MAGIC:
        magic = PTOC_SPECIAL_A10_VALUE;
        break;
    case PTOC_A11_MAGIC:
        magic = PTOC_SPECIAL_A11_VALUE;
        break;
    default:
        magic = infop->magic;
        break;
    }

    rettype = magic & 7;
    magic >>= 3;
    while ((sizeflag = magic & 7)) {
        switch (sizeflag) {
        case 1:
            args[count++] = POPSB();
            break;
        case 2:
            args[count++] = POPSW();
            break;
        case 3:
            ptv = POPSW();
            pth = POPSW();
#if !defined(LITTLEENDIAN)
            args[count++] = (ptv << 16) | pth;
#else  /* defined(LITTLEENDIAN) */
            args[count++] = (pth << 16) | ptv;
#endif /* defined(LITTLEENDIAN) */
            break;
        case 4:
            args[count++] = POPSL();
            break;
        case 5:
            args[count++] = (intptr_t)SYN68K_TO_US_CHECK0_CHECKNEG1(POPSL());
            break;
        }
        magic >>= 3;
    }

    funcp = infop->wheretogo;
    switch (count) {
    case 0:
        retval = (*(func0argsp_t)funcp)();
        break;
    case 1:
        retval = (*(func1argsp_t)funcp)(args[0]);
        break;
    case 2:
        retval = (*(func2argsp_t)funcp)(args[1], args[0]);
        break;
    case 3:
        retval = (*(func3argsp_t)funcp)(args[2], args[1], args[0]);
        break;
    case 4:
        retval = (*(func4argsp_t)funcp)(args[3], args[2], args[1], args[0]);
        break;
    case 5:
        retval = (*(func5argsp_t)funcp)(args[4], args[3], args[2], args[1], args[0]);
        break;
    case 6:
        retval = (*(func6argsp_t)funcp)(args[5], args[4], args[3], args[2], args[1], args[0]);
        break;
    case 7:
        retval = (*(func7argsp_t)funcp)(args[6], args[5], args[4], args[3], args[2], args[1], args[0]);
        break;
    case 8:
        retval = (*(func8argsp_t)funcp)(args[7], args[6], args[5], args[4], args[3], args[2], args[1], args[0]);
        break;
    case 9:
        retval =
            (*(func9argsp_t)funcp)(args[8], args[7], args[6], args[5], args[4], args[3], args[2], args[1], args[0]);
        break;
    case 10:
        retval = (*(func10argsp_t)funcp)(args[9], args[8], args[7], args[6], args[5], args[4], args[3], args[2],
                                         args[1], args[0]);
        break;
    case 11:
        retval = (*(func11argsp_t)funcp)(args[10], args[9], args[8], args[7], args[6], args[5], args[4], args[3],
                                         args[2], args[1], args[0]);
        break;
#if !defined(LETGCCWAIL)
    default:
        retval = 0;
#endif
    }

    switch (rettype) {
    case 1:
        WRITEUW(EM_A7, 0); /* needed for WordPerfect */
        WRITEUB(EM_A7, retval);
        break;
    case 2:
        WRITEUW(EM_A7, retval);
        break;
    case 4:
        WRITEUL(EM_A7, retval);
        break;
    case 5:
        WRITEUL(EM_A7, US_TO_SYN68K_CHECK0_CHECKNEG1(retval));
        break;
    }
    return retaddr;
}

PRIVATE intptr_t CToPascalCall_m68k(void* wheretogo, unsigned long long magic, va_list ap)
{
    intptr_t retval;
    ULONGINT ul;
    int retvaltype;
    M68kReg saveregs[14];

    memcpy(saveregs, &EM_D1, sizeof(saveregs)); /* d1-d7/a0-a6 */

    retvaltype = magic & 7;
    switch (retvaltype) {
    case 1:
    case 2:
        PUSHUW(0);
        break;
    case 4:
    case 5:
        PUSHUL(0);
        break;
    }
    magic >>= 3;
    while (magic) {
        switch (magic & 7) {
        case 1:
            PUSHUB((unsigned char)va_arg(ap, unsigned long));
            break;
        case 2:
            PUSHUW((unsigned short)va_arg(ap, unsigned long));
            break;
        case 3:
            ul = va_arg(ap, ULONGINT);
#if !defined(LITTLEENDIAN)
            PUSHUW(ul);
            PUSHUW(ul >> 16);
#else  /* defined(LITTLEENDIAN) */
            PUSHUW(ul >> 16);
            PUSHUW(ul);
#endif /* defined(LITTLEENDIAN) */
            break;
        case 4:
            PUSHUL(va_arg(ap, ULONGINT));
            break;
        case 5:
            PUSHUL(US_TO_SYN68K_CHECK0_CHECKNEG1(va_arg(ap, void*)));
            break;
        }
        magic >>= 3;
    }

    CALL_EMULATOR((syn68k_addr_t)US_TO_SYN68K(wheretogo));

    switch (retvaltype) {
    case 0:
        retval = 0;
        break;
    case 1:
        retval = POPSB();
        break;
    case 2:
        retval = POPSW();
        break;
    case 4:
        retval = POPSL();
        break;
    case 5:
        retval = (intptr_t)SYN68K_TO_US_CHECK0_CHECKNEG1(POPSL());
        break;
#if !defined(LETGCCWAIL)
    default:
        retval = 0;
        break;
#endif
    }
    memcpy(&EM_D1, saveregs, sizeof(saveregs)); /* d1-d7/a0-a6 */
    return retval;
}

PRIVATE intptr_t CToPascalCall_v(void* wheretogo, uint64 magic, va_list ap)
{
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    intptr_t retval;
    extern int ROMlib_ppc_callback(void*, uint64, va_list, intptr_t*);
    if (ROMlib_ppc_callback(wheretogo, magic, ap, &retval))
        return retval;
#endif
    return CToPascalCall_m68k(wheretogo, magic, ap);
}

PUBLIC intptr_t ROMlib_CToPascalCall64(void* wheretogo, uint64 magic, ...)
{
    va_list ap;
    va_start(ap, magic);
    intptr_t retval = CToPascalCall_v(wheretogo, magic, ap);
    va_end(ap);
    return retval;
}

PUBLIC intptr_t CToPascalCall(void* wheretogo, unsigned long magic_in, ...)
{
    va_list ap;
    uint64 magic;
    switch (magic_in) {
    case CTOP_A10_MAGIC:
        magic = CTOP_SPECIAL_A10_VALUE;
        break;
    case CTOP_A11_MAGIC:
        magic = CTOP_SPECIAL_A11_VALUE;
        break;
    default:
        magic = magic_in;
        break;
    }
    va_start(ap, magic_in);
    intptr_t retval = CToPascalCall_v(wheretogo, magic, ap);
    va_end(ap);
    return retval;
}
