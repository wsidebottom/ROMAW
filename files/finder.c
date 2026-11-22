/*+===================================================================
  File:      finder.c

  Summary:   Implements finder support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: PBDTCloseDown, PBDTDelete, PBDTFlush, PBDTGetPath, PBDTOpenInform

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Desktop Manager; API and disk record references: SuperMario DTDBMgr.a,
   DTDBMgrPriv.a and BTreeMgr/BTreeFuncs.a. */
#include "rsys/common.h"
#include "Finder.h"
#include "FileMgr.h"
#include "rsys/file.h"
#include "rsys/hfs.h"
#include "rsys/mman.h"
#include "rsys/desktop_database.h"
#include "finder/image_volumes.h"
#include "rsys/application_registry.h"
#include "rsys/configprefs.h"

static HVCB* desktop_volumes[128];

static HVCB* desktop_volume(DTPBPtr pb)
{
    unsigned index = (unsigned)(-CW(pb->ioDTRefNum) - 16384);
    if (index >= NELEM(desktop_volumes))
        return NULL;
    for (HVCB* v = (HVCB*)PPR(VCBQHdr.qHead); v; v = (HVCB*)PPR(v->qLink))
        if (v == desktop_volumes[index])
            return v;
    return NULL;
}

static OSErr desktop_call(DTPBPtr pb, enum desktop_operation op, unsigned char key[38], unsigned index, void* data,
                          size_t* size, uint32_t* tag)
{
    HVCB* v = desktop_volume(pb);
    if (!v)
        return rfNumErr;
    if (ROMlib_image_volume_kind(v))
        return ROMlib_finder_image_desktop(CW(v->vcbVRefNum), op, key, index, data, size, tag);
    if (!config_drive_sidecars(((VCBExtra*)v)->unixname))
        return extFSErr;
    if ((op == desktop_put || op == desktop_remove || op == desktop_reset) &&
        (CW(v->vcbAtrb) & (VHARDLOCKBIT | VSOFTLOCKBIT)))
        return wPrErr;
    return romaw_desktop_database(((VCBExtra*)v)->unixname, op, key, index, data, size, tag);
}

static void desktop_key(DTPBPtr pb, unsigned char key[38], unsigned kind)
{
    memset(key, 0, 38);
    key[0] = kind == 1 ? 11 : 5;
    key[1] = kind;
    memcpy(key + 2, &pb->ioFileCreator, 4);
    if (kind == 1) {
        memcpy(key + 6, &pb->ioFileType, 4);
        key[10] = pb->ioIconType;
    }
}

static void* desktop_buffer(DTPBPtr pb, size_t size)
{
    uint32 address = CL((uint32)(uintptr_t)PP_RAW(pb->ioDTBuffer)) & syn68k_address_mask;
    return address && address < ROMlib_guest_memory_size && size <= ROMlib_guest_memory_size - address
               ? SYN68K_TO_US(address)
               : NULL;
}

