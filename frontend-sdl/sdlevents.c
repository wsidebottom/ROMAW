/*+===================================================================
  File:      sdlevents.c

  Summary:   Implements sdlevents support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_set_use_scancodes, handle_sdl_events, handle_sdl_mouse, sdl_apply_pending_resize,
             sdl_event_interrupt, sdl_events_init, sdl_finish_resume, sdl_preferences_input_reset,
             sdl_queue_window_size, sdl_resume_event_delivered

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1997, 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "EventMgr.h"
#include "SegmentLdr.h"
#include "OSEvent.h"
#include "ToolboxEvent.h"
#include "ScrapMgr.h"

#include "rsys/segment.h"
#include "rsys/system_error.h"
#include "rsys/m68kint.h"
#include "rsys/osevent.h"
#include "rsys/keyboard.h"
#include "rsys/desk.h"
#include "sdlevents.h"
#include "rsys/adb.h"
#include "rsys/scrap.h"
#include "rsys/cquick.h"
#include "rsys/vdriver.h"
#include "rsys/toolevent.h"
#include "rsys/keyboard.h"

#include "SDL/SDL.h"
#include "windowprefs.h"
#include "rsys/displayscale.h"
#include "rsys/options.h"

PRIVATE boolean_t use_scan_codes = FALSE;
PRIVATE int pending_width, pending_height;

void sdl_queue_window_size(int width, int height)
{
    pending_width = width;
    pending_height = height;
}

void sdl_apply_pending_resize(void)
{
    int width, height;

    if (!pending_width || !pending_height)
        return;
    width = pending_width;
    height = pending_height;
    pending_width = pending_height = 0;
    boolean_t resized =
        width == vdriver_width && height == vdriver_height && ROMlib_sdl_display_scale() != ROMlib_display_scale
            ? vdriver_set_mode(width, height, vdriver_bpp, vdriver_grayscale_p)
            : ROMlib_resize_screen(width, height);
    if (resized) {
#if defined(_WIN32)
        if (!ROMlib_fullscreen_p)
            window_prefs_save(vdriver_width, vdriver_height);
#endif
    }
    if (resized)
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    ROMlib_sdl_apply_window_position();
}

PUBLIC void ROMlib_set_use_scancodes(boolean_t val)
{
    use_scan_codes = val;
}

#if SDL_MAJOR_VERSION == 0 && SDL_MINOR_VERSION < 9

#include "sdlk_to_mkv.h"

PRIVATE void init_sdlk_to_mkv(void)
{
}
#else

enum { NOTAKEY = 0x89 };

PRIVATE unsigned char sdlk_to_mkv[SDLK_LAST];

typedef struct {
    SDLKey sdlk;
    unsigned char mkv;
} sdl_to_mkv_map_t;

PRIVATE sdl_to_mkv_map_t map[] = {
    {SDLK_BACKSPACE, MKV_BACKSPACE},
    {
        SDLK_TAB,
        MKV_TAB,
    },
    {
        SDLK_CLEAR,
        NOTAKEY,
    },
    {
        SDLK_RETURN,
        MKV_RETURN,
    },
    {
        SDLK_ESCAPE,
        MKV_ESCAPE,
    },
    {
        SDLK_SPACE,
        MKV_SPACE,
    },
    {
        SDLK_QUOTE,
        MKV_TICK,
    },
    {
        SDLK_COMMA,
        MKV_COMMA,
    },
    {
        SDLK_MINUS,
        MKV_MINUS,
    },
    {
        SDLK_PERIOD,
        MKV_PERIOD,
    },
    {
        SDLK_SLASH,
        MKV_SLASH,
    },
    {
        SDLK_0,
        MKV_0,
    },
    {
        SDLK_1,
        MKV_1,
    },
    {
        SDLK_2,
        MKV_2,
    },
    {
        SDLK_3,
        MKV_3,
    },
    {
        SDLK_4,
        MKV_4,
    },
    {
        SDLK_5,
        MKV_5,
    },
    {
        SDLK_6,
        MKV_6,
    },
    {
        SDLK_7,
        MKV_7,
    },
    {
        SDLK_8,
        MKV_8,
    },
    {
        SDLK_9,
        MKV_9,
    },
    {
        SDLK_SEMICOLON,
        MKV_SEMI,
    },
    {
        SDLK_EQUALS,
        MKV_EQUAL,
    },
    {
        SDLK_KP0,
        MKV_NUM0,
    },
    {
        SDLK_KP1,
        MKV_NUM1,
    },
    {
        SDLK_KP2,
        MKV_NUM2,
    },
    {
        SDLK_KP3,
        MKV_NUM3,
    },
    {
        SDLK_KP4,
        MKV_NUM4,
    },
    {
        SDLK_KP5,
        MKV_NUM5,
    },
    {
        SDLK_KP6,
        MKV_NUM6,
    },
    {
        SDLK_KP7,
        MKV_NUM7,
    },
    {
        SDLK_KP8,
        MKV_NUM8,
    },
    {
        SDLK_KP9,
        MKV_NUM9,
    },
    {
        SDLK_KP_PERIOD,
        MKV_NUMPOINT,
    },
    {
        SDLK_KP_DIVIDE,
        MKV_NUMDIVIDE,
    },
    {
        SDLK_KP_MULTIPLY,
        MKV_NUMMULTIPLY,
    },
    {
        SDLK_KP_MINUS,
        MKV_NUMMINUS,
    },
    {
        SDLK_KP_PLUS,
        MKV_NUMPLUS,
    },
    {
        SDLK_KP_ENTER,
        MKV_NUMENTER,
    },
    {
        SDLK_LEFTBRACKET,
        MKV_LEFTBRACKET,
    },
    {
        SDLK_BACKSLASH,
        MKV_BACKSLASH,
    },
    {
        SDLK_RIGHTBRACKET,
        MKV_RIGHTBRACKET,
    },
    {
        SDLK_BACKQUOTE,
        MKV_BACKTICK,
    },
    {
        SDLK_a,
        MKV_a,
    },
    {
        SDLK_b,
        MKV_b,
    },
    {
        SDLK_c,
        MKV_c,
    },
    {
        SDLK_d,
        MKV_d,
    },
    {
        SDLK_e,
        MKV_e,
    },
    {
        SDLK_f,
        MKV_f,
    },
    {
        SDLK_g,
        MKV_g,
    },
    {
        SDLK_h,
        MKV_h,
    },
    {
        SDLK_i,
        MKV_i,
    },
    {
        SDLK_j,
        MKV_j,
    },
    {
        SDLK_k,
        MKV_k,
    },
    {
        SDLK_l,
        MKV_l,
    },
    {
        SDLK_m,
        MKV_m,
    },
    {
        SDLK_n,
        MKV_n,
    },
    {
        SDLK_o,
        MKV_o,
    },
    {
        SDLK_p,
        MKV_p,
    },
    {
        SDLK_q,
        MKV_q,
    },
    {
        SDLK_r,
        MKV_r,
    },
    {
        SDLK_s,
        MKV_s,
    },
    {
        SDLK_t,
        MKV_t,
    },
    {
        SDLK_u,
        MKV_u,
    },
    {
        SDLK_v,
        MKV_v,
    },
    {
        SDLK_w,
        MKV_w,
    },
    {
        SDLK_x,
        MKV_x,
    },
    {
        SDLK_y,
        MKV_y,
    },
    {
        SDLK_z,
        MKV_z,
    },
    {
        SDLK_DELETE,
        MKV_DELFORWARD,
    },
    {
        SDLK_F1,
        MKV_F1,
    },
    {
        SDLK_F2,
        MKV_F2,
    },
    {
        SDLK_F3,
        MKV_F3,
    },
    {
        SDLK_F4,
        MKV_F4,
    },
    {
        SDLK_F5,
        MKV_F5,
    },
    {
        SDLK_F6,
        MKV_F6,
    },
    {
        SDLK_F7,
        MKV_F7,
    },
    {
        SDLK_F8,
        MKV_F8,
    },
    {
        SDLK_F9,
        MKV_F9,
    },
    {
        SDLK_F10,
        MKV_F10,
    },
    {
        SDLK_F11,
        MKV_F11,
    },
    {
        SDLK_F12,
        MKV_F12,
    },
    {
        SDLK_F13,
        MKV_F13,
    },
    {
        SDLK_F14,
        MKV_F14,
    },
    {
        SDLK_F15,
        MKV_F15,
    },
    {
        SDLK_PAUSE,
        MKV_PAUSE,
    },
    {
        SDLK_NUMLOCK,
        MKV_NUMCLEAR,
    },
    {
        SDLK_UP,
        MKV_UPARROW,
    },
    {
        SDLK_DOWN,
        MKV_DOWNARROW,
    },
    {
        SDLK_RIGHT,
        MKV_RIGHTARROW,
    },
    {
        SDLK_LEFT,
        MKV_LEFTARROW,
    },
    {
        SDLK_INSERT,
        MKV_HELP,
    },
    {
        SDLK_HOME,
        MKV_HOME,
    },
    {
        SDLK_END,
        MKV_END,
    },
    {
        SDLK_PAGEUP,
        MKV_PAGEUP,
    },
    {
        SDLK_PAGEDOWN,
        MKV_PAGEDOWN,
    },
    {
        SDLK_CAPSLOCK,
        MKV_CAPS,
    },
    {
        SDLK_SCROLLOCK,
        MKV_SCROLL_LOCK,
    },
    {
        SDLK_RSHIFT,
        MKV_RIGHTSHIFT,
    },
    {
        SDLK_LSHIFT,
        MKV_LEFTSHIFT,
    },
    {
        SDLK_RCTRL,
        MKV_RIGHTCNTL,
    },
    {
        SDLK_LCTRL,
        MKV_LEFTCNTL,
    },
    {
        SDLK_RALT,
        MKV_RIGHTOPTION,
    },
    {
        SDLK_LALT,
        MKV_CLOVER,
    },
    {
        SDLK_RMETA,
        MKV_RIGHTOPTION,
    },
    {
        SDLK_LMETA,
        MKV_CLOVER,
    },
    {
        SDLK_HELP,
        MKV_HELP,
    },
    {
        SDLK_PRINT,
        MKV_PRINT_SCREEN,
    },
    {
        SDLK_SYSREQ,
        NOTAKEY,
    },
    {
        SDLK_MENU,
        NOTAKEY,
    },
    {
        SDLK_BREAK,
        NOTAKEY,
    },
};

PRIVATE void init_sdlk_to_mkv(void)
{
    static boolean_t been_here = FALSE;

    if (!been_here) {
        unsigned int i;

        for (i = 0; i < NELEM(sdlk_to_mkv); ++i)
            sdlk_to_mkv[i] = NOTAKEY;

        for (i = 0; i < NELEM(map); ++i) {
            SDLKey sdlk;
            unsigned char mkv;

            sdlk = map[i].sdlk;
            mkv = map[i].mkv;
            sdlk_to_mkv[sdlk] = mkv;
        }
        been_here = TRUE;
    }
}

#endif

#include "sdlevents.h"
#include "sdlscrap.h"
#include "sdlquit.h"
#include "syswm_map.h"

static unsigned char sdl_mac_virtual_key(const SDL_keysym* key)
{
    unsigned char mapped;
    init_sdlk_to_mkv();
    if (use_scan_codes) {
        if (key->scancode >= NELEM(ibm_virt_to_mac_virt))
            return 0xff;
        mapped = ibm_virt_to_mac_virt[key->scancode];
    }
    else {
        if ((unsigned)key->sym >= NELEM(sdlk_to_mkv))
            return 0xff;
        mapped = sdlk_to_mkv[key->sym];
    }
    /* The classic keyboard bitmap and KCHR tables have 128 virtual keys.
     Media keys and unmapped host keys are left to Windows. */
    return mapped < 128 ? mapped : 0xff;
}

