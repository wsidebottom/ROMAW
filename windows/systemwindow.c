/*+===================================================================
  File:      systemwindow.c

  Summary:   Process-owned windows in a shared, never-active floating layer.

  Classes:   None.

  Functions: ROMlib_system_window_dispose, ROMlib_system_window_event, ROMlib_system_window_forget,
             ROMlib_system_window_new, ROMlib_system_window_p, ROMlib_system_window_reset

  Origin:    Existing repository source in executor/src/windows; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Process-owned windows in a shared, never-active floating layer. */
#include "rsys/common.h"
#include "rsys/everything.h"
#include "rsys/wind.h"
#include "rsys/process.h"
#include "rsys/systemwindow.h"
#include "rsys/layers.h"
#include "rsys/ctopflags.h"

typedef struct system_window {
    struct system_window* next;
    WindowPtr window;
    void* owner;
    unsigned busy;
} system_window;

static system_window* floating_windows;
static int resetting;

static system_window* lookup(WindowPtr window)
{
    system_window* p;
    for (p = floating_windows; p; p = p->next)
        if (p->window == window)
            return p;
    return NULL;
}

PUBLIC BOOLEAN ROMlib_system_window_p(WindowPtr window)
{
    return lookup(window) != NULL;
}

PUBLIC WindowPtr ROMlib_system_window_new(Rect* bounds, StringPtr title, BOOLEAN visible, INTEGER proc_id,
                                          WindowPtr behind, BOOLEAN wind_close, LONGINT refcon, ProcPtr callback,
                                          RgnHandle mouse_region)
{
    system_window* p;
    WindowPtr window;
    void* previous_layer;
    HIDDEN_Ptr layer;
    void* owner = ROMlib_process_identity();
    if (resetting || !owner || !bounds || !callback)
        return NULL;
    if (ROMlib_new_float_layer(&layer) != noErr)
        return NULL;
    p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    window = (WindowPtr)NewPtrSysClear(0xc6);
    if (!window) {
        free(p);
        return NULL;
    }
    p->window = window;
    p->owner = owner;
    p->next = floating_windows;
    floating_windows = p;
    previous_layer = ROMlib_layer_swap(MR(HIDDEN_RAW(layer)));
    C_NewCWindow((Ptr)window, bounds, title, visible, proc_id, behind, wind_close, refcon);
    ROMlib_layer_set(previous_layer);
    /* Fields established from the Mac OS 9.2.1 ProcessMgrLib implementation.
     Owner identity and A5 remain tied to the real ROMAW process. */
    WRITEUL(US_TO_SYN68K(window) + 0xaa, US_TO_SYN68K(owner));
    WRITEUL(US_TO_SYN68K(window) + 0xae, 0x0000ffd7);
    WRITEUL(US_TO_SYN68K(window) + 0xb2, US_TO_SYN68K(callback));
    WRITEUL(US_TO_SYN68K(window) + 0xb6, EM_A5);
    WRITEUL(US_TO_SYN68K(window) + 0xba, mouse_region ? US_TO_SYN68K(mouse_region) : 0);
    WINDOW_KIND_X(window) = CWC(5);
    C_HiliteWindow(window, TRUE);
    return window;
}

PUBLIC void ROMlib_system_window_forget(WindowPtr window)
{
    system_window **link, *p;
    for (link = &floating_windows; *link && (*link)->window != window; link = &(*link)->next)
        ;
    if ((p = *link)) {
        *link = p->next;
        free(p);
    }
}

PUBLIC OSErr ROMlib_system_window_dispose(WindowPtr window)
{
    if (!lookup(window) || !ROMlib_valid_window(window))
        return paramErr;
    C_DisposeWindow(window);
    return noErr;
}

PUBLIC BOOLEAN ROMlib_system_window_event(EventRecord* event)
{
    system_window* p;
    WindowPtr* snapshot;
    WindowPeek w;
    size_t count = 0, i = 0;
    unsigned what = CW(event->what);
    uint32 saved_a5 = EM_A5;
    BOOLEAN handled = FALSE;
    HIDDEN_WindowPtr mouse_window;
    WindowPtr event_window = NULL;
    BOOLEAN targeted = FALSE;
    INTEGER part = 0;
    if (what >= 16 || !floating_windows)
        return FALSE;
    if (what == mouseDown || what == mouseUp) {
        Point where;
        where.h = CW(event->where.h);
        where.v = CW(event->where.v);
        part = C_FindWindow(where, &mouse_window);
        event_window = MR(HIDDEN_RAW(mouse_window));
        targeted = TRUE;
    }
    else if (what == updateEvt || what == activateEvt) {
        event_window = (WindowPtr)MR(event->message);
        targeted = TRUE;
    }
    else if (what == keyDown || what == keyUp || what == autoKey) {
        event_window = C_FrontWindow();
        targeted = TRUE;
    }
    if (targeted && !ROMlib_system_window_p(event_window))
        return FALSE;
    for (p = floating_windows; p; p = p->next)
        ++count;
    if (count > SIZE_MAX / sizeof *snapshot || !(snapshot = malloc(count * sizeof *snapshot)))
        return FALSE;
    /* Traverse drawing order; callbacks can dispose themselves or other windows. */
    for (w = ROMlib_window_first_all(); w && i < count; w = ROMlib_window_next_all(w))
        if (ROMlib_system_window_p((WindowPtr)w))
            snapshot[i++] = (WindowPtr)w;
    count = i;
    for (i = 0; i < count && !handled; ++i) {
        uint32 address = US_TO_SYN68K(snapshot[i]), callback;
        if (!(p = lookup(snapshot[i])) || p->busy || p->owner != ROMlib_process_identity())
            continue;
        if (targeted && snapshot[i] != event_window)
            continue;
        if (!(READUL(address + 0xae) & (1u << what)))
            continue;
        callback = READUL(address + 0xb2);
        if (!callback)
            continue;
        ++p->busy;
        EM_A5 = READUL(address + 0xb6);
        CToPascalCall(SYN68K_TO_US(callback), 2 | (5 << 3) | (5 << 6) | (2 << 9) | (5 << 12), snapshot[i], event, part,
                      &handled);
        if ((p = lookup(snapshot[i])))
            --p->busy;
        EM_A5 = saved_a5;
    }
    free(snapshot);
    return handled;
}

PUBLIC void ROMlib_system_window_reset(void)
{
    resetting = 1;
    while (floating_windows) {
        WindowPtr window = floating_windows->window;
        if (ROMlib_valid_window(window))
            C_DisposeWindow(window);
        else
            ROMlib_system_window_forget(window);
    }
    resetting = 0;
}
