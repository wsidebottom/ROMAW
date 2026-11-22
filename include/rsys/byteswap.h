/*+===================================================================
  File:      byteswap.h

  Summary:   Declares byteswap support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_BYTESWAP_H_)
#define _BYTESWAP_H_

#if !defined(BIGENDIAN) && !defined(LITTLEENDIAN)
#error "One of BIGENDIAN or LITTLEENDIAN must be #defined"
#endif

#if defined(BIGENDIAN)

#define CW(rhs) (rhs)
#define CL(rhs) (rhs)
#define CWC(rhs) (rhs)
#define CLC(rhs) (rhs)
#define CWV(rhs) (rhs)
#define CLV(rhs) (rhs)
#define Cx(rhs) (rhs)

/*
 * Do not use these blindly on big-endian machines.  There's no longer a
 * guarantee that we can get low-memory on all big-endian machines.  It doesn't
 * work, for example, on Mac OS X PPC.
 *
 * #define MR(rhs)		(rhs) / * Mac to ROMlib * /
 * #define RM(rhs)		(rhs) / * ROMlib to Mac * /
 */

#define MR(n)                                              \
    ((typeof(n))({                                         \
        typeof(n) _t = n;                                  \
        _t ? (SYN68K_TO_US_CHECK0((unsigned long)_t)) : 0; \
    }))
#define RM(n)                                   \
    ((typeof(n))({                              \
        typeof(n) _t = n;                       \
        _t ? (US_TO_SYN68K((uintptr_t)_t)) : 0; \
    }))

#define CLC_NULL NULL

#else /* !defined (BIGENDIAN) */

#define CW(n) ((typeof(n))(long)swap16((unsigned short)(long)(n)))
#define CL(n) ((typeof(n))swap32((unsigned long)((n) | 0)))

/* YY decodes raw guest pointer words, including heap fields on x86. */
#define YY(n) SYN68K_TO_US_CHECK0(swap32((uint32)(uintptr_t)(n)))

#if SIZEOF_CHAR_P == 4

#define MR(n)                                                      \
    ((typeof(n))({                                                 \
        typeof(n) _t = n;                                          \
        _t ? (SYN68K_TO_US_CHECK0(swap32((unsigned long)_t))) : 0; \
    }))

#else

#define MR(n)                       \
    ((typeof((n) + (uintptr_t)0))({ \
        typeof(n) _t = n;           \
        _t ? (YY(_t)) : 0;          \
    }))
#endif

#define RM(n)                                           \
    ((typeof(n))(uintptr_t)({                           \
        typeof(n) _t = n;                               \
        _t ? (swap32(US_TO_SYN68K((uintptr_t)_t))) : 0; \
    }))

/* Packed Pointer to ROMlib  */
#if SIZEOF_CHAR_P == 4
#define PPR(n) MR(n)
#else
#define PPR(n)                   \
    ((typeof(n.type[0]))({       \
        typeof(n) _t = n;        \
        _t.pp ? (YY(_t.pp)) : 0; \
    }))
#endif

/* Guest pointer fields stay four bytes on every host. Raw access is for
   encoded guest words; PP_ASSIGN takes an ordinary host pointer. */
#if SIZEOF_CHAR_P == 4
#define PP_RAW(field) (field)
#define HIDDEN_RAW(value) ((value).p)
#define HIDDEN_RAW_SET(value, raw) ((value).p = (typeof((value).p))(uintptr_t)(raw))
#else
#define PP_RAW(field) ((field).pp)
#define HIDDEN_RAW(value) ((typeof((value).type[0]))(uintptr_t)(value).pp)
#define HIDDEN_RAW_SET(value, raw) ((value).pp = (uint32)(uintptr_t)(raw))
#endif
#define PP_ASSIGN(field, pointer) (PP_RAW(field) = (typeof(PP_RAW(field)))(uintptr_t)RM(pointer))
#define HIDDEN_NATIVE(value) MR(HIDDEN_RAW(value))

/* HASSIGN's values are already encoded. Copy their low bytes without
   assigning an eight-byte host pointer into a four-byte guest field. */
#ifdef BIGENDIAN
#define FIELD_RAW_OFFSET(field, value) (sizeof(value) - sizeof(field))
#else
#define FIELD_RAW_OFFSET(field, value) 0
#endif
#define FIELD_ASSIGN_RAW(field, value)                                                                        \
    ({                                                                                                        \
        typeof(value) __field_value = (value);                                                                \
        void* __field_destination = &(field);                                                                 \
        if (sizeof(field) > sizeof(__field_value)) {                                                          \
            memset(__field_destination, 0, sizeof(field));                                                    \
            memcpy((char*)__field_destination + FIELD_RAW_OFFSET(__field_value, field), &__field_value,       \
                   sizeof(__field_value));                                                                    \
        }                                                                                                     \
        else                                                                                                  \
            memcpy(__field_destination, (const char*)&__field_value + FIELD_RAW_OFFSET(field, __field_value), \
                   sizeof(field));                                                                            \
        __field_value;                                                                                        \
    })

#define CWC(n) ((typeof(n))(signed short)((((n) << 8) & 0xFF00) | (((n) >> 8) & 0x00FF)))
#define CLC(n)                                                                                    \
    ((typeof(n))(long)((((long)((n) | 0) & 0x000000FF) << 24) | (((long)(n) & 0x0000FF00) << 8) | \
                       (((long)(n) & 0x00FF0000) >> 8) | (((unsigned long)(n) & 0xFF000000) >> 24)))

#define CLC_NULL NULL