/* The current state of the keyboard modifiers */
static uint16 keymod = btnState;
static uint16 right_button_keymod = 0;
static unsigned held_mouse_buttons;
static Point mouseloc; /* To save mouse location at interrupt */

static int modifier_p(unsigned char virt, uint16* modstore)
{
    /* Note: shift and control can be cleared if right* and left* are pressed */
    switch (virt) {
    case MKV_LEFTSHIFT:
    case MKV_RIGHTSHIFT:
        *modstore = shiftKey;
        break;
    case MKV_CAPS:
        *modstore = alphaLock;
        break;
    case MKV_LEFTCNTL:
    case MKV_RIGHTCNTL:
        *modstore = ControlKey;
        break;
    case MKV_CLOVER:
        *modstore = cmdKey;
        break;
    case MKV_LEFTOPTION:
    case MKV_RIGHTOPTION:
        *modstore = optionKey;
        break;
    default:
        *modstore = 0;
        return 0;
    }
    return 1;
}

/* Dialog input belongs to the host. Reconcile held keys without posting
   dialog keystrokes into the guest event queue. */
void sdl_preferences_input_reset(void)
{
    int count, i;
    Uint8* keys = SDL_GetKeyState(&count);
    unsigned buttons = SDL_GetMouseState(NULL, NULL);
    right_button_keymod = 0;
    held_mouse_buttons = buttons & SDL_BUTTON(SDL_BUTTON_LEFT);
    if ((buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) &&
        ROMlib_right_button_modifier != (optionKey | keyDownMask | keyUpMask)) {
        held_mouse_buttons |= SDL_BUTTON(SDL_BUTTON_RIGHT);
        right_button_keymod = ROMlib_right_button_modifier;
    }
    keymod = held_mouse_buttons ? 0 : btnState;
    memset(KeyMap, 0, sizeof_KeyMap);
    for (i = 0; i < count; ++i)
        if (keys[i]) {
            SDL_keysym key = {0};
            uint16 modifier;
            unsigned char virt;
            key.sym = i;
            virt = sdl_mac_virtual_key(&key);
            if (virt == 0xff)
                continue;
            virt = ROMlib_right_to_left_key_map(virt);
            if (modifier_p(virt, &modifier))
                keymod |= modifier;
            ROMlib_zapmap(virt, 1);
        }
}

