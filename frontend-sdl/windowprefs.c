/*+===================================================================
  File:      windowprefs.c

  Summary:   Implements windowprefs support for host-platform configuration and integration.

  Classes:   None.

  Functions: file_dialog_prefs_load, file_dialog_prefs_save, window_location_parse, window_prefs_init,
             window_prefs_location, window_prefs_save, window_prefs_save_location, window_prefs_select,
             window_prefs_set_saving_enabled, window_size_parse

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "windowprefs.h"
#include "rsys/prefspacing.h"
#include "rsys/configprefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

static char* prefs_filename;
static char application_section[CONFIG_PREFS_SECTION_SIZE];
static int min_w, min_h, max_w, max_h;
static int saving_enabled = 1;

int window_size_parse(const char* text, int min_width, int min_height, int max_width, int max_height, int* width,
                      int* height)
{
    unsigned long w, h;
    char* end;

    if (!text || !isdigit((unsigned char)*text))
        return 0;
    errno = 0;
    w = strtoul(text, &end, 10);
    if (errno || (*end != 'x' && *end != 'X') || !isdigit((unsigned char)end[1]))
        return 0;
    h = strtoul(end + 1, &end, 10);
    if (errno || *end || w < (unsigned)min_width || h < (unsigned)min_height || w > (unsigned)max_width ||
        h > (unsigned)max_height)
        return 0;
    *width = (int)w;
    *height = (int)h;
    return 1;
}

#ifdef _WIN32
static DWORD read_window_pref(const char* section, const char* key, const char* fallback, char* value, DWORD capacity,
                              const char* path)
{
    size_t n = config_prefs_get(path, section, key, value, capacity);
    if (!n && capacity && *fallback) {
        snprintf(value, capacity, "%s", fallback);
        n = strlen(value);
    }
    return n;
}

static int write_window_pref(const char* section, const char* key, const char* value, const char* path)
{
    return config_prefs_set(path, section, key, value);
}
#endif

void window_prefs_init(const char* filename, int min_width, int min_height, int max_width, int max_height)
{
    free(prefs_filename);
    prefs_filename = malloc(strlen(filename) + 1);
    if (prefs_filename)
        strcpy(prefs_filename, filename);
    application_section[0] = 0;
    min_w = min_width;
    min_h = min_height;
    max_w = max_width;
    max_h = max_height;
#ifdef _WIN32
    if (prefs_filename)
        prefs_space_sections(prefs_filename);
#endif
}

int window_prefs_select(const unsigned char* name, unsigned long type, unsigned long creator, int* width, int* height)
{
    char size[64];
    (void)name;
    config_prefs_app_section(type, creator, application_section);
#ifdef _WIN32
    if (prefs_filename && read_window_pref(application_section, "Size", "", size, sizeof size, prefs_filename))
        return window_size_parse(size, min_w, min_h, max_w, max_h, width, height);
#endif
    return 0;
}

/* Bootstrap windows can exit during a memory-profile restart before the
   selected application geometry has been applied. Never persist that window. */
void window_prefs_set_saving_enabled(int enabled)
{
    saving_enabled = enabled != 0;
}

int window_prefs_save(int width, int height)
{
    char size[64];
    if (!saving_enabled || !prefs_filename || !application_section[0] || width < min_w || width > max_w ||
        height < min_h || height > max_h)
        return 0;
    sprintf(size, "%dx%d", width, height);
#ifdef _WIN32
    int saved = write_window_pref(application_section, "Size", size, prefs_filename) != 0;
    if (saved)
        prefs_space_sections(prefs_filename);
    return saved;
#else
    return 0;
#endif
}

int window_location_parse(const char* text, int* x, int* y)
{
    long horizontal, vertical;
    char* end;
    const char* p = text;
    if (!p)
        return 0;
    if (*p == '-' || *p == '+')
        ++p;
    if (!isdigit((unsigned char)*p))
        return 0;
    errno = 0;
    horizontal = strtol(text, &end, 10);
    if (errno || *end != ',')
        return 0;
    p = ++end;
    if (*p == '-' || *p == '+')
        ++p;
    if (!isdigit((unsigned char)*p))
        return 0;
    errno = 0;
    vertical = strtol(end, &end, 10);
    if (errno || *end || horizontal < -1000000 || horizontal > 1000000 || vertical < -1000000 || vertical > 1000000)
        return 0;
    *x = horizontal;
    *y = vertical;
    return 1;
}

int window_prefs_location(int* x, int* y)
{
#ifdef _WIN32
    char location[64];
    if (prefs_filename && application_section[0] &&
        read_window_pref(application_section, "Location", "", location, sizeof location, prefs_filename))
        return window_location_parse(location, x, y);
#endif
    return 0;
}

int window_prefs_save_location(int x, int y)
{
#ifdef _WIN32
    char location[64];
    if (!saving_enabled || !prefs_filename || !application_section[0] || x < -1000000 || x > 1000000 || y < -1000000 ||
        y > 1000000)
        return 0;
    sprintf(location, "%d,%d", x, y);
    int saved = write_window_pref(application_section, "Location", location, prefs_filename) != 0;
    if (saved)
        prefs_space_sections(prefs_filename);
    return saved;
#else
    return 0;
#endif
}

int file_dialog_prefs_load(int saving, int* width, int* height, int* x, int* y)
{
#ifdef _WIN32
    char bounds[128];
    if (!prefs_filename || !application_section[0] ||
        !read_window_pref(application_section, saving ? "SaveDialogBounds" : "OpenDialogBounds", "", bounds,
                          sizeof bounds, prefs_filename))
        return 0;
    char* location = strchr(bounds, '@');
    if (!location)
        return 0;
    *location++ = 0;
    int w, h, left, top;
    if (!window_size_parse(bounds, 1, 1, 32767, 32767, &w, &h) || !window_location_parse(location, &left, &top))
        return 0;
    *width = w;
    *height = h;
    *x = left;
    *y = top;
    return 1;
#else
    return 0;
#endif
}

int file_dialog_prefs_save(int saving, int width, int height, int x, int y)
{
#ifdef _WIN32
    char bounds[128];
    if (!prefs_filename || !application_section[0] || width < 1 || width > 32767 || height < 1 || height > 32767 ||
        x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000)
        return 0;
    snprintf(bounds, sizeof bounds, "%dx%d@%d,%d", width, height, x, y);
    int saved = write_window_pref(application_section, saving ? "SaveDialogBounds" : "OpenDialogBounds", bounds,
                                  prefs_filename);
    if (saved)
        prefs_space_sections(prefs_filename);
    return saved;
#else
    return 0;
#endif
}
