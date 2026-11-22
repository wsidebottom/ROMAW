/*+===================================================================
  File:      sdl_quit_test.c

  Summary:   Direct launches must request guest quit without a termination dialog.

  Classes:   None.

  Functions: MessageBoxA, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Direct launches must request guest quit without a termination dialog. */
#define NDEBUG 1
#include "../../frontend-sdl/sdlquit.c"
#undef main
#undef NDEBUG
#include <assert.h>

int32 ROMlib_options;
char ROMlib_exit;
char* ROMlib_WindowName;
static unsigned dialogs;
static int reply;

int WINAPI MessageBoxA(HWND window, LPCSTR text, LPCSTR title, UINT flags)
{
    assert(strstr(text, "Terminate"));
    ++dialogs;
    return reply;
}

int main(void)
{
    reply = IDCANCEL;
    assert(!sdl_close_is_quit() && !sdl_really_quit() && dialogs == 1);
    reply = IDOK;
    assert(sdl_really_quit() && dialogs == 2);
    ROMlib_exit = 1;
    reply = IDCANCEL;
    assert(sdl_close_is_quit() && sdl_really_quit() && dialogs == 2);
    ROMlib_exit = 0;
    ROMlib_options = ROMLIB_CLOSE_IS_QUIT_BIT;
    assert(sdl_close_is_quit() && sdl_really_quit() && dialogs == 2);
    ROMlib_options = 0;
    assert(!sdl_close_is_quit());
    puts("SDL close policy: direct launches and close-as-quit skip termination prompts");
    return 0;
}
