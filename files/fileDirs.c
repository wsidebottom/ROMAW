/*+===================================================================
  File:      fileDirs.c

  Summary:   Implements file dirs support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: ROMlib_PBMoveOrRename, ufsPBCatMove, ufsPBGetCatInfo, ufsPBOpenWD, ufsPBSetCatInfo

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in FileMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "FileMgr.h"
#include "OSEvent.h"
#include "VRetraceMgr.h"
#include "OSUtil.h"
#include "MemoryMgr.h"

#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/filedouble.h"

/*
 * Remember, vrn and wdn are negative numbers
 */

A2(PUBLIC, OSErr, ufsPBGetCatInfo, CInfoPBPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    LONGINT swapped_dir;
    OSErr retval;

    swapped_dir = pb->hFileInfo.ioDirID;
    retval = ROMlib_PBGetSetFInfoD((ParmBlkPtr)pb, a, Get, &swapped_dir, TRUE);

    pb->hFileInfo.ioACUser = 0;

    return retval;
}

A2(PUBLIC, OSErr, ufsPBSetCatInfo, CInfoPBPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    LONGINT swapped_dir;
    OSErr retval;

    swapped_dir = pb->hFileInfo.ioDirID;
    retval = ROMlib_PBGetSetFInfoD((ParmBlkPtr)pb, a, Set, &swapped_dir, TRUE);
    return retval;
}

A6(PUBLIC, OSErr, ROMlib_PBMoveOrRename, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a, LONGINT, dir, LONGINT, newdir, char*, newname, MoveOrRenameType, op)
{
    ParamBlockRec npb;
    OSErr err;
    char *oldpathname, *oldfilename, *oldendname;
    char *newpathname, *newfilename, *newendname, *fullnewname;
    char *rname, *rnew;
    int renamefailed;
    VCBExtra* vcbp;
    struct stat sbuf;
    LONGINT dirid;
    ALLOCABEGIN

    oldpathname = 0;
    newpathname = 0;
    rname = 0;
    rnew = 0;

    if ((err = ROMlib_nami(pb, dir, NoIndex, &oldpathname, &oldfilename, &oldendname, TRUE, &vcbp, &sbuf)) == noErr) {
        /* Renaming a mounted host root changes its guest label only. */
        if (op != CatMove && !strcmp(oldpathname, vcbp->unixname)) {
            err = ROMlib_rename_host_volume(vcbp, (StringPtr)newname);
            goto done;
        }
        FIELD_ASSIGN_RAW(npb.ioParam.ioNamePtr, RM((StringPtr)newname));
        npb.ioParam.ioVRefNum = pb->ioParam.ioVRefNum;
        if ((err = ROMlib_nami(&npb, newdir, NoIndex, &newpathname, &newfilename, &newendname, TRUE, (VCBExtra**)0,
                               (struct stat*)0)) == noErr) {
            if (op == CatMove) {
                fullnewname = ALLOCA(newendname - newpathname + oldendname - oldfilename + 2); /* + '/' and null */
                sprintf(fullnewname, "%s/%s", newpathname, oldfilename);
            }
            else
                fullnewname = newpathname;
            if (strcmp(oldpathname, fullnewname) != 0 && ROMlib_ufs_stat(fullnewname, &sbuf) != -1)
                err = dupFNErr;
            else {
                if (stat(oldpathname, &sbuf) != 0) {
                    sbuf.st_ino = 0;
#if defined(CYGWIN32)
                    sbuf.st_rdev = 0;
#endif
                }

                rname = ROMlib_resname(oldpathname, oldfilename, oldendname);
                if (!rname) {
                    err = memFullErr;
                    goto done;
                }
                char* resource = ROMlib_sidecar_resource_name(rname);
                if (ROMlib_empty_fork_busy(oldpathname) || ROMlib_empty_fork_busy(rname) ||
                    (resource && ROMlib_empty_fork_busy(resource))) {
                    free(resource);
                    err = fBsyErr;
                    goto done;
                }
                free(resource);
                boolean_t data_present = Ustat(oldpathname, &sbuf) == 0;
                renamefailed = data_present ? Urename(oldpathname, fullnewname) : 0;
                if (renamefailed == 0) {
                    ROMlib_dbm_delete_inode(vcbp, ST_INO(sbuf));
                    dirid = 0;
                    ROMlib_dbm_store(vcbp, fullnewname, &dirid, TRUE);
                    if (op == CatMove) {
                        newfilename = strrchr(fullnewname, '/') + 1;
                        newendname = fullnewname + strlen(fullnewname) + 1;
                    }
                    rnew = ROMlib_sidecar_rename_name(rname, fullnewname, newfilename);
                    renamefailed = ROMlib_sidecar_move(rname, rnew);
                    /* TODO: eventually put a call to ROMlib_fcbrename here, but that requires
					   us to know the parent id number, something we don't know right now. */
                }
                if (renamefailed != 0)
                    err = ROMlib_maperrno();
            }
        }
    }
done:
    if (oldpathname)
        free(oldpathname);
    if (newpathname)
        free(newpathname);
    if (rname)
        free(rname);
    if (rnew)
        free(rnew);
    return err;
    ALLOCAEND
}

A2(PUBLIC, OSErr, ufsPBCatMove, CMovePBPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    return ROMlib_PBMoveOrRename((ParmBlkPtr)pb, a, Cx(pb->ioDirID), Cx(pb->ioNewDirID), (char*)PPR(pb->ioNewName),
                                 CatMove);
}

A2(PUBLIC, OSErr, ufsPBOpenWD, WDPBPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    OSErr err;
    ParamBlockRec newpb;
    struct stat sbuf;
    char *pathname, *filename, *endname;
    VCBExtra* vcbp;
    LONGINT dirid;

    FIELD_ASSIGN_RAW(newpb.ioParam.ioNamePtr, pb->ioNamePtr);
    newpb.ioParam.ioVRefNum = pb->ioVRefNum;

    err = ROMlib_nami(&newpb, Cx(pb->ioWDDirID), NoIndex, &pathname, &filename, &endname, TRUE, &vcbp, &sbuf);
    if (err == noErr) {
        LONGINT l;

        ROMlib_automount(pathname);
        vcbp = (VCBExtra*)ROMlib_vcbbybiggestunixname(pathname);
        if (S_ISDIR(sbuf.st_mode))
            dirid = ST_INO(sbuf);
        else
            dirid = Cx(pb->ioWDDirID);
        if (dirid == vcbp->u.ufs.ino)
            dirid = 2;
        l = dirid;
        if (S_ISDIR(sbuf.st_mode))
            ROMlib_dbm_store(vcbp, pathname, &l, TRUE);
        free(pathname);
        err = ROMlib_mkwd(pb, (VCB*)vcbp, dirid, Cx(pb->ioWDProcID));
    }
    return err;
}