PUBLIC OSErr PBDTOpenInform(DTPBPtr pb)
{
    LONGINT directory;
    HVCB* v = ROMlib_findvcb(CW(pb->ioVRefNum), NULL, &directory, TRUE);
    OSErr error = !v                                                                ? nsvErr
                  : ROMlib_image_volume_kind(v)                                     ? noErr
                  : v->vcbCTRef || !config_drive_sidecars(((VCBExtra*)v)->unixname) ? extFSErr
                                                                                    : noErr;
    size_t existed = 0;
    uint32_t tag = 0;
    unsigned index = 0;
    if (!error) {
        for (; index < NELEM(desktop_volumes); ++index)
            if (desktop_volumes[index] == v)
                break;
        if (index == NELEM(desktop_volumes))
            for (index = 0; index < NELEM(desktop_volumes); ++index)
                if (!desktop_volumes[index])
                    break;
        error = index == NELEM(desktop_volumes) ? tmfoErr
                : ROMlib_image_volume_kind(v)
                    ? ROMlib_finder_image_desktop(CW(v->vcbVRefNum), desktop_open, NULL, 0, NULL, &existed, &tag)
                    : romaw_desktop_database(((VCBExtra*)v)->unixname, desktop_open, NULL, 0, NULL, &existed, &tag);
        if (!error) {
            if (!existed) {
                uint32 files, directories;
                ROMlib_ufs_volume_counts(((VCBExtra*)v)->unixname, &files, &directories);
                v->vcbFilCnt = CL(files);
                v->vcbDirCnt = CL(directories);
            }
            desktop_volumes[index] = v;
            pb->ioDTRefNum = CW(-16384 - index);
            pb->ioTagInfo = CL(existed ? 1 : 0);
            pb->ioVRefNum = v->vcbVRefNum;
            static const unsigned char names[][12] = {"\012Desktop DB", "\012Desktop DF"};
            for (unsigned i = 0; !ROMlib_image_volume_kind(v) && i < 2; ++i) {
                FInfo info = {0};
                info.fdCreator = TICKX("DMGR");
                info.fdType = i ? TICKX("DTFL") : TICKX("BTFL");
                info.fdFlags = CW(fInvisible);
                FSSpec spec = {0};
                spec.vRefNum = v->vcbVRefNum;
                spec.parID = CL(2);
                memcpy(spec.name, names[i], 11);
                FSpSetFInfo(&spec, &info);
            }
        }
    }
    pb->ioResult = CW(error);
    return error;
}

PUBLIC OSErr PBDTGetPath(DTPBPtr pb)
{
    return PBDTOpenInform(pb);
}

PUBLIC OSErr PBDTCloseDown(DTPBPtr pb)
{
    OSErr error = desktop_volume(pb) ? noErr : rfNumErr;
    if (!error)
        desktop_volumes[-CW(pb->ioDTRefNum) - 16384] = NULL;
    pb->ioResult = CW(error);
    return error;
}

static OSErr desktop_icon(DTPBPtr pb, enum desktop_operation op)
{
    unsigned char key[38], value[10];
    desktop_key(pb, key, 1);
    LONGINT requested = CL(pb->ioDTReqCount);
    size_t size = requested < 0 ? 0 : MIN(requested, 4500);
    uint32_t tag = CL(pb->ioTagInfo);
    void* buffer = desktop_buffer(pb, size);
    OSErr error;
    if (op == desktop_index) {
        key[0] = 5;
        size = sizeof value;
        error = desktop_call(pb, op, key, (uint16)CW(pb->ioIndex), value, &size, &tag);
        if (!error) {
            memcpy(&pb->ioTagInfo, value, 4);
            pb->ioDTActCount = CL(value[8] * 256u + value[9]);
            memcpy(&pb->ioFileType, key + 6, 4);
            pb->ioIconType = key[10];
        }
    }
    else {
        pb->ioDTActCount = 0;
        error = requested < 0 || (op == desktop_put && requested > 4500) || (size && !buffer)
                    ? paramErr
                    : desktop_call(pb, op, key, 0, buffer, &size, &tag);
        if (!error && op == desktop_get) {
            pb->ioDTActCount = CL(size);
            pb->ioTagInfo = CL(tag);
        }
    }
    return error;
}

static OSErr desktop_spec(DTPBPtr pb, FSSpec* spec, CInfoPBRec* info)
{
    HVCB* v = desktop_volume(pb);
    if (!v)
        return rfNumErr;
    OSErr error = FSMakeFSSpec(CW(v->vcbVRefNum), CL(pb->ioDirID), PPR(pb->ioNamePtr), spec);
    if (error)
        return error;
    memset(info, 0, sizeof *info);
    info->hFileInfo.ioVRefNum = spec->vRefNum;
    info->hFileInfo.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(info->hFileInfo.ioNamePtr, RM((StringPtr)spec->name));
    return PBGetCatInfo(info, FALSE);
}

