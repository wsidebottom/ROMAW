/*+===================================================================
  File:      desk_shape_test.c

  Summary:   Native shape semantics: transparent gaps without colour-keying controls.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native shape semantics: transparent gaps without colour-keying controls. */
#include "../../frontend-sdl/syswm_map.c"
#undef main
#undef assert
#include <assert.h>

int main(void)
{
    SDL_Rect parts[] = {{2, 0, 96, 2}, {0, 2, 100, 58}, {140, 20, 50, 60}};
    HRGN actual = CreateRectRgn(0, 0, 0, 0);
    SDL_Window = CreateWindowExA(0, "STATIC", "Desk shape test", WS_POPUP, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    assert(SDL_Window && actual);
    assert(ROMlib_sdl_desk_shape(parts, 3));
    assert(GetWindowRgn(SDL_Window, actual) == COMPLEXREGION);
    assert(!PtInRegion(actual, 0, 0));
    assert(PtInRegion(actual, 20, 0));
    assert(PtInRegion(actual, 10, 30));
    assert(!PtInRegion(actual, 120, 30));
    assert(PtInRegion(actual, 150, 30));
    assert(!PtInRegion(actual, 150, 90));
    /* A dialog closing or moving must remove the previous shape. */
    assert(ROMlib_sdl_desk_shape(parts, 1));
    assert(GetWindowRgn(SDL_Window, actual) != ERROR);
    assert(!PtInRegion(actual, 150, 30));
    assert(PtInRegion(actual, 20, 0));
    assert(ROMlib_sdl_desk_shape(NULL, 0));
    assert(GetWindowRgn(SDL_Window, actual) == ERROR);
    DeleteObject(actual);
    DestroyWindow(SDL_Window);
    puts("Desk accessory host shape tests passed");
    return 0;
}
