/*+===================================================================
  File:      preferences.c

  Summary:   Additional controls for the System-file Preferences dialog.

  Classes:   None.

  Functions: ROMlib_extra_preferences_apply, ROMlib_extra_preferences_init, ROMlib_extra_preferences_save,
             ROMlib_extra_preferences_valid, ROMlib_preferences_check, ROMlib_preferences_check_label,
             ROMlib_preferences_field_label, ROMlib_preferences_redraw, ROMlib_preferences_toggle,
             ROMlib_preferences_write_string

  Origin:    Existing repository source in executor/src/ui; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Additional controls for the System-file Preferences dialog. */
#include "rsys/common.h"
#include "DialogMgr.h"
#include "ControlMgr.h"
#include "MemoryMgr.h"
#include "ToolboxUtil.h"
#include "OSUtil.h"
#include "rsys/prefpanel.h"
#include "rsys/options.h"
#include "rsys/prefs.h"
#include "rsys/parse.h"
#include "rsys/print.h"
#include "rsys/wind.h"
#include "rsys/ecf.h"
#include "rsys/displayscale.h"
#include <errno.h>
#include <limits.h>

#include "rsys/parseopt.h"
#include "rsys/refresh.h"
#include "rsys/version.h"
#include "rsys/cquick.h"
#include "rsys/vdriver.h"

static const struct {
    const char* label;
    int* value;
} live_checks[] = {
    {"Hide Macintosh clock", &ROMlib_noclock},          {"Map newlines to carriage returns", &ROMlib_newlinetocr},
    {"Direct disk access", &ROMlib_directdiskaccess},   {"Flush translation cache often", &ROMlib_flushoften},
    {"Pretend Help Manager", &ROMlib_pretend_help},     {"Pretend Edition Manager", &ROMlib_pretend_edition},
    {"Pretend Script Manager", &ROMlib_pretend_script}, {"Pretend Alias Manager", &ROMlib_pretend_alias},
    {"Suppress 32-bit warnings", &ROMlib_nowarn32},     {"Prefer m68k for FAT apps", &ROMlib_prefer_m68k},
};

static const struct {
    const char *name, *label;
    int32 bit;
} extra_options[] = {
    {"CloseIsQuit", "Close requests application Quit", ROMLIB_CLOSE_IS_QUIT_BIT},
    {"NoSuspend", "Keep running when inactive", ROMLIB_NOSUSPEND_BIT},
    {"RectScreen", "Rectangular screen corners", ROMLIB_RECT_SCREEN_BIT},
    {"StripAddress", "Strip 24-bit address flags", ROMLIB_STRIPADDRESSHACK_BIT},
    {"DisposHandleHack", "Handle disposal compatibility", ROMLIB_DISPOSHANDLE_HACK_BIT},
    {"NoPrefPanel", "Disable Preferences after closing", ROMLIB_NOPREFS_BIT},
    {"Debug", "Application debug option", ROMLIB_DEBUG_BIT},
    /* Accepted legacy options without additional live controls. Preserve them. */
    {"DirtyVariant", NULL, ROMLIB_DIRTY_VARIANT_BIT},
    {"TextDisableHack", NULL, ROMLIB_TEXT_DISABLE_BIT},
    {"NoLower", NULL, ROMLIB_NOLOWER_BIT},
    {"PrintingHack", NULL, ROMLIB_PRINTING_HACK_BIT},
    {"NoClock", NULL, ROMLIB_NOCLOCK_BIT},
    {"Accelerated", NULL, ROMLIB_ACCELERATED_BIT},
};

enum { editable_option_count = 7 };

static const char* field_labels[] = {
    "Window title",
    "Delay",
    "Apple-menu character (0..255)",
    "Right-click modifiers (0..65535)",
    /* Keep these slots for legacy preference handling without displaying controls. */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "SysZone (system heap)",
    "ApplZone (application heap)",
    "Reported system version",
    "Refresh rate",
    "Comments",
    NULL,
    "Updates (0 normal/1 trap/2 anim)",
    "Colors (bits per pixel)",
    "Display scale (0.25x..16x)",
};

static char** string_field(unsigned field)
{
    switch (field) {
    case 0:
        return &ROMlib_WindowName;
    case 7:
        return &ROMlib_new_printer_name;
    case 8:
        return &ROMlib_new_label;
    case 14:
        return &ROMlib_Comments;
    default:
        return NULL;
    }
}

