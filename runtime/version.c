/*+===================================================================
  File:      version.c

  Summary:   Implements version support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_set_system_version

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "rsys/version.h"
#include "rsys/gestalt.h"
#include "rsys/prefs.h"

#include "ResourceMgr.h"
#include "MemoryMgr.h"

/* The application version identifies the exact source commit. */
const char ROMlib_romaw_version[] = ROMAW_VERSION;

char* ROMlib_romaw_full_name = ROMAW_VERSION;

static void update_version_resource(OSType type, INTEGER id, const char* bytes, int length)
{
    INTEGER saved_map = CurMap;
    CurMap = SysMap;
    Handle h = C_Get1Resource(type, id);
    CurMap = saved_map;
    /* Editable System overrides may remove a version resource or leave its
       handle unloaded. Version reporting must not dereference either case. */
    if (!h)
        return;
    LoadResource(h);
    if (!STARH(h))
        return;
    HUnlock(h); /* safe to do -- app not running */
    SetHandleSize(h, length);
    if (MemErr != noErr || !STARH(h))
        return;
    memcpy(STARH(h), bytes, length);
    if (type == TICK("STR "))
        *(char*)STARH(h) = length - 1;
    else if (type == TICK("vers"))
        ((char*)STARH(h))[12] = length - 13;
}

#define UPDATE_RES(typ, id, fmt, args...)               \
    do {                                                \
        char str[1024];                                 \
        int len = snprintf(str, sizeof str, fmt, args); \
        if (len > 0 && len < sizeof str)                \
            update_version_resource(typ, id, str, len); \
    } while (0)

#define UPDATE_VERS(major, minor, rev, vers, fmt, args...)                                                       \
    ({                                                                                                           \
        int _major;                                                                                              \
        int _minor;                                                                                              \
        int _rev;                                                                                                \
                                                                                                                 \
        _major = (major);                                                                                        \
        _minor = (minor);                                                                                        \
        _rev = (rev);                                                                                            \
                                                                                                                 \
        UPDATE_RES(TICK("vers"), vers, "%c%c\x80%c%c%c\x5%d.%d.%d" fmt, _major, ((_minor << 4) | _rev), 0, 0, 0, \
                   _major, _minor, _rev, args);                                                                  \
    })

PUBLIC void ROMlib_set_system_version(uint32 version)
{
    static uint32 old_version = -1;

    if (version != old_version) {
        int major, minor, rev;

        enum { MINOR_MASK = 0xF, REV_MASK = 0xF };

        system_version = version;
        SysVersion = CW(version);

        major = version >> 8;
        minor = (version >> 4) & MINOR_MASK;
        rev = (version)&REV_MASK;
        gestalt_set_system_version(version);

        UPDATE_RES(TICK("STR "), 0,
                   "XROMAW System Software, "
                   "version %d.%d\r\r\r\xa9 ARDI 1986-2000\r"
                   "All rights reserved.",
                   major, minor);

        UPDATE_VERS(major, minor, rev, 1, "X%d.%d.%d \xa9 ARDI 1986-00", major, minor, rev);

        UPDATE_VERS(major, minor, rev, 2, "XSystem %d.%d Version %d.%d.%d", major, minor, major, minor, rev);

        old_version = version;
    }
}
