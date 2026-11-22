/*+===================================================================
  File:      sdl_preferences_test.c

  Summary:   Exercise the preferences editor and loading overlays against the Windows SDL backend.

  Classes:   None.

  Functions: ROMlib_extra_preferences_apply, ROMlib_extra_preferences_valid, ROMlib_preferences_check,
             ROMlib_preferences_check_label, ROMlib_preferences_field_label, ROMlib_preferences_redraw,
             ROMlib_preferences_toggle, ROMlib_sdl_software_presentation, main, saveprefvalues, sdl_preferences_input_reset

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the preferences editor and loading overlays against the Windows SDL backend. */
#include "../../frontend-sdl/sdlpreferences.c"
#undef main
#include <assert.h>

static int applied, saved, invalid, fail_save;
static ROMlib_extra_preferences applied_values;
static SDL_Event events[32];
static unsigned event_count;
static unsigned software_holds;

void ROMlib_sdl_software_presentation(int hold)
{
    if (hold)
        ++software_holds;
    else {
        assert(software_holds);
        --software_holds;
    }
}

const char* ROMlib_preferences_field_label(unsigned field)
{
    static const char* labels[] = {"Title",
                                   "Delay",
                                   "Apple character",
                                   "Modifiers",
                                   "Screen size",
                                   "Mac size",
                                   "Location",
                                   "Printer",
                                   "Label",
                                   "Version",
                                   "SysZone (system heap)",
                                   "ApplZone (application heap)",
                                   "System version",
                                   "Refresh",
                                   "Comments",
                                   "Sound",
                                   "Updates",
                                   "Colors",
                                   "Display scale"};
    return field < 19 ? labels[field] : NULL;
}

const char* ROMlib_preferences_check_label(unsigned check)
{
    return check < 23 ? "Compatibility option" : NULL;
}

int ROMlib_preferences_check(ROMlib_extra_preferences* prefs, unsigned check)
{
    return !!(prefs->checks & (1u << check));
}

void ROMlib_preferences_toggle(ROMlib_extra_preferences* prefs, unsigned check)
{
    prefs->checks ^= 1u << check;
}

int ROMlib_extra_preferences_valid(ROMlib_extra_preferences* prefs, unsigned* bad)
{
    (void)prefs;
    if (invalid) {
        --invalid;
        *bad = 10;
        return 0;
    }
    return 1;
}

int ROMlib_extra_preferences_apply(ROMlib_extra_preferences* prefs)
{
    ++applied;
    applied_values = *prefs;
    return 1;
}

int saveprefvalues(char* filename, int32_t x, int32_t y)
{
    assert(filename && !strcmp(filename, "test.ecf") && !x && !y);
    ++saved;
    return !fail_save;
}

void ROMlib_preferences_redraw(void)
{
}

void sdl_preferences_input_reset(void)
{
}

static void key_event(SDLKey key, SDLMod mod, int unicode)
{
    SDL_Event* e = &events[event_count++];
    memset(e, 0, sizeof *e);
    e->type = SDL_KEYDOWN;
    e->key.keysym.sym = key;
    e->key.keysym.mod = mod;
    e->key.keysym.unicode = unicode;
}

static void click(int x, int y)
{
    SDL_Event* e = &events[event_count++];
    memset(e, 0, sizeof *e);
    e->type = SDL_MOUSEBUTTONDOWN;
    e->button.button = SDL_BUTTON_LEFT;
    e->button.x = x;
    e->button.y = y;
}

static int send_events(void* unused)
{
    unsigned i;
    (void)unused;
    SDL_Delay(50);
    for (i = 0; i < event_count; ++i) {
        assert(!SDL_PushEvent(&events[i]));
        SDL_Delay(10);
    }
    return 0;
}

static int old_filter(const SDL_Event* event)
{
    (void)event;
    return 1;
}

