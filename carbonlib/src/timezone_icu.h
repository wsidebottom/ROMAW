/*+===================================================================
  File:      timezone_icu.h

  Summary:   Declares the Windows ICU C functions used by Carbon time zones.

  Classes:   None.

  Functions: Interface declarations; see below.

  Origin:    Microsoft Windows ICU ABI and ICU4C ucal.h and uenum.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_TIMEZONE_ICU_H
#define ROMAW_TIMEZONE_ICU_H

#include <stdint.h>

typedef struct {
    void* (*open)(const uint16_t*, int32_t, const char*, int, int32_t*);
    void (*close)(void*);
    int32_t (*canonical)(const uint16_t*, int32_t, uint16_t*, int32_t, int8_t*, int32_t*);
    void (*set_date)(void*, double, int32_t*);
    int32_t (*field)(const void*, int, int32_t*);
    int8_t (*next_transition)(const void*, int, double*, int32_t*);
    int32_t (*display_name)(const void*, int, const char*, uint16_t*, int32_t, int32_t*);
    void* (*open_names)(int32_t*);
    const char* (*next_name)(void*, int32_t*, int32_t*);
    void (*close_names)(void*);
} zone_icu;

const zone_icu* ROMlib_carbon_timezone_icu(void);

#endif
