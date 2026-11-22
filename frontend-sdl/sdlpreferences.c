/*+===================================================================
  File:      sdlpreferences.c

  Summary:   Portable SDL configuration editor. The guest is paused while editing.

  Classes:   None.

  Functions: ROMlib_sdl_image_progress_begin, ROMlib_sdl_image_progress_end, ROMlib_sdl_image_progress_pump,
             ROMlib_sdl_load_panel_font, ROMlib_sdl_preferences

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Portable SDL configuration editor. The guest is paused while editing. */
#include "rsys/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/prefpanel.h"
#include "SDL/SDL.h"
#include "sdlevents.h"
#include "FontMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "sdlgpu.h"

static unsigned char panel_glyphs[128][8];

void ROMlib_sdl_load_panel_font(void)
{
    Handle handle = GetResource(TICK("NFNT"), 8);
    if (!handle || !STARH(handle))
        return;
    Size size = GetHandleSize(handle);
    if (size < (Size)sizeof(FontRec))
        return;
    FontRec* font = (FontRec*)STARH(handle);
    unsigned height = (unsigned short)CW(font->fRectHeight);
    unsigned words = (unsigned short)CW(font->rowWords);
    int first = CW(font->firstChar);
    size_t bitmap_bytes = (size_t)words * height * 2;

    if (!height || height > 256 || !words || first < 0 || first > ' ' || CW(font->lastChar) < 126 ||
        (size_t)size < sizeof(FontRec) + bitmap_bytes + (128 - first) * 2)
        return;
    const unsigned char* bitmap = (const unsigned char*)(font + 1);
    const INTEGER* locations = (const INTEGER*)bitmap + words * height;
    int top = CW(font->ascent) - 6;

    memset(panel_glyphs, 0, sizeof panel_glyphs);
    for (int c = ' '; c < 127; ++c) {
        int start = CW(locations[c - first]);
        int width = CW(locations[c - first + 1]) - start;
        if (start < 0 || width < 0 || (unsigned)(start + width) > words * 16)
            continue;
        for (int row = 0; row < 8; ++row) {
            int source_row = top + row;
            if (source_row < 0 || source_row >= (int)height)
                continue;
            for (int col = 0; col < width && col < 8; ++col) {
                int bit = start + col;
                if (bitmap[source_row * words * 2 + bit / 8] & (0x80 >> (bit % 8)))
                    panel_glyphs[c][row] |= 1 << col;
            }
        }
    }
}

#define WIDTH 512
#define HEIGHT 342

typedef struct {
    ROMlib_extra_preferences edited;
    int page, focus, cursor, anchor;
    char error[128];
} preferences_ui;

static int field_page(unsigned field)
{
    if (!ROMlib_preferences_field_label(field))
        return -1;
    return field == 10 || field == 11 ? 2 : field >= 12 ? 0 : 1;
}

static int field_row(unsigned field)
{
    unsigned i;
    int row = 0;
    for (i = 0; i < field; ++i)
        if (field_page(i) == field_page(field))
            ++row;
    return row;
}

static void box(SDL_Surface* surface, int x, int y, int w, int h, int color)
{
    SDL_Rect rect = {x, y, w, h};
    SDL_FillRect(surface, &rect, color);
}

static void text(SDL_Surface* surface, int x, int y, const char* string, int color, int limit)
{
    int row, col;
    for (; *string && limit-- > 0; ++string, x += 8) {
        unsigned c = (unsigned char)*string;
        if (c >= 128)
            c = '?';
        for (row = 0; row < 8; ++row)
            for (col = 0; col < 8; ++col)
                if (panel_glyphs[c][row] & (1 << col))
                    box(surface, x + col, y + row, 1, 1, color);
    }
}

static void button(SDL_Surface* surface, int x, int y, int w, const char* label, int selected)
{
    box(surface, x, y, w, 22, 1);
    box(surface, x + 1, y + 1, w - 2, 20, selected ? 3 : 2);
    text(surface, x + (w - strlen(label) * 8) / 2, y + 7, label, selected ? 0 : 1, w / 8);
}

static void focus_field(preferences_ui* ui, int field, int select)
{
    ui->focus = field;
    ui->cursor = strlen(ui->edited.fields[field]);
    ui->anchor = select ? 0 : ui->cursor;
}

