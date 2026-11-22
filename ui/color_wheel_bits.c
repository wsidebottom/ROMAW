/*+===================================================================
  File:      color_wheel_bits.c

  Summary:   Compile editable indexed PNG color-wheel artwork for the Color Picker.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995-1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/color_wheel_bits.h"

char color_wheel_bits_4[] = {
#include "generated/color_wheel_4.inc"
};

char color_wheel_bits_8[] = {
#include "generated/color_wheel_8.inc"
};
