/*+===================================================================
  File:      preferences.c

  Summary:   Additional controls for the System-file Preferences dialog.

  Classes:   None.

  Functions: ROMlib_extra_preferences_apply, ROMlib_extra_preferences_init, ROMlib_extra_preferences_save,
             ROMlib_extra_preferences_valid, ROMlib_preferences_check, ROMlib_preferences_check_label,
             ROMlib_preferences_field_label, ROMlib_preferences_redraw, ROMlib_preferences_toggle,
             ROMlib_profile_preferences_init, ROMlib_profile_preferences_save, ROMlib_preferences_capture_defaults,
             ROMlib_preferences_set_cli_overrides, ROMlib_preferences_boolean_overridden,
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
#include "rsys/configprefs.h"
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
#include "rsys/menu.h"
#include "rsys/text.h"
#include "rsys/autorefresh.h"

static const struct {
    const char *key, *label;
    int* value;
} live_checks[] = {
    {"NoClock", "Hide Macintosh clock", &ROMlib_noclock},
    {"NewLineToCR", "Map newlines to carriage returns", &ROMlib_newlinetocr},
    {"DirectDiskAccess", "Direct disk access", &ROMlib_directdiskaccess},
    {"FlushOften", "Flush translation cache often", &ROMlib_flushoften},
    {"PretendHelp", "Pretend Help Manager", &ROMlib_pretend_help},
    {"PretendEdition", "Pretend Edition Manager", &ROMlib_pretend_edition},
    {"PretendScript", "Pretend Script Manager", &ROMlib_pretend_script},
    {"PretendAlias", "Pretend Alias Manager", &ROMlib_pretend_alias},
    {"NoWarn32", "Suppress 32-bit warnings", &ROMlib_nowarn32},
    {"PreferM68K", "Prefer m68k for FAT apps", &ROMlib_prefer_m68k},
    {"StickyMenus", "Sticky menus", &ROMlib_sticky_menus_p},
    {"ForwardDelete", "Delete key deletes forwards", &ROMlib_forward_del_p},
    {"AutoRefresh", "Detect drawing that bypasses QuickDraw", &do_autorefresh_p},
    {"SubstituteFonts", "Substitute standard PostScript fonts", &substitute_fonts_p},
};

static unsigned cli_overrides;

void ROMlib_preferences_set_cli_overrides(unsigned mask)
{
    cli_overrides = mask;
}

int ROMlib_preferences_boolean_overridden(const int* value)
{
    for (unsigned i = 0; i < 4; ++i)
        if (value == live_checks[10 + i].value)
            return !!(cli_overrides & (1u << i));
    return 0;
}

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
    NULL, /* Retained for existing preference files, not shown in the editor. */
    "Delay",
    NULL,
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
    "Refresh interval (ticks; 0 = automatic)",
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

static const char* const default_field_keys[] = {
    [0] = "WindowName", [1] = "Delay",         [2] = "AppleChar",      [3] = "RightButtonModifier",
    [10] = "SysZone",   [11] = "ApplZone",     [12] = "SystemVersion", [13] = "RefreshNumber",
    [14] = "Comments",  [17] = "BitsPerPixel", [18] = "DisplayScale",
};
static const char* const update_keys[] = {"BlitInOSEvent", "BlitAtTrapEnd", "BlitOften"};

static int profile_flag(const char* filename, const char* section, const char* key, int fallback)
{
    char value[8];
    if (config_prefs_get(filename, section, key, value, sizeof value) != 1)
        return fallback;
    return value[0] == '1' ? 1 : value[0] == '0' ? 0 : fallback;
}

static ROMlib_extra_preferences startup_defaults;
static int have_startup_defaults;

void ROMlib_preferences_capture_defaults(void)
{
    if (!have_startup_defaults) {
        ROMlib_extra_preferences_init(&startup_defaults);
        have_startup_defaults = 1;
    }
}

