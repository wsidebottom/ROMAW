/*+===================================================================
  File:      hfsWorkingdir.c

  Summary:   Implements hfs workingdir support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: GetWDInfo, ROMlib_adjustdirid, ROMlib_dirbusy, ROMlib_mkwd, hfsPBCloseWD, hfsPBGetWDInfo, hfsPBOpenWD

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "OSUtil.h"
#include "FileMgr.h"
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "MemoryMgr.h"

/*
 * TODO: use this working directory stuff in ROMlib
 */

PUBLIC OSErr ROMlib_dirbusy(LONGINT dirid, HVCB* vcbp)
{
#if defined(MAC)
    wdentry *wdp, *ewdp;

    for (wdp = (wdentry*)(CL(WDCBsPtr) + sizeof(INTEGER)),
        ewdp = (wdentry*)(CL(WDCBsPtr) + CW(*(INTEGER*)CL(WDCBsPtr)));
         wdp != ewdp; wdp++)
        ;
    return wdp == ewdp ? noErr : fBsyErr;
#else
    return noErr;
#endif
}

PUBLIC OSErr ROMlib_mkwd(WDPBPtr pb, HVCB* vcbp, LONGINT dirid, LONGINT procid)
{
    wdentry *wdp, *ewdp, *firstfreep;
    OSErr retval;
    INTEGER n_wd_bytes, new_n_wd_bytes;
    Ptr newptr;
    THz saveZone;

    firstfreep = 0;
    for (wdp = (wdentry*)(MR(WDCBsPtr) + sizeof(INTEGER)),
        ewdp = (wdentry*)(MR(WDCBsPtr) + CW(*(INTEGER*)MR(WDCBsPtr)));
         wdp != ewdp; wdp++) {
        if (!firstfreep && !PP_RAW(wdp->vcbp))
            firstfreep = wdp;
        if (PPR(wdp->vcbp) == vcbp && CL(wdp->dirid) == dirid && CL(wdp->procid) == procid) {
            pb->ioVRefNum = CW(WDPTOWDNUM(wdp));
            /*-->*/ return noErr;
        }
    }
    if (!firstfreep) {
        n_wd_bytes = CW(*(INTEGER*)MR(WDCBsPtr));
        new_n_wd_bytes = (n_wd_bytes - sizeof(INTEGER)) * 2 + sizeof(INTEGER);
        saveZone = TheZone;
        HIDDEN_RAW_SET(TheZone_H, SysZone);
        newptr = NewPtr(new_n_wd_bytes);
        HIDDEN_RAW_SET(SysZone_H, TheZone);
        if (!newptr)
            retval = tmwdoErr;
        else {
            BlockMove(MR(WDCBsPtr), newptr, n_wd_bytes);
            DisposPtr(MR(WDCBsPtr));
            HIDDEN_RAW_SET(WDCBsPtr_H, RM(newptr));
            *(INTEGER*)newptr = CW(new_n_wd_bytes);
            firstfreep = (wdentry*)(newptr + n_wd_bytes);
            retval = noErr;
        }
    }
    else
        retval = noErr;
    if (retval == noErr) {
        FIELD_ASSIGN_RAW(firstfreep->vcbp, RM(vcbp));
        firstfreep->dirid = CL(dirid);
        firstfreep->procid = CL(procid);
        pb->ioVRefNum = CW(WDPTOWDNUM(firstfreep));
        retval = noErr;
    }
    return retval;
}

PUBLIC OSErr hfsPBOpenWD(WDPBPtr pb, BOOLEAN async)
{
    LONGINT dirid;
    OSErr retval;
    filekind kind;
    btparam btparamrec;
    HVCB* vcbp;
    StringPtr namep;

    kind = regular | directory;
    retval = ROMlib_findvcbandfile((ioParam*)pb, Cx(pb->ioWDDirID), &btparamrec, &kind, FALSE);
    if (retval != noErr)
        PBRETURN(pb, retval);
    vcbp = btparamrec.vcbp;
    retval = ROMlib_cleancache(vcbp);
    if (retval != noErr)
        PBRETURN(pb, retval);
    namep = PPR(pb->ioNamePtr);
    if (kind == directory && namep && namep[0])
        dirid = CL(((directoryrec*)DATAPFROMKEY(btparamrec.foundp))->dirDirID);
    else
        dirid = CL(pb->ioWDDirID);
    retval = ROMlib_mkwd(pb, vcbp, dirid, CL(pb->ioWDProcID));

    PBRETURN(pb, retval);
}

