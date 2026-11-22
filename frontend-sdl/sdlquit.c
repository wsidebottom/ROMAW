/*+===================================================================
  File:      sdlquit.c

  Summary:   Implements sdlquit support for host-platform configuration and integration.

  Classes:   None.

  Functions: sdl_close_is_quit, sdl_really_quit

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "sdlquit.h"
#include "rsys/options.h"
#include "rsys/segment.h"

#if defined(_WIN32)

PRIVATE int os_specific_really_quit(void)
{
    /* This should really be an internal ROMlib dialog */
    int retval;
    int reply;
    char* mess;
    char* name;

    /* would be nice to use snprintf here, but mingw32 doesn't provide it */
#define FORMAT_STR "Terminate %s?"

    name = ROMlib_WindowName ? ROMlib_WindowName : "application";
    mess = alloca(sizeof FORMAT_STR + strlen(name));
    sprintf(mess, FORMAT_STR, name);

    reply = MessageBox(NULL, mess, "Terminate?", MB_OKCANCEL);

    retval = reply == IDOK;
    return retval;
}

#else

PRIVATE int os_specific_really_quit(void)
{
    int retval;

    retval = TRUE;
    return retval;
}

#endif

int sdl_close_is_quit(void)
{
    /* Direct launches already exit ROMAW when the application quits. */
    return ROMlib_exit == 1 || (ROMlib_options & ROMLIB_CLOSE_IS_QUIT_BIT);
}

/* Accept normal quit requests here; dispatch them on the emulator thread. */
int sdl_really_quit(void)
{
    return sdl_close_is_quit() || os_specific_really_quit();
}
