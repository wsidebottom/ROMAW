/*+===================================================================
  File:      sdlwin.c

  Summary:   Implements sdlwin support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_sdl_desk_title, ROMlib_sdl_display_scale, ROMlib_sdl_guest_point, ROMlib_sdl_select_application,
             ROMlib_sdl_software_presentation,
             host_flush_shadow_screen, sdl_hold_presentation, vdriver_acceptable_mode_p, vdriver_flush_display,
             vdriver_get_colors, vdriver_init, vdriver_opt_register, vdriver_set_colors, vdriver_set_mode,
             vdriver_shutdown, vdriver_update_screen, vdriver_update_screen_rects

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/srcblt.h"
#include "rsys/refresh.h"
#include "rsys/mman.h"
#include "rsys/sdlpixels.h"
#include "rsys/flags.h"
#include "rsys/notmac.h"
#include "WindowMgr.h"
#include "rsys/wind.h"
#include "rsys/desk.h"
#include "rsys/layers.h"
#include "rsys/region.h"
#include "rsys/options.h"

#if defined(CYGWIN32)
#include "win_screen.h"
#endif

#include "SDL/SDL.h"

#include "sdlevents.h"
#include "syswm_map.h"
#include "windowprefs.h"
#include "sdlgpu.h"

/* This is our video display */
static SDL_Surface* screen;
static boolean_t presentation_held;
static SDL_Color pending_colors[256];
static unsigned char pending_color_valid[256];
static unsigned software_presentation;
static boolean_t gpu_surface_stale;

/* Overlays and held frames need a current CPU surface before drawing into it. */
void ROMlib_sdl_software_presentation(int hold)
{
#if !defined(ROMAW_NO_GPU)
    if (hold) {
        ++software_presentation;
        sdl_gpu_reset();
        if (gpu_surface_stale && screen)
            vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    }
    else if (software_presentation)
        --software_presentation;
#endif
}

/* Keep the already uploaded surface, including its palette, while the guest
   hides/recreates its windows for classic suspend/resume notifications. */
void sdl_hold_presentation(int hold)
{
    if (!screen || presentation_held == hold)
        return;
    ROMlib_sdl_software_presentation(hold);
    presentation_held = hold;
    if (!hold)
        for (int first = 0; first < 256;) {
            if (!pending_color_valid[first]) {
                ++first;
                continue;
            }
            int end = first + 1;
            while (end < 256 && pending_color_valid[end])
                ++end;
            SDL_SetColors(screen, pending_colors + first, first, end - first);
            memset(pending_color_valid + first, 0, end - first);
            first = end;
        }
}

static int view_left, view_top;
static int view_width, view_height, display_scale = 100;

int ROMlib_sdl_display_scale(void)
{
    return display_scale;
}

static int title_left, title_top, title_right, title_bottom;
static uint64_t desk_shape_key;

