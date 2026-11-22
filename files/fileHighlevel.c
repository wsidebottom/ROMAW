/*+===================================================================
  File:      fileHighlevel.c

  Summary:   Implements file highlevel support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: HCreate, HOpenRF, PBMakeFSSpec, ROMlib_file_complete

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* NOTE:  This is the quick hack version that uses old-style calls.
          Eventually we should extract the common functionality and
    then use that for both.  That will be a little cleaner, a little
    faster and a little easier to debug, but not enough of any to
    make it sensible to do that initially.  */

#include "rsys/common.h"

#include "MemoryMgr.h"
#include "FileMgr.h"
#include "ProcessMgr.h"

#include "rsys/file.h"
#include "rsys/string.h"
#include "rsys/hfs.h"
#include "rsys/romaw.h"

/*
 * extract the last component of a name:
 *
 * aaa:bbb:ccc  -> ccc
 * aaa:bbb:ccc: -> ccc
 */

PRIVATE OSErr extract_name(Str63 dest, StringPtr source)
{
    int len, new_len;
    Byte* p;

    len = source[0];
    if (source[len] == ':') /* ignore trailing ':' */
        --len;
    for (p = source + len; p >= source + 1 && *p != ':'; --p)
        ;
    new_len = len - (p - source);
    if (new_len > sizeof(Str63) - 1)
        return bdNamErr;
    dest[0] = new_len;
    memmove(dest + 1, p + 1, new_len);
    return noErr;
}

OSErrRET PBMakeFSSpec(HParmBlkPtr pb, BOOLEAN asynchronous)
{
    if (!pb)
        return paramErr;

    const StringPtr input = PPR(pb->ioParam.ioNamePtr);
    FSSpecPtr output = SYN68K_TO_US_CHECK0(CL(pb->ioParam.ioMisc));
    Str255 name = {0};
    if (input)
        str255assign(name, input);
    const OSErr status = C_FSMakeFSSpec(CW(pb->ioParam.ioVRefNum), CL(pb->fileParam.ioDirID), name, output);
    FAKEASYNC(pb, asynchronous, status);
}

OSErr ROMlib_file_complete(void* block, BOOLEAN asynchronous, OSErr status)
{
    FAKEASYNC(block, asynchronous, status);
}

