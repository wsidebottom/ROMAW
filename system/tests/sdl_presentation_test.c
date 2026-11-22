/*+===================================================================
  File:      sdl_presentation_test.c

  Summary:   Hold the displayed palette while guest suspend code changes its own colors.

  Classes:   None.

  Functions: SDL_SetColors, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Hold the displayed palette while guest suspend code changes its own colors. */
#define NDEBUG 1
#define ROMAW_NO_GPU 1
#include "rsys/common.h"
#include "../../frontend-sdl/sdlwin.c"
#undef main
#undef NDEBUG
#include <assert.h>

static SDL_Color applied[256];
static unsigned color_calls;

int SDLCALL SDL_SetColors(SDL_Surface* surface, SDL_Color* colors, int first, int count)
{
    assert(surface == screen && first >= 0 && count >= 0 && first + count <= 256);
    memcpy(applied + first, colors, count * sizeof *colors);
    ++color_calls;
    return 1;
}

int main(void)
{
    SDL_Surface surface = {0};
    ColorSpec colors[3] = {0};
    colors[0].rgb = (RGBColor){CWC(0xffff), CWC(0), CWC(0)};
    colors[1].rgb = (RGBColor){CWC(0), CWC(0xffff), CWC(0)};
    colors[2].rgb = (RGBColor){CWC(0), CWC(0), CWC(0xffff)};
    sdl_hold_presentation(TRUE); /* Startup without a surface is harmless. */
    assert(!presentation_held);
    screen = &surface;
    vdriver_set_colors(0, 3, colors);
    assert(color_calls == 1 && applied[0].r == 255 && applied[1].g == 255);
    sdl_hold_presentation(TRUE);
    vdriver_set_colors(0, 1, colors + 2);
    vdriver_set_colors(2, 1, colors + 1);
    vdriver_set_colors(255, 1, colors);
    assert(color_calls == 1 && applied[0].r == 255 && !applied[0].b);
    sdl_hold_presentation(FALSE);
    assert(!presentation_held && color_calls == 4);
    assert(applied[0].b == 255 && !applied[0].r);
    assert(applied[1].g == 255 && applied[2].g == 255 && applied[255].r == 255);
    sdl_hold_presentation(FALSE);
    assert(color_calls == 4);
    sdl_hold_presentation(TRUE);
    sdl_hold_presentation(FALSE);
    assert(color_calls == 4);
    puts("SDL held presentation: deferred palette updates and release passed");
    return 0;
}
