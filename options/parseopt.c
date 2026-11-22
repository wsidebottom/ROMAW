/*+===================================================================
  File:      parseopt.c

  Summary:   Implements parseopt support for ROMAW options and preferences.

  Classes:   None.

  Functions: ROMlib_parse_version, parse_prres_opt, parse_size_opt, parse_system_version

  Origin:    Existing repository source in executor/src/options; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "Gestalt.h"
#include "rsys/parseopt.h"
#include "rsys/vdriver.h"
#include "rsys/flags.h"
#include "rsys/prefs.h"
#include "rsys/version.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* Parse version e.g. "romaw -system 7.0.2".  Omitted
 * digits will be zero, so "romaw -system 7" is equivalent to
 * "romaw -system 7.0.0".  Returns TRUE on success, else FALSE.
 */

boolean_t ROMlib_parse_version(const char* vers, uint32* version_out)
{
    unsigned components[3] = {0, 0, 0};
    const unsigned char* p = (const unsigned char*)vers;
    int i;

    if (!vers || !version_out)
        return FALSE;
    for (i = 0; i < 3; ++i) {
        if (!isdigit(*p))
            return FALSE;
        do {
            /* Each encoded component occupies one nibble. Checking after
           * each digit keeps the intermediate value below 160. */
            components[i] = components[i] * 10 + (*p++ - '0');
            if (components[i] > 15)
                return FALSE;
        } while (isdigit(*p));
        if (!*p)
            break;
        if (*p++ != '.' || i == 2)
            return FALSE;
    }
    if (!components[0])
        return FALSE;
    *version_out = CREATE_SYSTEM_VERSION(components[0], components[1], components[2]);
    return TRUE;
}

/* Parse -system option, e.g. "romaw -system 7.0.2".  Omitted
 * digits will be zero, so "romaw -system 7" is equivalent to
 * "romaw -system 7.0.0".  Returns TRUE on success, else FALSE.
 */
boolean_t parse_system_version(const char* vers)
{
    boolean_t retval;

    retval = ROMlib_parse_version(vers, &system_version);
    if (retval)
        ROMlib_set_system_version(system_version);
    else {
        fprintf(stderr, "%s: bad option `-system': invalid version\n", program_name);
    }

    return retval;
}

/* Parse -size option, e.g. "romaw -size 640x480".  Returns FALSE
 * on parse error.
 */
boolean_t parse_size_opt(const char* opt, const char* arg)
{
    boolean_t success_p;
    int w, h;

    w = h = 0;
    if (arg != NULL) {
#if defined(CYGWIN32)
        if (strcasecmp(arg, "maximum") == 0) {
            h = os_maximum_window_height();
            w = os_maximum_window_width();
        }
        else
#else
#warning we should support "-size maximum"
#endif
        {
            char* end;
            unsigned long parsed_w, parsed_h;

            errno = 0;
            parsed_w = strtoul(arg, &end, 10);
            if (!errno && isdigit((unsigned char)*arg) && (*end == 'x' || *end == 'X') &&
                isdigit((unsigned char)end[1])) {
                parsed_h = strtoul(end + 1, &end, 10);
                if (!errno && !*end && parsed_w <= SHRT_MAX && parsed_h <= SHRT_MAX) {
                    w = parsed_w;
                    h = parsed_h;
                }
            }
        }
    }

    if (w == 0 || h == 0) {
        fprintf(stderr,
                "Invalid screen size.  Use something like "
                "\"-%s 640x480\".\n",
                opt);
        success_p = FALSE;
    }
    else if (w < VDRIVER_MIN_SCREEN_WIDTH || h < VDRIVER_MIN_SCREEN_HEIGHT) {
        fprintf(stderr, "Screen size must be at least %dx%d.\n", VDRIVER_MIN_SCREEN_WIDTH, VDRIVER_MIN_SCREEN_HEIGHT);
        success_p = FALSE;
    }
    else if (
#if defined(SDL)
        w > SDL_MAX_SCREEN_WIDTH || h > SDL_MAX_SCREEN_HEIGHT
#else
        w > SHRT_MAX || h > SHRT_MAX
#endif
    ) {
        fprintf(stderr, "Screen size exceeds the supported maximum.\n");
        success_p = FALSE;
    }
    else {
        flag_width = w;
        flag_height = h;
        success_p = TRUE;
    }

    return success_p;
}

/* Parse two positive decimal resolutions without narrowing until validated. */
PUBLIC boolean_t parse_prres_opt(INTEGER* outx, INTEGER* outy, const char* arg)
{
    unsigned values[2] = {0, 0};
    const unsigned char* p = (const unsigned char*)arg;
    int i;

    if (!arg || !outx || !outy)
        return FALSE;
    for (i = 0; i < 2; ++i) {
        if (!isdigit(*p))
            return FALSE;
        do {
            unsigned digit = *p++ - '0';
            if (values[i] > (SHRT_MAX - digit) / 10)
                return FALSE;
            values[i] = values[i] * 10 + digit;
        } while (isdigit(*p));
        if (!values[i])
            return FALSE;
        if (i == 0 && *p++ != 'x')
            return FALSE;
    }
    if (*p)
        return FALSE;
    *outx = values[0];
    *outy = values[1];
    return TRUE;
}
