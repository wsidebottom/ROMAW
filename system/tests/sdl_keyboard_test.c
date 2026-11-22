/*+===================================================================
  File:      sdl_keyboard_test.c

  Summary:   Unmapped/media keys must not reach KCHR translation or the guest bitmap.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Unmapped/media keys must not reach KCHR translation or the guest bitmap. */
#define NDEBUG 1
#include "rsys/common.h"
#include "../../frontend-sdl/sdlevents.c"
#include "../../events/ibm_keycodes.c"
#undef main
#undef NDEBUG
#include <assert.h>

int main(void)
{
    SDL_keysym key = {0};
    unsigned symbol;
    key.sym = SDLK_a;
    assert(sdl_mac_virtual_key(&key) == 0);
    key.sym = SDLK_UNKNOWN;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    key.sym = SDLK_LAST;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    key.sym = (SDLKey)-1;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    for (symbol = 0; symbol < SDLK_LAST; ++symbol) {
        key.sym = symbol;
        assert(sdl_mac_virtual_key(&key) < 128 || sdl_mac_virtual_key(&key) == 0xff);
    }
    ROMlib_set_use_scancodes(TRUE);
    key.scancode = 0;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    key.scancode = 0x1e;
    assert(sdl_mac_virtual_key(&key) == 0);
    key.scancode = 0x78;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    key.scancode = 0xff;
    assert(sdl_mac_virtual_key(&key) == 0xff);
    puts("SDL unmapped-key and scancode checks passed");
    return 0;
}