static void desk_shape(void)
{
    uint64_t key = 14695981039346656037ULL;
    RgnHandle region;
    SDL_Rect* rects = NULL;
    size_t count = 0, capacity = 0;
    int x, y;
    WindowPeek w;
    /* Hash geometry rather than the framebuffer, so animation does not rebuild
     the native shape. Include every visible auxiliary/modal window. */
    for (w = ROMlib_window_first_all(); w; w = ROMlib_window_next_all(w))
        if (WINDOW_VISIBLE(w) && WINDOW_STRUCT_REGION(w)) {
            RgnHandle r = WINDOW_STRUCT_REGION(w);
            const unsigned char* bytes = (const unsigned char*)STARH(r);
            size_t n = RGN_SIZE(r), i;
            for (i = 0; i < n; ++i)
                key = (key ^ bytes[i]) * 1099511628211ULL;
        }
    key ^= (uint64_t)(unsigned)view_left << 32 | (unsigned)view_top;
    key ^= display_scale;
    if (key == desk_shape_key)
        return;
    region = C_NewRgn();
    if (!region)
        return;
    for (w = ROMlib_window_first_all(); w; w = ROMlib_window_next_all(w))
        if (WINDOW_VISIBLE(w) && WINDOW_STRUCT_REGION(w))
            C_UnionRgn(region, WINDOW_STRUCT_REGION(w), region);
    for (y = 0; y < view_height; ++y)
        for (x = 0; x < view_width;) {
            Point p = {y + view_top, x + view_left};
            int start = x;
            if (!C_PtInRgn(p, region)) {
                ++x;
                continue;
            }
            do
                p.h = ++x + view_left;
            while (x < view_width && C_PtInRgn(p, region));
            if (count == capacity) {
                size_t next = capacity ? capacity * 2 : 128;
                SDL_Rect* grown = realloc(rects, next * sizeof *rects);
                if (!grown)
                    goto done;
                rects = grown;
                capacity = next;
            }
            int left = ROMlib_scale_edge(start, display_scale);
            int top = ROMlib_scale_edge(y, display_scale);
            int right = ROMlib_scale_edge(x, display_scale);
            int bottom = ROMlib_scale_edge(y + 1, display_scale);
            if (right > left && bottom > top)
                rects[count++] = (SDL_Rect){left, top, right - left, bottom - top};
        }
    if (count && count <= INT_MAX && ROMlib_sdl_desk_shape(rects, count))
        desk_shape_key = key;
done:
    free(rects);
    C_DisposeRgn(region);
}

int ROMlib_sdl_desk_title(int x, int y)
{
    x = ROMlib_scale_guest(x, display_scale);
    y = ROMlib_scale_guest(y, display_scale);
    return ROMlib_desk_host_active && x >= title_left && x < title_right && y >= title_top && y < title_bottom;
}

void ROMlib_sdl_guest_point(int* x, int* y)
{
    *x = ROMlib_scale_guest(*x, display_scale) + view_left;
    *y = ROMlib_scale_guest(*y, display_scale) + view_top;
}

/* Only presentation changes: Macintosh ports retain their desktop coordinates.
   Include auxiliary dialogs as well as the driver's window. */
static int desk_view(void)
{
    int left = vdriver_width, top = vdriver_height, right = 0, bottom = 0;
    WindowPeek front = NULL;
    if (!ROMlib_desk_host_active || !screen) {
        if (desk_shape_key && screen && ROMlib_sdl_desk_shape(NULL, 0))
            desk_shape_key = 0;
        return 0;
    }
    for (WindowPeek w = ROMlib_window_first_all(); w; w = ROMlib_window_next_all(w))
        if (WINDOW_VISIBLE(w) && WINDOW_STRUCT_REGION(w)) {
            if (!front)
                front = w;
            Rect bounds = HxX(WINDOW_STRUCT_REGION(w), rgnBBox);
            left = MIN(left, CW(bounds.left));
            top = MIN(top, CW(bounds.top));
            right = MAX(right, CW(bounds.right));
            bottom = MAX(bottom, CW(bounds.bottom));
        }
    left = MAX(0, left);
    top = MAX(0, top);
    right = MIN(vdriver_width, right);
    bottom = MIN(vdriver_height, bottom);
    if (right <= left || bottom <= top)
        return 0;
    if (front) {
        Rect structure = HxX(WINDOW_STRUCT_REGION(front), rgnBBox);
        Rect content = HxX(WINDOW_CONT_REGION(front), rgnBBox);
        title_left = CW(structure.left) - left + 24;
        title_right = CW(structure.right) - left;
        title_top = CW(structure.top) - top;
        title_bottom = CW(content.top) - top;
    }
    int changed = left != view_left || top != view_top;
    if (view_width != right - left || view_height != bottom - top || !(screen->flags & SDL_NOFRAME)) {
        SDL_Color colors[256];
        int count = MIN(256, screen->format->palette->ncolors);
        memcpy(colors, screen->format->palette->colors, count * sizeof *colors);
        sdl_gpu_reset();
        SDL_Surface* surface = SDL_SetVideoMode(ROMlib_scale_edge(right - left, display_scale),
                                                ROMlib_scale_edge(bottom - top, display_scale), 8,
                                                SDL_SWSURFACE | SDL_HWPALETTE | SDL_NOFRAME);
        if (!surface)
            return 0;
        screen = surface;
        view_width = right - left;
        view_height = bottom - top;
        SDL_SetColors(screen, colors, 0, count);
        sdl_syswm_init();
        desk_shape_key = 0;
        changed = 1;
    }
    view_left = left;
    view_top = top;
    desk_shape();
    return changed;
}