static void render(SDL_Surface* canvas, preferences_ui* ui)
{
    static const char* pages[] = {"General", "Display", "Memory", "Compatibility"};
    unsigned i;
    box(canvas, 0, 0, WIDTH, HEIGHT, 0);
    text(canvas, 12, 9, "ROMAW Preferences", 1, 61);
    for (i = 0; i < 4; ++i)
        button(canvas, 8 + i * 126, 25, 122, pages[i], ui->page == (int)i);
    if (ui->page == 3)
        for (i = 0; ROMlib_preferences_check_label(i); ++i) {
            int y = 54 + i * 10;
            box(canvas, 12, y, 8, 8, 1);
            box(canvas, 13, y + 1, 6, 6, 0);
            if (ROMlib_preferences_check(&ui->edited, i))
                box(canvas, 14, y + 2, 4, 4, 3);
            text(canvas, 28, y, ROMlib_preferences_check_label(i), 1, 59);
            if (ui->focus == 19 + (int)i)
                text(canvas, 2, y, ">", 3, 1);
        }
    for (i = 0; i < 19; ++i)
        if (field_page(i) == ui->page) {
            int y = 56 + field_row(i) * 22;
            const char* value = ui->edited.fields[i];
            int offset = ui->focus == (int)i && ui->cursor > 27 ? ui->cursor - 27 : 0;
            text(canvas, 12, y + 5, ROMlib_preferences_field_label(i), 1, 32);
            box(canvas, 274, y, 226, 19, ui->focus == (int)i ? 3 : 1);
            box(canvas, 275, y + 1, 224, 17, 2);
            if (ui->focus == (int)i && ui->cursor != ui->anchor) {
                int a = MIN(ui->cursor, ui->anchor), b = MAX(ui->cursor, ui->anchor);
                a = MAX(a, offset);
                b = MIN(b, offset + 27);
                if (b > a)
                    box(canvas, 278 + (a - offset) * 8, y + 3, (b - a) * 8, 12, 4);
            }
            text(canvas, 278, y + 5, value + offset, 1, 27);
            if (ui->focus == (int)i)
                box(canvas, 278 + (ui->cursor - offset) * 8, y + 3, 1, 12, 1);
        }
    if (ui->page == 0) {
        static const char* scales[] = {"1x", "1.5x", "2x", "4x", "8x", "Custom"};
        for (unsigned j = 0; j < 6; ++j)
            button(canvas, 8 + j * 82, 218, 78, scales[j], 0);
    }
    if (ui->page == 2) {
        text(canvas, 12, 112, "Bytes or k/m suffix, for example 8m or 512k.", 1, 61);
        text(canvas, 12, 130, "Blank or 0 uses startup defaults.", 1, 61);
        text(canvas, 12, 148, "Memory changes apply on the next launch.", 1, 61);
        text(canvas, 12, 174, "Command-line -syszone, -applzone and -memory", 1, 61);
        text(canvas, 12, 192, "take precedence over saved settings.", 1, 61);
    }
    text(canvas, 12, 289, ui->error[0] ? ui->error : "Tab: next control   Ctrl+A: select all   Esc: cancel",
         ui->error[0] ? 5 : 1, 61);
    button(canvas, 238, 310, 74, "OK", ui->focus == 100);
    button(canvas, 320, 310, 88, "Save", ui->focus == 101);
    button(canvas, 416, 310, 84, "Cancel", ui->focus == 102);
}

static void next_focus(preferences_ui* ui, int backwards)
{
    int items[48], count = 0, index = -1, i;
    if (ui->page == 3)
        for (i = 0; ROMlib_preferences_check_label(i); ++i)
            items[count++] = 19 + i;
    else
        for (i = 0; i < 19; ++i)
            if (field_page(i) == ui->page)
                items[count++] = i;
    items[count++] = 100;
    items[count++] = 101;
    items[count++] = 102;
    for (i = 0; i < count; ++i)
        if (items[i] == ui->focus)
            index = i;
    index = (index + (backwards ? count - 1 : 1)) % count;
    ui->focus = items[index];
    if (ui->focus < 19)
        focus_field(ui, ui->focus, 1);
}

