/*+===================================================================
  File:      syswm_map.c

  Summary:   Implements syswm map support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_sdl_desk_drag, ROMlib_sdl_desk_shape, ROMlib_sdl_startup_begin, ROMlib_sdl_startup_ready,
             sdl_syswm_event, sdl_syswm_init

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/*
 * Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "syswm_vars.h"
#include "syswm_map.h"

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/flags.h"
#include "rsys/notmac.h"

#include "sdlevents.h"
#include "windowprefs.h"
#include "rsys/displayscale.h"

/* SDL 1.2 has no hidden-window video flag. Suppress its initial show request
   until the launcher knows whether to present a desktop or a cropped DA. */
static int startup_pending = TRUE;

static HHOOK startup_hook;
static const char startup_proc_property[] = "ROMAWStartupProc";

static LRESULT CALLBACK startup_host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    WNDPROC previous = (WNDPROC)GetPropA(window, startup_proc_property);
    if (startup_pending && message == WM_WINDOWPOSCHANGING)
        ((WINDOWPOS*)lparam)->flags &= ~SWP_SHOWWINDOW;
    LRESULT result = previous ? CallWindowProc(previous, window, message, wparam, lparam)
                              : DefWindowProc(window, message, wparam, lparam);
    if (message == WM_NCDESTROY)
        RemovePropA(window, startup_proc_property);
    return result;
}

static LRESULT CALLBACK startup_create_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HCBT_CREATEWND) {
        HWND window = (HWND)wparam;
        char name[256];
        const char* extension = ROMlib_appname ? strrchr(ROMlib_appname, '.') : NULL;
        size_t length =
            ROMlib_appname ? (extension ? (size_t)(extension - ROMlib_appname) : strlen(ROMlib_appname)) : 0;
        if (GetClassNameA(window, name, sizeof name) &&
            (!strcmp(name, "SDL_app") ||
             (length && strlen(name) == length && !strncmp(name, ROMlib_appname, length)))) {
            CBT_CREATEWND* create = (CBT_CREATEWND*)lparam;
            create->lpcs->style &= ~WS_VISIBLE;
            WNDPROC previous = (WNDPROC)GetWindowLongPtr(window, GWLP_WNDPROC);
            if (previous && SetPropA(window, startup_proc_property, (HANDLE)previous))
                if (!SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)startup_host_proc))
                    RemovePropA(window, startup_proc_property);
        }
    }
    return CallNextHookEx(startup_hook, code, wparam, lparam);
}

void ROMlib_sdl_startup_begin(void)
{
    startup_pending = TRUE;
    window_prefs_set_saving_enabled(FALSE);
    startup_hook = SetWindowsHookEx(WH_CBT, startup_create_hook, NULL, GetCurrentThreadId());
}

void ROMlib_sdl_startup_ready(void)
{
    if (startup_pending) {
        startup_pending = FALSE;
        window_prefs_set_saving_enabled(TRUE);
        if (startup_hook) {
            UnhookWindowsHookEx(startup_hook);
            startup_hook = NULL;
        }
        if (SDL_Window)
            ShowWindow(SDL_Window, SW_SHOW);
    }
}

/* Shape the host window from guest structure regions, never from pixel colour:
   white controls and black title-bar borders must remain opaque. */
int ROMlib_sdl_desk_shape(const SDL_Rect* rects, int count)
{
    if (!count)
        return SetWindowRgn(SDL_Window, NULL, TRUE) != 0;
    HRGN shape = CreateRectRgn(0, 0, 0, 0);
    int i;
    if (!shape)
        return 0;
    for (i = 0; i < count; ++i) {
        HRGN part = CreateRectRgn(rects[i].x, rects[i].y, rects[i].x + rects[i].w, rects[i].y + rects[i].h);
        if (!part || CombineRgn(shape, shape, part, RGN_OR) == ERROR) {
            if (part)
                DeleteObject(part);
            DeleteObject(shape);
            return 0;
        }
        DeleteObject(part);
    }
    if (!SetWindowRgn(SDL_Window, shape, TRUE)) {
        DeleteObject(shape);
        return 0;
    }
    /* Windows owns shape after a successful SetWindowRgn. */
    return 1;
}

/* Move the host window, leaving the Macintosh desktop/port coordinates fixed.
   Macintosh hit testing keeps its title bar and close box distinct. */
int ROMlib_sdl_desk_drag(void)
{
    POINT position;
    GetCursorPos(&position);
    ReleaseCapture();
    SendMessage(SDL_Window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(position.x, position.y));
    return 1;
}

