/* SPDX-License-Identifier: MIT */
#ifndef SYN68K_SOFTFLOAT_COMPAT_H
#define SYN68K_SOFTFLOAT_COMPAT_H
#include <stdint.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int8_t flag;
typedef uint8_t bits8;
typedef uint16_t bits16;
typedef uint32_t bits32;
typedef uint64_t bits64;
typedef int8_t sbits8;
typedef int16_t sbits16;
typedef int32_t sbits32;
typedef int64_t sbits64;
#define INLINE static inline
#define LIT64(a) a##ULL
#include "softfloat.h"
#endif
