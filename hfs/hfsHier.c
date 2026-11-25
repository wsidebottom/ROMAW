/*+===================================================================
  File:      hfsHier.c

  Summary:   Implements hfs hier support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: ROMlib_hfs_move_rename, hfsPBCatMove, hfsPBGetCatInfo, hfsPBSetCatInfo

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "OSUtil.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "rsys/hfs.h"
#include "rsys/file.h"

typedef enum { catGet, catSet } catop;

PRIVATE OSErr cathelper(CInfoPBPtr pb, BOOLEAN async, catop op)
{
    filekind kind;
    filerec* frp;
    void* voidp;
    directoryrec* drp;
    HFileInfo* pbf;
    DirInfo* pbd;
    OSErr err, err1;
    btparam btparamrec;
    HVCB* vcbp;
    catkey* catkeyp;
    BOOLEAN ignorename;

    vcbp = 0;
    if (CW(pb->hFileInfo.ioFDirIndex) > 0 && op == catGet) {
        err = ROMlib_btpbindex((ioParam*)pb, CL(pb->hFileInfo.ioDirID), &vcbp, &frp, &catkeyp, FALSE);
        if (err != noErr)
            goto done;
        voidp = DATAPFROMKEY(catkeyp);
        switch (((filerec*)voidp)->cdrType) {
        case FILETYPE:
            kind = regular;
            break;
        case DIRTYPE:
            kind = directory;
            break;
        default:
            warning_unexpected("unknown cdrtype in cathelper");
            err = fsDSIntErr;
            goto done;
            break;
        }
    }
    else {
        if (CW(pb->hFileInfo.ioFDirIndex) < 0) {
            kind = directory;
            ignorename = TRUE;
        }
        else {
            kind = regular | directory;
            ignorename = FALSE;
        }
        err = ROMlib_findvcbandfile((ioParam*)pb, CL(pb->hFileInfo.ioDirID), &btparamrec, &kind, ignorename);
        if (err != noErr)
            goto done;
        vcbp = btparamrec.vcbp;
        if (op == catSet) {
            err = ROMlib_writevcbp(vcbp);
            if (err != noErr)
                goto done;
        }
        voidp = DATAPFROMKEY(btparamrec.foundp);
        catkeyp = (catkey*)btparamrec.foundp;
    }

    switch (kind) {
    case regular:
        frp = voidp;
        pbf = (HFileInfo*)pb;
        if (op == catGet) {
            if (UPDATE_IONAMEPTR_P(*pbf))
                str255assign(PPR(pbf->ioNamePtr), catkeyp->ckrCName);
            pbf->ioACUser = 0;
            pbf->ioFlAttrib = CB(open_attrib_bits(CL(frp->filFlNum), vcbp, &pbf->ioFRefNum));
            pbf->ioFlAttrib |= frp->filFlags & CB(INHERITED_FLAG_BITS);
            memmove(&pbf->ioFlFndrInfo, &frp->filUsrWds, (LONGINT)sizeof(pbf->ioFlFndrInfo));
            pbf->ioDirID = frp->filFlNum;
            pbf->ioFlStBlk = frp->filStBlk;
            pbf->ioFlLgLen = frp->filLgLen;
            pbf->ioFlPyLen = frp->filPyLen;
            pbf->ioFlRStBlk = frp->filRStBlk;
            pbf->ioFlRLgLen = frp->filRLgLen;
            pbf->ioFlRPyLen = frp->filRPyLen;
            pbf->ioFlCrDat = frp->filCrDat;
            pbf->ioFlMdDat = frp->filMdDat;
            pbf->ioFlBkDat = frp->filBkDat;
            memmove(&pbf->ioFlXFndrInfo, frp->filFndrInfo, (LONGINT)sizeof(pbf->ioFlXFndrInfo));
            pbf->ioFlParID = catkeyp->ckrParID;
            pbf->ioFlClpSiz = frp->filClpSize;
        }
        else {
            frp->filFlags &= ~FILEFLAGSUSERSETTABLEMASK;
            frp->filFlags |= FILEFLAGSUSERSETTABLEMASK & pbf->ioFlAttrib;
            memmove(&frp->filUsrWds, &pbf->ioFlFndrInfo, (LONGINT)sizeof(frp->filUsrWds));
            frp->filCrDat = pbf->ioFlCrDat;
            frp->filMdDat = pbf->ioFlMdDat;
            frp->filBkDat = pbf->ioFlBkDat;
            memmove(&frp->filFndrInfo, &pbf->ioFlXFndrInfo, (LONGINT)sizeof(frp->filFndrInfo));
            frp->filClpSize = pbf->ioFlClpSiz;
            ROMlib_dirtyleaf(frp, vcbp);
            err = ROMlib_flushvcbp(vcbp);
        }
        break;
    case directory:
        drp = voidp;
        pbd = (DirInfo*)pb;
        if (op == catGet) {
            if (UPDATE_IONAMEPTR_P(*pbd))
                str255assign(PPR(pbd->ioNamePtr), catkeyp->ckrCName);

            pbd->ioACUser = 0;

            /* NOTE: IMIV-155 claims that pbd->ioFRefNum is updated, but the Mac+
			 doesn't do the updating */

            pbd->ioFlAttrib = drp->dirFlags >> 8;
            pbd->ioFlAttrib |= ATTRIB_ISADIR;
            memmove(&pbd->ioDrUsrWds, drp->dirUsrInfo, (LONGINT)sizeof(pbd->ioDrUsrWds));
            pbd->ioDrDirID = drp->dirDirID;
            pbd->ioDrNmFls = drp->dirVal;
            pbd->ioDrCrDat = drp->dirCrDat;
            pbd->ioDrMdDat = drp->dirMdDat;
            pbd->ioDrBkDat = drp->dirBkDat;
            memmove(&pbd->ioDrFndrInfo, drp->dirFndrInfo, (LONGINT)sizeof(pbd->ioDrFndrInfo));
            pbd->ioDrParID = catkeyp->ckrParID;
        }
        else {
            memmove(&drp->dirUsrInfo, &pbd->ioDrUsrWds, (LONGINT)sizeof(drp->dirUsrInfo));
            drp->dirCrDat = pbd->ioDrCrDat;
            drp->dirMdDat = pbd->ioDrMdDat;
            drp->dirBkDat = pbd->ioDrBkDat;
            memmove(&drp->dirFndrInfo, &pbd->ioDrFndrInfo, (LONGINT)sizeof(drp->dirFndrInfo));
            ROMlib_dirtyleaf(drp, vcbp);
            err = ROMlib_flushvcbp(vcbp);
        }
        break;
    default:
        warning_unexpected("unknown kind in cathelper");
        err = fsDSIntErr;
        goto done;
        break;
    }
