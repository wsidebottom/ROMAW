/*+===================================================================
  File:      win_clip.h

  Summary:   Declares win clip support for host-platform configuration and integration.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_win_clip_h_)
#define _win_clip_h_

/*
 * Copyright 1999 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

#if defined(SDL)
extern void render_all_clipboard_formats(void);
extern void discard_cached_clipboard_picture(void);
extern void write_surfp_to_clipboard(SDL_Surface* surfp);
extern unsigned long ROMlib_romaw_format(LONGINT type);
extern void write_pict_as_dib_to_clipboard(void);
extern void write_pict_as_pict_to_clipboard(void);
#endif

#endif