syn68k_addr_t handle_sdl_mouse(syn68k_addr_t interrupt_addr, void* unused)
{
    MouseLocation = mouseloc;
    adb_apeiron_hack(FALSE);
    return (MAGIC_RTE_ADDRESS);
}

static boolean_t presentation_active = TRUE;
static boolean_t resume_delivered;
static boolean_t presentation_waiting;

void sdl_resume_event_delivered(void)
{
    if (presentation_active)
        resume_delivered = TRUE;
}

void sdl_finish_resume(void)
{
    if (resume_delivered && presentation_active) {
        resume_delivered = FALSE;
        presentation_waiting = FALSE;
        sdl_hold_presentation(FALSE);
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    }
}

static void sdl_present_for_input(void)
{
    /* A modal loop may exclude osEvt, leaving its queued resume unread.
     Once the user interacts with the foreground application, its live
     framebuffer must show the resulting changes instead of a held snapshot. */
    if (presentation_active && presentation_waiting) {
        resume_delivered = FALSE;
        presentation_waiting = FALSE;
        sdl_hold_presentation(FALSE);
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    }
}

/* Windows may discard the presented surface while another application owns
   the foreground, without producing a separate SDL_VIDEOEXPOSE event. Restore
   the shadow framebuffer on focus/visibility gain even if the guest is idle. */