done:
    err1 = vcbp ? ROMlib_cleancache(vcbp) : noErr;
    if (err == noErr)
        err = err1;
    fs_err_hook(err);
    PBRETURN((ioParam*)pb, err);
}

PUBLIC OSErr hfsPBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    OSErr err;
    err = cathelper(pb, async, catGet);
    fs_err_hook(err);
    return err;
}

PUBLIC OSErr hfsPBSetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    OSErr err;
    err = cathelper(pb, async, catSet);
    fs_err_hook(err);
    return err;
}

PRIVATE OSErr parentchild(HVCB* vcbp, catkey* parentcatp, directoryrec* parentdirp, catkey* childcatp,
                          directoryrec* childdirp)
{
    OSErr err;
    LONGINT parid, newid;
    INTEGER ctref;
    btparam btparamrec;

    err = noErr;
    parid = CL(parentdirp->dirDirID);
    if (parid == CL(childdirp->dirDirID))
        err = badMovErr;                /* can't move into oneself */
    else if (parentdirp->dirVal != 0) { /* no need to check if no children */
        if (parid <= 2)                 /* automatic disqualification; can't move */
            err = badMovErr;            /* root directory */
        else {
            newid = CL(childcatp->ckrParID);
            err = ROMlib_makecatparam(&btparamrec, vcbp, (LONGINT)0, 0, (Ptr)0);
            ctref = CW(vcbp->vcbCTRef);
            while (err == noErr && newid > 2 && newid != parid) {
                btparamrec.tofind.catk.ckrParID = CL(newid);
                err = ROMlib_keyfind(&btparamrec);
                if (err == noErr && !btparamrec.success) {
                    err = fsDSIntErr;
                    warning_unexpected("no success in parentchild");
                }
                newid = CL(((threadrec*)DATAPFROMKEY(btparamrec.foundp))->thdParID);
            }
            if (err == noErr)
                err = newid <= 2 ? noErr : badMovErr;
        }
    }
    fs_err_hook(err);
    return err;
}

