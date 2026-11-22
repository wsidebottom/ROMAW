/*+===================================================================
  File:      macinfo.c

  Summary:   Implements macinfo support for host operating-system support.

  Classes:   None.

  Functions: ROMlib_mac_info_control, ROMlib_mac_info_request, ROMlib_mac_info_take

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/common.h"
#include "FileMgr.h"
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/notmac.h"
#include "rsys/macinfo.h"
#include "rsys/desk.h"
#include "rsys/vdriver.h"
static char* requested;

int ROMlib_mac_info_control(unsigned action)
{
    (void)action;
    return 0;
}

int ROMlib_mac_info_request(const char* path)
{
    char* copy = copystr(path);
    if (!copy)
        return 0;
    free(requested);
    requested = copy;
    return 1;
}

int ROMlib_mac_info_take(char* out, size_t capacity)
{
    if (!requested)
        return 0;
    char* path = requested;
    requested = NULL;
    for (char* p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
    if (!strncmp(path, "image:", 6)) {
        char* part = path + 6;
        size_t used = 0;
        while (*part) {
            char* slash = strchr(part, '/');
            size_t length = slash ? (size_t)(slash - part) : strlen(part);
            length -= ROMlib_UNIX7_to_Mac(part, length);
            if (length > 31 || used + length + 2 > capacity) {
                free(path);
                return bdNamErr;
            }
            memcpy(out + used, part, length);
            used += length;
            if (!slash)
                break;
            out[used++] = ':';
            part = slash + 1;
        }
        out[used] = 0;
        free(path);
        return 1;
    }
    ROMlib_automount(path);
    HVCB* volume = ROMlib_vcbbybiggestunixname(path);
    if (!volume) {
        free(path);
        return nsvErr;
    }
    size_t root = strlen(((VCBExtra*)volume)->unixname);
    size_t used = volume->vcbVN[0];
    if (capacity < used + 2) {
        free(path);
        return bdNamErr;
    }
    memcpy(out, volume->vcbVN + 1, used);
    out[used++] = ':';
    const char* part = path + root;
    while (*part == '/')
        ++part;
    while (*part) {
        const char* slash = strchr(part, '/');
        size_t length = slash ? (size_t)(slash - part) : strlen(part);
        char name[256];
        if (length > 255) {
            free(path);
            return bdNamErr;
        }
        memcpy(name, part, length);
        length -= ROMlib_UNIX7_to_Mac(name, length);
        if (used + length + 2 > capacity) {
            free(path);
            return bdNamErr;
        }
        memcpy(out + used, name, length);
        used += length;
        if (!slash || !slash[1])
            break;
        out[used++] = ':';
        part = slash + 1;
    }
    out[used] = 0;
    free(path);
    return 1;
}