PUBLIC OSErr hfsPBCloseWD(WDPBPtr pb, BOOLEAN async)
{
    wdentry* wdp;
    OSErr retval;

    retval = noErr;
    if (ISWDNUM(Cx(pb->ioVRefNum))) {
        wdp = WDNUMTOWDP(Cx(pb->ioVRefNum));
        if (wdp)
            FIELD_ASSIGN_RAW(wdp->vcbp, 0);
        else
            retval = nsvErr;
    }
    PBRETURN(pb, retval);
}

PUBLIC OSErr hfsPBGetWDInfo(WDPBPtr pb, BOOLEAN async)
{
    OSErr retval;
    wdentry *wdp, *ewdp;
    BOOLEAN foundelsewhere;
    HVCB* vcbp;

    foundelsewhere = FALSE;
    retval = noErr;
    wdp = 0;
    if (Cx(pb->ioWDIndex) > 0) {
        INTEGER index = Cx(pb->ioWDIndex);
        INTEGER volume_selector = Cx(pb->ioVRefNum);

        /* Count only open entries that match the requested owner and volume. */
        wdp = (wdentry*)(MR(WDCBsPtr) + sizeof(INTEGER));
        ewdp = (wdentry*)(MR(WDCBsPtr) + CW(*(INTEGER*)MR(WDCBsPtr)));
        for (; wdp != ewdp; ++wdp) {
            if (!PP_RAW(wdp->vcbp) || (pb->ioWDProcID && wdp->procid != pb->ioWDProcID))
                continue;
            vcbp = PPR(wdp->vcbp);
            if ((volume_selector < 0 && vcbp->vcbVRefNum != pb->ioVRefNum) ||
                (volume_selector > 0 && vcbp->vcbDrvNum != pb->ioVRefNum))
                continue;
            if (--index == 0)
                break;
        }
        if (wdp == ewdp)
            wdp = 0;
    }
    else if (ISWDNUM(Cx(pb->ioVRefNum)))
        wdp = WDNUMTOWDP(Cx(pb->ioVRefNum));
    else {
        vcbp = ROMlib_findvcb(Cx(pb->ioVRefNum), (StringPtr)0, (LONGINT*)0, TRUE);
        if (vcbp) {
            if (PP_RAW(pb->ioNamePtr))
                str255assign(PPR(pb->ioNamePtr), (StringPtr)vcbp->vcbVN);
            pb->ioWDProcID = 0;
            pb->ioVRefNum = pb->ioWDVRefNum = vcbp->vcbVRefNum;
            pb->ioWDDirID = vcbp == MR(DefVCBPtr) ? DefDirID : CLC(2);
            foundelsewhere = TRUE;
        }
    }

    if (!foundelsewhere) {
        if (wdp) {
            if (PP_RAW(pb->ioNamePtr))
                str255assign(PPR(pb->ioNamePtr), (StringPtr)PPR(wdp->vcbp)->vcbVN);
            if (Cx(pb->ioWDIndex) > 0)
                pb->ioVRefNum = PPR(wdp->vcbp)->vcbVRefNum;
            pb->ioWDProcID = wdp->procid;
            pb->ioWDVRefNum = PPR(wdp->vcbp)->vcbVRefNum;
            pb->ioWDDirID = wdp->dirid;
        }
        else
            retval = nsvErr;
    }

    PBRETURN(pb, retval);
}

PUBLIC OSErr GetWDInfo(INTEGER wd, INTEGER* vrefp, LONGINT* dirp, LONGINT* procp)
{
    OSErr retval;

    WDPBRec wdp;
    memset(&wdp, 0, sizeof wdp);
    wdp.ioVRefNum = CW(wd);
    retval = PBGetWDInfo(&wdp, FALSE);
    if (retval == noErr) {
        *vrefp = wdp.ioVRefNum;
        *dirp = wdp.ioWDDirID;
        *procp = wdp.ioWDProcID;
    }
    return retval;
}

PUBLIC void ROMlib_adjustdirid(LONGINT* diridp, HVCB* vcbp, INTEGER vrefnum)
{
    wdentry* wdp;

    if (*(ULONGINT*)diridp <= 1 && ISWDNUM(vrefnum)) {
        wdp = WDNUMTOWDP(vrefnum);
        if (PPR(wdp->vcbp) == vcbp)
            *diridp = CL(wdp->dirid);
    }
    else if (*diridp == 0 && !vrefnum /* vcbp == CL(DefVCBPtr) */)
        *diridp = CL(DefDirID);
    if (*diridp == 0)
        *diridp = 2;
}