static OSErr hfs_catalog_move(CMovePBPtr pb, const Byte* leaf, bool move_rename)
{
    filekind source_kind = regular | directory;
    btparam source = {0};
    OSErr error = ROMlib_findvcbandfile((ioParam*)pb, CL(pb->ioDirID), &source, &source_kind, FALSE);
    if (error)
        return error;
    error = ROMlib_writevcbp(source.vcbp);
    if (error)
        return error;

    const catkey source_key = source.foundp->catk;
    directoryrec directory_record;
    filerec file_record;
    if (source_kind == directory) {
        directory_record = *(directoryrec*)DATAPFROMKEY(source.foundp);
        if (CL(directory_record.dirDirID) <= 2)
            return badMovErr;
    }
    else {
        file_record = *(filerec*)DATAPFROMKEY(source.foundp);
        if (move_rename && (file_record.filFlags & FSOFTLOCKBIT))
            return fLckdErr;
    }

    ioParam target_pb = *(ioParam*)pb;
    PP_RAW(target_pb.ioNamePtr) = PP_RAW(pb->ioNewName);
    btparam destination;
    filekind target_kind = directory;
    error = ROMlib_findvcbandfile(&target_pb, CL(pb->ioNewDirID), &destination, &target_kind,
                                 PP_RAW(target_pb.ioNamePtr) == 0);
    if (error)
        goto finish;
    if (source.vcbp != destination.vcbp) {
        error = badMovErr;
        goto finish;
    }

    directoryrec* target_dir = (directoryrec*)DATAPFROMKEY(destination.foundp);
    const uint32 parent_id = CL(target_dir->dirDirID);
    const Byte* name = leaf ? leaf : source_key.ckrCName;
    if (move_rename && parent_id == CL(source_key.ckrParID) &&
        name[0] == source_key.ckrCName[0] && !memcmp(name + 1, source_key.ckrCName + 1, name[0]))
        goto finish;
    if (source_kind == directory) {
        error = parentchild(source.vcbp, (catkey*)&source_key, &directory_record,
                            (catkey*)destination.foundp, target_dir);
        if (error)
            goto finish;
    }

    error = ROMlib_makecatkey((catkey*)&destination.tofind, parent_id, name[0], (Ptr)name + 1);
    if (error)
        goto finish;
    destination.leafindex = -1;
    error = ROMlib_keyfind(&destination);
    if (error)
        goto finish;
    if (destination.success) {
        error = dupFNErr;
        goto finish;
    }
    const catkey destination_key = destination.tofind.catk;
    destination.leafindex = -1;
    source.tofind.catk = source_key;
    source.leafindex = -1;
    if (source_kind == directory) {
        error = ROMlib_keyfind(&source);
        if (error || !source.success) {
            if (!error)
                error = fnfErr;
            goto finish;
        }
        /* The new directory and its old catalog entry share a thread key. */
        error = ROMlib_dirdelete(&source);
        if (!error) {
            error = ROMlib_dircreate(&destination, &directory_record);
            if (error) {
                destination.tofind.catk = destination_key;
                destination.leafindex = -1;
                if (!ROMlib_keyfind(&destination) && destination.success &&
                    ROMlib_filedelete(&destination, directory))
                    warning_unexpected("could not discard the partial HFS catalog directory");
                source.tofind.catk = source_key;
                source.leafindex = -1;
                if (ROMlib_dircreate(&source, &directory_record))
                    warning_unexpected("could not restore the original HFS catalog directory");
            }
        }
    }
    else {
        error = ROMlib_filecreate(&destination, &file_record, regular);
        if (!error) {
            error = ROMlib_filedelete(&source, regular);
            if (!error && (file_record.filFlags & FSFILETHREADBIT))
                error = ROMlib_hfs_update_file_thread(source.vcbp, CL(file_record.filFlNum), parent_id, name);
            if (error) {
                source.leafindex = -1;
                destination.leafindex = -1;
                if (!ROMlib_keyfind(&source) && source.success && ROMlib_filedelete(&destination, regular))
                    warning_unexpected("could not restore the original HFS file catalog state");
            }
        }
    }

finish: {
        OSErr flushed = ROMlib_cleancache(source.vcbp);
        if (!flushed)
            flushed = ROMlib_flushvcbp(source.vcbp);
        if (!error)
            error = flushed;
    }
    return error;
}

PUBLIC OSErr hfsPBCatMove(CMovePBPtr pb, BOOLEAN async)
{
    const OSErr error = hfs_catalog_move(pb, NULL, false);
    fs_err_hook(error);
    PBRETURN(pb, error);
}

OSErr ROMlib_hfs_move_rename(CopyParam* pb)
{
    return hfs_catalog_move((CMovePBPtr)pb, PPR(pb->ioCopyName), true);
}