P4(PUBLIC pascal trap, OSErr, FSMakeFSSpec, int16, vRefNum, int32, dir_id, Str255, file_name, FSSpecPtr, spec)
{
    Str255 local_file_name;
    OSErr retval;
    HParamBlockRec hpb;

    if (!file_name || !spec)
        return paramErr;

    /*
   * Colons make things tricky because they are used both to identify volumes
   * and to delimit directories and sometimes to identify partial paths.  Right
   * now, most of these uses of colon shouldn't cause problems, but there are
   * some issues that need to be checked on a real Mac.  What do all these
   * mean?
   *
   *     dir_id      file_name
   *     310         foo:
   *     310         :foo:
   *     310         foo:bar
   *     310         :foo:bar
   *     310         :foo:bar:
   *
   * what about all those file_names, but with dir_id = 0, dir_id = 1, dir_id = 2
   * and dir_id = a number that isn't a directory id?
   */

    if (pstr_index_after(file_name, ':', 0))
        warning_unexpected("colon found");

    str255assign(local_file_name, file_name);
    FIELD_ASSIGN_RAW(hpb.volumeParam.ioNamePtr, (StringPtr)RM((Ptr)local_file_name));
    hpb.volumeParam.ioVRefNum = CW(vRefNum);
    if (file_name[0])
        hpb.volumeParam.ioVolIndex = CLC(-1);
    else
        hpb.volumeParam.ioVolIndex = CLC(0);

    retval = PBHGetVInfo(&hpb, FALSE);

    if (retval == noErr) {
        CInfoPBRec cpb;

        str255assign(local_file_name, file_name);
        FIELD_ASSIGN_RAW(cpb.hFileInfo.ioNamePtr, (StringPtr)RM((Ptr)local_file_name));
        cpb.hFileInfo.ioVRefNum = CW(vRefNum);
        if (file_name[0])
            cpb.hFileInfo.ioFDirIndex = CWC(0);
        else
            cpb.hFileInfo.ioFDirIndex = CWC(-1);
        cpb.hFileInfo.ioDirID = CL(dir_id);
        retval = PBGetCatInfo(&cpb, FALSE);
        if (retval == noErr) {
            spec->vRefNum = hpb.volumeParam.ioVRefNum;
            spec->parID = cpb.hFileInfo.ioFlParID;
            retval = extract_name(spec->name, PPR(cpb.hFileInfo.ioNamePtr));
        }
        else if (retval == fnfErr) {
            /* A missing leaf still has a usable FSSpec when its parent exists.
               Resolve the prefix before stripping it from the leaf name. */
            int end = file_name[0];
            if (end && file_name[end] == ':')
                --end;
            int prefix = end;
            while (prefix && file_name[prefix] != ':')
                --prefix;
            if (prefix && !(prefix == 1 && file_name[1] == ':')) {
                local_file_name[0] = prefix;
                memcpy(local_file_name + 1, file_name + 1, prefix);
                PP_ASSIGN(cpb.hFileInfo.ioNamePtr, (StringPtr)local_file_name);
                cpb.hFileInfo.ioFDirIndex = CWC(0);
            }
            else {
                PP_ASSIGN(cpb.hFileInfo.ioNamePtr, NULL);
                cpb.hFileInfo.ioFDirIndex = CWC(-1);
            }
            cpb.hFileInfo.ioVRefNum = CW(vRefNum);
            cpb.hFileInfo.ioDirID = CL(dir_id);
            OSErr err = PBGetCatInfo(&cpb, FALSE);
            if (err == noErr) {
                if (cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR) {
                    spec->vRefNum = hpb.volumeParam.ioVRefNum;
                    spec->parID = cpb.dirInfo.ioDrDirID;
                    err = extract_name(spec->name, file_name);
                    if (err != noErr)
                        retval = err;
                }
                else
                    retval = dirNFErr;
            }
            else
                retval = err == fnfErr ? dirNFErr : err;
        }
    }
    return retval;
}

/*
 * Part of ugly PAUP-specific hack below
 */

PRIVATE void create_temp_name(Str63 name, int i)
{
    OSErr err;
    ProcessSerialNumber psn;

    err = GetCurrentProcess(&psn);
    if (err == noErr)
        sprintf((char*)name + 1, "%x%x.%x", CL(psn.highLongOfPSN), CL(psn.lowLongOfPSN), i);
    else
        sprintf((char*)name + 1, "%d.%x", err, i);
    name[0] = strlen((char*)name + 1);
}

/*
 * On real HFS volumes we could do some B-tree manipulation to achieve
 * the correct results, *but* we'd need this code for ufs volumes anyway,
 * so for now we do the same thing on both.
 */

P2(PUBLIC pascal trap, OSErr, FSpExchangeFiles, FSSpecPtr, src, FSSpecPtr, dst)
{
    OSErr retval;

    warning_unimplemented("poorly implemented");
    if (src->vRefNum != dst->vRefNum)
        retval = diffVolErr;
    else if (ROMlib_creator != TICK("PAUP") || src->parID != dst->parID)
        retval = wrgVolTypeErr;
    else {
        /* Evil hack to get PAUP to work -- doesn't bother adjusting FCBs */
        FSSpec tmp_spec;
        int i;

        i = 0;
        tmp_spec = *dst;
        do {
            create_temp_name(tmp_spec.name, i++);
            retval = FSpRename(dst, tmp_spec.name);
        } while (retval == dupFNErr);
        if (retval == noErr) {
            retval = FSpRename(src, dst->name);
            if (retval != noErr)
                FSpRename(&tmp_spec, dst->name);
            else {
                retval = FSpRename(&tmp_spec, src->name);
                if (retval != noErr) {
                    FSpRename(dst, src->name);
                    FSpRename(&tmp_spec, dst->name);
                }
            }
        }
    }
    return retval;
}

