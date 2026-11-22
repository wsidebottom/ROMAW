/*+===================================================================
  File:      metadata.h

  Summary:   Declares metadata support for finder metadata package support.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/finder-metadata/include/finder_metadata; original attribution and
             provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             finder-metadata/LICENSE.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef FINDER_METADATA_H
#define FINDER_METADATA_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FinderWindowState {
    int x, y, width, height, view;
} FinderWindowState;

typedef struct FinderItemPosition {
    char name[256];
    /* Icon centre, relative to the unscrolled content below the status bar. */
    int x, y, present;
} FinderItemPosition;

int finder_layout_read(const char* folder, FinderItemPosition* items, size_t count, char* error, size_t capacity);
int finder_layout_write(const char* folder, FinderItemPosition* items, size_t count, char* error, size_t capacity);
void finder_layout_schedule(const char* folder, const FinderItemPosition* items, size_t count);
/* 1 found/saved, 0 absent, -1 error. Existing unknown records are preserved. */
int finder_window_read(const char* folder, FinderWindowState* state, char* error, size_t capacity);
int finder_window_write(const char* folder, const FinderWindowState* state, char* error, size_t capacity);
int finder_icon_position(const char* folder, const char* name, int* x, int* y, int write, char* error, size_t capacity);
int finder_metadata_rename(const char* folder, const char* old_name, const char* new_name, char* error,
                           size_t capacity);
/* Session paths are UTF-8 and stored quoted in ROMAW.prefs. */
typedef int (*FinderSessionVisitor)(const char*, void*);
int finder_native_session_read(FinderSessionVisitor visitor, void* context);
int finder_native_session_write(const char* const* paths, const FinderWindowState* states, size_t count);
int finder_native_session_window(const char* path, FinderWindowState* state);
/* UI writes are serialized off the event thread and drained at clean exit. */
void finder_window_schedule(const char* folder, const FinderWindowState* state);
void finder_metadata_flush(void);
#ifdef __cplusplus
}
#endif
#endif