/* These variables are required by the vdriver interface. */
uint8* vdriver_fbuf;
int vdriver_row_bytes;
int vdriver_width = 0;
int vdriver_height = 0;
int vdriver_bpp = 8, vdriver_log2_bpp;
int vdriver_max_bpp, vdriver_log2_max_bpp;
vdriver_modes_t* vdriver_mode_list;

/* Currently a private colormap is the default */
static int video_flags = (SDL_SWSURFACE | SDL_HWPALETTE | SDL_RESIZABLE);
extern unsigned char* vdriver_shadow_fbuf;

/* The modes structure is just checked for error messages, fake it here */
static vdriver_modes_t sdl_impotent_modes = {0, 0};

/* SDL vdriver implementation */

void vdriver_opt_register(void)
{
}

PUBLIC boolean_t ROMlib_fullscreen_p = FALSE;
PUBLIC boolean_t ROMlib_fixed_window_p = FALSE;

boolean_t vdriver_init(int _max_width, int _max_height, int _max_bpp, boolean_t fixed_p, int* argc, char* argv[])
{
    int flags;

#if defined(_WIN32)
    {
        char* filename = malloc(strlen(ROMlib_userdir) + sizeof "\\ROMAW.prefs");
        if (filename) {
            sprintf(filename, "%s\\ROMAW.prefs", ROMlib_userdir);
            window_prefs_init(filename, VDRIVER_MIN_SCREEN_WIDTH, VDRIVER_MIN_SCREEN_HEIGHT, SDL_MAX_SCREEN_WIDTH,
                              SDL_MAX_SCREEN_HEIGHT);
            free(filename);
        }
    }
#endif

    flags = SDL_INIT_VIDEO;

    ROMlib_sdl_startup_begin();
    if (SDL_Init(flags) < 0)
        return (FALSE);
    sdl_events_init();

    vdriver_max_bpp = 8;
    vdriver_log2_max_bpp = ROMlib_log2[vdriver_max_bpp];
    vdriver_mode_list = &sdl_impotent_modes;

    /* Try for fullscreen on platforms that support it */
    if (getenv("SDL_FULLSCREEN") != NULL || ROMlib_fullscreen_p) {
        ROMlib_fullscreen_p = TRUE;
        video_flags = (video_flags | SDL_FULLSCREEN) & ~SDL_RESIZABLE;
    }

    /* Keep the title bar but remove resizing/maximizing affordances. */
    if (ROMlib_fixed_window_p)
        video_flags &= ~SDL_RESIZABLE;

    /* Clean up on exit */
    atexit(vdriver_shutdown);
    return (TRUE);
}

void ROMlib_sdl_select_application(const unsigned char* name, unsigned long type, unsigned long creator)
{
    int width = vdriver_width, height = vdriver_height;

#if defined(_WIN32)
    ROMlib_sdl_save_window_position();
    window_prefs_select(name, type, creator, &width, &height);
    ROMlib_sdl_queue_window_position();
#endif
    if (flag_width && flag_height) {
        width = flag_width;
        height = flag_height;
    }
    if (!ROMlib_fullscreen_p)
        sdl_queue_window_size(width, height);
}