static boolean_t parse_integer(const char* text, long min, long max, long* value)
{
    char* end;
    long parsed;
    if (!text || !*text)
        return FALSE;
    errno = 0;
    parsed = strtol(text, &end, text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : 10);
    if (errno || *end || parsed < min || parsed > max)
        return FALSE;
    *value = parsed;
    return TRUE;
}

static boolean_t parse_pair(const char* text, unsigned field, pair_t* pair)
{
    char first[256];
    const char* comma;
    long a, b;
    if (!*text) {
        pair->first = pair->second = INITIALPAIRVALUE;
        return TRUE;
    }
    comma = strchr(text, ',');
    if (!comma || comma - text >= sizeof first)
        return FALSE;
    memcpy(first, text, comma - text);
    first[comma - text] = 0;
    if (!parse_integer(first, field == 6 ? -32768 : 1, 32767, &a) ||
        !parse_integer(comma + 1, field == 6 ? -32768 : 1, 32767, &b))
        return FALSE;
    if (field != 6 && (a < 512 || b < 342 || a > 4096 || b > 4096))
        return FALSE;
    pair->first = a;
    pair->second = b;
    return TRUE;
}

static boolean_t parse_printer_version(const char* text, long* value)
{
    char major[256];
    const char* dot = strchr(text, '.');
    long a, b = 0;
    if (!dot) {
        if (!parse_integer(text, 0, 3276, &a))
            return FALSE;
    }
    else {
        if (dot - text >= sizeof major)
            return FALSE;
        memcpy(major, text, dot - text);
        major[dot - text] = 0;
        if (!parse_integer(major, 0, 3276, &a) || !parse_integer(dot + 1, 0, 9, &b))
            return FALSE;
    }
    *value = a * 10 + b;
    return *value <= 32767;
}

int ROMlib_extra_preferences_valid(ROMlib_extra_preferences* prefs, unsigned* bad)
{
    unsigned i;
    long value;
    pair_t pair;
    int zone;
    for (i = 0; i < NELEM(prefs->fields); ++i) {
        if (!strcmp(prefs->fields[i], prefs->original[i]) || string_field(i))
            continue;
        *bad = i;
        if (i == 18) {
            int percent;
            if (!ROMlib_scale_parse(prefs->fields[i], &percent))
                return FALSE;
        }
        else if (i >= 4 && i <= 6) {
            if (!parse_pair(prefs->fields[i], i, &pair))
                return FALSE;
        }
        else if (i == 12) {
            uint32 version;
            if (!ROMlib_parse_version(prefs->fields[i], &version))
                return FALSE;
        }
        else if (i == 17) {
            if (!parse_integer(prefs->fields[i], 1, 32, &value) ||
                !(value == 1 || value == 2 || value == 4 || value == 8 || value == 16 || value == 32) ||
                !C_HasDepth(MR(MainDevice), value, 0, 0))
                return FALSE;
        }
        else if (i == 10 || i == 11) {
            if (!ROMlib_ecf_parse_zone(prefs->fields[i], i == 10, &zone))
                return FALSE;
        }
        else if (i == 9) {
            if (!parse_printer_version(prefs->fields[i], &value))
                return FALSE;
        }
        else if (!parse_integer(prefs->fields[i], 0,
                                i == 2    ? 255
                                : i == 3  ? 65535
                                : i == 16 ? 2
                                : i == 13 ? 32767
                                          : INT_MAX,
                                &value))
            return FALSE;
    }
    return TRUE;
}

