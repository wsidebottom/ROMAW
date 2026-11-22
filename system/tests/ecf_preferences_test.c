/*+===================================================================
  File:      ecf_preferences_test.c

  Summary:   Implements ecf preferences test support for regression tests and test fixtures.

  Classes:   None.

  Functions: C_HasDepth, C_SetDepth, ROMlib_WriteWhen, ROMlib_clockonoff, ROMlib_set_system_version, main,
             sdl_queue_window_size, set_refresh_rate, syn68k_guest_to_host

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define NDEBUG 1
#include "../../ui/preferences.c"
#include "../../options/parsenum.c"
#include "../../options/ecf.c"
#include "../../options/parseopt.c"
#undef main
#undef NDEBUG
#include <assert.h>

int32 ROMlib_options;
int ROMlib_delay, ROMlib_AppleChar, ROMlib_right_button_modifier;
int ROMlib_noclock, ROMlib_passpostscript, ROMlib_newlinetocr;
int ROMlib_directdiskaccess, ROMlib_nowarn32, ROMlib_flushoften;
int ROMlib_fontsubstitution, ROMlib_cacheheuristic;
int ROMlib_pretend_help, ROMlib_pretend_edition, ROMlib_pretend_script, ROMlib_pretend_alias;
int ROMlib_PretendSound, ROMlib_refresh, ROMlib_desired_bpp = 8;
int ROMlib_prefer_m68k;
int vdriver_bpp = 8;
int ROMlib_display_scale = 100;

void sdl_queue_window_size(int width, int height)
{
    (void)width;
    (void)height;
}

int vdriver_width = 512, vdriver_height = 342;
uint32 system_version = 0x711;
WriteWhenType ROMlib_when = WriteInOSEvent;
char* ROMlib_Comments;
__typeof__(ROMlib_offset) ROMlib_offset;
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    abort();
}

static unsigned char lowmem[4096];
char* program_name = "ecf-test";

void ROMlib_set_system_version(uint32 value)
{
    system_version = value;
}

void ROMlib_clockonoff(LONGINT on)
{
    (void)on;
}

void set_refresh_rate(int value)
{
    ROMlib_refresh = value;
}

void ROMlib_WriteWhen(WriteWhenType value)
{
    ROMlib_when = value;
}

INTEGER
C_HasDepth(GDHandle device, INTEGER depth, INTEGER flags, INTEGER flags2)
{
    (void)device;
    (void)depth;
    (void)flags;
    (void)flags2;
    return 1;
}

OSErr C_SetDepth(GDHandle device, INTEGER depth, INTEGER flags, INTEGER flags2)
{
    (void)device;
    (void)flags;
    (void)flags2;
    ROMlib_desired_bpp = depth;
    return noErr;
}

char *ROMlib_WindowName, *ROMlib_new_printer_name, *ROMlib_new_label;
uint32 ROMlib_PrDrvrVers = 70;
BOOLEAN ROMlib_dirtyvariant;
pair_t ROMlib_ScreenSize = {INITIALPAIRVALUE, INITIALPAIRVALUE};
pair_t ROMlib_MacSize = {INITIALPAIRVALUE, INITIALPAIRVALUE};
pair_t ROMlib_ScreenLocation = {INITIALPAIRVALUE, INITIALPAIRVALUE};

int main(void)
{
    ROMlib_extra_preferences prefs;
    const int32 legacy_options =
        ROMLIB_DIRTY_VARIANT_BIT | ROMLIB_TEXT_DISABLE_BIT | ROMLIB_NOLOWER_BIT | ROMLIB_PRINTING_HACK_BIT;
    ROMlib_offset = (uintptr_t)lowmem;
    pair_t pair;
    long number;
    unsigned i, bad;
    FILE* file;
    char text[4096];
    char* original;
    assert(parse_integer("09", 0, 100, &number) && number == 9);
    assert(parse_integer("0x100", 0, 65535, &number) && number == 256);
    assert(!parse_integer("99999999999999999999999", 0, INT_MAX, &number));
    assert(!parse_integer("1junk", 0, INT_MAX, &number));
    assert(!parse_integer("-1", 0, INT_MAX, &number));
    assert(parse_pair("800,600", 4, &pair) && pair.first == 800 && pair.second == 600);
    assert(!parse_pair("800", 4, &pair));
    assert(!parse_pair("511,342", 4, &pair));
    assert(!parse_pair("4097,342", 4, &pair));
    assert(parse_pair("-100,20", 6, &pair) && pair.first == -100);
    assert(parse_pair("", 4, &pair) && pair.first == INITIALPAIRVALUE);
    assert(parse_printer_version("7.1", &number) && number == 71);
    assert(!parse_printer_version("7.10", &number));
    assert(!parse_printer_version("3276.8", &number));
    original = malloc(4096);
    assert(original);
    memset(original, 'A', 4095);
    original[4095] = 0;
    ROMlib_WindowName = original;
    ROMlib_options = legacy_options;
    ROMlib_extra_preferences_init(&prefs);
    assert(ROMlib_extra_preferences_apply(&prefs));
    assert(ROMlib_WindowName == original && strlen(original) == 4095);
    for (i = 0; i < editable_option_count; ++i)
        prefs.flags |= extra_options[i].bit;
    for (i = 4; i <= 9; ++i)
        assert(!ROMlib_preferences_field_label(i));
    unsigned advanced = 0;
    for (i = 0; ROMlib_preferences_check_label(i); ++i) {
        const char* label = ROMlib_preferences_check_label(i);
        assert(!strstr(label, "print") && strcmp(label, "Cache heuristics") &&
               strcmp(label, "Alternate control redraw"));
        if (ROMlib_preferences_check_page(i) == ROMAW_PREF_ADVANCED)
            ++advanced;
        if (!strcmp(label, "Prefer m68k for FAT apps"))
            ROMlib_preferences_toggle(&prefs, i);
    }
    assert(advanced == 3);
    strcpy(prefs.fields[0], "Changed title");
    strcpy(prefs.fields[1], "12");
    strcpy(prefs.fields[2], "256");
    assert(!ROMlib_extra_preferences_valid(&prefs, &bad) && bad == 2);
    assert(!ROMlib_extra_preferences_apply(&prefs));
    assert(ROMlib_options == legacy_options && !ROMlib_delay && ROMlib_WindowName == original && !ROMlib_prefer_m68k);
    strcpy(prefs.fields[2], "20");
    strcpy(prefs.fields[3], "0x100");
    strcpy(prefs.fields[4], "800,600");
    strcpy(prefs.fields[6], "-50,20");
    strcpy(prefs.fields[7], "Printer name");
    strcpy(prefs.fields[8], "Printer \"label\"");
    strcpy(prefs.fields[9], "7.1");
    strcpy(prefs.fields[10], "8m");
    strcpy(prefs.fields[11], "16m");
    strcpy(prefs.fields[18], "1.5x");
    assert(ROMlib_extra_preferences_valid(&prefs, &bad));
    assert(ROMlib_extra_preferences_apply(&prefs));
    assert(ROMlib_prefer_m68k == 1);
    assert(ROMlib_display_scale == 150);
    assert(ROMlib_config_syszone_size == 8 * 1024 * 1024 && ROMlib_config_applzone_size == 16 * 1024 * 1024);
    assert(ROMlib_options == prefs.flags && ROMlib_dirtyvariant);
    assert(!strcmp(ROMlib_WindowName, "Changed title") && ROMlib_delay == 12);
    assert(ROMlib_AppleChar == 20 && ROMlib_right_button_modifier == 256);
    assert(ROMlib_ScreenSize.first == 800 && ROMlib_ScreenSize.second == 600);
    assert(ROMlib_ScreenLocation.first == -50 && ROMlib_PrDrvrVers == 71);
    file = tmpfile();
    assert(file);
    ROMlib_extra_preferences_save(file);
    rewind(file);
    text[fread(text, 1, sizeof text - 1, file)] = 0;
    fclose(file);
    for (i = 0; i < 11; ++i) {
        char* found = strstr(text, extra_options[i].name);
        assert(found && !strstr(found + strlen(extra_options[i].name), extra_options[i].name));
    }
    assert(strstr(text, "ScreenSize=800,600") && strstr(text, "PrinterName=\"Printer name\"") &&
           strstr(text, "PrinterTypeLabel=\"Printer 'label'\"") && strstr(text, "PrVers=7.1"));
    assert(strstr(text, "SysZone=8388608") && strstr(text, "ApplZone=16777216"));
    assert(strstr(text, "DisplayScale=150"));
    assert(strstr(text, "PreferM68K=1"));
    assert(!strcmp(ROMlib_new_label, "Printer \"label\""));
    free(ROMlib_WindowName);
    free(ROMlib_new_printer_name);
    free(ROMlib_new_label);
    puts("ECF preferences: bounded fields, atomic validation, option persistence and unchanged long strings passed");
    return 0;
}