#include "win_clip.h"

/* System dependent variables */

#include "win_screen.h"

HWND SDL_Window;
HWND Win_Window;

static WNDPROC desk_window_proc;
static HWND desk_window;

static LRESULT CALLBACK desk_host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    extern int ROMlib_desk_host_close;
    /* SDL's popup style has no system menu, so forward host close gestures
     explicitly. The emulation loop performs CloseDeskAcc, outside WndProc. */
    if (message == WM_CLOSE || (message == WM_SYSKEYDOWN && wparam == VK_F4)) {
        ROMlib_desk_host_close = TRUE;
        return 0;
    }
    if (message == WM_NCHITTEST) {
        POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        ScreenToClient(window, &point);
        if (ROMlib_sdl_desk_title(point.x, point.y))
            return HTCAPTION;
    }
    return CallWindowProc(desk_window_proc, window, message, wparam, lparam);
}

/* Initialize the system dependent variables */
PUBLIC int sdl_syswm_init(void)
{
    int retval;
    SDL_SysWMinfo info;

    /* Grab the window manager specific information */
    SDL_VERSION(&info.version);
    if (SDL_GetWMInfo(&info) >= 0) {
        /* * */
        Win_Window = SDL_Window = info.window;
        {
            extern int ROMlib_desk_host_active;
            if (ROMlib_desk_host_active && desk_window != SDL_Window) {
                WNDPROC previous = (WNDPROC)GetWindowLongPtr(SDL_Window, GWLP_WNDPROC);
                if (SetWindowLongPtr(SDL_Window, GWLP_WNDPROC, (LONG_PTR)desk_host_proc)) {
                    desk_window_proc = previous;
                    desk_window = SDL_Window;
                }
            }
        }
        {
            extern void ROMlib_sdl_apply_application_icon(void);
            ROMlib_sdl_apply_application_icon();
        }

        retval = 0;
    }
    else {
        retval = -1;
    }
    return (retval);
}

/* Handle system dependent events */
PUBLIC int sdl_syswm_event(const SDL_Event* event)
{
    int retval;

    retval = 0;
    switch (event->syswm.msg->msg) {
    case WM_EXITSIZEMOVE: {
        extern void ROMlib_sdl_save_window_position(void);
        ROMlib_sdl_save_window_position();
    } break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits = (MINMAXINFO*)event->syswm.msg->lParam;
        RECT outer, client;
        HWND window = event->syswm.msg->hwnd;
        int border_width, border_height;

        GetWindowRect(window, &outer);
        GetClientRect(window, &client);
        border_width = outer.right - outer.left - client.right;
        border_height = outer.bottom - outer.top - client.bottom;
        int scale = ROMlib_sdl_display_scale();
        limits->ptMinTrackSize.x = ROMlib_scale_edge(512, scale) + border_width;
        limits->ptMinTrackSize.y = ROMlib_scale_edge(342, scale) + border_height;
        extern int ROMlib_desk_host_active;
        if (ROMlib_desk_host_active) {
            limits->ptMinTrackSize.x = 1;
            limits->ptMinTrackSize.y = 1;
        }
        limits->ptMaxTrackSize.x = MIN(32767, ROMlib_scale_edge(SDL_MAX_SCREEN_WIDTH, scale)) + border_width;
        limits->ptMaxTrackSize.y = MIN(32767, ROMlib_scale_edge(SDL_MAX_SCREEN_HEIGHT, scale)) + border_height;
        if (ROMlib_fixed_window_p && !ROMlib_desk_host_active) {
            limits->ptMinTrackSize.x = ROMlib_scale_edge(flag_width, scale) + border_width;
            limits->ptMinTrackSize.y = ROMlib_scale_edge(flag_height, scale) + border_height;
            limits->ptMaxTrackSize = limits->ptMinTrackSize;
            limits->ptMaxSize = limits->ptMinTrackSize;
        }
    } break;
    case WM_DESTROYCLIPBOARD:
        discard_cached_clipboard_picture();
        break;
    case WM_RENDERFORMAT:
        if (event->syswm.msg->wParam == CF_DIB)
            write_pict_as_dib_to_clipboard();
        else if (event->syswm.msg->wParam == ROMlib_romaw_format(TICK("PICT")))
            write_pict_as_pict_to_clipboard();
        break;
    case WM_RENDERALLFORMATS:
        render_all_clipboard_formats();
        break;
    default:
        break;
    }

    return retval;
}