static void edit_key(preferences_ui* ui, SDL_keysym* key)
{
    char* value = ui->edited.fields[ui->focus];
    int length = strlen(value), a, b;
    int selecting = !!(key->mod & KMOD_SHIFT);
    if ((key->mod & (KMOD_CTRL | KMOD_META)) && key->sym == SDLK_a) {
        ui->anchor = 0;
        ui->cursor = length;
        return;
    }
    if (key->sym == SDLK_LEFT || key->sym == SDLK_RIGHT || key->sym == SDLK_HOME || key->sym == SDLK_END) {
        if (key->sym == SDLK_HOME)
            ui->cursor = 0;
        else if (key->sym == SDLK_END)
            ui->cursor = length;
        else
            ui->cursor = MAX(0, MIN(length, ui->cursor + (key->sym == SDLK_LEFT ? -1 : 1)));
        if (!selecting)
            ui->anchor = ui->cursor;
        return;
    }
    a = MIN(ui->cursor, ui->anchor);
    b = MAX(ui->cursor, ui->anchor);
    if (key->sym == SDLK_BACKSPACE && a == b && a)
        --a;
    else if (key->sym == SDLK_DELETE && a == b && b < length)
        ++b;
    else if (key->sym != SDLK_BACKSPACE && key->sym != SDLK_DELETE &&
             (key->unicode < 32 || key->unicode > 126 || (key->mod & (KMOD_CTRL | KMOD_META))))
        return;
    if (a != b)
        memmove(value + a, value + b, length - b + 1);
    length -= b - a;
    ui->cursor = ui->anchor = a;
    if (key->sym != SDLK_BACKSPACE && key->sym != SDLK_DELETE && length < 255) {
        memmove(value + a + 1, value + a, length - a + 1);
        value[a] = key->unicode;
        ui->cursor = ui->anchor = a + 1;
    }
}

/* Do not deliver dialog input or close requests to the Macintosh guest. */
static int preferences_filter(const SDL_Event* event)
{
    (void)event;
    return 1;
}

