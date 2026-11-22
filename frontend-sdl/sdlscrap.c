/*+===================================================================
  File:      sdlscrap.c

  Summary:   Derived from public domain source code written by Sam Lantinga Handle clipboard text and data in arbitrary
             formats

  Classes:   None.

  Functions: surface_from_dib

  Origin:    Derived from public domain source code written by Sam Lantinga

  Copyright and Legal notices: Original public-domain and warranty notices are retained below.
===================================================================+*/

/*
 * Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 *
 * Derived from public domain source code written by Sam Lantinga
 */

/* Handle clipboard text and data in arbitrary formats */

#include <stdio.h>

#include "syswm_vars.h"
#include "sdl_mem.h"

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"

#include "rsys/error.h"

#include "SDL_bmp.h"

PUBLIC SDL_Surface* surface_from_dib(void* lp, size_t length)
{
    SDL_Surface* retval;

    retval = SDL_LoadCF_DIB(lp, length);
    if (retval) {
        int first_bpp;

        first_bpp = retval->format->BitsPerPixel;

        if (first_bpp < 8) {
            /* TODO: convert < 8bpp to 8bpp */
        }
        else if (first_bpp > 8) {
            /* convert > 8bpp to our particular 32bpp */
            SDL_Surface* new_surface;
            int pixels_per_line;
            int n_lines;

            enum { A = 0x00000000, R = 0x0000FF00, G = 0x00FF0000, B = 0xFF000000 };

            pixels_per_line = retval->w;
            n_lines = retval->h;
            new_surface = SDL_AllocSurface(SDL_SWSURFACE, pixels_per_line, n_lines, 32, R, G, B, A);
            if (!new_surface || SDL_BlitSurface(retval, NULL, new_surface, NULL) != 0) {
                if (new_surface) {
                    SDL_FreeSurface(new_surface);
                    new_surface = NULL;
                }
            }
            SDL_FreeSurface(retval);
            retval = new_surface;
        }
    }

    return retval;
}