void ROMlib_extra_preferences_init(ROMlib_extra_preferences* prefs)
{
    unsigned i;
    memset(prefs, 0, sizeof *prefs);
    prefs->flags = ROMlib_options;
    for (i = 0; i < NELEM(live_checks); ++i)
        if (*live_checks[i].value)
            prefs->checks |= 1u << i;
    for (i = 0; i < NELEM(prefs->fields); ++i) {
        char** string = string_field(i);
        if (string)
            snprintf(prefs->fields[i], 256, "%s", *string ? *string : "");
    }
    snprintf(prefs->fields[1], 256, "%d", ROMlib_delay);
    snprintf(prefs->fields[2], 256, "%d", ROMlib_AppleChar);
    snprintf(prefs->fields[3], 256, "%d", ROMlib_right_button_modifier);
    for (i = 4; i <= 6; ++i) {
        pair_t* pair = i == 4 ? &ROMlib_ScreenSize : i == 5 ? &ROMlib_MacSize : &ROMlib_ScreenLocation;
        if (pair->first != INITIALPAIRVALUE)
            snprintf(prefs->fields[i], 256, "%ld,%ld", (long)pair->first, (long)pair->second);
    }
    snprintf(prefs->fields[9], 256, "%lu.%lu", (unsigned long)ROMlib_PrDrvrVers / 10,
             (unsigned long)ROMlib_PrDrvrVers % 10);
    if (ROMlib_config_syszone_size > 0)
        snprintf(prefs->fields[10], 256, "%d", ROMlib_config_syszone_size);
    if (ROMlib_config_applzone_size > 0)
        snprintf(prefs->fields[11], 256, "%d", ROMlib_config_applzone_size);
    snprintf(prefs->fields[12], 256, "%u.%u.%u", (system_version >> 8) & 15, (system_version >> 4) & 15,
             system_version & 15);
    snprintf(prefs->fields[13], 256, "%d", ROMlib_refresh);
    snprintf(prefs->fields[16], 256, "%d",
             ROMlib_when == WriteAtEndOfTrap                              ? 1
             : ROMlib_when == WriteInBltrgn || ROMlib_when == WriteAlways ? 2
                                                                          : 0);
    snprintf(prefs->fields[17], 256, "%d", vdriver_bpp);
    snprintf(prefs->fields[18], 256, "%d.%02dx", ROMlib_display_scale / 100, ROMlib_display_scale % 100);
    memcpy(prefs->original, prefs->fields, sizeof prefs->fields);
}

int ROMlib_extra_preferences_apply(ROMlib_extra_preferences* prefs)
{
    long numbers[19] = {0};
    pair_t pairs[3];
    char* copies[19] = {0};
    unsigned i, bad;
    if (!ROMlib_extra_preferences_valid(prefs, &bad))
        return FALSE;
    for (i = 0; i < NELEM(prefs->fields); ++i) {
        if (!strcmp(prefs->fields[i], prefs->original[i]))
            continue;
        if (string_field(i)) {
            copies[i] = strdup(prefs->fields[i]);
            if (!copies[i])
                goto fail;
        }
        else if (i == 18) {
            int percent;
            if (!ROMlib_scale_parse(prefs->fields[i], &percent))
                goto fail;
            numbers[i] = percent;
        }
        else if (i >= 4 && i <= 6) {
            if (!parse_pair(prefs->fields[i], i, &pairs[i - 4]))
                goto fail;
        }
        else if (i == 12) {
            uint32 version;
            ROMlib_parse_version(prefs->fields[i], &version);
            numbers[i] = version;
        }
        else if (i == 10 || i == 11) {
            int zone;
            if (!ROMlib_ecf_parse_zone(prefs->fields[i], i == 10, &zone))
                goto fail;
            numbers[i] = zone;
        }
        else if (i == 9) {
            if (!parse_printer_version(prefs->fields[i], &numbers[i]))
                goto fail;
        }
        else if (!parse_integer(prefs->fields[i], 0, i == 2 ? 255 : i == 3 ? 65535 : INT_MAX, &numbers[i]))
            goto fail;
    }
    for (i = 0; i < NELEM(extra_options); ++i)
        if (extra_options[i].label)
            ROMlib_options = (ROMlib_options & ~extra_options[i].bit) | (prefs->flags & extra_options[i].bit);
    for (i = 0; i < NELEM(live_checks); ++i)
        *live_checks[i].value = !!(prefs->checks & (1u << i));
    ROMlib_clockonoff(!ROMlib_noclock);
    ROMlib_dirtyvariant = !!(ROMlib_options & ROMLIB_DIRTY_VARIANT_BIT);
    for (i = 0; i < NELEM(prefs->fields); ++i) {
        if (!strcmp(prefs->fields[i], prefs->original[i]))
            continue;
        if (string_field(i)) {
            free(*string_field(i));
            *string_field(i) = copies[i];
        }
        else
            switch (i) {
            case 1:
                ROMlib_delay = numbers[i];
                break;
            case 2:
                ROMlib_AppleChar = numbers[i];
                break;
            case 3:
                ROMlib_right_button_modifier = numbers[i];
                break;
            case 4:
                ROMlib_ScreenSize = pairs[0];
                break;
            case 5:
                ROMlib_MacSize = pairs[1];
                break;
            case 6:
                ROMlib_ScreenLocation = pairs[2];
                break;
            case 12:
                ROMlib_set_system_version(numbers[i]);
                break;
            case 13:
                set_refresh_rate(numbers[i]);
                break;
            case 16:
                ROMlib_WriteWhen(numbers[i] == 2 ? WriteInBltrgn : numbers[i] == 1 ? WriteAtEndOfTrap : WriteInOSEvent);
                break;
            case 17:
                C_SetDepth(MR(MainDevice), numbers[i], 0, 0);
                ROMlib_desired_bpp = numbers[i];
                break;
            case 18:
                ROMlib_display_scale = numbers[i];
#if defined(SDL)
                sdl_queue_window_size(vdriver_width, vdriver_height);
#endif
                break;
            case 10:
                ROMlib_config_syszone_size = numbers[i];
                break;
            case 11:
                ROMlib_config_applzone_size = numbers[i];
                break;
            case 9:
                ROMlib_PrDrvrVers = numbers[i];
                break;
            }
    }
    return TRUE;
fail:
    for (i = 0; i < NELEM(copies); ++i)
        free(copies[i]);
    return FALSE;
}