boolean_t vdriver_acceptable_mode_p(int width, int height, int bpp, boolean_t grayscale_p, boolean_t exact_match_p)
{
    boolean_t retval;

    if (!width)
        width = vdriver_width;

    if (!height)
        height = vdriver_height;

    if (ROMlib_fixed_window_p && (width != flag_width || height != flag_height))
        return FALSE;

    if (!bpp)
        bpp = vdriver_bpp;

    if (width < VDRIVER_MIN_SCREEN_WIDTH || height < VDRIVER_MIN_SCREEN_HEIGHT || width > SDL_MAX_SCREEN_WIDTH ||
        height > SDL_MAX_SCREEN_HEIGHT)
        retval = FALSE;
#if defined(SYN68K)
    else if (syn68k_address_mask == 0x00FFFFFFUL && vdriver_fbuf &&
             (uint64)US_TO_SYN68K_RAW(vdriver_fbuf) + (uint64)SDL_MAX_SCREEN_WIDTH * (height - 1) + width > 0x1000000)
        retval = FALSE;
#endif
    else if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8)
        retval = FALSE;
    else if (!SDL_VideoModeOK(width, height, 8, video_flags))
        retval = FALSE;
    else if (grayscale_p != vdriver_grayscale_p)
        retval = FALSE;
    else
        retval = bpp <= 8;

    return retval;
}

boolean_t vdriver_set_mode(int width, int height, int bpp, boolean_t grayscale_p)
{
    SDL_Surface* new_screen;
    boolean_t initial_mode_p = screen == NULL;
    /* Keep depth changes and Macintosh display requests at the locked size. */
    if (ROMlib_fixed_window_p) {
        width = flag_width;
        height = flag_height;
    }
    /* Massage the width and height parameters */
    if (width == 0) {
        width = vdriver_width;
        if (width == 0) {
            width = VDRIVER_DEFAULT_SCREEN_WIDTH;
#if 1
#warning "TODO: fixme so we can use fullscreen in Mac OS X"
#else
            if (ROMlib_fullscreen_p)
                width = MAX(width, os_current_screen_width());
#endif
        }
    }

    if (height == 0) {
        height = vdriver_height;
        if (height == 0) {
            height = VDRIVER_DEFAULT_SCREEN_HEIGHT;
#if 1
#warning "TODO: fixme so we can use fullscreen in Mac OS X"
#else
            if (ROMlib_fullscreen_p)
                height = MAX(height, os_current_screen_height());
#endif
        }
    }

    if (bpp == 0)
        bpp = vdriver_bpp;

    if (!vdriver_acceptable_mode_p(width, height, bpp, grayscale_p, FALSE))
        return (FALSE);

    /* Set the video mode */
    /* Keep Macintosh base addresses and row strides valid during host resizes.
   * SDL owns a separate presentation surface, uploaded by the update routines.
   */
    if (vdriver_fbuf == NULL) {
        vdriver_fbuf = ROMlib_guest_framebuffer;
        if (!vdriver_fbuf)
            vdriver_fbuf = calloc(SDL_MAX_SCREEN_WIDTH * SDL_MAX_SCREEN_HEIGHT + 64, 1);
        if (vdriver_fbuf == NULL)
            return FALSE;
    }
    /* SDL presents byte-sized palette indices; Macintosh pixels may be packed. */
    int scale = ROMlib_display_scale;
    int host_width = ROMlib_scale_edge(width, scale);
    int host_height = ROMlib_scale_edge(height, scale);
    if (scale < 25 || scale > 1600 || host_width > 32767 || host_height > 32767)
        return FALSE;
    int old_scale = display_scale;
    SDL_Color palette[256];
    int colors = screen && screen->format->palette ? MIN(256, screen->format->palette->ncolors) : 0;
    if (colors)
        memcpy(palette, screen->format->palette->colors, colors * sizeof *palette);
    display_scale = scale;
    sdl_gpu_reset();
    new_screen = SDL_SetVideoMode(host_width, host_height, 8, video_flags);
    if (new_screen == NULL) {
        display_scale = old_scale;
        return FALSE;
    }
    screen = new_screen;
    /* A replacement surface cannot retain the old frame. Resume presentation
     rather than leaving a resized surface permanently frozen. */
    if (colors)
        SDL_SetColors(screen, palette, 0, colors);
    sdl_hold_presentation(FALSE);

    /* Fill the vdriver globals */
    vdriver_width = view_width = width;
    vdriver_height = view_height = height;
    view_left = view_top = 0;
    desk_shape_key = 0;
    vdriver_bpp = bpp;
    vdriver_log2_bpp = ROMlib_log2[vdriver_bpp];
    vdriver_row_bytes = (SDL_MAX_SCREEN_WIDTH * bpp + 7) / 8;
    free(vdriver_shadow_fbuf);
    vdriver_shadow_fbuf = NULL;

    sdl_syswm_init();
    ROMlib_sdl_refresh_cursor();

#if defined(CYGWIN32)
    if (initial_mode_p)
        ROMlib_recenter_window();
#endif

    return (TRUE);
}