int ROMlib_sdl_preferences(ROMlib_extra_preferences* prefs, const char* filename)
{
    preferences_ui ui = {*prefs, 0, -1, 0, 0, ""};
    SDL_Surface *screen = SDL_GetVideoSurface(), *canvas;
    SDL_Color saved[256], palette[256] = {{0}};
    SDL_Event event, resize;
    SDL_EventFilter filter;
    int unicode, repeat_delay, repeat_interval, cursor, done = 0, action = 0, have_resize = 0;
    int result = 0, i;
    if (!screen || screen->format->BitsPerPixel != 8 || !screen->format->palette)
        return 0;
    canvas = SDL_CreateRGBSurface(SDL_SWSURFACE, WIDTH, HEIGHT, 8, 0, 0, 0, 0);
    if (!canvas)
        return 0;
    ROMlib_sdl_software_presentation(1);
    memcpy(saved, screen->format->palette->colors, sizeof saved);
    palette[0] = (SDL_Color){238, 240, 243, 0};
    palette[1] = (SDL_Color){24, 30, 40, 0};
    palette[2] = (SDL_Color){255, 255, 255, 0};
    palette[3] = (SDL_Color){34, 90, 170, 0};
    palette[4] = (SDL_Color){190, 215, 245, 0};
    palette[5] = (SDL_Color){160, 20, 20, 0};
    SDL_SetColors(canvas, palette, 0, 256);
    SDL_SetColors(screen, palette, 0, 256);
    filter = SDL_GetEventFilter();
    SDL_SetEventFilter(preferences_filter);
    unicode = SDL_EnableUNICODE(1);
    SDL_GetKeyRepeat(&repeat_delay, &repeat_interval);
    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
    cursor = SDL_ShowCursor(SDL_ENABLE);
    /* Flush shortcut releases and queued input before entering the editor. */
    while (SDL_PollEvent(&event))
        if (event.type == SDL_VIDEORESIZE) {
            resize = event;
            have_resize = 1;
        }
    while (!done) {
        render(canvas, &ui);
        SDL_SoftStretch(canvas, NULL, screen, NULL);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
        if (!SDL_WaitEvent(&event))
            break;
        action = 0;
        if (event.type == SDL_QUIT)
            done = 1;
        else if (event.type == SDL_VIDEORESIZE) {
            resize = event;
            have_resize = 1;
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            int x = event.button.x * WIDTH / screen->w;
            int y = event.button.y * HEIGHT / screen->h;
            if (y >= 25 && y < 47 && x >= 8 && x < 508) {
                ui.page = MIN(3, (x - 8) / 126);
                ui.focus = -1;
            }
            else if (y >= 310 && y < 332) {
                if (x >= 416)
                    done = 1;
                else if (x >= 320)
                    action = 2;
                else if (x >= 238)
                    action = 1;
            }
            else if (ui.page == 0 && y >= 218 && y < 240 && x >= 8 && x < 500) {
                static const char* scales[] = {"1x", "1.5x", "2x", "4x", "8x"};
                int selected = (x - 8) / 82;
                if (selected < 5)
                    snprintf(ui.edited.fields[18], 256, "%s", scales[selected]);
                focus_field(&ui, 18, 1);
            }
            else if (ui.page == 3 && x >= 8 && y >= 54 && y < 284) {
                i = (y - 54) / 10;
                if (ROMlib_preferences_check_label(i)) {
                    ROMlib_preferences_toggle(&ui.edited, i);
                    ui.focus = 19 + i;
                }
            }
            else
                for (i = 0; i < 19; ++i)
                    if (field_page(i) == ui.page && y >= 56 + field_row(i) * 22 && y < 75 + field_row(i) * 22 &&
                        x >= 274 && x < 500) {
                        focus_field(&ui, i, 1);
                        break;
                    }
        }
        else if (event.type == SDL_KEYDOWN) {
            SDL_keysym* key = &event.key.keysym;
            if (key->sym == SDLK_ESCAPE)
                done = 1;
            else if (key->sym == SDLK_TAB)
                next_focus(&ui, key->mod & KMOD_SHIFT);
            else if ((key->mod & (KMOD_CTRL | KMOD_META)) && key->sym == SDLK_PAGEUP) {
                ui.page = (ui.page + 3) % 4;
                ui.focus = -1;
            }
            else if ((key->mod & (KMOD_CTRL | KMOD_META)) && key->sym == SDLK_PAGEDOWN) {
                ui.page = (ui.page + 1) % 4;
                ui.focus = -1;
            }
            else if (key->sym == SDLK_RETURN || key->sym == SDLK_KP_ENTER ||
                     (key->sym == SDLK_SPACE && ui.focus >= 19)) {
                if (ui.focus == 102)
                    done = 1;
                else if (ui.focus == 101)
                    action = 2;
                else if (ui.focus >= 19 && ui.focus < 100)
                    ROMlib_preferences_toggle(&ui.edited, ui.focus - 19);
                else
                    action = 1;
            }
            else if (ui.focus >= 0 && ui.focus < 19)
                edit_key(&ui, key);
        }
        if (action) {
            unsigned bad = 0;
            if (!ROMlib_extra_preferences_valid(&ui.edited, &bad)) {
                ui.page = field_page(bad);
                focus_field(&ui, bad, 1);
                snprintf(ui.error, sizeof ui.error, "Invalid %s", ROMlib_preferences_field_label(bad));
            }
            else {
                SDL_SetColors(screen, saved, 0, 256);
                if (!ROMlib_extra_preferences_apply(&ui.edited))
                    snprintf(ui.error, sizeof ui.error, "Unable to apply settings: out of memory.");
                else if (action == 2 && (!filename || !saveprefvalues((char*)filename, 0, 0)))
                    snprintf(ui.error, sizeof ui.error, "Could not save preferences. Check folder permissions.");
                else {
                    *prefs = ui.edited;
                    result = action;
                    done = 1;
                }
                screen = SDL_GetVideoSurface();
                if (!screen || !screen->format->palette)
                    break;
                memcpy(saved, screen->format->palette->colors, sizeof saved);
                SDL_SetColors(screen, palette, 0, 256);
            }
        }
    }
    SDL_SetColors(screen, saved, 0, 256);
    SDL_FreeSurface(canvas);
    SDL_EnableUNICODE(unicode);
    SDL_EnableKeyRepeat(repeat_delay, repeat_interval);
    SDL_ShowCursor(cursor);
    sdl_preferences_input_reset();
    SDL_SetEventFilter(filter);
    if (have_resize)
        SDL_PushEvent(&resize);
    ROMlib_sdl_software_presentation(0);
    ROMlib_preferences_redraw();
    return result;
}

