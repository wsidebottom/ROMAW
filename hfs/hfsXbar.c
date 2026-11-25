/*+===================================================================
  File:      hfsXbar.c

  Summary:   Implements hfs xbar support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: OpenDeny, OpenRFDeny, PBCatSearch, PBCreateFileIDRef, PBDeleteFileIDRef, PBHMoveRename, PBResolveFileIDRef,
             cachecheck, test_serial

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in FileMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "FileMgr.h"
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/stdfile.h"
#include "rsys/flags.h"
#include "rsys/legacysound.h"
#include "rsys/configprefs.h"
#include "rsys/time.h"
#include "OSUtil.h"

/*
 * TODO: pass the information gleaned by hfsvol and hfsfil into the
 *       various hfsPB and ufsPB routines.
 */

#if defined(CACHECHECK)
void cachecheck(HVCB* vcbp)
{
    cacheentry* cachep;
    cachehead* headp;
    INTEGER i;

    headp = (cachehead*)CL(vcbp->vcbCtlBuf);
    for (i = Cx(headp->nitems), cachep = Cx(headp->flink); --i >= 0; cachep = Cx(cachep->flink))
        if (Cx(cachep->flags) & CACHEBUSY)
            warning_unexpected("busy");
}
#endif /* defined(CACHECHECK) */

/* Protect hard-locked volumes, including host-backed image snapshots, through
   every write trap so metadata and resource sidecars stay immutable too. */
static BOOLEAN host_image_locked(ioParam* pb, BOOLEAN by_file)
{
    HVCB* vcb;
    LONGINT directory;
    if (by_file) {
        filecontrolblock* fcb = ROMlib_refnumtofcbp(CW(pb->ioRefNum));
        vcb = fcb ? PPR(fcb->fcbVPtr) : NULL;
    }
    else
        vcb = ROMlib_findvcb(CW(pb->ioVRefNum), PPR(pb->ioNamePtr), &directory, TRUE);
    return vcb && (CW(vcb->vcbAtrb) & VHARDLOCKBIT);
}

PRIVATE BOOLEAN hfsvol(ioParam* pb)
{
    HVCB* vcbp;
    LONGINT dir;

    vcbp = ROMlib_findvcb(Cx(pb->ioVRefNum), PPR(pb->ioNamePtr), &dir, TRUE);
    if (!vcbp) {
        vcbp = ROMlib_findvcb(Cx(pb->ioVRefNum), (StringPtr)0, &dir, TRUE);
        if (!vcbp)
            return TRUE; /* hopefully is a messed up working dir
                     reference */
    }
    if (vcbp->vcbCTRef) {
#if defined(CACHECHECK)
        cachecheck(vcbp);
#endif /* defined(CACHECHECK) */
        return TRUE;
    }
    else
        return FALSE;
}

PRIVATE BOOLEAN hfsIvol(volumeParam* pb) /* potentially Indexed vol */
{
    BOOLEAN retval;
    HVCB* vcbp;

    retval = FALSE;
    if (Cx(pb->ioVolIndex) > 0) {
        vcbp = (HVCB*)ROMlib_indexqueue(&VCBQHdr, Cx(pb->ioVolIndex));
        if (vcbp && vcbp->vcbCTRef)
            retval = TRUE;
    }
    else
        retval = hfsvol((ioParam*)pb);
    return retval;
}