typedef OSErrRET (*open_procp)(HParmBlkPtr pb, BOOLEAN sync);

PRIVATE OSErr open_helper(FSSpecPtr spec, SignedByte perms, int16* refoutp, open_procp procp)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.ioParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(hpb.ioParam.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    if (perms == fsWrPerm) {
        warning_unexpected(NULL_STRING);
        perms = fsRdWrPerm;
    }
    hpb.ioParam.ioPermssn = perms;
    warning_unimplemented("poorly implemented ... will try to open drivers");
    retval = procp(&hpb, FALSE);
    if (retval == noErr)
        *refoutp = hpb.ioParam.ioRefNum;
    return retval;
}

P3(PUBLIC pascal trap, OSErr, FSpOpenDF, FSSpecPtr, spec, SignedByte, perms, int16*, refoutp)
{
    return open_helper(spec, perms, refoutp, PBHOpen);
}

P3(PUBLIC pascal trap, OSErr, FSpOpenRF, FSSpecPtr, spec, SignedByte, perms, int16*, refoutp)
{
    return open_helper(spec, perms, refoutp, PBHOpenRF);
}

P4(PUBLIC pascal trap, OSErr, FSpCreate, FSSpecPtr, spec, OSType, creator, OSType, file_type, ScriptCode, script)
{
    OSErr retval;

    retval = HCreate(CW(spec->vRefNum), CL(spec->parID), spec->name, creator, file_type);
    return retval;
}

P3(PUBLIC pascal trap, OSErr, FSpDirCreate, FSSpecPtr, spec, ScriptCode, script, int32*, created_dir_id)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.ioParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(hpb.ioParam.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    retval = PBDirCreate(&hpb, FALSE);
    if (retval == noErr && created_dir_id)
        *created_dir_id = hpb.fileParam.ioDirID;
    return retval;
}

P1(PUBLIC pascal trap, OSErr, FSpDelete, FSSpecPtr, spec)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.ioParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(hpb.ioParam.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    retval = PBHDelete(&hpb, FALSE);
    return retval;
}

P2(PUBLIC pascal trap, OSErr, FSpGetFInfo, FSSpecPtr, spec, FInfo*, fndr_info)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.fileParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(hpb.fileParam.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    hpb.fileParam.ioFDirIndex = CWC(0);
    retval = PBHGetFInfo(&hpb, FALSE);
    if (retval == noErr)
        *fndr_info = hpb.fileParam.ioFlFndrInfo;
    return retval;
}

P2(PUBLIC pascal trap, OSErr, FSpSetFInfo, FSSpecPtr, spec, FInfo*, fndr_info)
{
    OSErr retval;
    HParamBlockRec hpb;

    warning_unimplemented("poorly implemented: call of PBHGetFInfo wasteful");
    hpb.fileParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    FIELD_ASSIGN_RAW(hpb.fileParam.ioNamePtr, (StringPtr)RM((Ptr)spec->name));
    hpb.fileParam.ioFDirIndex = CWC(0);
    retval = PBHGetFInfo(&hpb, FALSE);
    if (retval == noErr) {
        hpb.fileParam.ioDirID = spec->parID;
        hpb.fileParam.ioFlFndrInfo = *fndr_info;
        retval = PBHSetFInfo(&hpb, FALSE);
    }
    return retval;
}

typedef OSErrRET (*lock_procp)(HParmBlkPtr pb, BOOLEAN async);

PRIVATE OSErr lock_helper(FSSpecPtr spec, lock_procp procp)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.fileParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    PP_RAW(hpb.fileParam.ioNamePtr) = (StringPtr)RM((Ptr)spec->name);
    retval = procp(&hpb, FALSE);
    return retval;
}