/* Image decoding pauses the guest. Draw a host overlay without re-entering
   Toolbox code; retain the original screen and cursor until decoding ends. */
static SDL_Surface* image_saved_screen;
static SDL_Cursor *image_saved_cursor, *image_wait_cursor;
static Uint32 image_progress_tick;
static int image_cursor_visible;

void ROMlib_sdl_image_progress_pump(void)
{
    SDL_PumpEvents();
    SDL_Surface* screen = SDL_GetVideoSurface();
    if (!image_saved_screen || !screen || screen->w < 64 || screen->h < 90)
        return;
    Uint32 now = SDL_GetTicks();
    if (image_progress_tick && now - image_progress_tick < 50)
        return;
    image_progress_tick = now;
    int width = MIN(420, screen->w), height = MIN(108, screen->h);
    int x = (screen->w - width) / 2, y = (screen->h - height) / 2;
    Uint32 border = SDL_MapRGB(screen->format, 0, 0, 0);
    Uint32 background = SDL_MapRGB(screen->format, 238, 238, 238);
    Uint32 progress_color = SDL_MapRGB(screen->format, 64, 96, 160);
    box(screen, x, y, width, height, border);
    box(screen, x + 2, y + 2, width - 4, height - 4, background);
    text(screen, x + 16, y + 16, "Loading disk image...", border, 45);
    text(screen, x + 16, y + 35, "Preparing mounted volumes", border, 45);
    box(screen, x + 16, y + 64, width - 32, 20, border);
    box(screen, x + 17, y + 65, width - 34, 18, background);
    int track = width - 34, segment = track / 4;
    int offset = (now / 12) % (track + segment) - segment;
    int left = MAX(0, offset), right = MIN(track, offset + segment);
    if (right > left)
        box(screen, x + 17 + left, y + 65, right - left, 18, progress_color);
    SDL_UpdateRect(screen, x, y, width, height);
}

void ROMlib_sdl_image_progress_begin(void)
{
    SDL_Surface* screen = SDL_GetVideoSurface();
    if (!screen || image_saved_screen)
        return;
    ROMlib_sdl_software_presentation(1);
    image_saved_screen = SDL_ConvertSurface(screen, screen->format, SDL_SWSURFACE);
    if (!image_saved_screen) {
        ROMlib_sdl_software_presentation(0);
        return;
    }
    static Uint8 hourglass[32] = {0x7f, 0xfe, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08, 0x08, 0x10, 0x04,
                                  0x20, 0x02, 0x40, 0x01, 0x80, 0x01, 0x80, 0x02, 0x40, 0x04, 0x20,
                                  0x08, 0x10, 0x10, 0x08, 0x20, 0x04, 0x40, 0x02, 0x7f, 0xfe};
    image_saved_cursor = SDL_GetCursor();
    image_wait_cursor = SDL_CreateCursor(hourglass, hourglass, 16, 16, 7, 7);
    if (image_wait_cursor)
        SDL_SetCursor(image_wait_cursor);
    image_cursor_visible = SDL_ShowCursor(SDL_ENABLE);
    image_progress_tick = 0;
    ROMlib_sdl_image_progress_pump();
}

void ROMlib_sdl_image_progress_end(void)
{
    if (!image_saved_screen)
        return;
    SDL_Surface* screen = SDL_GetVideoSurface();
    if (screen) {
        SDL_BlitSurface(image_saved_screen, NULL, screen, NULL);
        SDL_UpdateRect(screen, 0, 0, 0, 0);
    }
    SDL_FreeSurface(image_saved_screen);
    image_saved_screen = NULL;
    ROMlib_sdl_software_presentation(0);
    if (image_saved_cursor)
        SDL_SetCursor(image_saved_cursor);
    if (image_wait_cursor)
        SDL_FreeCursor(image_wait_cursor);
    image_wait_cursor = image_saved_cursor = NULL;
    SDL_ShowCursor(image_cursor_visible);
}