static OSErr desktop_application(DTPBPtr pb, enum desktop_operation op)
{
    unsigned char key[38], value[40], first[40], first_key[38];
    uint32_t tag = 0;
    size_t size = sizeof value;
    desktop_key(pb, key, 2);
    if (op == desktop_get) {
        unsigned index = (uint16)CW(pb->ioIndex);
        OSErr error = desktop_call(pb, desktop_index, key, index ? index : 1, value, &size, &tag);
        if (!error) {
            memcpy(&pb->ioTagInfo, value, 4);
            memcpy(&pb->ioAPPLParID, value + 4, 4);
            if (PPR(pb->ioNamePtr))
                memcpy(PPR(pb->ioNamePtr), value + 8, value[8] + 1);
        }
        return error;
    }
    FSSpec spec;
    CInfoPBRec info;
    OSErr error = desktop_spec(pb, &spec, &info);
    if (error)
        return error;
    if (spec.name[0] > 31)
        return bdNamErr;
    unsigned sequence = 65535, entries = 0;
    for (unsigned index = 1; index <= 65536; ++index) {
        desktop_key(pb, key, 2);
        size = sizeof value;
        error = desktop_call(pb, desktop_index, key, index, value, &size, &tag);
        if (error == -413)
            break;
        if (error)
            return error;
        if (!entries++) {
            memcpy(first_key, key, sizeof key);
            memcpy(first, value, sizeof value);
        }
        unsigned used = key[6] * 256u + key[7];
        if (used <= sequence)
            sequence = used ? used - 1 : 65536;
        if (!memcmp(value + 4, &spec.parID, 4) && !memcmp(value + 8, spec.name, spec.name[0] + 1)) {
            if (op == desktop_remove)
                return desktop_call(pb, op, key, 0, NULL, &size, &tag);
            if (!memcmp(value, &info.hFileInfo.ioFlCrDat, 4))
                return noErr;
            error = desktop_call(pb, desktop_remove, key, 0, NULL, &size, &tag);
            if (error)
                return error;
            return desktop_application(pb, op);
        }
    }
    if (op == desktop_remove)
        return -413;
    if (sequence > 65535)
        return dskFulErr;
    memset(value, 0, sizeof value);
    memcpy(value, &info.hFileInfo.ioFlCrDat, 4);
    memcpy(value + 4, &spec.parID, 4);
    memcpy(value + 8, spec.name, spec.name[0] + 1);
    desktop_key(pb, key, 2);
    key[0] = 7;
    key[6] = sequence >> 8;
    key[7] = sequence;
    size = sizeof value;
    if (entries) {
        uint32_t date, previous;
        memcpy(&date, value, 4);
        memcpy(&previous, first, 4);
        if ((uint32_t)CL(date) < (uint32_t)CL(previous)) {
            error = desktop_call(pb, desktop_put, key, 0, first, &size, &tag);
            if (error)
                return error;
            memcpy(key, first_key, sizeof key);
        }
    }
    return desktop_call(pb, desktop_put, key, 0, value, &size, &tag);
}

static OSErr desktop_comment(DTPBPtr pb, enum desktop_operation op)
{
    FSSpec spec;
    CInfoPBRec info;
    OSErr error = desktop_spec(pb, &spec, &info);
    if (error)
        return error;
    unsigned char key[38] = {5, 3}, value[200];
    memcpy(key + 2, &info.hFileInfo.ioDirID, 4);
    LONGINT requested = CL(pb->ioDTReqCount);
    size_t count = requested < 0 ? 0 : MIN(requested, 199);
    void* buffer = desktop_buffer(pb, count);
    uint32_t tag = 0;
    if (op != desktop_remove && (requested < 0 || (count && !buffer)))
        return paramErr;
    size_t size = sizeof value;
    if (op == desktop_put) {
        value[0] = count;
        if (count)
            memcpy(value + 1, buffer, count);
        size = count + 1;
    }
    error = desktop_call(pb, op, key, 0, value, &size, &tag);
    if (!error && op == desktop_get) {
        count = MIN(count, value[0]);
        if (count)
            memcpy(buffer, value + 1, count);
        pb->ioDTActCount = CL(count);
    }
    return error;
}

