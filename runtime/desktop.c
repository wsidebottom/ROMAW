/*+===================================================================
  File:      desktop.c

  Summary:   Run the built-in Finder after classic Toolbox initialization.

  Classes:   None.

  Functions: ROMlib_desktop_set_entry, ROMlib_desktop_active,
             ROMlib_desktop_is_ui_thread, ROMlib_desktop_run

  Origin:    Internal Finder startup separated from the existing Executor runtime
             native application bridge.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include "rsys/desktop.h"
#include "rsys/segment.h"

static int (*desktop_entry)(void);
static _Thread_local int desktop_ui_thread;

void ROMlib_desktop_set_entry(int (*entry)(void))
{
    desktop_entry = entry;
}

int ROMlib_desktop_active(void)
{
    return desktop_entry != NULL;
}

int ROMlib_desktop_is_ui_thread(void)
{
    return desktop_ui_thread;
}

int ROMlib_desktop_run(void)
{
    if (!desktop_entry)
        return 0;
    desktop_ui_thread = 1;
    ROMlib_exit = TRUE;
    int status = desktop_entry();
    if (status) {
        fprintf(stderr, "Finder desktop exited with status %d\n", status);
        exit(2);
    }
    return 1;
}