static void load_profile(ROMlib_extra_preferences* prefs, const char* filename, const char* section)
{
    char options[8];
    if (config_prefs_get(filename, section, "Options", options, sizeof options) == 1 && options[0] == '0') {
        prefs->flags = 0;
        prefs->checks &= ~((1u << 9) - 1);
    }
    for (unsigned i = 0; i < NELEM(default_field_keys); ++i) {
        char value[258];
        if (!default_field_keys[i])
            continue;
        size_t length = config_prefs_get(filename, section, default_field_keys[i], value, sizeof value);
        if (!length)
            continue;
        if (string_field(i) && length >= 2 && value[0] == '"' && value[length - 1] == '"') {
            value[length - 1] = 0;
            memmove(value, value + 1, length - 1);
        }
        snprintf(prefs->fields[i], sizeof prefs->fields[i], "%.255s", value);
    }
    for (unsigned i = 0; i < NELEM(update_keys); ++i)
        if (profile_flag(filename, section, update_keys[i], 0))
            snprintf(prefs->fields[16], sizeof prefs->fields[16], "%u", i);
    for (unsigned i = 0; i < editable_option_count; ++i) {
        int32 bit = extra_options[i].bit;
        if (profile_flag(filename, section, extra_options[i].name, (prefs->flags & bit) != 0))
            prefs->flags |= bit;
        else
            prefs->flags &= ~bit;
    }
    for (unsigned i = 0; i < NELEM(live_checks); ++i) {
        uint32_t bit = 1u << i;
        if (profile_flag(filename, section, live_checks[i].key, (prefs->checks & bit) != 0))
            prefs->checks |= bit;
        else
            prefs->checks &= ~bit;
    }
    memcpy(prefs->original, prefs->fields, sizeof prefs->fields);
}

void ROMlib_profile_preferences_init(ROMlib_extra_preferences* prefs, const char* filename, const char* section)
{
    if (have_startup_defaults)
        *prefs = startup_defaults;
    else
        ROMlib_extra_preferences_init(prefs);
    load_profile(prefs, filename, "Defaults");
    if (strcmp(section, "Defaults"))
        load_profile(prefs, filename, section);
}

void ROMlib_default_preferences_init(ROMlib_extra_preferences* prefs, const char* filename)
{
    ROMlib_profile_preferences_init(prefs, filename, "Defaults");
}

/* Publish only changed controls so advanced rules and other Defaults survive. */
int ROMlib_profile_preferences_save(ROMlib_extra_preferences* prefs, const ROMlib_extra_preferences* original,
                                    const char* filename, const char* section)
{
    const char* keys[NELEM(default_field_keys) + NELEM(update_keys) + editable_option_count + NELEM(live_checks)];
    const char* values[NELEM(keys)];
    char serialized[NELEM(default_field_keys)][258];
    size_t count = 0;
    for (unsigned i = 0; i < NELEM(default_field_keys); ++i) {
        const char* text = prefs->fields[i];
        if (!default_field_keys[i] || !strcmp(text, prefs->original[i]))
            continue;
        keys[count] = default_field_keys[i];
        if (string_field(i)) {
            char* out = serialized[i];
            *out++ = '"';
            for (; *text; ++text)
                *out++ = *text == '"' ? '\'' : *text;
            *out++ = '"';
            *out = 0;
            values[count++] = serialized[i];
        }
        else if (i == 18) {
            int percent;
            if (!ROMlib_scale_parse(text, &percent))
                return 0;
            snprintf(serialized[i], sizeof serialized[i], "%d", percent);
            values[count++] = serialized[i];
        }
        else
            values[count++] = *text ? text : NULL;
    }
    if (strcmp(prefs->fields[16], prefs->original[16])) {
        long update;
        if (!parse_integer(prefs->fields[16], 0, NELEM(update_keys) - 1, &update))
            return 0;
        for (unsigned i = 0; i < NELEM(update_keys); ++i) {
            keys[count] = update_keys[i];
            values[count++] = i == (unsigned)update ? "1" : "0";
        }
    }
    for (unsigned i = 0; i < editable_option_count; ++i) {
        int32 bit = extra_options[i].bit;
        if ((prefs->flags ^ original->flags) & bit) {
            keys[count] = extra_options[i].name;
            values[count++] = prefs->flags & bit ? "1" : "0";
        }
    }
    for (unsigned i = 0; i < NELEM(live_checks); ++i) {
        uint32_t bit = 1u << i;
        if ((prefs->checks ^ original->checks) & bit) {
            keys[count] = live_checks[i].key;
            values[count++] = prefs->checks & bit ? "1" : "0";
        }
    }
    return !count || config_prefs_set_many(filename, section, keys, values, count);
}

int ROMlib_default_preferences_save(ROMlib_extra_preferences* prefs, const ROMlib_extra_preferences* original,
                                    const char* filename)
{
    return ROMlib_profile_preferences_save(prefs, original, filename, "Defaults");
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
    for (i = 10; i < NELEM(live_checks); ++i)
        fprintf(file, "%s=%d\n", live_checks[i].key, !!*live_checks[i].value);
    if (ROMlib_display_scale != 100)
        fprintf(file, "DisplayScale=%d\n", ROMlib_display_scale);
    for (i = 0; i < NELEM(extra_options); ++i)
        if (extra_options[i].bit == ROMLIB_NOCLOCK_BIT ? ROMlib_noclock : ROMlib_options & extra_options[i].bit)
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
    if (check >= 10 && check < NELEM(live_checks))
        return ROMAW_PREF_ADVANCED;
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