static void sdl_handle_activation(const SDL_ActiveEvent* event)
{
    if (ROMlib_desk_host_active) {
        /* Standalone accessories and cdev panels are hosted by Desk Manager,
         not a suspend/resume-aware application. Their host must keep drawing
         without waiting for an osEvt acknowledgement from the driver. */
        if (event->state & SDL_APPINPUTFOCUS)
            presentation_active = !!event->gain;
        presentation_waiting = resume_delivered = FALSE;
        sdl_hold_presentation(FALSE);
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
        return;
    }
    if (event->state & SDL_APPINPUTFOCUS) {
        if (!event->gain) {
            /* Upload the final foreground frame before the guest hides it. */
            vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
            sdl_hold_presentation(TRUE);
            presentation_active = FALSE;
            presentation_waiting = TRUE;
            resume_delivered = FALSE;
            held_mouse_buttons = right_button_keymod = 0;
            keymod |= btnState;
            ROMlib_mods = keymod;
            sendsuspendevent();
        }
        else {
            presentation_active = TRUE;
            resume_delivered = FALSE;
            if (!we_lost_clipboard())
                sendresumeevent(FALSE);
            else {
                ZeroScrap();
                sendresumeevent(TRUE);
            }
            if (!(size_info.size_flags & SZacceptSuspendResumeEvents) || (ROMlib_options & ROMLIB_NOSUSPEND_BIT)) {
                presentation_waiting = FALSE;
                sdl_hold_presentation(FALSE);
            }
        }
    }
    if (event->gain && (event->state & (SDL_APPINPUTFOCUS | SDL_APPACTIVE)))
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
}

