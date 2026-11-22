/*+===================================================================
  File:      sdlevents.h

  Summary:   Declares sdlevents support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SDLEVENTS_H_)
#define _SDLEVENTS_H_

/*
 * Copyright 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 * $Id: sdlevents.h 63 2004-12-24 18:19:43Z ctm $
 */

extern void sdl_preferences_input_reset(void);
extern void sdl_events_init(void);
extern void sdl_hold_presentation(int hold);
extern void sdl_resume_event_delivered(void);
extern void sdl_finish_resume(void);
extern void ROMlib_sdl_guest_point(int* x, int* y);
extern int ROMlib_sdl_display_scale(void);
extern void ROMlib_sdl_refresh_cursor(void);
extern int ROMlib_sdl_desk_drag(void);
extern int ROMlib_sdl_desk_title(int x, int y);

#endif