int main(int argc, char** argv)
{
    ROMlib_extra_preferences prefs = {0}, original;
    preferences_ui ui = {0};
    SDL_Surface *screen, *canvas;
    SDL_Thread* thread;
    SDL_Color colors[256];
    int delay, interval;
    (void)argc;
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER));
    screen = SDL_SetVideoMode(512, 342, 8, SDL_SWSURFACE);
    assert(screen && screen->format->palette);
    memcpy(colors, screen->format->palette->colors, sizeof colors);
    SDL_SetEventFilter(old_filter);
    SDL_EnableUNICODE(0);
    SDL_EnableKeyRepeat(0, 0);
    original = prefs;
    key_event(SDLK_ESCAPE, KMOD_NONE, 0);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread);
    assert(!ROMlib_sdl_preferences(&prefs, "test.ecf"));
    SDL_WaitThread(thread, NULL);
    assert(!memcmp(&prefs, &original, sizeof prefs) && !applied && !saved);
    assert(SDL_GetEventFilter() == old_filter && !SDL_EnableUNICODE(-1));
    SDL_GetKeyRepeat(&delay, &interval);
    assert(!delay && !interval);
    assert(!memcmp(colors, screen->format->palette->colors, sizeof colors));

    event_count = 0;
    click(320, 36);
    click(300, 60);
    key_event(SDLK_8, KMOD_NONE, '8');
    key_event(SDLK_m, KMOD_NONE, 'm');
    key_event(SDLK_TAB, KMOD_NONE, 0);
    key_event(SDLK_1, KMOD_NONE, '1');
    key_event(SDLK_6, KMOD_NONE, '6');
    key_event(SDLK_m, KMOD_NONE, 'm');
    click(350, 320);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread);
    assert(ROMlib_sdl_preferences(&prefs, "test.ecf") == 2);
    SDL_WaitThread(thread, NULL);
    assert(applied == 1 && saved == 1 && !strcmp(applied_values.fields[10], "8m") &&
           !strcmp(applied_values.fields[11], "16m"));

    event_count = 0;
    invalid = 1;
    click(270, 320);
    key_event(SDLK_ESCAPE, KMOD_NONE, 0);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread);
    assert(!ROMlib_sdl_preferences(&prefs, "test.ecf"));
    SDL_WaitThread(thread, NULL);
    assert(!invalid && applied == 1);

    event_count = 0;
    fail_save = 1;
    click(350, 320);
    key_event(SDLK_ESCAPE, KMOD_NONE, 0);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread);
    assert(!ROMlib_sdl_preferences(&prefs, "test.ecf"));
    SDL_WaitThread(thread, NULL);
    assert(applied == 2 && saved == 2);

    memset(ui.edited.fields[10], 'x', 255);
    ui.edited.fields[10][255] = 0;
    focus_field(&ui, 10, 0);
    {
        SDL_keysym key = {0};
        key.sym = SDLK_a;
        key.unicode = 'a';
        edit_key(&ui, &key);
        assert(strlen(ui.edited.fields[10]) == 255);
        key.mod = KMOD_CTRL;
        edit_key(&ui, &key);
        key.mod = KMOD_NONE;
        key.sym = SDLK_BACKSPACE;
        edit_key(&ui, &key);
        assert(!*ui.edited.fields[10] && !ui.cursor);
    }
    canvas = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 8, 0, 0, 0, 0);
    assert(canvas);
    ui.page = 2;
    render(canvas, &ui);
    if (argc > 1)
        assert(!SDL_SaveBMP(canvas, argv[1]));
    SDL_FreeSurface(canvas);
    /* Loading overlays must preserve guest pixels and host cursor state. */
    for (int bits = 8; bits <= 32; bits *= 2) {
        screen = SDL_SetVideoMode(512, 342, bits, SDL_SWSURFACE);
        assert(screen);
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 255, 0, 0));
        size_t bytes = screen->pitch * screen->h;
        unsigned char* original_pixels = malloc(bytes);
        assert(original_pixels);
        memcpy(original_pixels, screen->pixels, bytes);
        SDL_Cursor* original_cursor = SDL_GetCursor();
        int visible = SDL_ShowCursor(SDL_QUERY);
        ROMlib_sdl_image_progress_begin();
        assert(image_saved_screen);
        assert(memcmp(original_pixels, screen->pixels, bytes));
        SDL_Delay(60);
        ROMlib_sdl_image_progress_pump();
        ROMlib_sdl_image_progress_end();
        assert(!image_saved_screen && !image_wait_cursor);
        assert(!memcmp(original_pixels, screen->pixels, bytes));
        assert(SDL_GetCursor() == original_cursor);
        assert(SDL_ShowCursor(SDL_QUERY) == visible);
        free(original_pixels);
    }
    screen = SDL_SetVideoMode(512, 342, 8, SDL_SWSURFACE);
    assert(screen);
    fail_save = 0;
    event_count = 0;
    click(94, 225); // 1.5x preset on General.
    click(350, 320);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread && ROMlib_sdl_preferences(&prefs, "test.ecf") == 2);
    SDL_WaitThread(thread, NULL);
    assert(!strcmp(applied_values.fields[18], "1.5x"));
    event_count = 0;
    click(424, 225); // Custom selects the multiplier for editing.
    key_event(SDLK_1, KMOD_NONE, '1');
    key_event(SDLK_PERIOD, KMOD_NONE, '.');
    key_event(SDLK_2, KMOD_NONE, '2');
    key_event(SDLK_5, KMOD_NONE, '5');
    click(350, 320);
    thread = SDL_CreateThread(send_events, NULL);
    assert(thread && ROMlib_sdl_preferences(&prefs, "test.ecf") == 2);
    SDL_WaitThread(thread, NULL);
    assert(!strcmp(applied_values.fields[18], "1.25"));
    assert(!software_holds);
    SDL_Quit();
    puts("SDL preferences: editing, selection bounds, validation, Save, Cancel and SDL state restoration passed");
    return 0;
}
