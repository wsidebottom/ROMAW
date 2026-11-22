/*+===================================================================
  File:      alias.c

  Summary:   Implements alias support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: ROMlib_follow_finder_alias, ROMlib_get_folder_name, ROMlib_is_alias_file,
             ROMlib_resolve_alias_file_with_mount_flags

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"

#include <stdarg.h>

#include "FileMgr.h"
#include "AliasMgr.h"
#include "MemoryMgr.h"
#include "ToolboxUtil.h"
#include "OSUtil.h"
#include "ResourceMgr.h"

#include "rsys/file.h"
#include "rsys/hfs.h"
#include "rsys/string.h"
#if defined(CYGWIN32)
#include "win_temp.h"
#endif

#include "rsys/alias.h"
#include "rsys/alias_record.h"
#include "rsys/notmac.h"

#define paramErr (-50)

/* NOTE: if we want to be more like the Mac, we should have a 'fld#',0
   resource that will have in it: type, four bytes of 0, pascal string,
   potential padding to even things up, type, four bytes of 0, ... */

PRIVATE const char* find_sub_dir(OSType folderType)
{
    typedef struct {
        OSType type;
        const char* name;
    } sys_sub_dir_match_t;

    static sys_sub_dir_match_t matches[] = {
        {
            kPrintMonitorDocsFolderType,
            "PrintMonitor Documents",
        },
        {
            kStartupFolderType,
            "Startup Items",
        },
        {
            kAppleMenuFolderType,
            "Apple Menu Items",
        },
        {
            kExtensionFolderType,
            "Extensions",
        },
        {
            kPreferencesFolderType,
            "Preferences",
        },
        {
            kControlPanelFolderType,
            "Control Panels",
        },
        {
            kFontFolderType,
            "Fonts",
        },
        {
            kTextEncodingsFolderType,
            "Text Encodings",
        },
        {
            TICK("prof"),
            "ColorSync Profiles",
        },
    };
    int i;
    const char* retval;

    for (i = 0; i < (int)NELEM(matches) && matches[i].type != folderType; ++i)
        ;
    if (i < (int)NELEM(matches))
        retval = matches[i].name;
    else
        retval = 0;
    return retval;
}

PRIVATE OSErr get_sys_vref_and_dirid(INTEGER* sys_vrefp, LONGINT* sys_diridp)
{
    OSErr err;
    WDPBRec wdp;

    wdp.ioVRefNum = BootDrive;
    wdp.ioWDIndex = CWC(0);
    FIELD_ASSIGN_RAW(wdp.ioNamePtr, CLC(0));
    err = PBGetWDInfo(&wdp, FALSE);
    if (err == noErr) {
        *sys_vrefp = CW(wdp.ioWDVRefNum);
        *sys_diridp = CL(wdp.ioWDDirID);
    }
    return err;
}

/* FolderMgr.a GetFolderName (AliasDispatch 14) returns hard-wired names
   and preserves the supplied volume reference, without touching the disk. */
OSErr ROMlib_get_folder_name(INTEGER volume, OSType type, INTEGER* found_volume, StringPtr name)
{
    const char* text;
    switch (type) {
    case kDesktopFolderType:
        text = "Desktop Folder";
        break;
    case kTrashFolderType:
        text = "Trash";
        break;
    case kWhereToEmptyTrashFolderType:
        text = "Network Trash Folder";
        break;
    case kTemporaryFolderType:
        text = "Temporary Items";
        break;
    default:
        return paramErr;
    }
    if (!found_volume || !name)
        return paramErr;
    str255_from_c_string(name, text);
    *found_volume = CW(volume);
    return noErr;
}

PRIVATE OSErr try_to_find(INTEGER vref, const char* str, INTEGER* vrefp, LONGINT* diridp)
{
    OSErr err;
    HVCB* vcbp;

    warning_trace_info("str = '%s'", str);
    vcbp = ROMlib_vcbbybiggestunixname(str);

    if (!vcbp)
        err = nsvErr;
    else {
        VCBExtra* vcbextrap;
        struct stat sbuf;

        vcbextrap = (VCBExtra*)vcbp;
        warning_trace_info("unixname = '%s'", vcbextrap->unixname);
        if (Ustat(str, &sbuf) < 0) {
            warning_trace_info("stat failed, errno = %d", errno);
            err = ROMlib_maperrno();
        }
        else {
            warning_trace_info("ino = %ld, ufs.ino = %ld", (long)ST_INO(sbuf), (long)vcbextrap->u.ufs.ino);
            if (vref != CW(vcbp->vcbVRefNum))
                err = fnfErr;
            else {
                *vrefp = vref;
                if (ST_INO(sbuf) == vcbextrap->u.ufs.ino)
                    *diridp = 2;
                else
                    *diridp = ST_INO(sbuf);
                err = noErr;
            }
        }
    }
    warning_trace_info("err = %d", err);
    return err;
}

PRIVATE OSErr look_for_volume(const char* vol_name, INTEGER* vrefp, LONGINT* diridp)
{
    OSErr retval;
    ParamBlockRec pbr;
    Str255 pvol_name;

    str255_from_c_string(pvol_name, vol_name);
    FIELD_ASSIGN_RAW(pbr.volumeParam.ioNamePtr, RM(&pvol_name[0]));
    pbr.volumeParam.ioVolIndex = CLC(-1);
    pbr.volumeParam.ioVRefNum = CWC(0);
    retval = PBGetVInfo(&pbr, FALSE);
    if (retval == noErr) {
        *vrefp = CW(pbr.volumeParam.ioVRefNum);
        *diridp = 2;
    }
    return retval;
}

/* A temporary folder must be an actual child directory. Returning the volume
   root makes Finder construct a folder object with no ordinary parent, then
   dispatch through that null parent when refreshing the volume window. The
   classic Folder Manager uses "Temporary Items" in the requested root. */

