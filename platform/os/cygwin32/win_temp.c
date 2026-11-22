/*+===================================================================
  File:      win_temp.c

  Summary:   Implements win temp support for host-platform configuration and integration.

  Classes:   None.

  Functions: win_temp

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/error.h"

#include <windows.h>
#include <string.h>
#include "win_temp.h"

static void normalize_directory_name(char* str)
{
    char* p;
    size_t length;

    for (p = str; *p; ++p)
        if (*p == '\\')
            *p = '/';
    length = p - str;
    /* Keep a drive root absolute, including extended drive-root paths. */
    if (length > 1 && str[length - 1] == '/' && str[length - 2] != ':')
        str[length - 1] = 0;
}

PUBLIC char* win_temp(void)
{
    char buf[2048];
    DWORD length, capacity;
    char* result;

    length = GetTempPathA(sizeof buf, buf);
    if (!length)
        return NULL;
    if (length < sizeof buf) {
        result = malloc((size_t)length + 1);
        if (result)
            memcpy(result, buf, length + 1);
    }
    else {
        if (length == MAXDWORD)
            return NULL;
        capacity = length + 1;
        result = malloc(capacity);
        if (!result)
            return NULL;
        length = GetTempPathA(capacity, result);
        if (!length || length >= capacity) {
            free(result);
            return NULL;
        }
    }
    if (result)
        normalize_directory_name(result);
    return result;
}