void vdriver_set_colors(int first_color, int num_colors, const ColorSpec* colors)
{
    int i;
    SDL_Color* sdl_cmap;

    sdl_cmap = (SDL_Color*)alloca(num_colors * sizeof(SDL_Color));
    for (i = 0; i < num_colors; ++i) {
        sdl_cmap[i].r = (CW(colors[i].rgb.red) >> 8);
        sdl_cmap[i].g = (CW(colors[i].rgb.green) >> 8);
        sdl_cmap[i].b = (CW(colors[i].rgb.blue) >> 8);
    }
    if (presentation_held && first_color >= 0 && num_colors >= 0 && first_color <= 256 &&
        num_colors <= 256 - first_color) {
        memcpy(pending_colors + first_color, sdl_cmap, num_colors * sizeof *sdl_cmap);
        memset(pending_color_valid + first_color, 1, num_colors);
    }
    else {
        SDL_SetColors(screen, sdl_cmap, first_color, num_colors);
#if !defined(ROMAW_NO_GPU)
        if (gpu_surface_stale && !sdl_gpu_repaint(screen))
            vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
#endif
    }
}

void vdriver_get_colors(int first_color, int num_colors, ColorSpec* colors)
{
    gui_fatal("`!vdriver_fixed_clut_p' and `vdriver_get_colors ()' called");
}

int vdriver_update_screen_rects(int num_rects, const vdriver_rect_t* r, boolean_t cursor_p)
{
    SDL_Rect* rects;
    int i, count;
    vdriver_rect_t full;

    /* A driver/cdev host has no application event loop to acknowledge a queued
     process-resume event. Focus can be lost while its initial SDL window is
     still hidden; retaining that snapshot would prevent the first crop/paint
     and therefore prevent the accessory from ever becoming visible. */
    if (presentation_held && ROMlib_desk_host_active)
        sdl_hold_presentation(FALSE);

    if (presentation_held) {
        /* Expose events must re-present the saved frame, not the guest's hidden
         window state. The SDL surface is separate from vdriver_fbuf. */
        SDL_UpdateRect(screen, 0, 0, 0, 0);
        return 0;
    }
    if (desk_view()) {
        full.left = view_left;
        full.top = view_top;
        full.right = view_left + view_width;
        full.bottom = view_top + view_height;
        r = &full;
        num_rects = 1;
    }

    if (!software_presentation && !ROMlib_desk_host_active) {
        int left = vdriver_width, top = vdriver_height, right = 0, bottom = 0;
        for (i = 0; i < num_rects; ++i) {
            left = MIN(left, r[i].left);
            top = MIN(top, r[i].top);
            right = MAX(right, r[i].right);
            bottom = MAX(bottom, r[i].bottom);
        }
        if (sdl_gpu_present(screen, vdriver_fbuf, vdriver_row_bytes, vdriver_width, vdriver_height, vdriver_bpp,
                            display_scale, left, top, right, bottom)) {
            gpu_surface_stale = TRUE;
            return 0;
        }
    }
    /* SDL has not received GPU frames. Recover the entire surface after a
       device failure, a software overlay, or a switch to shaped presentation. */
    if (gpu_surface_stale) {
        full = (vdriver_rect_t){view_top, view_left, view_top + view_height, view_left + view_width};
        r = &full;
        num_rects = 1;
    }

    rects = (SDL_Rect*)alloca(num_rects * sizeof(SDL_Rect));
    if (SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) < 0)
        return -1;
    for (i = count = 0; i < num_rects; ++i) {
        int top = MAX(view_top, r[i].top), left = MAX(view_left, r[i].left);
        int bottom = MIN(view_top + view_height, r[i].bottom);
        int right = MIN(view_left + view_width, r[i].right);
        int y;

        if (right <= left || bottom <= top)
            continue;
        int host_left = ROMlib_scale_edge(left - view_left, display_scale);
        int host_right = ROMlib_scale_edge(right - view_left, display_scale);
        int host_top = ROMlib_scale_edge(top - view_top, display_scale);
        int host_bottom = ROMlib_scale_edge(bottom - view_top, display_scale);
        for (y = host_top; y < host_bottom; ++y) {
            unsigned char* dest = (uint8*)screen->pixels + y * screen->pitch + host_left;
            const unsigned char* source =
                vdriver_fbuf + (view_top + ROMlib_scale_guest(y, display_scale)) * vdriver_row_bytes;
            if (display_scale == 100)
                ROMlib_sdl_copy_cropped_row(dest, source, left, right, vdriver_bpp);
            else
                ROMlib_sdl_copy_scaled_row(dest, source, host_left, host_right, view_left, vdriver_bpp, display_scale);
        }
        if (host_right > host_left && host_bottom > host_top)
            rects[count++] = (SDL_Rect){host_left, host_top, host_right - host_left, host_bottom - host_top};
    }
    if (SDL_MUSTLOCK(screen))
        SDL_UnlockSurface(screen);
    SDL_UpdateRects(screen, count, rects);
    gpu_surface_stale = FALSE;
    if (ROMlib_desk_host_active && desk_shape_key)
        ROMlib_sdl_startup_ready();
    return (0);
}