PRIVATE BOOLEAN hfsfil(ioParam* pb)
{
    filecontrolblock* fcbp;
    HVCB* vcbp;

    fcbp = ROMlib_refnumtofcbp(Cx(pb->ioRefNum));
    if (fcbp) {
        vcbp = PPR(fcbp->fcbVPtr);
        if (vcbp->vcbCTRef) {
#if defined(CACHECHECK)
            cachecheck(vcbp);
#endif /* defined(CACHECHECK) */
            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}

A2(PUBLIC trap, OSErrRET, PBHRename, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHRename(pb, async);
    else
        retval = ufsPBHRename(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHCreate, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHCreate(pb, async);
    else
        retval = ufsPBHCreate(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBDirCreate, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBDirCreate(pb, async);
    else
        retval = ufsPBDirCreate(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHDelete, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHDelete(pb, async);
    else
        retval = ufsPBHDelete(pb, async);
    FAKEASYNC(pb, async, retval);
}

PRIVATE void try_to_reopen(DrvQExtra* dqp)
{
#if defined(LINUX)
    drive_flags_t flags;
    dqp->hfs.fd = linuxfloppy_open(0, &dqp->hfs.bsize, &flags, (char*)dqp->devicename);
#else
    /* #warning need to be able to reopen a drive */
    warning_unimplemented("Unable to reopen a drive because the code has not "
                          "yet\nbeen written for this platform.");
#endif
}

PUBLIC int ROMlib_directdiskaccess = FALSE;

A2(PUBLIC trap, OSErrRET, PBRead, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (!ROMlib_fork_access_allowed(CW(pb->ioParam.ioRefNum), file_access_read)) {
        pb->ioParam.ioActCount = 0;
        FAKEASYNC(pb, async, permErr);
    }
    OSErr retval;
    DrvQExtra* dqp;

    switch (Cx(pb->ioParam.ioRefNum)) {
    case OURHFSDREF:
        if (ROMlib_directdiskaccess) {
            dqp = ROMlib_dqbydrive(Cx(pb->ioParam.ioVRefNum));
            if (!dqp) {
                pb->ioParam.ioResult = CW(nsvErr);
                pb->ioParam.ioActCount = 0;
            }
            else {
                if (dqp->hfs.fd == -1)
                    try_to_reopen(dqp);
                if (dqp->hfs.fd == -1) {
                    pb->ioParam.ioResult = CWC(nsvErr);
                    pb->ioParam.ioActCount = 0;
                }
                else if (Cx(pb->ioParam.ioPosMode) != fsFromStart) {
                    pb->ioParam.ioResult = CW(paramErr); /* for now */
                    pb->ioParam.ioActCount = 0;
                }
                else
                    pb->ioParam.ioResult = CW(ROMlib_transphysblk(
                        &dqp->hfs, Cx(pb->ioParam.ioPosOffset), Cx(pb->ioParam.ioReqCount) / PHYSBSIZE,
                        PPR(pb->ioParam.ioBuffer), reading, &pb->ioParam.ioActCount));
            }
        }
        else {
            pb->ioParam.ioResult = CW(vLckdErr);
            pb->ioParam.ioActCount = 0;
        }
        retval = Cx(pb->ioParam.ioResult);
        break;

    default:
        if (pb->ioParam.ioPosMode & CWC(NEWLINEMODE)) {
            char *buf, *p_to_find, *p_alternate, *p;
            unsigned char to_find;
            long act_count;
            ParamBlockRec pbr;

            pbr = *pb;
            to_find = CW(pb->ioParam.ioPosMode) >> 8;
            pbr.ioParam.ioPosMode &= CWC(0x7F);

            buf = alloca(CL(pb->ioParam.ioReqCount));

            PP_RAW(pbr.ioParam.ioBuffer) = (Ptr)RM(buf);
            retval = PBRead(&pbr, FALSE);
            pb->ioParam.ioActCount = pbr.ioParam.ioActCount;
            pb->ioParam.ioPosOffset = pbr.ioParam.ioPosOffset;

            act_count = CL(pb->ioParam.ioActCount);
            p_to_find = memchr(buf, to_find, act_count);

            if (to_find == '\r' && ROMlib_newlinetocr)
                p_alternate = memchr(buf, '\n', act_count);
            else
                p_alternate = 0;

            if (p_alternate && (!p_to_find || p_alternate < p_to_find))
                p = p_alternate;
            else
                p = p_to_find;

            if (p) {
                long to_backup;

                retval = noErr; /* we couldn't have gotten EOF yet */
                to_backup = act_count - (p + 1 - buf);
                SWAPPED_OPL(pb->ioParam.ioActCount, -, to_backup);
                {
                    ParamBlockRec newpb;
                    OSErr newerr;

                    newpb.ioParam.ioRefNum = pb->ioParam.ioRefNum;
                    newpb.ioParam.ioPosMode = CWC(fsFromMark);
                    newpb.ioParam.ioPosOffset = CL(-to_backup);
                    newerr = PBSetFPos(&newpb, FALSE);
                    if (newerr != noErr)
                        warning_unexpected("err = %d", newerr);
                }
                if (ROMlib_newlinetocr && to_find == '\r')
                    *p = '\r';
            }
            memcpy(PPR(pb->ioParam.ioBuffer), PPR(pbr.ioParam.ioBuffer), CL(pb->ioParam.ioActCount));
            ROMlib_destroy_blocks((syn68k_addr_t)(long)US_TO_SYN68K(PPR(pb->ioParam.ioBuffer)),
                                  CL(pb->ioParam.ioActCount), TRUE);
        }
        else {
            if (hfsfil((ioParam*)pb))
                retval = hfsPBRead(pb, async);
            else
                retval = ufsPBRead(pb, async);
        }
        break;
    }
    FAKEASYNC(pb, async, retval);
}

#define SOUND_DRIVER_REF (-4)

A2(PUBLIC trap, OSErrRET, PBWrite, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (!ROMlib_fork_access_allowed(CW(pb->ioParam.ioRefNum), file_access_write)) {
        pb->ioParam.ioActCount = 0;
        FAKEASYNC(pb, async, wrPermErr);
    }
    if (host_image_locked((ioParam*)pb, TRUE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;
    HVCB* vcbp;
    DrvQExtra* dqp;

    switch (Cx(pb->ioParam.ioRefNum)) {
    case SOUND_DRIVER_REF:
        return ROMlib_legacy_sound_write(pb, async);
    case OURHFSDREF:
        if (!ROMlib_directdiskaccess)
            pb->ioParam.ioResult = CW(vLckdErr);
        else {
            dqp = ROMlib_dqbydrive(Cx(pb->ioParam.ioVRefNum));
            if (dqp && dqp->hfs.fd == -1)
                try_to_reopen(dqp);
            vcbp = ROMlib_vcbbydrive(Cx(pb->ioParam.ioVRefNum));
            if (!dqp)
                pb->ioParam.ioResult = CW(nsvErr);
            else if (vcbp && (Cx(vcbp->vcbAtrb) & VSOFTLOCKBIT))
                pb->ioParam.ioResult = CW(vLckdErr);
            else if (vcbp && (Cx(vcbp->vcbAtrb) & VHARDLOCKBIT))
                pb->ioParam.ioResult = CW(wPrErr);
            else if (Cx(pb->ioParam.ioPosMode) != fsFromStart)
                pb->ioParam.ioResult = CW(paramErr); /* for now */
            else
                pb->ioParam.ioResult = CW(
                    ROMlib_transphysblk(&dqp->hfs, Cx(pb->ioParam.ioPosOffset), Cx(pb->ioParam.ioReqCount) / PHYSBSIZE,
                                        PPR(pb->ioParam.ioBuffer), writing, &pb->ioParam.ioActCount));
        }
        if (pb->ioParam.ioResult != noErr)
            pb->ioParam.ioActCount = 0;
        retval = Cx(pb->ioParam.ioResult);
        break;

    default:
        if (hfsfil((ioParam*)pb))
            retval = hfsPBWrite(pb, async);
        else
            retval = ufsPBWrite(pb, async);
        break;
    }
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBClose, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBClose(pb, async);
    else
        retval = ufsPBClose(pb, async);
    FAKEASYNC(pb, async, retval);
}

/*
 * The test for ioBuffer being 0 was determined necessary after MacRoads
 * couldn't open the serial drivers.  When I first tested PBHOpen to see
 * if it could open drivers, it couldn't, but that's because my test was
 * coded according to Inside Macintosh and I was assuming that ioBuffer
 * wasn't examined.  It took a few runs of binary searches stuffing fields
 * with zeros to find out that ioBuffer had this magic property.
 *
 * PBOpen doesn't require ioBuffer to be 0 in order to open a driver.
 */

A2(PUBLIC trap, OSErrRET, PBHOpen, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        if (pb->ioParam.ioPermssn == fsCurPerm)
            pb->ioParam.ioPermssn = fsRdPerm;
        else if (pb->ioParam.ioPermssn != fsRdPerm) {
            pb->ioParam.ioRefNum = 0;
            FAKEASYNC(pb, async, wPrErr);
        }
    }
    OSErr retval;

    if (PP_RAW(pb->ioParam.ioBuffer) == 0 && PP_RAW(pb->ioParam.ioNamePtr) && PPR(pb->ioParam.ioNamePtr)[0] &&
        PPR(pb->ioParam.ioNamePtr)[1] == '.')
        retval = ROMlib_driveropen((ParmBlkPtr)pb, async);
    else if (hfsvol((ioParam*)pb))
        retval = hfsPBHOpen(pb, async);
    else
        retval = ufsPBHOpen(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBOpenDF, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        if (pb->ioParam.ioPermssn == fsCurPerm)
            pb->ioParam.ioPermssn = fsRdPerm;
        else if (pb->ioParam.ioPermssn != fsRdPerm) {
            pb->ioParam.ioRefNum = 0;
            FAKEASYNC(pb, async, wPrErr);
        }
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHOpen(pb, async);
    else
        retval = ufsPBHOpen(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHOpenRF, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        if (pb->ioParam.ioPermssn == fsCurPerm)
            pb->ioParam.ioPermssn = fsRdPerm;
        else if (pb->ioParam.ioPermssn != fsRdPerm) {
            pb->ioParam.ioRefNum = 0;
            FAKEASYNC(pb, async, wPrErr);
        }
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHOpenRF(pb, async);
    else
        retval = ufsPBHOpenRF(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetCatInfo, CInfoPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;
    BOOLEAN ishfs;
    StringPtr savep;

    if (CW(pb->dirInfo.ioFDirIndex) < 0 && pb->hFileInfo.ioDirID == CLC(1))
        retval = -43; /* perhaps we should check for a valid volume
                 first */
    else {
        savep = PP_RAW(pb->dirInfo.ioNamePtr);
        if (pb->dirInfo.ioFDirIndex != CWC(0)) /* IMIV-155, 156 */
            PP_RAW(pb->dirInfo.ioNamePtr) = 0;
        ishfs = hfsvol((ioParam*)pb);
        PP_RAW(pb->dirInfo.ioNamePtr) = savep;

        if (ishfs)
            retval = hfsPBGetCatInfo(pb, async);
        else
            retval = ufsPBGetCatInfo(pb, async);
    }
    FAKEASYNC(pb, async, retval);
}

static OSErr open_fork_with_deny(HParmBlkPtr request, bool resource)
{
    ForkAccessParam* access = (ForkAccessParam*)request;
    const unsigned modes = CW(access->ioDenyModes) &
                           (file_access_read | file_access_write | file_deny_read | file_deny_write);
    HParamBlockRec block = {0};
    PP_RAW(block.ioParam.ioNamePtr) = PP_RAW(access->ioNamePtr);
    block.ioParam.ioVRefNum = access->ioVRefNum;
    block.fileParam.ioDirID = access->ioDirID;
    const bool writing = (modes & file_access_write) != 0;
    block.ioParam.ioPermssn = writing ? ((modes & file_access_read) ? fsRdWrShPerm : fsWrPerm) : fsRdPerm;

    if (writing && host_image_locked(&block.ioParam, FALSE)) {
        access->ioRefNum = 0;
        return wPrErr;
    }
    const OSErr status = hfsvol(&block.ioParam) ? ROMlib_hfs_open_deny(&block, resource, modes)
                                               : ROMlib_ufs_open_deny(&block, resource, modes);
    access->ioRefNum = block.ioParam.ioRefNum;
    return status;
}

A2(PUBLIC trap, OSErrRET, OpenDeny, HParmBlkPtr, pb, BOOLEAN, async)
{
    const OSErr status = open_fork_with_deny(pb, false);
    FAKEASYNC(pb, async, status);
}

A2(PUBLIC trap, OSErrRET, OpenRFDeny, HParmBlkPtr, pb, BOOLEAN, async)
{
    const OSErr status = open_fork_with_deny(pb, true);
    FAKEASYNC(pb, async, status);
}

A2(PUBLIC trap, OSErrRET, PBSetCatInfo, CInfoPBPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBSetCatInfo(pb, async);
    else
        retval = ufsPBSetCatInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBCatMove, CMovePBPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBCatMove(pb, async);
    else
        retval = ufsPBCatMove(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHMoveRename, HParmBlkPtr, pb, BOOLEAN, async)
{
    CopyParam* move = (CopyParam*)pb;
    const Byte* leaf = PPR(move->ioCopyName);
    if (leaf && (!leaf[0] || leaf[0] > 31 || memchr(leaf + 1, ':', leaf[0]) || memchr(leaf + 1, 0, leaf[0]))) {
        FAKEASYNC(pb, async, bdNamErr);
    }
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    const OSErr error = hfsvol((ioParam*)pb) ? ROMlib_hfs_move_rename(move) : ROMlib_ufs_move_rename(move);
    FAKEASYNC(pb, async, error);
}

A2(PUBLIC trap, OSErrRET, PBGetVInfo, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsIvol((volumeParam*)pb))
        retval = hfsPBGetVInfo(pb, async);
    else
        retval = ufsPBGetVInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A1(PUBLIC trap, OSErrRET, PBUnmountVol, ParmBlkPtr, pb)
{
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBUnmountVol(pb);
    else
        retval = ufsPBUnmountVol(pb);
    PBRETURN(pb, retval);
}

A1(PUBLIC trap, OSErrRET, PBEject, ParmBlkPtr, pb)
{
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBEject(pb);
    else
        retval = ufsPBEject(pb);
    PBRETURN(pb, retval);
}

A2(PUBLIC trap, OSErrRET, PBAllocate, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, TRUE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBAllocate(pb, async);
    else
        retval = ufsPBAllocate(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBAllocContig, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, TRUE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBAllocContig(pb, async);
    else
        retval = ufsPBAllocContig(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHGetFInfo, HParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;
    BOOLEAN ishfs;
    StringPtr savep;

    savep = PP_RAW(pb->ioParam.ioNamePtr);
    if (CW(pb->fileParam.ioFDirIndex) > 0) /* IMIV-155, 156 */
        PP_RAW(pb->ioParam.ioNamePtr) = 0;
    ishfs = hfsvol((ioParam*)pb);
    PP_RAW(pb->ioParam.ioNamePtr) = savep;

    if (ishfs)
        retval = hfsPBHGetFInfo(pb, async);
    else
        retval = ufsPBHGetFInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetEOF, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, TRUE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBSetEOF(pb, async);
    else
        retval = ufsPBSetEOF(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBOpen, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        if (pb->ioParam.ioPermssn == fsCurPerm)
            pb->ioParam.ioPermssn = fsRdPerm;
        else if (pb->ioParam.ioPermssn != fsRdPerm) {
            pb->ioParam.ioRefNum = 0;
            FAKEASYNC(pb, async, wPrErr);
        }
    }
    OSErr retval;

    if (PP_RAW(pb->ioParam.ioNamePtr) && PPR(pb->ioParam.ioNamePtr)[0] && PPR(pb->ioParam.ioNamePtr)[1] == '.')
        retval = ROMlib_driveropen(pb, async);
    else if (hfsvol((ioParam*)pb))
        retval = hfsPBOpen(pb, async);
    else
        retval = ufsPBOpen(pb, async);
    FAKEASYNC(pb, async, retval);
}

#if !defined(NDEBUG)
PUBLIC void test_serial(void)
{
    OSErr open_in_val, open_out_val, close_in_val, close_out_val;
    ParamBlockRec pb_in, pb_out;
    static int count = 0;

    memset(&pb_in, 0, sizeof pb_in);
    memset(&pb_out, 0, sizeof pb_out);
    PP_RAW(pb_in.ioParam.ioNamePtr) = RM((StringPtr) "\004.AIn");
    PP_RAW(pb_out.ioParam.ioNamePtr) = RM((StringPtr) "\005.AOut");
    open_in_val = PBOpen(&pb_in, FALSE);
    open_out_val = PBOpen(&pb_out, FALSE);
    close_in_val = PBClose(&pb_in, FALSE);
    close_out_val = PBClose(&pb_out, FALSE);
    ++count;
}
#endif

A2(PUBLIC trap, OSErrRET, PBOpenRF, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        if (pb->ioParam.ioPermssn == fsCurPerm)
            pb->ioParam.ioPermssn = fsRdPerm;
        else if (pb->ioParam.ioPermssn != fsRdPerm) {
            pb->ioParam.ioRefNum = 0;
            FAKEASYNC(pb, async, wPrErr);
        }
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBOpenRF(pb, async);
    else
        retval = ufsPBOpenRF(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBLockRange, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBLockRange(pb, async);
    else
        retval = ufsPBLockRange(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBUnlockRange, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBUnlockRange(pb, async);
    else
        retval = ufsPBUnlockRange(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetFPos, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBGetFPos(pb, async);
    else
        retval = ufsPBGetFPos(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetFPos, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBSetFPos(pb, async);
    else
        retval = ufsPBSetFPos(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetEOF, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBGetEOF(pb, async);
    else
        retval = ufsPBGetEOF(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBFlushFile, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsfil((ioParam*)pb))
        retval = hfsPBFlushFile(pb, async);
    else
        retval = ufsPBFlushFile(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBCreate, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBCreate(pb, async);
    else
        retval = ufsPBCreate(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBDelete, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBDelete(pb, async);
    else
        retval = ufsPBDelete(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBOpenWD, WDPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBOpenWD(pb, async);
    else
        retval = ufsPBOpenWD(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBCloseWD, WDPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBCloseWD(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetWDInfo, WDPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBGetWDInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetFInfo, ParmBlkPtr, pb, BOOLEAN, async)
{
    BOOLEAN ishfs;
    StringPtr savep;
    OSErr retval;

    savep = PP_RAW(pb->ioParam.ioNamePtr);
    if (CW(pb->fileParam.ioFDirIndex) > 0) /* IMIV-155, 156 */
        PP_RAW(pb->ioParam.ioNamePtr) = 0;
    ishfs = hfsvol((ioParam*)pb);
    PP_RAW(pb->ioParam.ioNamePtr) = savep;

    if (ishfs)
        retval = hfsPBGetFInfo(pb, async);
    else
        retval = ufsPBGetFInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetFInfo, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBSetFInfo(pb, async);
    else
        retval = ufsPBSetFInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHSetFInfo, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHSetFInfo(pb, async);
    else
        retval = ufsPBHSetFInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetFLock, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBSetFLock(pb, async);
    else
        retval = ufsPBSetFLock(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHSetFLock, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHSetFLock(pb, async);
    else
        retval = ufsPBHSetFLock(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBRstFLock, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBRstFLock(pb, async);
    else
        retval = ufsPBRstFLock(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHRstFLock, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBHRstFLock(pb, async);
    else
        retval = ufsPBHRstFLock(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetFVers, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBSetFVers(pb, async);
    else
        retval = ufsPBSetFVers(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBRename, ParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBRename(pb, async);
    else
        retval = ufsPBRename(pb, async);
    FAKEASYNC(pb, async, retval);
}

/*
 * The code below has a hack in it that is due to the non-standard way in
 * which we handle floppy drives.  Currently, under ROMAW, Cmd-Shift-2
 * causes all potentially removable media drives to be scanned for new volumes
 * and any that are found are immediately mounted.  This means that programs
 * that use GetOSEvent to pick up disk inserted events *before* mounting that
 * then do their own mounting will lose, since the volume will already be
 * mounted and hence their own mount will fail.  BodyWorks 3.0's Installer
 * does this.  So currently if drive 1 or drive 2 is explictly being mounted
 * we return noErr.  This can get us into trouble later, but to fix it will
 * require a major rewrite of how we handle removable media.
 *
 * As a matter of fact, Browser used to have problems with this because it
 * would call PBMountVol to determine whether or not the floppy drive had
 * a disk in it that either was mounted or could be mounted, and it would
 * then call PBGetVInfo and not look at the return value and unconditionally
 * use the volume name it expected to be returned.  Ugh.
 */

/* #warning hacked PBMountVol */

A1(PUBLIC trap, OSErr, PBMountVol, ParmBlkPtr, pb)
{
    INTEGER vref;
    OSErr retval;

    vref = CW(pb->ioParam.ioVRefNum);
    if (vref == 1 || vref == 2)
        retval = noErr;
    else
        retval = ufsPBMountVol(pb);
    PBRETURN(pb, retval);
}

A2(PUBLIC trap, OSErrRET, PBHGetVInfo, HParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsIvol((volumeParam*)pb))
        retval = hfsPBHGetVInfo(pb, async);
    else
        retval = ufsPBHGetVInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

/* Smaller Installer needs bHasExtFSVol. Desktop Folder is supported by
   FindFolder: SuperMario's FolderMgr rejects it when bNoDeskItems is set. */

enum {
    VOL_BITS = ((1L << bHasExtFSVol) | (1L << bNoSysDir) | (1L << bNoBootBlks) | (1L << bNoSwitchTo) |
                (1L << bNoLclSync) | (1L << bNoVNEdit) | (1L << bNoMiniFndr))
};

A2(PUBLIC trap, OSErrRET, PBHGetVolParms, HParmBlkPtr, pb, BOOLEAN, async)
{
    LONGINT dir;
    HVCB* vcbp;
    OSErr err;
    LONGINT requested = CL(pb->ioParam.ioReqCount);

    pb->ioParam.ioActCount = 0;
    vcbp = ROMlib_findvcb(Cx(pb->ioParam.ioVRefNum), PPR(pb->ioParam.ioNamePtr), &dir, FALSE);
    if (!vcbp)
        err = nsvErr;
    else if (requested < 0)
        err = paramErr;
    else {
        /* SuperMario returns the caller-sized prefix of a complete version-2
         record, including prefixes ending inside a field. Zero denotes an
         unrated volume and no supported foreign privilege model. */
        getvolparams_info_t info = {0};
        info.vMVersion = CWC(2);
        info.vMAttrib = CLC(VOL_BITS | (1UL << bHasOpenDeny) | (1UL << bHasMoveRename) | (1UL << bHasCopyFile));
        /* Host volumes implement the classic Desktop DB/DF traps. Image
         volumes retain their existing resource-based Desktop path. */
        if (!vcbp->vcbCTRef) {
            info.vMAttrib &= ~CL(1UL << bNoVNEdit);
            if (config_drive_sidecars(((VCBExtra*)vcbp)->unixname))
                info.vMAttrib |= CL(1UL << bHasDesktopMgr);
            else
                info.vMAttrib |= CL(1UL << bNoDeskItems);
        }
        else
            info.vMAttrib |= CLC(1UL << bHasFileIDs);
        size_t count = MIN((size_t)requested, sizeof info);
        syn68k_addr_t buffer = CL((uint32)(uintptr_t)PP_RAW(pb->ioParam.ioBuffer));
        buffer &= syn68k_address_mask;
        if (count && (!buffer || !syn68k_memory_span(buffer, count)))
            err = paramErr;
        else {
            if (count)
                memcpy(PPR(pb->ioParam.ioBuffer), &info, count);
            pb->ioParam.ioActCount = CL((LONGINT)count);
            err = noErr;
        }
    }
    FAKEASYNC(pb, async, err);
}

A2(PUBLIC trap, OSErrRET, PBSetVInfo, HParmBlkPtr, pb, BOOLEAN, async)
{
    if (host_image_locked((ioParam*)pb, FALSE)) {
        FAKEASYNC(pb, async, wPrErr);
    }
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBSetVInfo(pb, async);
    else
        retval = ufsPBSetVInfo(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBGetVol, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBGetVol(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHGetVol, WDPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBHGetVol(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBSetVol, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBSetVol(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBHSetVol, WDPBPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    retval = hfsPBHSetVol(pb, async);
    FAKEASYNC(pb, async, retval);
}

A2(PUBLIC trap, OSErrRET, PBFlushVol, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBFlushVol(pb, async);
    else
        retval = ufsPBFlushVol(pb, async);
    FAKEASYNC(pb, async, retval);
}

A1(PUBLIC trap, OSErrRET, PBOffLine, ParmBlkPtr, pb)
{
    OSErr retval;

    if (hfsvol((ioParam*)pb))
        retval = hfsPBOffLine(pb);
    else
        retval = ufsPBOffLine(pb);
    PBRETURN(pb, retval);
}

A2(PUBLIC trap, OSErrRET, PBExchangeFiles, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

_Static_assert(offsetof(catalog_search_pb_t, position) == 52, "CSParam position ABI");
_Static_assert(sizeof(catalog_search_pb_t) == 76, "CSParam ABI");

static void* catalog_search_pointer(uint32 address, size_t size)
{
    address &= syn68k_address_mask;
    return address && syn68k_memory_span(address, size) ? SYN68K_TO_US(address) : NULL;
}

static uint32 catalog_search_long(const void* data, size_t offset)
{
    ULONGINT value;
    memcpy(&value, (const char*)data + offset, sizeof value);
    return CL(value);
}

static BOOLEAN catalog_search_match(const CInfoPBRec* info, const unsigned char* name, const CInfoPBRec* lower,
                                    const CInfoPBRec* upper, uint32 bits, const unsigned char* wanted)
{
    BOOLEAN match = TRUE;
    BOOLEAN directory = !!(info->hFileInfo.ioFlAttrib & ATTRIB_ISADIR);
    if (bits & 3) {
        match = FALSE;
        unsigned length = wanted[0];
        for (unsigned i = 0; i + length <= name[0]; ++i) {
            if ((bits & 2) && (i || length != name[0]))
                break;
            if (!ROMlib_RelString((unsigned char*)name + 1 + i, (unsigned char*)wanted + 1, FALSE, TRUE,
                                  (length << 16) | length)) {
                match = TRUE;
                break;
            }
        }
    }
    if ((bits & 4) && ((info->hFileInfo.ioFlAttrib ^ lower->hFileInfo.ioFlAttrib) & upper->hFileInfo.ioFlAttrib &
                       (ATTRIB_ISLOCKED | ATTRIB_ISADIR)))
        match = FALSE;
    for (unsigned group = 0; group < 2; ++group) {
        if (!(bits & (group ? 4096 : 8)))
            continue;
        size_t offset = group ? offsetof(HFileInfo, ioFlXFndrInfo) : offsetof(HFileInfo, ioFlFndrInfo);
        for (unsigned i = 0; i < 16; ++i) {
            const unsigned char* value = (const unsigned char*)info + offset;
            const unsigned char* target = (const unsigned char*)lower + offset;
            const unsigned char* mask = (const unsigned char*)upper + offset;
            if ((value[i] ^ target[i]) & mask[i])
                match = FALSE;
        }
    }
    if (directory && (bits & 16) &&
        (CW(info->dirInfo.ioDrNmFls) < CW(lower->dirInfo.ioDrNmFls) ||
         CW(info->dirInfo.ioDrNmFls) > CW(upper->dirInfo.ioDrNmFls)))
        match = FALSE;
    static const size_t offsets[] = {
        offsetof(HFileInfo, ioFlLgLen),  offsetof(HFileInfo, ioFlPyLen),
        offsetof(HFileInfo, ioFlRLgLen), offsetof(HFileInfo, ioFlRPyLen),
        offsetof(HFileInfo, ioFlCrDat),  offsetof(HFileInfo, ioFlMdDat),
        offsetof(HFileInfo, ioFlBkDat),  0,
        offsetof(HFileInfo, ioFlParID),
    };
    for (unsigned i = 0; i < NELEM(offsets); ++i) {
        if (!offsets[i] || !(bits & (32U << i)) || (directory && i < 4))
            continue;
        uint32 value = catalog_search_long(info, offsets[i]);
        if (value < catalog_search_long(lower, offsets[i]) || value > catalog_search_long(upper, offsets[i]))
            match = FALSE;
    }
    return bits & 16384 ? !match : match;
}

static OSErr catalog_search(catalog_search_pb_t* search)
{
    enum { position_magic = 0x43530000, maximum_depth = 128 };

    search->actual = 0;
    uint32 bits = CL(search->search_bits);
    LONGINT requested = CL(search->requested);
    uint32 milliseconds = CL(search->milliseconds);
    if (requested <= 0 || (bits & ~0x7fffU) || (bits & 3) == 3)
        return paramErr;
    FSSpec* matches = catalog_search_pointer(CL(search->matches), (size_t)requested * sizeof *matches);
    CInfoPBRec* lower = catalog_search_pointer(CL(search->lower), sizeof *lower);
    CInfoPBRec* upper = catalog_search_pointer(CL(search->upper), sizeof *upper);
    if (!matches || !lower || !upper)
        return paramErr;
    const unsigned char* wanted = NULL;
    if (bits & 3) {
        uint32 address = catalog_search_long(lower, offsetof(HFileInfo, ioNamePtr));
        wanted = catalog_search_pointer(address, 1);
        if (!wanted || !catalog_search_pointer(address, wanted[0] + 1))
            return paramErr;
    }
    LONGINT ignored_directory;
    HVCB* volume = ROMlib_findvcb(CW(search->ioVRefNum), PPR(search->ioNamePtr), &ignored_directory, FALSE);
    if (!volume)
        return nsvErr;
    uint32 marker = CL(search->position[0]);
    uint32 directory = CL(search->position[1]);
    uint32 index = CL(search->position[2]);
    uint32 child = CL(search->position[3]);
    unsigned depth = marker & 0xffff;
    if (!marker) {
        directory = 2;
        index = child = depth = 0;
    }
    else if ((marker & 0xffff0000) != position_magic || depth > maximum_depth || index > 32768)
        return paramErr;
    unsigned long started = msecs_elapsed();
    LONGINT count = 0;
    OSErr result = noErr;
    while (directory) {
        CInfoPBRec info = {0};
        Str255 name;
        info.hFileInfo.ioVRefNum = volume->vcbVRefNum;
        info.hFileInfo.ioDirID = CL(directory);
        info.hFileInfo.ioFDirIndex = CW(index && index <= 32767 ? index : -1);
        FIELD_ASSIGN_RAW(info.hFileInfo.ioNamePtr, RM((StringPtr)name));
        int saved_counts = ROMlib_catalog_skip_counts;
        if (!(bits & 16))
            ROMlib_catalog_skip_counts = 1;
        OSErr error = PBGetCatInfo(&info, FALSE);
        ROMlib_catalog_skip_counts = saved_counts;
        if ((error == fnfErr && index) || index > 32767) {
            if (directory == 2) {
                directory = 0;
                break;
            }
            info.hFileInfo.ioFDirIndex = CWC(-1);
            info.hFileInfo.ioDirID = CL(directory);
            error = PBGetCatInfo(&info, FALSE);
            if (!error) {
                child = directory;
                directory = CL(info.dirInfo.ioDrParID);
                index = 1;
                if (!depth || directory == child)
                    return paramErr;
                --depth;
            }
        }
        else if (!error) {
            BOOLEAN is_directory = !!(info.hFileInfo.ioFlAttrib & ATTRIB_ISADIR);
            uint32 found_id = CL(info.dirInfo.ioDrDirID);
            if (child) {
                if (is_directory && found_id == child)
                    child = 0;
                ++index;
            }
            else {
                if (catalog_search_match(&info, name, lower, upper, bits, wanted)) {
                    if (name[0] >= sizeof matches[count].name)
                        return bdNamErr;
                    FSSpec spec = {0};
                    spec.vRefNum = volume->vcbVRefNum;
                    spec.parID = info.hFileInfo.ioFlParID;
                    memcpy(spec.name, name, name[0] + 1);
                    matches[count++] = spec;
                }
                if (index && is_directory) {
                    if (found_id == directory || depth == maximum_depth)
                        return dirNFErr;
                    directory = found_id;
                    index = 1;
                    ++depth;
                }
                else
                    ++index;
            }
        }
        if (error) {
            result = error;
            break;
        }
        if (count == requested || (milliseconds && msecs_elapsed() - started >= (unsigned long)milliseconds))
            break;
    }
    search->actual = CL(count);
    search->position[0] = CL(position_magic | depth);
    search->position[1] = CL(directory);
    search->position[2] = CL(index);
    search->position[3] = CL(child);
    return result ? result : directory ? noErr : eofErr;
}

A2(PUBLIC trap, OSErrRET, PBCatSearch, ParmBlkPtr, pb, BOOLEAN, async)
{
    OSErr result = catalog_search((catalog_search_pb_t*)pb);
    FAKEASYNC(pb, async, result);
}

static HVCB* file_id_volume(FIDParam* pb)
{
    LONGINT directory;
    return ROMlib_findvcb(CW(pb->ioVRefNum), PPR(pb->ioNamePtr), &directory, TRUE);
}

A2(PUBLIC trap, OSErrRET, PBCreateFileIDRef, ParmBlkPtr, pb, BOOLEAN, async)
{
    FIDParam* file_id = (FIDParam*)pb;
    HVCB* volume = file_id_volume(file_id);
    OSErr status = nsvErr;
    if (volume)
        status = volume->vcbCTRef ? ROMlib_hfs_create_file_id(file_id) : wrgVolTypErr;
    FAKEASYNC(pb, async, status);
}

A2(PUBLIC trap, OSErrRET, PBDeleteFileIDRef, ParmBlkPtr, pb, BOOLEAN, async)
{
    FIDParam* file_id = (FIDParam*)pb;
    HVCB* volume = file_id_volume(file_id);
    OSErr status = nsvErr;
    if (volume)
        status = volume->vcbCTRef ? ROMlib_hfs_resolve_file_id(volume, file_id, true) : wrgVolTypErr;
    FAKEASYNC(pb, async, status);
}

A2(PUBLIC trap, OSErrRET, PBResolveFileIDRef, ParmBlkPtr, pb, BOOLEAN, async)
{
    FIDParam* file_id = (FIDParam*)pb;
    HVCB* volume = file_id_volume(file_id);
    OSErr status = nsvErr;
    if (volume)
        status = volume->vcbCTRef ? ROMlib_hfs_resolve_file_id(volume, file_id, false) : wrgVolTypErr;
    FAKEASYNC(pb, async, status);
}
