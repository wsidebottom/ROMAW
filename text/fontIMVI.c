/*+===================================================================
  File:      fontIMVI.c

  Summary:   Implements font imvi support for Macintosh fonts, scripts and international text.

  Classes:   None.

  Functions: C_FlushFonts, C_GetOutlinePreferred, C_GetPreserveGlyph, C_IsAntiAliasedTextEnabled, C_IsOutline,
             C_OutlineMetrics, C_SetAntiAliasedTextEnabled, C_SetOutlinePreferred, C_SetPreserveGlyph.

  Origin:    Existing repository source in executor/src/text; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include "FontMgr.h"
#include "rsys/byteswap.h"

static boolean_t outline_preferred_p = FALSE;

P1(PUBLIC pascal trap, void, SetOutlinePreferred, Boolean, _outline_preferred_p)
{
    outline_preferred_p = _outline_preferred_p;
}

P0(PUBLIC pascal trap, Boolean, GetOutlinePreferred)
{
    return outline_preferred_p;
}

P2(PUBLIC pascal trap, Boolean, IsOutline, Point, numer, Point, denom)
{
    return FALSE;
}

P9(PUBLIC pascal trap, OSErr, OutlineMetrics, int16, byte_count, Ptr, text, Point, numer, Point, denom, int16*, y_max,
   int16*, y_min, Fixed*, aw_array, Fixed*, lsb_array, Rect*, bounds_array)
{
    warning_unimplemented(NULL_STRING);
    /* ### paramErr */
    return -50;
}

static boolean_t preserve_glyph_p = FALSE;
static Boolean antialiased_text_enabled;
static int16 antialiased_text_minimum_size;

P1(PUBLIC pascal trap, void, SetPreserveGlyph, Boolean, _preserve_glyph_p)
{
    preserve_glyph_p = _preserve_glyph_p;
}

P0(PUBLIC pascal trap, Boolean, GetPreserveGlyph)
{
    return preserve_glyph_p;
}

P0(PUBLIC pascal trap, OSErr, FlushFonts)
{
    warning_unimplemented(NULL_STRING);
    /* ### paramErr */
    return -50;
}

/* FontDispatch 0x11 and 0x12 share the QuickDraw font preference with ATSUI. */
int32 C_SetAntiAliasedTextEnabled(Boolean enabled, int16 minimum_size)
{
    antialiased_text_enabled = enabled != 0;
    antialiased_text_minimum_size = minimum_size;
    return noErr;
}

Boolean C_IsAntiAliasedTextEnabled(int16* minimum_size)
{
    if (minimum_size)
        *minimum_size = CW(antialiased_text_minimum_size);

    return antialiased_text_enabled;
}