syn68k_addr_t handle_sdl_events(syn68k_addr_t interrupt_addr, void* unused)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_VIDEORESIZE:
            if (!ROMlib_fullscreen_p && !ROMlib_fixed_window_p) {
                pending_width =
                    MAX(VDRIVER_MIN_SCREEN_WIDTH,
                        MIN(ROMlib_scale_guest(event.resize.w, ROMlib_sdl_display_scale()), SDL_MAX_SCREEN_WIDTH));
                pending_height =
                    MAX(VDRIVER_MIN_SCREEN_HEIGHT,
                        MIN(ROMlib_scale_guest(event.resize.h, ROMlib_sdl_display_scale()), SDL_MAX_SCREEN_HEIGHT));
            }
            break;

        case SDL_VIDEOEXPOSE:
            vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
            break;

        case SDL_ACTIVEEVENT:
            sdl_handle_activation(&event.active);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            sdl_present_for_input();
            boolean_t down_p;
            unsigned previous_buttons = held_mouse_buttons;
            int32 when;
            Point where;

            where.h = event.button.x;
            where.v = event.button.y;
            {
                int x = where.h, y = where.v;
                ROMlib_sdl_guest_point(&x, &y);
                where.h = x;
                where.v = y;
            }
            /* SDL 1.2 represents wheel movement as button 4/5 pulses.
               Releases and auxiliary buttons are never guest mouse clicks. */
            if (event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN) {
                if (event.type == SDL_MOUSEBUTTONDOWN && !held_mouse_buttons)
                    ROMlib_queue_mouse_wheel(where, event.button.button == SDL_BUTTON_WHEELUP ? -1 : 1,
                                             (keymod & shiftKey) != 0);
                break;
            }
            if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT)
                break;

            uint16 click_modifier = right_button_keymod;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                if (ROMlib_right_button_modifier != (optionKey | keyDownMask | keyUpMask)) {
                    if (event.type == SDL_MOUSEBUTTONDOWN)
                        right_button_keymod |= ROMlib_right_button_modifier;
                    else
                        right_button_keymod &= ~ROMlib_right_button_modifier;
                    click_modifier |= ROMlib_right_button_modifier;
                }
                else {
                    /* rewrite the event as though it were the option
		       key rather than a mouse click */
                    if (event.type == SDL_MOUSEBUTTONDOWN) {
                        event.type = SDL_KEYDOWN;
                        event.key.state = SDL_PRESSED;
                    }
                    else {
                        event.type = SDL_KEYUP;
                        event.key.state = 0;
                    }
                    event.key.keysym.scancode = 0x64;
                    event.key.keysym.sym = SDLK_RALT;
                    /*-->*/ goto key_down_or_key_up;
                }
            }

            down_p = (event.button.state == SDL_PRESSED);
            if (down_p)
                held_mouse_buttons |= SDL_BUTTON(event.button.button);
            else
                held_mouse_buttons &= ~SDL_BUTTON(event.button.button);
            if (held_mouse_buttons)
                keymod &= ~btnState;
            else
                keymod |= btnState;
            if (!!previous_buttons == !!held_mouse_buttons) {
                ROMlib_mods = keymod | right_button_keymod;
                break;
            }
            when = TickCount();
            ROMlib_PPostEvent(down_p ? mouseDown : mouseUp, 0, (HIDDEN_EvQElPtr*)0, when, where,
                              keymod | click_modifier);
            ROMlib_mods = keymod | right_button_keymod;
            adb_apeiron_hack(FALSE);
        } break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
        key_down_or_key_up: {
            sdl_present_for_input();
            boolean_t down_p;
            unsigned char mkvkey;
            uint16 mod;
            LONGINT keywhat;
            int32 when;
            Point where;

            down_p = (event.key.state == SDL_PRESSED);

            mkvkey = sdl_mac_virtual_key(&event.key.keysym);
            if (mkvkey == 0xff)
                break;
            mkvkey = ROMlib_right_to_left_key_map(mkvkey);
            if (modifier_p(mkvkey, &mod)) {
                if (down_p)
                    keymod |= mod;
                else
                    keymod &= ~mod;
            }
            when = TickCount();
            where.h = CW(MouseLocation.h);
            where.v = CW(MouseLocation.v);
            keywhat = ROMlib_xlate(mkvkey, keymod, down_p);
            post_keytrans_key_events(down_p ? keyDown : keyUp, keywhat, when, where, keymod | right_button_keymod,
                                     mkvkey);
        } break;

        case SDL_QUIT: {
            /* The system error dialog does not dispatch guest quit events. */
            if (ROMlib_system_error_request_close())
                break;
            if (ROMlib_desk_host_active) {
                ROMlib_desk_host_close = TRUE;
                break;
            }
            if (sdl_close_is_quit())
                ROMlib_send_quit();
            else {
                ROMlib_exit = 1;
                ExitToShell();
            }
        } break;
        }
    }
    return (MAGIC_RTE_ADDRESS);
}

