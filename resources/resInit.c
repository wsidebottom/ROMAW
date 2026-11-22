/*+===================================================================
  File:      resInit.c

  Summary:   Implements res init support for classic Macintosh resources.

  Classes:   None.

  Functions: No externally linked function definitions.

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986 - 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "ResourceMgr.h"
#include "QuickDraw.h"
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "AliasMgr.h"

#include "rsys/resource.h"
#include "rsys/file.h"
#include "rsys/mman.h"
#include "rsys/flags.h"
#include "rsys/version.h"
#include "rsys/appearance.h"
#include "rsys/system_resources.h"
#include "rsys/notmac.h"
#include <sys/stat.h>

/* extracts an int32 from a string of the form [^(]*([0-9]*).*
 * () == 0, any invalid string == -1
 */

PRIVATE int32 extract_vers_num(Handle h)
{
    uint32 retval, old_retval;
    const char *p, *ep;
    boolean_t seen_left, seen_right, done;

    retval = old_retval = 0;
    seen_left = FALSE;
    seen_right = FALSE;
    done = FALSE;
    for (p = (char*)STARH(h), ep = p + GetHandleSize(h); !done && p < ep; ++p) {
        switch (*p) {
        case '(':
            if (seen_left)
                done = TRUE;
            else
                seen_left = TRUE;
            break;
        case ')':
            seen_right = TRUE;
            done = TRUE;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (seen_left)
                retval = retval * 10 + *p - '0';
            if (retval < old_retval)
                done = TRUE;
            else
                old_retval = retval;
            break;
        default:
            if (seen_left)
                done = TRUE;
            break;
        }
    }
    if (!seen_right)
        retval = -1;
    return retval;
}

P0(PUBLIC pascal trap, INTEGER, InitResources)
{
    /* Initialize globals */
    ZONE_SAVE_EXCURSION(SysZone, {
        Handle versh;
        int32 versnum;

        ROMlib_setreserr(noErr);
        str255assign(SysResName, SYSMACNAME);
        SysMap = CW(ROMlib_open_embedded_system());

        if (SysMap == CWC(-1)) {
            fprintf(stderr, "Unable to initialize embedded System resources\n");

            exit(1);
        }

        HIDDEN_RAW_SET(SysMapHndl_H, TopMapHndl);
        ROMlib_invalar();
        SetResLoad(TRUE);
        versh = GetResource(TICK("vers"), 1);
        versnum = extract_vers_num(versh);
        if (versnum < MINIMUM_SYSTEM_FILE_NEEDED)
            system_file_version_skew_p = TRUE;

        ROMlib_set_appearance();
        {
            char path[sizeof "/ROMAW.rsrc" + strlen(ROMlib_startdir)];
            struct stat info;
            int fd;
            INTEGER fonts;

            sprintf(path, "%s/ROMAW.rsrc", ROMlib_startdir);
            fd = open(path, O_RDONLY | O_BINARY);
            if (fd < 0)
                gui_fatal("Unable to open ROMAW fonts: %s", path);
            if (fstat(fd, &info) || info.st_size < 30 || info.st_size > 16 * 1024 * 1024) {
                close(fd);
                gui_fatal("Invalid ROMAW font suitcase: %s", path);
            }
            fonts = ROMlib_open_internal_resource(fd, info.st_size);
            if (fonts == -1)
                gui_fatal("Unable to load ROMAW font suitcase: %s", path);
            ROMlib_install_font_resources(fonts);
            CloseResFile(fonts);
        }
        {
            INTEGER volume;
            LONGINT directory;
            if (FindFolder((INTEGER)0x8000, kFontFolderType, FALSE, &volume, &directory) == noErr) {
                for (INTEGER index = 1; index > 0; ++index) {
                    CInfoPBRec info;
                    Str255 name;
                    INTEGER fonts;
                    memset(&info, 0, sizeof info);
                    PP_ASSIGN(info.hFileInfo.ioNamePtr, (StringPtr)name);
                    info.hFileInfo.ioVRefNum = volume;
                    info.hFileInfo.ioDirID = directory;
                    info.hFileInfo.ioFDirIndex = CW(index);
                    if (PBGetCatInfo(&info, FALSE) != noErr)
                        break;
                    /* Older builds installed their bundled fonts under this name. */
                    if ((info.hFileInfo.ioFlAttrib & 0x10) ||
                        !memcmp(name, "\013ROMAW Fonts", sizeof "\013ROMAW Fonts" - 1))
                        continue;
                    fonts = HOpenResFile(CW(volume), CL(directory), name, fsRdPerm);
                    if (fonts != -1) {
                        ROMlib_install_font_resources(fonts);
                        CloseResFile(fonts);
                    }
                }
            }
        }
        ROMlib_load_system_preferences();
    });

    return CW(SysMap);
}

P0(PUBLIC pascal trap, void, RsrcZoneInit) /* no op */
{
}