static OSErr desktop_control(DTPBPtr pb, enum desktop_operation op)
{
    size_t size = 0;
    uint32_t tag = 0;
    OSErr error = desktop_call(pb, op, NULL, 0, NULL, &size, &tag);
    if (!error && op == desktop_info) {
        pb->ioDTLgLen = pb->ioDTPyLen = CL(size);
        pb->ioVRefNum = desktop_volume(pb)->vcbVRefNum;
        pb->ioDirID = CL(2);
        if (PPR(pb->ioNamePtr))
            memcpy(PPR(pb->ioNamePtr), "\012Desktop DB", 11);
    }
    if (!error && op == desktop_reset) {
        HVCB* volume = desktop_volume(pb);
        uint32 files, directories;
        ROMlib_ufs_volume_counts(((VCBExtra*)volume)->unixname, &files, &directories);
        volume->vcbFilCnt = CL(files);
        volume->vcbDirCnt = CL(directories);
        ROMlib_application_registry_reset();
    }
    return error;
}

#define DESKTOP_ASYNC(name, function, operation) \
    PUBLIC OSErr name(DTPBPtr pb, BOOLEAN async) \
    {                                            \
        OSErr error = function(pb, operation);   \
        if (error == -413 || error == fnfErr)    \
            error = -5012;                       \
        FAKEASYNC(pb, async, error);             \
        return error;                            \
    }
DESKTOP_ASYNC(PBDTGetIcon, desktop_icon, desktop_get)
DESKTOP_ASYNC(PBDTGetIconInfo, desktop_icon, desktop_index)
DESKTOP_ASYNC(PBDTAddIcon, desktop_icon, desktop_put)
DESKTOP_ASYNC(PBDTGetAPPL, desktop_application, desktop_get)
DESKTOP_ASYNC(PBDTAddAPPL, desktop_application, desktop_put)
DESKTOP_ASYNC(PBDTRemoveAPPL, desktop_application, desktop_remove)
DESKTOP_ASYNC(PBDTGetComment, desktop_comment, desktop_get)
DESKTOP_ASYNC(PBDTSetComment, desktop_comment, desktop_put)
DESKTOP_ASYNC(PBDTRemoveComment, desktop_comment, desktop_remove)
DESKTOP_ASYNC(PBDTGetInfo, desktop_control, desktop_info)
DESKTOP_ASYNC(PBDTReset, desktop_control, desktop_reset)
#undef DESKTOP_ASYNC

PUBLIC OSErr PBDTFlush(DTPBPtr pb, BOOLEAN async)
{
    size_t size = 0;
    uint32_t tag = 0;
    OSErr error = desktop_call(pb, desktop_info, NULL, 0, NULL, &size, &tag);
    FAKEASYNC(pb, async, error);
    return error;
}

PUBLIC OSErr PBDTDelete(DTPBPtr pb, BOOLEAN async)
{
    LONGINT directory;
    HVCB* v = ROMlib_findvcb(CW(pb->ioVRefNum), NULL, &directory, TRUE);
    size_t size = 0;
    uint32_t tag = 0;
    OSErr error = !v ? nsvErr : v->vcbCTRef || !config_drive_sidecars(((VCBExtra*)v)->unixname) ? extFSErr : noErr;
    if (!error && (CW(v->vcbAtrb) & (VHARDLOCKBIT | VSOFTLOCKBIT)))
        error = wPrErr;
    if (!error)
        error = romaw_desktop_database(((VCBExtra*)v)->unixname, desktop_delete, NULL, 0, NULL, &size, &tag);
    if (!error) {
        for (unsigned i = 0; i < NELEM(desktop_volumes); ++i)
            if (desktop_volumes[i] == v)
                desktop_volumes[i] = NULL;
        ROMlib_application_registry_reset();
    }
    FAKEASYNC(pb, async, error);
    return error;
}
