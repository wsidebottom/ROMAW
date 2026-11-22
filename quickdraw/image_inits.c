/*+===================================================================
  File:      image_inits.c

  Summary:   Implements image inits support for Macintosh QuickDraw graphics.

  Classes:   None.

  Functions: image_inits

  Origin:    Existing repository source in executor/src/quickdraw; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/ctl.h"

void image_inits(void)
{
    image_wind_scroll_arrow_active_up_init();
    image_wind_scroll_arrow_inactive_up_init();
    image_wind_scroll_arrow_active_down_init();
    image_wind_scroll_arrow_inactive_down_init();
    image_wind_scroll_arrow_active_left_init();
    image_wind_scroll_arrow_inactive_left_init();
    image_wind_scroll_arrow_active_init();
    image_wind_scroll_arrow_clicked_up_init();
    image_wind_scroll_arrow_clicked_down_init();
    image_wind_scroll_arrow_clicked_left_init();
    image_wind_scroll_arrow_clicked_init();
    image_wind_scroll_arrow_inactive_init();

    image_wind_scroll_thumb_init();
    image_wind_scroll_thumb_vertical_init();

    image_wind_close_clicked_init();
    image_wind_close_init();
    image_wind_grow_init();
    image_wind_zoom_init();

    image_apple_init();
}