int vdriver_update_screen(int top, int left, int bottom, int right, boolean_t cursor_p)
{
    vdriver_rect_t rect;

    if (top < 0)
        top = 0;
    if (left < 0)
        left = 0;

    if (bottom > vdriver_height)
        bottom = vdriver_height;
    if (right > vdriver_width)
        right = vdriver_width;

    rect.left = left;
    rect.right = right;
    rect.top = top;
    rect.bottom = bottom;
    return vdriver_update_screen_rects(1, &rect, cursor_p);
}

void vdriver_flush_display(void)
{
}

void vdriver_shutdown(void)
{
    sdl_gpu_reset();
#if defined(_WIN32)
    if (screen && !ROMlib_fullscreen_p && !(screen->flags & SDL_NOFRAME)) {
        window_prefs_save(vdriver_width, vdriver_height);
        ROMlib_sdl_save_window_position();
    }
#endif
    SDL_Quit();
    if (vdriver_fbuf != ROMlib_guest_framebuffer)
        free(vdriver_fbuf);
    vdriver_fbuf = NULL;
}

/* host functions that should go away */

/* shadow buffer; created on demand */
unsigned char* vdriver_shadow_fbuf = NULL;

void host_flush_shadow_screen(void)
{
    int top_long, left_long, bottom_long, right_long;

    /* Lazily allocate a shadow screen.  We won't be doing refresh that often,
   * so don't waste the memory unless we need it.  Note: memory never reclaimed
   */
    if (vdriver_shadow_fbuf == NULL) {
        vdriver_shadow_fbuf = malloc(vdriver_row_bytes * vdriver_height);
        memcpy(vdriver_shadow_fbuf, vdriver_fbuf, vdriver_row_bytes * vdriver_height);
        vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
    }
    else if (find_changed_rect_and_update_shadow((uint32*)vdriver_fbuf, (uint32*)vdriver_shadow_fbuf,
                                                 (vdriver_row_bytes / sizeof(uint32)), vdriver_height, &top_long,
                                                 &left_long, &bottom_long, &right_long)) {
        vdriver_update_screen(top_long, (left_long * 32) >> vdriver_log2_bpp, bottom_long,
                              (right_long * 32) >> vdriver_log2_bpp, FALSE);
    }
}
