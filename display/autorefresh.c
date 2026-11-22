/*+===================================================================
  File:      autorefresh.c

  Summary:   Implements autorefresh support for display refresh and pixel conversion.

  Classes:   None.

  Functions: autodetect_refresh, note_romaw_changed_screen

  Origin:    Existing repository source in executor/src/display; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/autorefresh.h"
#include "rsys/vdriver.h"
#include "rsys/refresh.h"
#include "rsys/flags.h"
#include "rsys/prefs.h"

/* This file provides a mechanism to detect when applications are
 * bypassing QuickDraw and writing directly to screen memory.
 * It works by partitioning the screen into NUM_AUTOREFRESH_STRIPS
 * horizontal strips of equal size.  Each strip is periodically
 * checksummed and compared against the last checksum.  If the
 * checksum value changes, and QuickDraw didn't change that area
 * of the screen, we know we need to turn on "refresh" mode.
 */

static boolean_t romaw_changed_screen_p[NUM_AUTOREFRESH_STRIPS];

/* Check auto refresh? */
boolean_t do_autorefresh_p;

/* Returns a value that will tend to change when the screen contents change
 * for the specified horizontal strip on the screen.
 */
static uint32 checksum_strip(int which_strip)
{
    int n, row_size, strip_rows, next_row_delta;
    const uint32* base;
    uint32 sum;

    strip_rows = vdriver_height / NUM_AUTOREFRESH_STRIPS;
    base = (const uint32*)(vdriver_fbuf + which_strip * strip_rows * vdriver_row_bytes);
    row_size = vdriver_row_bytes / (4U * sizeof(uint32));
    next_row_delta = (vdriver_row_bytes * 2 / sizeof(uint32)) - (row_size * 4U);

    /* Sum (most of) the longs on every other row in this strip. */
    for (sum = 0, n = strip_rows; n > 0; n -= 2) {
        int r;

        for (r = row_size; r > 0; r--) {
            sum += base[0] + base[1] + base[2] + base[3];
            base += 4;
        }

        base += next_row_delta;
    }

    return sum;
}

/* Call this when part of the screen has been changed by ROMAW,
 * so we don't mistakenly think we need refresh mode.
 */
void note_romaw_changed_screen(int top, int bottom)
{
    int i, first, last, strip_rows;

    strip_rows = vdriver_height / NUM_AUTOREFRESH_STRIPS;
    first = top / strip_rows;
    last = (bottom - 1) / strip_rows;

    if (first >= (int)NUM_AUTOREFRESH_STRIPS || last < 0)
        return;

    if (first < 0)
        first = 0;
    if (last > (int)NUM_AUTOREFRESH_STRIPS - 1)
        last = NUM_AUTOREFRESH_STRIPS - 1;

    for (i = first; i <= last; i++)
        romaw_changed_screen_p[i] = TRUE;
}

/* Returns TRUE if refresh should be turned on. */
boolean_t autodetect_refresh(void)
{
    static uint32 last_checksum[NUM_AUTOREFRESH_STRIPS];
    static boolean_t last_checksum_valid_p[NUM_AUTOREFRESH_STRIPS];
    boolean_t need_refresh_p;
    int i;

    /* We don't need to autodetect refresh if we're already doing refresh
   * or if we have direct screen access.
   */
    if (ROMlib_refresh != 0 || !ROMlib_shadow_screen_p) {
        memset(last_checksum_valid_p, 0, sizeof last_checksum_valid_p);
        return FALSE;
    }

    need_refresh_p = FALSE; /* default value */

    /* Compute checksums for the screen. */
    for (i = NUM_AUTOREFRESH_STRIPS - 1; i >= 0; i--) {
        if (romaw_changed_screen_p[i]) {
            romaw_changed_screen_p[i] = FALSE; /* reset it */
            last_checksum_valid_p[i] = FALSE;
        }
        else {
            uint32 c;
            c = checksum_strip(i);
            if (last_checksum_valid_p[i] && c != last_checksum[i])
                need_refresh_p = TRUE;
            last_checksum[i] = c;
            last_checksum_valid_p[i] = TRUE;
        }
    }

    return need_refresh_p;
}
