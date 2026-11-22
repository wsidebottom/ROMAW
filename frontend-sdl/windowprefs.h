/*+===================================================================
  File:      windowprefs.h

  Summary:   Per-application host-window and classic file-dialog geometry persistence.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_WINDOWPREFS_H
#define ROMAW_WINDOWPREFS_H

/* Sizes describe the client area, excluding the host window decorations. */
void window_prefs_init(const char* filename, int min_width, int min_height, int max_width, int max_height);
int window_size_parse(const char* text, int min_width, int min_height, int max_width, int max_height, int* width,
                      int* height);
int window_prefs_select(const unsigned char* name, unsigned long type, unsigned long creator, int* width, int* height);
/* Disable bootstrap writes until the selected application is ready to appear.
   Reads remain available while saving is disabled. */
void window_prefs_set_saving_enabled(int enabled);
int window_prefs_save(int width, int height);
int window_location_parse(const char* text, int* x, int* y);
int window_prefs_location(int* x, int* y);
int window_prefs_save_location(int x, int y);

/* Classic guest file dialogs have independent Open/Save geometry per app. */
int file_dialog_prefs_load(int saving, int* width, int* height, int* x, int* y);
int file_dialog_prefs_save(int saving, int width, int height, int x, int y);

#endif