/* These are better versions of CW and CL, but should not be nested
 * because of exponential growth in the preprocessed code size.
 */
#define CWV(n) ((typeof(n))(__builtin_constant_p((long)(n)) ? CWC(n) : CW(n)))
#define CLV(n) ((typeof(n))(__builtin_constant_p((long)(n)) ? CLC(n) : CL(n)))

/* This will cause a link error for an invalid Cx. */
extern int bad_cx_splosion;

#define Cx(rhs)                                               \
    (sizeof(rhs) == 1                         ? (rhs)         \
     : sizeof(rhs) == 2                       ? CW(rhs)       \
     : (sizeof(rhs) == 4 || sizeof(rhs) == 8) ? CL((rhs) | 0) \
                                              : (typeof(rhs))bad_cx_splosion)

#endif /* !defined (BIGENDIAN) */

#define CB(rhs) (rhs)
#define CBC(rhs) (rhs)
#define CBV(rhs) (rhs)

#if SIZEOF_CHAR_P == 4
#define STARH(h) MR((h)->p)
#define HxP(handle, field) MR(STARH(handle)->field)
#define HxX(handle, field) (STARH(handle)->field)
#define HxZ(handle, field) HxX(handle, field)
#else
#define STARH(h) HIDDEN_NATIVE(*(h))
#define HxP(handle, field) PPR(STARH(handle)->field)
#define HxX(handle, field) ((STARH(handle))->field)

// HxZ is a handle dereference where the member selected is itself some form
// of packed pointer, but we're only checking to see if it's zero or non-zero
// (e.g. if (HxZ(hand)) )

#define HxZ(handle, field) ((STARH(handle))->field.pp)
#endif

#define Hx(handle, field) Cx(STARH(handle)->field)

#if defined(BIGENDIAN)

#define SWAP_POINT(pt) (pt)

#define SWAPPED_OPW(big_endian_lvalue, op, v) ((void)((big_endian_lvalue)op## = (v)))
#define SWAPPED_OPL(big_endian_lvalue, op, v) ((void)((big_endian_lvalue)op## = (v)))
#define CMPW_P(big_endian_v, op, native_endian_v) ((big_endian_v)op(native_endian_v))
#define CMPL_P(big_endian_v, op, native_endian_v) ((big_endian_v)op(native_endian_v))

#else /* !BIGENDIAN */

#define SWAP_POINT(pt)         \
    ({                         \
        Point __pt_ = (pt);    \
                               \
        __pt_.h = CW(__pt_.h); \
        __pt_.v = CW(__pt_.v); \
                               \
        __pt_;                 \
    })

#define SWAPPED_OPW(big_endian_lvalue, op, v)                                     \
    ((void)(((#op[0] == '&' || #op[0] == '|' || #op[0] == '^') && #op[1] == '\0') \
                ? ((big_endian_lvalue)op## = CWV(v))                              \
                : ((big_endian_lvalue) = CW(CW(big_endian_lvalue) op(v)))))
#define SWAPPED_OPL(big_endian_lvalue, op, v)                                     \
    ((void)(((#op[0] == '&' || #op[0] == '|' || #op[0] == '^') && #op[1] == '\0') \
                ? ((big_endian_lvalue)op## = CLV(v))                              \
                : ((big_endian_lvalue) = CL(CL(big_endian_lvalue) op(v)))))

/* This compares a big endian 16 bit value to a native endian value,
 * using the specified operator.  The operator must be one of
 * ==, !=, <, >, <=, >=.
 */
#define CMPW_P(big_endian_v, op, native_endian_v)                                                         \
    ((__builtin_constant_p((long)(native_endian_v)) && (native_endian_v) == 0)                            \
         ? ((#op[0] == '>' && #op[1] == '=')                                                              \
                ? !((big_endian_v) & CWC(0x8000))                                                         \
                : ((#op[0] == '<' && #op[1] == '\0')                                                      \
                       ? (((big_endian_v) & CWC(0x8000)) != 0)                                            \
                       : (((#op[0] == '=' || #op[0] == '!') && #op[1] == '=') ? ((big_endian_v)op CWC(0)) \
                                                                              : CWV(big_endian_v) op 0))) \
         : (((#op[0] == '=' || #op[0] == '!') && #op[1] == '=') ? ((big_endian_v)op CWV(native_endian_v)) \
                                                                : CWV(big_endian_v) op(native_endian_v)))

/* This compares a big endian 32 bit value to a native endian value,
 * using the specified operator.  The operator must be one of
 * ==, !=, <, >, <=, >=.
 */
#define CMPL_P(big_endian_v, op, native_endian_v)                                                         \
    ((__builtin_constant_p((long)(native_endian_v)) && (native_endian_v) == 0)                            \
         ? ((#op[0] == '>' && #op[1] == '=')                                                              \
                ? !((big_endian_v) & CLC(0x80000000))                                                     \
                : ((#op[0] == '<' && #op[1] == '\0')                                                      \
                       ? (((big_endian_v) & CLC(0x80000000)) != 0)                                        \
                       : (((#op[0] == '=' || #op[0] == '!') && #op[1] == '=') ? ((big_endian_v)op CLC(0)) \
                                                                              : CLV(big_endian_v) op 0))) \
         : (((#op[0] == '=' || #op[0] == '!') && #op[1] == '=') ? ((big_endian_v)op CLV(native_endian_v)) \
                                                                : CLV(big_endian_v) op(native_endian_v)))

#endif /* !BIGENDIAN */

#endif /* !_BYTESWAP_H_ */