PRIVATE OSErr test_directory(INTEGER, LONGINT, const char*, LONGINT*);
PRIVATE OSErr create_directory(INTEGER, LONGINT, const char*, LONGINT*);

PRIVATE OSErr last_chance_tmp_vref_and_dirid(INTEGER vref, Boolean create, INTEGER* tmp_vrefp, LONGINT* tmp_diridp)
{
    OSErr retval;
    HParamBlockRec pb;

    memset(&pb, 0, sizeof pb);
    pb.volumeParam.ioVRefNum = CW(vref);
    retval = PBHGetVInfo(&pb, FALSE);
    if (retval == noErr) {
        static const char* top_level_names[] = {"tmp", "temp", "Temporary Items"};
        int i;
        OSErr err;
        LONGINT directory;

        for (i = 0, err = fnfErr; err != noErr && i < NELEM(top_level_names); ++i) {
            err = test_directory(CW(pb.volumeParam.ioVRefNum), 2, top_level_names[i], &directory);
        }
        if (err == fnfErr && create)
            err = create_directory(CW(pb.volumeParam.ioVRefNum), 2, "Temporary Items", &directory);
        if (err == noErr) {
            *tmp_vrefp = CW(pb.volumeParam.ioVRefNum);
            *tmp_diridp = directory;
        }
        retval = err;
    }

    return retval;
}

PRIVATE OSErr get_tmp_vref_and_dirid(INTEGER vref, Boolean create, INTEGER* tmp_vrefp, LONGINT* tmp_diridp)
{
    int i;
    OSErr retval;
    HParamBlockRec volume;
    static const char* guesses[] = {

#if !defined(CYGWIN32)
        "/tmp",
#else
#if defined(CYGWIN32)
        NULL,
#endif
        "c:/tmp", "c:/temp", "c:/"
#endif
    };

    /* FindFolder accepts kOnSystemDisk, zero and working-directory refs,
     while host-path matching requires the actual mounted volume refnum. */
    if (vref == (INTEGER)0x8000) {
        LONGINT sys_dirid;
        retval = get_sys_vref_and_dirid(&vref, &sys_dirid);
    }
    else {
        memset(&volume, 0, sizeof volume);
        volume.volumeParam.ioVRefNum = CW(vref);
        retval = PBHGetVInfo(&volume, FALSE);
        if (retval == noErr)
            vref = CW(volume.volumeParam.ioVRefNum);
    }
    if (retval != noErr)
        return retval;

    {
        static boolean_t been_here_p = FALSE;

        if (!been_here_p) {
            int j;

#if defined(CYGWIN32)
            guesses[0] = win_temp();
#endif

            for (j = 0; j < (int)NELEM(guesses); ++j) {
                char* p;

                if (guesses[j]) {
                    p = alloca(strlen(guesses[j]) + 1);
                    strcpy(p, guesses[j]);
                    ROMlib_automount(p);
                }
            }
            been_here_p = TRUE;
        }
    }

    retval = look_for_volume("tmp:", tmp_vrefp, tmp_diridp);
    if (retval == noErr && *tmp_diridp == 2)
        retval = fnfErr;

    for (i = 0; retval != noErr && i < (int)NELEM(guesses); ++i)
        if (guesses[i]) {
            retval = try_to_find(vref, guesses[i], tmp_vrefp, tmp_diridp);
            if (retval == noErr && *tmp_diridp == 2)
                retval = fnfErr;
        }

    if (retval != noErr)
        retval = last_chance_tmp_vref_and_dirid(vref, create, tmp_vrefp, tmp_diridp);

    return retval;
}