P1(PUBLIC pascal trap, OSErr, FSpSetFLock, FSSpecPtr, spec)
{
    return lock_helper(spec, PBHSetFLock);
}

P1(PUBLIC pascal trap, OSErr, FSpRstFLock, FSSpecPtr, spec)
{
    return lock_helper(spec, PBHRstFLock);
}

P2(PUBLIC pascal trap, OSErr, FSpRename, FSSpecPtr, spec, Str255, new_name)
{
    OSErr retval;
    HParamBlockRec hpb;

    hpb.fileParam.ioVRefNum = spec->vRefNum;
    hpb.fileParam.ioDirID = spec->parID;
    PP_RAW(hpb.fileParam.ioNamePtr) = (StringPtr)RM((Ptr)spec->name);
    hpb.ioParam.ioMisc = RM((uintptr_t)new_name);
    retval = PBHRename(&hpb, FALSE);
    return retval;
}

P2(PUBLIC pascal trap, OSErr, FSpCatMove, FSSpecPtr, src, FSSpecPtr, dst)
{
    OSErr retval;
    CMovePBRec cbr;

    if (src->vRefNum != dst->vRefNum)
        retval = paramErr;
    else {
        cbr.ioVRefNum = src->vRefNum;
        cbr.ioDirID = src->parID;
        PP_RAW(cbr.ioNamePtr) = (StringPtr)RM((Ptr)src->name);
        PP_RAW(cbr.ioNewName) = (StringPtr)RM((Ptr)dst->name);
        cbr.ioNewDirID = dst->parID;
        retval = PBCatMove(&cbr, FALSE);
    }
    return retval;
}

P4(PUBLIC pascal trap, void, FSpCreateResFile, FSSpecPtr, spec, OSType, creator, OSType, file_type, ScriptCode, script)
{
    HCreateResFile_helper(CW(spec->vRefNum), CL(spec->parID), spec->name, creator, file_type, script);
}

P2(PUBLIC pascal trap, INTEGER, FSpOpenResFile, FSSpecPtr, spec, SignedByte, perms)
{
    INTEGER retval;

    retval = HOpenResFile(CW(spec->vRefNum), CL(spec->parID), spec->name, perms);
    return retval;
}

/* NOTE: the HCreate and HOpenRF are not traps, they're just high level
   calls that are handy to use elsewhere, so they're included here. */

PUBLIC OSErr HCreate(INTEGER vref, LONGINT dirid, Str255 name, OSType creator, OSType type)
{
    HParamBlockRec hpb;
    OSErr retval;

    PP_RAW(hpb.fileParam.ioNamePtr) = RM(name);
    hpb.fileParam.ioVRefNum = CW(vref);
    hpb.fileParam.ioDirID = CL(dirid);
    retval = PBHCreate(&hpb, FALSE);
    if (retval == noErr) {
        hpb.fileParam.ioFDirIndex = CWC(0);
        retval = PBHGetFInfo(&hpb, FALSE);
        if (retval == noErr) {
            hpb.fileParam.ioFlFndrInfo.fdCreator = CL(creator);
            hpb.fileParam.ioFlFndrInfo.fdType = CL(type);
            hpb.fileParam.ioDirID = CL(dirid);
            retval = PBHSetFInfo(&hpb, FALSE);
        }
    }
    return retval;
}

PUBLIC OSErr HOpenRF(INTEGER vref, LONGINT dirid, Str255 name, SignedByte perm, INTEGER* refp)
{
    HParamBlockRec hpb;
    OSErr retval;

    PP_RAW(hpb.fileParam.ioNamePtr) = RM(name);
    hpb.fileParam.ioVRefNum = CW(vref);
    hpb.ioParam.ioPermssn = CB(perm);
    hpb.ioParam.ioMisc = CLC(0);
    hpb.fileParam.ioDirID = CL(dirid);
    retval = PBHOpenRF(&hpb, FALSE);
    if (retval == noErr)
        *refp = CW(hpb.ioParam.ioRefNum);
    return retval;
}