void ROMlib_preferences_write_string(FILE* file, const char* name, const char* text)
{
    if (!text)
        return;
    fprintf(file, "%s=\"", name);
    for (; *text; ++text)
        fputc(*text == '"' ? '\'' : *text, file);
    fputs("\"\n", file);
}

void ROMlib_extra_preferences_save(FILE* file)
{
    unsigned i;
    fprintf(file, "PreferM68K=%d\n", ROMlib_prefer_m68k);
    if (ROMlib_display_scale != 100)
        fprintf(file, "DisplayScale=%d\n", ROMlib_display_scale);
    for (i = 0; i < NELEM(extra_options); ++i)
        if (ROMlib_options & extra_options[i].bit)
            fprintf(file, "%s=1\n", extra_options[i].name);
    fprintf(file, "AppleChar=%d\nRightButtonModifier=%d\nPrVers=%lu.%lu\n", ROMlib_AppleChar,
            ROMlib_right_button_modifier, (unsigned long)ROMlib_PrDrvrVers / 10, (unsigned long)ROMlib_PrDrvrVers % 10);
    for (i = 0; i < 2; ++i) {
        pair_t* pair = i ? &ROMlib_MacSize : &ROMlib_ScreenSize;
        if (pair->first != INITIALPAIRVALUE)
            fprintf(file, "%s=%ld,%ld\n", i ? "MacSize" : "ScreenSize", (long)pair->first, (long)pair->second);
    }
    if (ROMlib_config_syszone_size >= 0)
        fprintf(file, "SysZone=%d\n", ROMlib_config_syszone_size);
    if (ROMlib_config_applzone_size >= 0)
        fprintf(file, "ApplZone=%d\n", ROMlib_config_applzone_size);
    ROMlib_preferences_write_string(file, "PrinterName", ROMlib_new_printer_name);
    ROMlib_preferences_write_string(file, "PrinterTypeLabel", ROMlib_new_label);
}

const char* ROMlib_preferences_field_label(unsigned field)
{
    return field < NELEM(field_labels) ? field_labels[field] : NULL;
}

const char* ROMlib_preferences_check_label(unsigned check)
{
    return check < editable_option_count                        ? extra_options[check].label
           : check - editable_option_count < NELEM(live_checks) ? live_checks[check - editable_option_count].label
                                                                : NULL;
}

int ROMlib_preferences_check_page(unsigned check)
{
    if (check < editable_option_count)
        return extra_options[check].bit == ROMLIB_DEBUG_BIT ? ROMAW_PREF_ADVANCED : ROMAW_PREF_COMPATIBILITY;
    check -= editable_option_count;
    if (check < NELEM(live_checks) &&
        (live_checks[check].value == &ROMlib_nowarn32 || live_checks[check].value == &ROMlib_prefer_m68k))
        return ROMAW_PREF_ADVANCED;
    return ROMAW_PREF_COMPATIBILITY;
}

int ROMlib_preferences_check(ROMlib_extra_preferences* prefs, unsigned check)
{
    return check < editable_option_count ? !!(prefs->flags & extra_options[check].bit)
           : check - editable_option_count < NELEM(live_checks)
               ? !!(prefs->checks & (1u << (check - editable_option_count)))
               : 0;
}

void ROMlib_preferences_toggle(ROMlib_extra_preferences* prefs, unsigned check)
{
    if (check < editable_option_count)
        prefs->flags ^= extra_options[check].bit;
    else if (check - editable_option_count < NELEM(live_checks))
        prefs->checks ^= 1u << (check - editable_option_count);
}

void ROMlib_preferences_redraw(void)
{
    vdriver_update_screen(0, 0, vdriver_height, vdriver_width, FALSE);
}