PRIVATE OSErr test_directory(INTEGER vref, LONGINT dirid, const char* sub_dirp, LONGINT* new_idp)
{
    OSErr err;
    CInfoPBRec cpb;
    Str255 file_name;

    str255_from_c_string(file_name, sub_dirp);
    FIELD_ASSIGN_RAW(cpb.hFileInfo.ioNamePtr, (StringPtr)RM((Ptr)file_name));
    cpb.hFileInfo.ioVRefNum = CW(vref);
    cpb.hFileInfo.ioFDirIndex = CWC(0);
    cpb.hFileInfo.ioDirID = CL(dirid);
    err = PBGetCatInfo(&cpb, FALSE);
    if (err == noErr && !(cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
        err = dupFNErr;
    if (err == noErr)
        *new_idp = CL(cpb.dirInfo.ioDrDirID);
    return err;
}

PRIVATE OSErr create_directory(INTEGER sys_vref, LONGINT sys_dirid, const char* sub_dirp, LONGINT* new_idp)
{
    HParamBlockRec pb;
    Str255 name;
    OSErr err;

    memset(&pb, 0, sizeof pb);
    str255_from_c_string(name, sub_dirp);
    FIELD_ASSIGN_RAW(pb.ioParam.ioNamePtr, RM((Ptr)name));
    pb.ioParam.ioVRefNum = CW(sys_vref);
    pb.fileParam.ioDirID = CL(sys_dirid);
    err = PBDirCreate(&pb, FALSE);
    if (err == noErr)
        *new_idp = CL(pb.fileParam.ioDirID);
    return err;
}

/* Trash selectors describe a directory on the requested volume. In
   particular, the temporary-directory fallback can return the volume root,
   whose parent ID 1 cannot be used as an ordinary Finder folder object. */
PRIVATE OSErr find_volume_trash(INTEGER requested, Boolean create, INTEGER* volume, LONGINT* directory)
{
    INTEGER actual;
    LONGINT trash;
    OSErr err;
    if (requested == (INTEGER)0x8000) {
        LONGINT system_directory;
        err = get_sys_vref_and_dirid(&actual, &system_directory);
    }
    else {
        HParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.volumeParam.ioVRefNum = CW(requested);
        err = PBHGetVInfo(&pb, FALSE);
        actual = CW(pb.volumeParam.ioVRefNum);
    }
    if (err != noErr)
        return err;
    err = test_directory(actual, 2, "Trash", &trash);
    if (err == fnfErr && create)
        err = create_directory(actual, 2, "Trash", &trash);
    if (err == noErr) {
        *volume = actual;
        *directory = trash;
    }
    return err;
}

P5(PUBLIC pascal trap, OSErr, FindFolder, int16, vRefNum, OSType, folderType, Boolean, createFolder, int16*,
   foundVRefNum, int32*, foundDirID)
{
    OSErr retval;
    const char* sub_dir;

    sub_dir = find_sub_dir(folderType);
    if (sub_dir) {
        INTEGER sys_vref;
        LONGINT sys_dirid, new_id;

        retval = get_sys_vref_and_dirid(&sys_vref, &sys_dirid);
        if (retval == noErr) {
            retval = test_directory(sys_vref, sys_dirid, sub_dir, &new_id);
            if (retval == fnfErr && createFolder)
                retval = create_directory(sys_vref, sys_dirid, sub_dir, &new_id);
            if (retval == noErr) {
                *foundVRefNum = CW(sys_vref);
                *foundDirID = CL(new_id);
            }
        }
    }
    else
        switch (folderType) {
        case 0x65786472: /* 'exdr' */
        {
            /* ROMAW extension: expose the per-user runtime directory, so guest
             caches need not assume a drive mapping or System Folder layout. */
            HVCB* volume = ROMlib_vcbbybiggestunixname(ROMlib_userdir);
            INTEGER found_volume;
            LONGINT directory;
            retval = volume ? try_to_find(CW(volume->vcbVRefNum), ROMlib_userdir, &found_volume, &directory) : nsvErr;
            if (retval == noErr) {
                *foundVRefNum = CW(found_volume);
                *foundDirID = CL(directory);
            }
        } break;
        case kSystemFolderType: {
            INTEGER sys_vref;
            LONGINT sys_dirid;

            retval = get_sys_vref_and_dirid(&sys_vref, &sys_dirid);
            if (retval == noErr) {
                /* NOTE: IMVI 9-44 tells us to not create System Folder if it
           doesn't already exist */
                *foundVRefNum = CW(sys_vref);
                *foundDirID = CL(sys_dirid);
            }
        } break;
        case kDesktopFolderType: {
            INTEGER sys_vref, desktop_vref;
            LONGINT sys_dirid, parent_id = 2, desktop_id;
            HParamBlockRec volume;
            CInfoPBRec directory;

            retval = get_sys_vref_and_dirid(&sys_vref, &sys_dirid);
            if (retval != noErr)
                break;
            desktop_vref = sys_vref;
            if (vRefNum != (int16)0x8000) /* kOnSystemDisk */
            {
                memset(&volume, 0, sizeof volume);
                volume.volumeParam.ioVRefNum = CW(vRefNum);
                retval = PBHGetVInfo(&volume, FALSE);
                if (retval != noErr)
                    break;
                desktop_vref = CW(volume.volumeParam.ioVRefNum);
            }

            /* Host volumes can contain the portable System Folder below their
         root. Keep that installation's desktop beside its System Folder. */
            if (desktop_vref == sys_vref) {
                memset(&directory, 0, sizeof directory);
                directory.dirInfo.ioVRefNum = CW(sys_vref);
                directory.dirInfo.ioFDirIndex = CWC(-1);
                directory.dirInfo.ioDrDirID = CL(sys_dirid);
                retval = PBGetCatInfo(&directory, FALSE);
                if (retval != noErr)
                    break;
                parent_id = CL(directory.dirInfo.ioDrParID);
            }
            retval = test_directory(desktop_vref, parent_id, "Desktop Folder", &desktop_id);
            if (retval == fnfErr && createFolder)
                retval = create_directory(desktop_vref, parent_id, "Desktop Folder", &desktop_id);
            if (retval == noErr) {
                *foundVRefNum = CW(desktop_vref);
                *foundDirID = CL(desktop_id);
            }
        } break;
        case kTrashFolderType:
        case kWhereToEmptyTrashFolderType: {
            INTEGER trash_volume;
            LONGINT trash_directory;
            retval = find_volume_trash(vRefNum, createFolder, &trash_volume, &trash_directory);
            if (retval == noErr) {
                *foundVRefNum = CW(trash_volume);
                *foundDirID = CL(trash_directory);
            }
        } break;
        case kTemporaryFolderType:
            /* These cases aren't properly handled, but they should allow some apps
         to get further */
            {
                INTEGER tmp_vref;
                LONGINT tmp_dirid;

                retval = get_tmp_vref_and_dirid(vRefNum, createFolder, &tmp_vref, &tmp_dirid);
                if (retval == noErr) {
                    *foundVRefNum = CW(tmp_vref);
                    *foundDirID = CL(tmp_dirid);
                }
            }
            break;
        default:
            warning_unexpected("unknown folderType");
            retval = fnfErr;
            break;
        }
    return retval;
}

/* A Pascal path bounds the useful ancestor walk as well as its output. */
#define ALIAS_MAX_ANCESTORS 128

static OSErr alias_directory(INTEGER volume, uint32 directory, uint32* actual, uint32* parent, Str255 name)
{
    CInfoPBRec pb = {0};
    OSErr err;
    name[0] = 0;
    FIELD_ASSIGN_RAW(pb.dirInfo.ioNamePtr, RM((Ptr)name));
    pb.dirInfo.ioVRefNum = CW(volume);
    pb.dirInfo.ioFDirIndex = CWC(-1);
    pb.dirInfo.ioDrDirID = CL(directory);
    err = PBGetCatInfo(&pb, FALSE);
    if (err == noErr) {
        if (!(pb.dirInfo.ioFlAttrib & ATTRIB_ISADIR))
            return dirNFErr;
        *actual = CL(pb.dirInfo.ioDrDirID);
        *parent = CL(pb.dirInfo.ioDrParID);
        if (!*actual || (*actual != 2 && (*parent < 2 || *parent == *actual)))
            return dirNFErr;
    }
    return err;
}

static OSErr alias_volume(INTEGER volume, INTEGER* actual)
{
    HParamBlockRec pb = {0};
    OSErr err;
    pb.volumeParam.ioVRefNum = CW(volume);
    err = PBHGetVInfo(&pb, FALSE);
    if (err == noErr)
        *actual = CW(pb.volumeParam.ioVRefNum);
    return err;
}

static size_t alias_write_tag(unsigned char* out, unsigned tag, const void* bytes, unsigned length)
{
    out[0] = tag >> 8;
    out[1] = tag;
    out[2] = length >> 8;
    out[3] = length;
    if (length)
        memcpy(out + 4, bytes, length);
    if (length & 1)
        out[4 + length] = 0;
    return 4 + length + (length & 1);
}

static OSErr alias_complete_paths(const FSSpec* from, const FSSpec* target, Handle alias)
{
    alias_head_t head;
    uint32 ids[ALIAS_MAX_ANCESTORS], visited[ALIAS_MAX_ANCESTORS];
    unsigned char reverse[256], tags[1024];
    Str255 name, parent_name = {0};
    size_t count = 0, start, amount = 0;
    uint32 current, actual, parent;
    INTEGER target_volume, from_volume;
    int from_levels = -1, to_levels = -1;
    OSErr err;
    Size old_size, new_size;
    memcpy(&head, STARH(alias), sizeof head);
    err = alias_volume(CW(target->vRefNum), &target_volume);
    if (err != noErr)
        return err;
    current = CL(head.zero_or_neg_one);
    start = sizeof reverse - head.fileName[0];
    memcpy(reverse + start, head.fileName + 1, head.fileName[0]);
    if (current == 1)
        start = sizeof reverse; /* A volume alias has no separate leaf. */
    while (current != 1 && current != 2) {
        if (count == ALIAS_MAX_ANCESTORS)
            return bdNamErr;
        err = alias_directory(target_volume, current, &actual, &parent, name);
        if (err != noErr)
            return err;
        if (actual == 2)
            break;
        for (size_t i = 0; i < count; ++i)
            if (ids[i] == actual)
                return dirNFErr;
        ids[count++] = actual;
        if (count == 1)
            memcpy(parent_name, name, name[0] + 1);
        if (!name[0] || name[0] + 1U > start)
            return bdNamErr;
        start -= name[0] + 1;
        memcpy(reverse + start, name + 1, name[0]);
        reverse[start + name[0]] = ':';
        current = parent;
    }
    if (sizeof reverse - start + head.volumeName[0] + 1 > 255)
        return bdNamErr;
    start -= head.volumeName[0] + 1;
    memcpy(reverse + start, head.volumeName + 1, head.volumeName[0]);
    reverse[start + head.volumeName[0]] = ':';

    if (from && CL(from->parID) != 1 && CL(head.zero_or_neg_one) != 1) {
        err = alias_volume(CW(from->vRefNum), &from_volume);
        if (err != noErr)
            return err;
        if (from_volume == target_volume) {
            current = CL(from->parID);
            for (unsigned level = 0; level < ALIAS_MAX_ANCESTORS; ++level) {
                if (current == 2) {
                    from_levels = level + 1;
                    to_levels = count + 1;
                    break;
                }
                err = alias_directory(from_volume, current, &actual, &parent, name);
                if (err != noErr)
                    return err;
                for (unsigned i = 0; i < level; ++i)
                    if (visited[i] == actual)
                        return dirNFErr;
                visited[level] = actual;
                for (size_t i = 0; i < count; ++i)
                    if (ids[i] == actual) {
                        from_levels = level + 1;
                        to_levels = i + 1;
                        break;
                    }
                if (from_levels != -1)
                    break;
                if (actual == 2) {
                    from_levels = level + 1;
                    to_levels = count + 1;
                    break;
                }
                current = parent;
            }
            if (from_levels == -1)
                return dirNFErr;
        }
    }
    if (parent_name[0])
        amount += alias_write_tag(tags + amount, 0, parent_name + 1, parent_name[0]);
    for (size_t i = 0; i < count; ++i)
        ids[i] = CL(ids[i]);
    amount += alias_write_tag(tags + amount, 1, ids, count * sizeof ids[0]);
    amount += alias_write_tag(tags + amount, 2, reverse + start, sizeof reverse - start);
    amount += alias_write_tag(tags + amount, 0xffff, NULL, 0);
    old_size = GetHandleSize(alias);
    new_size = old_size - 4 + amount;
    SetHandleSize(alias, new_size);
    err = MemError();
    if (err == noErr) {
        memcpy(STARH(alias) + old_size - 4, tags, amount);
        alias_head_t* updated = (alias_head_t*)STARH(alias);
        updated->length = CW(new_size);
        updated->mystery_words[0] = CW(from_levels);
        updated->mystery_words[1] = CW(to_levels);
    }
    return err;
}

P3(PUBLIC pascal trap, OSErr, NewAlias, FSSpecPtr, fromFile, FSSpecPtr, target, HIDDEN_AliasHandle*, alias)
{
    OSErr retval;

    if (!alias)
        return paramErr;
    HIDDEN_RAW_SET((*alias), NULL);
    retval = NewAliasMinimal(target, alias);
    if (retval == noErr) {
        Handle fresh = HIDDEN_NATIVE(*alias);
        retval = alias_complete_paths(fromFile, target, fresh);
        if (retval != noErr) {
            DisposHandle(fresh);
            HIDDEN_RAW_SET(*alias, NULL);
        }
    }
    return retval;
}

P4(PUBLIC pascal trap, OSErr, UpdateAlias, FSSpecPtr, fromFile, FSSpecPtr, target, AliasHandle, alias, Boolean*,
   wasChanged)
{
    HIDDEN_AliasHandle replacement;
    AliasHandle fresh;
    Size old_size, new_size, extra;
    OSErr err;
    SignedByte state;
    if (!wasChanged)
        return paramErr;
    *wasChanged = FALSE;
    if (!target || !alias || !STARH(alias))
        return paramErr;
    old_size = GetHandleSize(alias);
    if (!alias_record_valid((unsigned char*)STARH(alias), old_size))
        return paramErr;
    state = HGetState(alias);
    HLock(alias);
    extra = old_size - alias_word((unsigned char*)STARH(alias) + 4);
    err = NewAlias(fromFile, target, &replacement);
    if (err != noErr) {
        HSetState(alias, state);
        return err;
    }
    fresh = HIDDEN_NATIVE(replacement);
    new_size = GetHandleSize(fresh);
    /* Preserve application-owned data after aliasSize and the user type. */
    BlockMove(STARH(alias), STARH(fresh), 4);
    if (new_size < 0 || extra > INT32_MAX - new_size)
        err = memFullErr;
    else {
        if (extra)
            PtrAndHand(STARH(alias) + old_size - extra, fresh, extra);
        err = MemError();
    }
    HSetState(alias, state);
    if (err == noErr && (old_size != new_size + extra || memcmp(STARH(alias), STARH(fresh), old_size))) {
        SetHandleSize(alias, new_size + extra);
        err = MemError();
        if (err == noErr) {
            BlockMove(STARH(fresh), STARH(alias), new_size + extra);
            *wasChanged = TRUE;
        }
    }
    DisposHandle(fresh);
    return err;
}

enum {
    FULL_PATH_TAG = 0x0002,
    TAIL_TAG = 0x0009,
};

static OSErr alias_candidate_kind(const alias_head_t* head, FSSpecPtr candidate)
{
    CInfoPBRec pb = {0};
    OSErr err;
    FIELD_ASSIGN_RAW(pb.hFileInfo.ioNamePtr, RM((Ptr)candidate->name));
    pb.hFileInfo.ioVRefNum = candidate->vRefNum;
    pb.hFileInfo.ioDirID = candidate->parID;
    err = PBGetCatInfo(&pb, FALSE);
    if (err == noErr && !!(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR) != (CW(head->usually_0) == 1))
        err = fnfErr;
    return err;
}

static OSErr alias_resolve_relative(const FSSpec* from, AliasHandle alias, FSSpecPtr target)
{
    alias_head_t head;
    Str255 path, name;
    FSSpec source, found;
    const unsigned char* absolute;
    size_t length;
    uint32 directory, actual, parent;
    uint32 visited[ALIAS_MAX_ANCESTORS];
    int levels;
    OSErr err;
    if (!from)
        return fnfErr;
    source = *from;
    if (source.name[0] > 63)
        return paramErr;
    if (!alias || !alias_record_valid((unsigned char*)STARH(alias), GetHandleSize(alias)))
        return paramErr;
    memcpy(&head, STARH(alias), sizeof head);
    levels = CW(head.mystery_words[0]);
    absolute = alias_record_tag((unsigned char*)STARH(alias), 2, &length);
    if (levels < 1 || levels > ALIAS_MAX_ANCESTORS ||
        !alias_relative_path(absolute, length, CW(head.mystery_words[1]), head.fileName, path))
        return fnfErr;
    directory = CL(source.parID);
    if (directory == 1)
        return fnfErr;
    for (int i = 1; i < levels && directory != 2; ++i) {
        err = alias_directory(CW(source.vRefNum), directory, &actual, &parent, name);
        if (err != noErr)
            return err;
        for (int j = 1; j < i; ++j)
            if (visited[j - 1] == actual)
                return dirNFErr;
        visited[i - 1] = actual;
        if (actual == 2) {
            directory = 2;
            break;
        }
        directory = parent;
    }
    err = FSMakeFSSpec(CW(source.vRefNum), directory, path, &found);
    if (err == noErr)
        err = alias_candidate_kind(&head, &found);
    if (err == noErr)
        *target = found;
    return err;
}

/* Resolve stored names against the real catalog. Copy all record data before
   File Manager calls, which may move or purge the caller's alias handle. */
static OSErr alias_resolve_absolute(AliasHandle alias, FSSpecPtr target)
{
    alias_head_t head;
    Str255 volume, path;
    FSSpec resolved;
    HParamBlockRec pb = {0};
    const unsigned char* full_path;
    size_t path_length;
    OSErr err;
    LONGINT parent;
    if (!alias || !target || !alias_record_valid((unsigned char*)STARH(alias), GetHandleSize(alias)))
        return paramErr;
    memcpy(&head, STARH(alias), sizeof head);
    full_path = alias_record_tag((unsigned char*)STARH(alias), FULL_PATH_TAG, &path_length);
    err = fnfErr;
    if (full_path) {
        if (!path_length || path_length > 255)
            return paramErr;
        path[0] = path_length;
        memcpy(path + 1, full_path, path_length);
        err = FSMakeFSSpec(0, 0, path, &resolved);
    }
    /* Classic catalog names can truncate a long host directory name in the
       path tag. The stored parent ID still identifies that real directory.
       Also use it when a formerly valid absolute path no longer resolves. */
    if (!full_path || err == fnfErr || err == dirNFErr) {
        parent = CL(head.zero_or_neg_one);
        if (!parent)
            parent = CL(head.ioDirID); /* ROMAW's older minimal records. */
        if (!parent || parent == -1 || !head.volumeName[0])
            return err;
        memcpy(volume, head.volumeName, head.volumeName[0] + 1);
        volume[++volume[0]] = ':';
        FIELD_ASSIGN_RAW(pb.volumeParam.ioNamePtr, RM((Ptr)volume));
        pb.volumeParam.ioVolIndex = CWC(-1);
        err = PBHGetVInfo(&pb, FALSE);
        if (err == noErr) {
            memcpy(path, head.fileName, head.fileName[0] + 1);
            err = FSMakeFSSpec(CW(pb.volumeParam.ioVRefNum), parent, path, &resolved);
        }
    }
    if (err == noErr)
        err = alias_candidate_kind(&head, &resolved);
    if (err == noErr)
        *target = resolved;
    return err;
}

P4(PUBLIC pascal trap, OSErr, ResolveAlias, FSSpecPtr, fromFile, AliasHandle, alias, FSSpecPtr, target, Boolean*,
   wasAliased)
{
    FSSpec from, found;
    OSErr err;
    if (!wasAliased || !target)
        return paramErr;
    *wasAliased = FALSE;
    if (fromFile) {
        from = *fromFile;
        err = alias_resolve_relative(&from, alias, &found);
        if (err == noErr) {
            /* Resolution succeeds even if refreshing the alias cannot allocate.
             UpdateAlias preserves the original handle in that case. */
            (void)UpdateAlias(&from, &found, alias, wasAliased);
            *target = found;
            return noErr;
        }
        if (err == paramErr)
            return err;
    }
    return alias_resolve_absolute(alias, target);
}

/* Finder aliases prefer relative search, but ignore candidates in the alias
   file's own folder and retry direct search, as in SuperMario alExt.c. Host
   volumes are already mounted; network logon is not performed here. */
OSErr ROMlib_follow_finder_alias(FSSpecPtr from_file, AliasHandle alias, Boolean logon, FSSpecPtr target,
                                 Boolean* was_changed)
{
    FSSpec source, resolved;
    alias_head_t head;
    OSErr error;
    Boolean relative = FALSE;
    (void)logon;
    if (!target || !was_changed)
        return paramErr;
    *was_changed = FALSE;
    if (!from_file)
        return alias_resolve_absolute(alias, target);
    source = *from_file;
    if (source.name[0] > 63 || !alias || !alias_record_valid((unsigned char*)STARH(alias), GetHandleSize(alias)))
        return paramErr;
    memcpy(&head, STARH(alias), sizeof head);
    error = alias_resolve_relative(&source, alias, &resolved);
    if (error == noErr && !(source.vRefNum == resolved.vRefNum && source.parID == resolved.parID))
        relative = TRUE;
    else
        error = alias_resolve_absolute(alias, &resolved);
    if (error != noErr)
        return error;
    if (source.vRefNum == resolved.vRefNum && source.parID == resolved.parID &&
        !ROMlib_RelString(source.name + 1, resolved.name + 1, FALSE, TRUE,
                          ((LONGINT)source.name[0] << 16) | resolved.name[0]))
        return fnfErr;
    *target = resolved;
    *was_changed = relative && resolved.parID != head.zero_or_neg_one;
    return noErr;
}

OSErr ROMlib_is_alias_file(const FSSpec* spec, Boolean* is_alias, Boolean* is_folder)
{
    if (!spec || !is_alias || !is_folder || spec->name[0] > 63)
        return paramErr;
    CInfoPBRec catalog = {0};
    FIELD_ASSIGN_RAW(catalog.hFileInfo.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    catalog.hFileInfo.ioDirID = spec->parID;
    catalog.hFileInfo.ioVRefNum = spec->vRefNum;
    OSErr error = PBGetCatInfo(&catalog, FALSE);
    if (!error) {
        *is_folder = !!(catalog.hFileInfo.ioFlAttrib & ATTRIB_ISADIR);
        *is_alias = !*is_folder && !!(CW(catalog.hFileInfo.ioFlFndrInfo.fdFlags) & 0x8000);
    }
    return error;
}

P4(PUBLIC pascal trap, OSErr, ResolveAliasFile, FSSpecPtr, theSpec, Boolean, resolveAliasChains, Boolean*,
   targetIsFolder, Boolean*, wasAliased)
{
    FSSpec current;
    INTEGER saved_resource;
    OSErr error = noErr;
    unsigned hops = 0;

    if (!theSpec || !targetIsFolder || !wasAliased || theSpec->name[0] > 63)
        return paramErr;
    current = *theSpec;
    *targetIsFolder = *wasAliased = FALSE;
    saved_resource = CurResFile();
    for (;;) {
        INTEGER resource;
        Handle alias;
        Boolean is_alias, changed, saved_load;

        error = ROMlib_is_alias_file(&current, &is_alias, targetIsFolder);
        if (error != noErr)
            break;
        if (!is_alias || (!resolveAliasChains && hops))
            break;
        *wasAliased = TRUE;
        /* System 7 permits ten links, followed by a final catalog lookup. */
        if (hops++ == 10) {
            error = fnfErr;
            break;
        }
        resource = FSpOpenResFile(&current, fsRdPerm);
        if (resource == -1) {
            error = ResError();
            if (error == noErr)
                error = fnfErr;
            break;
        }
        UseResFile(resource);
        saved_load = ResLoad;
        SetResLoad(TRUE);
        /* Finder aliases use the first alis resource, not necessarily ID 0. */
        alias = Get1IndResource(TICK("alis"), 1);
        error = alias ? ROMlib_follow_finder_alias(&current, alias, TRUE, &current, &changed) : resNotFound;
        CloseResFile(resource);
        SetResLoad(saved_load);
        if (error != noErr)
            break;
    }
    UseResFile(saved_resource);
    if (error == noErr)
        *theSpec = current;
    return error;
}

OSErr ROMlib_resolve_alias_file_with_mount_flags(FSSpec* spec, Boolean resolve_chains, Boolean* is_folder,
                                                 Boolean* was_alias, uint32 mount_flags)
{
    if (mount_flags & ~((uint32)kResolveAliasFileNoUI))
        return paramErr;
    /* ROMAW resolves aliases against host-mounted volumes without a dialog. */
    return C_ResolveAliasFile(spec, resolve_chains, is_folder, was_alias);
}

P8(PUBLIC pascal trap, OSErr, MatchAlias, FSSpecPtr, fromFile, int32, rulesMask, AliasHandle, alias, int16*, aliasCount,
   FSSpecArrayPtr, aliasList, Boolean*, needsUpdate, AliasFilterProcPtr, aliasFilter, Ptr, yourDataPtr)
{
    enum { search_relative_first = 0x400 };

    if (!aliasCount || !needsUpdate)
        return paramErr;
    const int capacity = CW(*aliasCount);
    *aliasCount = 0;
    *needsUpdate = FALSE;
    if (capacity <= 0 || !aliasList || !alias)
        return paramErr;

    alias_head_t head;
    const unsigned char* bytes = (unsigned char*)STARH(alias);
    if (!alias_record_valid(bytes, GetHandleSize(alias)))
        return paramErr;
    memcpy(&head, bytes, sizeof head);
    FSSpec from;
    if (fromFile)
        from = *fromFile;
    int count = 0;
    OSErr error = fnfErr;
    for (int i = 0; i < 2 && count < capacity; ++i) {
        const int relative = (i == 0) == !!(rulesMask & search_relative_first);
        if (relative && !fromFile)
            continue;
        FSSpec candidate;
        const OSErr status =
            relative ? alias_resolve_relative(&from, alias, &candidate) : alias_resolve_absolute(alias, &candidate);
        if (status) {
            if (!count && error == fnfErr)
                error = status;
            continue;
        }
        if (count && candidate.vRefNum == aliasList[0].vRefNum && candidate.parID == aliasList[0].parID &&
            !ROMlib_RelString(candidate.name + 1, aliasList[0].name + 1, FALSE, TRUE,
                              ((LONGINT)candidate.name[0] << 16) | aliasList[0].name[0]))
            continue;
        if (aliasFilter) {
            CInfoPBRec catalog = {0};
            Str63 name;
            memcpy(name, candidate.name, sizeof name);
            FIELD_ASSIGN_RAW(catalog.hFileInfo.ioNamePtr, (StringPtr)RM((Ptr)name));
            catalog.hFileInfo.ioDirID = candidate.parID;
            catalog.hFileInfo.ioVRefNum = candidate.vRefNum;
            if (PBGetCatInfo(&catalog, FALSE))
                continue;
            Boolean quit = FALSE;
            const int skip =
                CToPascalCall(aliasFilter, 1 | (5 << 3) | (5 << 6) | (5 << 9), &catalog, &quit, yourDataPtr);
            if (quit)
                break;
            if (skip)
                continue;
        }
        if (!count)
            *needsUpdate = relative && candidate.parID != head.zero_or_neg_one;
        aliasList[count++] = candidate;
    }
    *aliasCount = CW(count);
    return count ? noErr : error;
}

P3(PUBLIC pascal trap, OSErr, GetAliasInfo, AliasHandle, alias, AliasTypeInfo, index, Str63, theString)
{
    return alias_record_info(alias ? (unsigned char*)STARH(alias) : NULL, alias ? GetHandleSize(alias) : 0, index,
                             theString);
}

PRIVATE int EVENUP(int n)
{
    int retval;

    retval = n;
    if (retval & 1)
        ++retval;
    return retval;
}

PRIVATE OSErr decompose_full_path(INTEGER path_len, Ptr fullPath, Str27 volumeName, Str31 fileName)
{
    OSErr retval;
    char* first_colon;
    char* last_colon;
    char *p, *ep;
    int volume_len;
    int file_len;

    for (p = (char*)fullPath, ep = p + path_len, first_colon = 0, last_colon = 0; p != ep; ++p) {
        if (*p == ':') {
            if (!first_colon)
                first_colon = p;
            last_colon = p;
        }
    }
    if (!first_colon)
        retval = paramErr;
    else {
        volume_len = first_colon - (char*)fullPath;
        file_len = ep - last_colon - 1;
        if (volume_len > 27 || file_len > 31)
            retval = paramErr;
        else {
            volumeName[0] = volume_len;
            memcpy(volumeName + 1, fullPath, volume_len);
            fileName[0] = file_len;
            memcpy(fileName + 1, last_colon + 1, file_len);
            retval = noErr;
        }
    }
    return retval;
}

PRIVATE void init_head(alias_head_t* headp, Str27 volumeName, Str31 fileName)
{
    memset(headp, 0, sizeof *headp);
    headp->usually_2 = CWC(2);
    memcpy(headp->volumeName, volumeName, volumeName[0] + 1);
    memcpy(headp->fileName, fileName, fileName[0] + 1);
    headp->mystery_words[0] = CWC(-1);
    headp->mystery_words[1] = CWC(-1);
    headp->mystery_words[3] = CWC(17);
}

PRIVATE void init_tail(alias_tail_t* tailp, const unsigned char* zoneName, const unsigned char* serverName,
                       const unsigned char* volumeName)
{
    Handle h;

    memset(tailp, 0, sizeof *tailp);
    memcpy(tailp->zone, zoneName, zoneName[0] + 1);
    memcpy(tailp->server, serverName, serverName[0] + 1);
    memcpy(tailp->volumeName, volumeName, volumeName[0] + 1);
    h = (Handle)GetString(-16096);
    if (!h)
        tailp->network_identity_owner_name[0] = 0;
    else {
        int name_len;

        name_len = MIN(GetHandleSize(h), 31);
        memcpy(tailp->network_identity_owner_name, STARH(h), name_len);
    }
    tailp->weird_info[0] = CWC(0x00A8);
    tailp->weird_info[1] = CWC(0x6166);
    tailp->weird_info[2] = CWC(0x706D);
    tailp->weird_info[5] = CWC(0x0003);
    tailp->weird_info[6] = CWC(0x0018);
    tailp->weird_info[7] = CWC(0x0039);
    tailp->weird_info[8] = CWC(0x0059);
    tailp->weird_info[9] = CWC(0x0075);
    tailp->weird_info[10] = CWC(0x0095);
    tailp->weird_info[11] = CWC(0x009E);
}

PRIVATE OSErr assemble_pieces(HIDDEN_AliasHandle* ahp, alias_head_t* headp, INTEGER n_pieces, ...)
{
    Size n_bytes_needed;
    va_list va;
    int i;
    Handle h;
    OSErr retval;

    n_bytes_needed = sizeof *headp;
    va_start(va, n_pieces);
    for (i = 0; i < n_pieces; ++i) {
        INTEGER tag;
        INTEGER length;
        void* p;

        tag = va_arg(va, int);
        length = va_arg(va, int);
        p = va_arg(va, void*);
        n_bytes_needed += sizeof(INTEGER) + sizeof(INTEGER) + EVENUP(length);
    }
    va_end(va);
    n_bytes_needed += sizeof(INTEGER) + sizeof(INTEGER);
    h = NewHandle(n_bytes_needed);
    if (!h)
        retval = MemError();
    else {
        char* op;

        headp->length = CW(n_bytes_needed);
        op = (char*)STARH(h);
        memcpy(op, headp, sizeof(*headp));
        op += sizeof(*headp);
        va_start(va, n_pieces);
        for (i = 0; i < n_pieces; ++i) {
            INTEGER tag, tag_x;
            INTEGER length, length_x;
            void* p;

            tag = va_arg(va, int);
            tag_x = CW(tag);
            length = va_arg(va, int);
            length_x = CW(length);
            p = va_arg(va, void*);
            memcpy(op, &tag_x, sizeof tag_x);
            op += sizeof tag_x;
            memcpy(op, &length_x, sizeof length_x);
            op += sizeof length_x;
            memcpy(op, p, length);
            op += length;
            if (length & 1)
                *op++ = 0;
        }
        va_end(va);
        memset(op, -1, sizeof(INTEGER));
        op += sizeof(INTEGER);
        memset(op, 0, sizeof(INTEGER));

        HIDDEN_RAW_SET((*ahp), RM(h));
        retval = noErr;
    }
    return retval;
}

/*
FULL_PATH_TAG, path_len, fullPath,
TAIL_TAG     , sizeof (tail)-2, &tail.weird_info)
*/

P5(PUBLIC pascal trap, OSErr, NewAliasMinimalFromFullPath, INTEGER, path_len, Ptr, fullPath, Str32, zoneName, Str31,
   serverName, HIDDEN_AliasHandle*, ahp)
{
    OSErr retval;

    if (!ahp)
        return paramErr;
    HIDDEN_RAW_SET(*ahp, NULL);
    if (path_len <= 0 || !fullPath || !zoneName || !serverName)
        return paramErr;
    if (zoneName[0] > 32 || serverName[0] > 31)
        retval = paramErr;
    else {
        Str27 volumeName;
        Str63 fileName;

        retval = decompose_full_path(path_len, fullPath, volumeName, fileName);
        if (retval == noErr) {
            alias_head_t head;
            alias_tail_t tail;

            init_head(&head, volumeName, fileName);
            if (volumeName[0] < 27)
                head.volumeName[volumeName[0] + 1] = ':';
            head.zero_or_one = CWC(1);
            head.zero_or_neg_one = CLC(-1);
            head.ioDirID = CLC(-1);
            head.ioFlCrDat = CLC(0);
            init_tail(&tail, zoneName, serverName, volumeName);
            retval = assemble_pieces(ahp, &head, 2, FULL_PATH_TAG, path_len, (void*)fullPath, TAIL_TAG,
                                     (int)sizeof(tail) - 2, (void*)&tail.weird_info);
        }
    }

    if (retval != noErr)
        HIDDEN_RAW_SET((*ahp), NULL);

    return retval;
}

P2(PUBLIC pascal trap, OSErr, NewAliasMinimal, FSSpecPtr, fsp, HIDDEN_AliasHandle*, ahp)
{
    HParamBlockRec hpb;
    CInfoPBRec catalog;
    OSErr retval;
    Str27 volName;

    if (!ahp)
        return paramErr;
    HIDDEN_RAW_SET(*ahp, NULL);
    if (!fsp || fsp->name[0] > 63)
        return paramErr;
    memset(&hpb, 0, sizeof hpb);
    FIELD_ASSIGN_RAW(hpb.ioParam.ioNamePtr, RM(&volName[0]));
    hpb.ioParam.ioVRefNum = fsp->vRefNum;
    retval = PBHGetVInfo(&hpb, FALSE);
    if (retval == noErr) {
        alias_head_t head;

        init_head(&head, volName, fsp->name);
        head.ioVCrDate = hpb.volumeParam.ioVCrDate;
        head.ioVSigWord = hpb.volumeParam.ioVSigWord;
        memset(&catalog, 0, sizeof catalog);
        FIELD_ASSIGN_RAW(catalog.hFileInfo.ioNamePtr, RM(&fsp->name[0]));
        catalog.hFileInfo.ioVRefNum = fsp->vRefNum;
        catalog.hFileInfo.ioDirID = fsp->parID;
        retval = PBGetCatInfo(&catalog, FALSE);
        if (retval == noErr) {
            alias_tail_t tail;
            Handle h;
            Str31 serverName;

            head.zero_or_neg_one = catalog.hFileInfo.ioFlParID;
            head.ioDirID = catalog.hFileInfo.ioDirID;
            head.ioFlCrDat = catalog.hFileInfo.ioFlCrDat;
            if (catalog.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                head.usually_0 = CWC(1);
            else {
                head.type_info = catalog.hFileInfo.ioFlFndrInfo.fdType;
                head.creator = catalog.hFileInfo.ioFlFndrInfo.fdCreator;
            }
            h = (Handle)GetString(-16413);
            if (!h)
                serverName[0] = 0;
            else {
                int len;

                len = MIN(GetHandleSize(h), 32);
                memcpy(serverName, STARH(h), len);
            }
            init_tail(&tail, (StringPtr) "\1*", serverName, volName);
            retval = assemble_pieces(ahp, &head, 1, TAIL_TAG, (int)sizeof(tail) - 2, (void*)&tail.weird_info);
        }
    }

    if (retval != noErr)
        HIDDEN_RAW_SET((*ahp), NULL);
    return retval;
}
