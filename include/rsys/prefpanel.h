/*+===================================================================
  File:      prefpanel.h

  Summary:   ifndef RSYS_PREFPANEL_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_PREFPANEL_H
#define RSYS_PREFPANEL_H
#include <stdint.h>
#include <stdio.h>

#define ROMAW_PREFS_MENU_LABEL "ROMAW Prefs"
#define ROMAW_PREFS_MENU_NAME "\013" ROMAW_PREFS_MENU_LABEL
extern void ROMlib_show_preferences(void);

typedef struct {
    int32_t flags;
    uint32_t checks;
    char fields[19][256], original[19][256];
} ROMlib_extra_preferences;

enum {
    ROMAW_PREF_CLI_STICKY = 1,
    ROMAW_PREF_CLI_FORWARD_DELETE = 2,
    ROMAW_PREF_CLI_AUTO_REFRESH = 4,
    ROMAW_PREF_CLI_SUBSTITUTE_FONTS = 8
};

extern void ROMlib_preferences_set_cli_overrides(unsigned mask);
extern int ROMlib_preferences_boolean_overridden(const int* value);
extern void ROMlib_preferences_capture_defaults(void);
extern void ROMlib_profile_preferences_init(ROMlib_extra_preferences* prefs, const char* filename, const char* section);
extern int ROMlib_profile_preferences_save(ROMlib_extra_preferences* prefs, const ROMlib_extra_preferences* original,
                                           const char* filename, const char* section);
extern void ROMlib_extra_preferences_init(ROMlib_extra_preferences* prefs);
extern int ROMlib_extra_preferences_valid(ROMlib_extra_preferences* prefs, unsigned* bad);
extern int ROMlib_extra_preferences_apply(ROMlib_extra_preferences* prefs);
extern void ROMlib_extra_preferences_save(FILE* file);
extern void ROMlib_default_preferences_init(ROMlib_extra_preferences* prefs, const char* filename);
extern int ROMlib_default_preferences_save(ROMlib_extra_preferences* prefs, const ROMlib_extra_preferences* original,
                                           const char* filename);
extern const char* ROMlib_preferences_field_label(unsigned field);
extern const char* ROMlib_preferences_check_label(unsigned check);

enum { ROMAW_PREF_COMPATIBILITY = 3, ROMAW_PREF_ADVANCED = 4, ROMAW_PREF_PAGE_COUNT = 5 };

extern int ROMlib_preferences_check_page(unsigned check);
extern int ROMlib_preferences_check(ROMlib_extra_preferences* prefs, unsigned check);
extern void ROMlib_preferences_toggle(ROMlib_extra_preferences* prefs, unsigned check);
extern int ROMlib_sdl_preferences(ROMlib_extra_preferences* prefs, const char* filename);
extern int ROMlib_win_preferences(ROMlib_extra_preferences* prefs, const char* filename);
extern void ROMlib_preferences_write_string(FILE* file, const char* name, const char* text);
extern void ROMlib_preferences_redraw(void);
extern void ROMlib_sdl_load_panel_font(void);
extern void ROMlib_sdl_image_progress_begin(void);
extern void ROMlib_sdl_image_progress_pump(void);
extern void ROMlib_sdl_image_progress_end(void);
extern int saveprefvalues(char* filename, int32_t x, int32_t y);
#endif
