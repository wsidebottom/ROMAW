/*+===================================================================
  File:      host_names.h

  Summary:   Declares host names support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef RSYS_HOST_NAMES_H
#define RSYS_HOST_NAMES_H
#include <stdint.h>
/* Convert a bounded host byte span to MacRoman without substitutions.
   Output capacity is length; return -1 when no lossless conversion exists. */
int ROMlib_host_name_to_mac(const char* input, int length, char* output);
int ROMlib_mac_name_to_utf8(const char* input, int length, char* output, int capacity);
struct stat;
int ROMlib_win_icon_stat(const char* path, struct stat* info);
int ROMlib_win_icon_open(const char* path, int flags, int mode);
void ROMlib_win_hide_desktop(int fd, const char* path);

typedef struct {
    uint32_t volume_serial;
    uint32_t index_high;
    uint32_t index_low;
} ROMlib_host_file_id;

int ROMlib_host_get_file_id(const char* path, ROMlib_host_file_id* id);
char* ROMlib_host_locate_file_id(const char* volume_path, const ROMlib_host_file_id* id);
#endif
