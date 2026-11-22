/*+===================================================================
  File:      hostdialog.h

  Summary:   Host modal dialogs. No Macintosh UI or guest pointers in this interface. ifndef RSYS_HOSTDIALOG_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Host modal dialogs. No Macintosh UI or guest pointers in this interface. */
#ifndef RSYS_HOSTDIALOG_H
#define RSYS_HOSTDIALOG_H
#include <stdio.h>

typedef struct {
    int width, height; /* Oriented paper size, in 1/72-inch points. */
    int first, last, copies;
    int left, top, right, bottom; /* Imageable margins in points. */
} host_print_settings;

/* 1 accepted, 0 cancelled, -1 failed. Cancellation preserves settings. */
int host_print_dialog(int page_setup, host_print_settings* settings);
FILE* host_print_spool(char** path);
int host_print_submit(const char* pdf_path);
void host_print_reset(void);
/* QuickDraw pages: top-down, MSB-first, zero white/one black. No interpreter. */
int host_raster_begin(int paper_width, int paper_height);
int host_raster_page(const unsigned char* pixels, int width, int height, int stride);
int host_raster_end(int cancel);
/* Titles are MacRoman. Paths/names use the platform filesystem encoding;
   caller frees *path. */
int host_file_dialog(int save, const char* title, const char* directory, const char* name, char** path);
/* Windows shell enumeration calls this synchronously for filesystem files.
   Directories remain navigable. Context and callback are native host values. */
typedef int (*host_file_filter)(const char* path, void* context);
int host_file_dialog_filtered(int save, const char* title, const char* directory, const char* name, char** path,
                              host_file_filter filter, void* context);
/* 0 cancelled, -1 failed, 1 accepted read-only, 2 accepted explicit read/write. */
int host_image_dialog(char** path);
/* Explain an incomplete source and offer explicit partial read-only access. */
int host_image_confirm_incomplete(const char* path, const char* explanation);
void host_dialog_error(const char* message);
#endif