/* This function runs in a separate thread (usually) */
int sdl_event_interrupt(const SDL_Event* event)
{
    if (event->type == SDL_MOUSEMOTION) {
        int x = event->motion.x, y = event->motion.y;
        ROMlib_sdl_guest_point(&x, &y);
        mouseloc.h = CW(x);
        mouseloc.v = CW(y);
        cpu_state.interrupt_pending[M68K_MOUSE_MOVED_PRIORITY] = 1;
        cpu_state.interrupt_status_changed = INTERRUPT_STATUS_CHANGED;
        return (0); /* Drop the event */
    }
    else if (event->type == SDL_QUIT) {
        /* Query whether or not we should quit */
        if (!ROMlib_desk_host_active && !sdl_really_quit())
            return (0);
    }
    else if (event->type == SDL_SYSWMEVENT) {
        /* Pass it to a system-specific event handler */
        return (sdl_syswm_event(event));
    }

    /* All other events go here */
    cpu_state.interrupt_pending[M68K_EVENT_PRIORITY] = 1;
    cpu_state.interrupt_status_changed = INTERRUPT_STATUS_CHANGED;
    return (1);
}

PUBLIC SDL_cond* ROMlib_shouldbeawake_cond = NULL;
PUBLIC SDL_mutex* ROMlib_shouldbeawake_mutex = NULL;

void sdl_events_init(void)
{
    syn68k_addr_t mouse_callback;
    syn68k_addr_t event_callback;

    ROMlib_shouldbeawake_cond = SDL_CreateCond();
    ROMlib_shouldbeawake_mutex = SDL_CreateMutex();

    /* hook into syn68k synchronous interrupts */
    mouse_callback = callback_install(handle_sdl_mouse, NULL);
    *(syn68k_addr_t*)SYN68K_TO_US(M68K_MOUSE_MOVED_VECTOR * 4) = CL(mouse_callback);
    event_callback = callback_install(handle_sdl_events, NULL);
    *(syn68k_addr_t*)SYN68K_TO_US(M68K_EVENT_VECTOR * 4) = CL(event_callback);

    /* then set up a filter that triggers the event interrupt */
    SDL_SetEventFilter(sdl_event_interrupt);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
}
