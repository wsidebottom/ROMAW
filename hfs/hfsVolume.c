/*+===================================================================
  File:      hfsVolume.c

  Summary:   Implements hfs volume support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: ROMlib_dqbydrive, ROMlib_findvcb, ROMlib_flushvcbp, ROMlib_pbvolrename, ROMlib_vcbbybiggestunixname,
             ROMlib_vcbbydrive, ROMlib_vcbbyunixname, ROMlib_vcbbyvrn, hfsPBEject, hfsPBFlushVol, hfsPBGetVInfo,
             hfsPBGetVol, hfsPBHGetVInfo, hfsPBHGetVol, hfsPBHSetVol, hfsPBMountVol, hfsPBOffLine, hfsPBSetVInfo,
             hfsPBSetVol, hfsPBUnmountVol, vcbsync

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 - 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "OSUtil.h"
#include "FileMgr.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/ecf.h"

#if defined(CYGWIN32)
#include "winfs.h"
#endif

/*
 * TODO: support read and write count.  This will make it possible to
 *	 discriminate between identically named disks (i.e. when you
 *	 have two Untitled disks that have been ejected).  Currently
 *	 disks are matched by name and name only.
 */

/*
 * NOTE: in the routines below there is no freeing of memory if an error is
 *       detected.  This should be done sometime.
 */

/*
 * NOTE: NewPtr_aligned_4 is so that our miscellaneous buffers will be aligned
 *	 properly for DMA transfers on the NeXT.  Depending on the
 *	implementation of NewPtr, the following loop could
 *	eat all of memory, but it would require a pretty unlikely
 *	implementation of NewPtr.
 */

#define NewPtr_aligned_4(x, y) NewPtr(x)

#if !defined(NewPtr_aligned_4)
PRIVATE Ptr NewPtr_aligned_4(Size size, INTEGER align)
{
    Ptr p, p2, p3;
    Size shim;

    p = NewPtr(size);
    if (((LONGINT)p & 3) == align)
        return p;
    else {
        shim = 0;
        while (p) {
            DisposPtr(p);
            p2 = NewPtr(++shim);
            p = NewPtr(size);
            p3 = NewPtr(shim);
            DisposPtr(p3);
            if (((LONGINT)p & 3) == align) {
                DisposPtr(p2);
                return p;
            }
            else {
                DisposPtr(p);
                DisposPtr(p2);
                p2 = NewPtr(shim + 1);
                p = NewPtr(size);
                p3 = NewPtr(shim + 1);
                DisposPtr(p3);
                if (((LONGINT)p & 3) == align) {
                    DisposPtr(p2);
                    return p;
                }
                else
                    DisposPtr(p2);
            }
        }
        return p;
    }
}
#endif

PRIVATE OSErr readvolumebitmap(HVCB* vcbp, volumeinfoPtr vp)
{
    OSErr err;
    short nphysrequired;

    if (Cx(vp->drSigWord) != 0x4244) {
        long* words;

        err = noMacDskErr;

        words = (long*)vp;
        warning_fs_log("sigword = 0x%02x (%08lx %08lx %08lx %08lx)", CW(vp->drSigWord), CL(words[0]), CL(words[1]),
                       CL(words[2]), CL(words[3]));
    }
    else {
        nphysrequired = NPHYSREQ(ROUNDUP8(Cx(vp->drNmAlBlks)) / 8);
        FIELD_ASSIGN_RAW(vcbp->vcbMAdr, RM(NewPtr_aligned_4(PHYSBSIZE * nphysrequired + MADROFFSET, 0)));
        vcbp->vcbMLen = CW(nphysrequired + MADROFFSET);
        /*really add MADROFFSET?*/
        if (!PP_RAW(vcbp->vcbMAdr))
            err = MemError();
        else
            err = ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, CW(vp->drVBMSt) * (ULONGINT)PHYSBSIZE, nphysrequired,
                                      PPR(vcbp->vcbMAdr) + MADROFFSET, reading, (LONGINT*)0);
    }
    return err;
}

PRIVATE OSErr initcache(HVCB* vcbp)
{
    THz savezone;
    cachehead* headp;
    cacheentry* cachep;
    INTEGER align;

    savezone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    align = ((char*)&cachep->buf - (char*)&cachep) & 3;
    FIELD_ASSIGN_RAW(vcbp->vcbCtlBuf,
                     RM(NewPtr_aligned_4(sizeof(cachehead) + NCACHEENTRIES * sizeof(cacheentry), align)));
    if (!PP_RAW(vcbp->vcbCtlBuf))
        return MemError();
    HIDDEN_RAW_SET(TheZone_H, savezone);
    headp = (cachehead*)PPR(vcbp->vcbCtlBuf);
    headp->nitems = CW(NCACHEENTRIES);
    headp->flags = 0;
    FIELD_ASSIGN_RAW(headp->flink, RM((cacheentry*)(headp + 1)));
    PP_RAW(headp->blink) = RM(PPR(headp->flink) + NCACHEENTRIES - 1);

    for (cachep = PPR(headp->flink); cachep <= PPR(headp->blink); cachep++) {
        PP_RAW(cachep->flink) = RM(cachep + 1);
        PP_RAW(cachep->blink) = RM(cachep - 1);
        PP_RAW(cachep->vptr) = 0;
        cachep->fileno = 0;
        cachep->flags = CACHEFREE;
    }
    PP_RAW(PPR(headp->flink)->blink) = RM((cacheentry*)headp);
    PP_RAW(PPR(headp->blink)->flink) = RM((cacheentry*)headp);

    return noErr;
}

PUBLIC boolean_t ROMlib_hfs_plus_support = FALSE;

PRIVATE boolean_t is_hfs_plus_wrapper(volumeinfoPtr vp)
{
    boolean_t retval;

    retval = vp->drVCSize == CWC(0x482b);
    return retval;
}

PRIVATE OSErr check_volume_size(volumeinfoPtr vp)
{
    OSErr retval;

    if (is_hfs_plus_wrapper(vp)) {
        warning_unexpected("Found wrapped HFS+ volume");
        retval = noErr;
    }
    else {
        unsigned short nmalblks;
        unsigned long dralblksiz;

        nmalblks = CW(vp->drNmAlBlks);
        dralblksiz = CL(vp->drAlBlkSiz);
        retval = ((long long)nmalblks * dralblksiz >= 2LL * 1024 * 1024 * 1024 ? paramErr : noErr);
        if (retval != noErr)
            warning_unexpected("drNmAlBlks = %d, drAlBlkSiz = %lu", (int)nmalblks, dralblksiz);
    }
    return retval;
}

PRIVATE OSErr readvolumeinfo(HVCB* vcbp) /* call once during mounting */
{
    OSErr err;

    PP_RAW(vcbp->vcbBufAdr) = RM(NewPtr_aligned_4((Size)PHYSBSIZE, 0));
    if (!vcbp)
        err = MemError();
    else {
        err = ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE, 1,
                                  PPR(vcbp->vcbBufAdr), reading, (LONGINT*)0);
        if (err == noErr)
            err = check_volume_size((volumeinfoPtr)PPR(vcbp->vcbBufAdr));
        if (err == noErr) {
            err = readvolumebitmap(vcbp, (volumeinfoPtr)PPR(vcbp->vcbBufAdr));
            if (err == noErr)
                err = initcache(vcbp);
        }
    }
    return err;
}

#define VOLUMEINFOBACKUP(vcbp) ((CW(vcbp->vcbNmAlBlks) * Cx(vcbp->vcbAlBlkSiz)) + (Cx(vcbp->vcbAlBlSt) * PHYSBSIZE))

void vcbsync(HVCB* vcbp)
{
    if (!ROMlib_nosync)
        fsync(((VCBExtra*)vcbp)->u.hfs.fd);
}

PRIVATE OSErr writevolumeinfo(HVCB* vcbp, Ptr p)
{
    OSErr err;

    err = ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE, 1, p, writing,
                              (LONGINT*)0);
    if (err == noErr)
        err = ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, (ULONGINT)VOLUMEINFOBACKUP(vcbp), 1, p, writing,
                                  (LONGINT*)0);
    vcbsync(vcbp);
    return err;
}

OSErr ROMlib_flushvcbp(HVCB* vcbp)
{
    Ptr realp, p;
    OSErr retval;
    volumeinfoPtr vip;
    filecontrolblock* fcbp;

    retval = ROMlib_flushcachevcbp(vcbp);
    if (retval == noErr) {
        if (Cx(vcbp->vcbFlags) & VCBDIRTY) {
            realp = alloca((Size)512 + 4); /* needs to be aligned on unix */

            p = (Ptr)(((uintptr_t)realp + 3) & ~(uintptr_t)3);
            vip = (volumeinfoPtr)p;
            memmove(&vip->drSigWord, &vcbp->vcbSigWord, (LONGINT)64);
            memmove(&vip->drVolBkUp, &vcbp->vcbVolBkUp, (LONGINT)66);
            fcbp = (filecontrolblock*)((char*)MR(FCBSPtr) + Cx(vcbp->vcbXTRef));
            vip->drXTFlSize = fcbp->fcbPLen;
            memmove(&vip->drXTExtRec, &fcbp->fcbExtRec, (LONGINT)sizeof(fcbp->fcbExtRec));
            fcbp = (filecontrolblock*)((char*)MR(FCBSPtr) + Cx(vcbp->vcbCTRef));
            vip->drCTFlSize = fcbp->fcbPLen;
            memmove(&vip->drCTExtRec, &fcbp->fcbExtRec, (LONGINT)sizeof(fcbp->fcbExtRec));
            retval = writevolumeinfo(vcbp, p);
            vcbp->vcbFlags &= CW(~VCBDIRTY);
        }
    }
    if (retval == noErr && !(CW(vcbp->vcbAtrb) & VHARDLOCKBIT))
        retval = ROMlib_image_sync(((VCBExtra*)vcbp)->u.hfs.fd, ((VCBExtra*)vcbp)->u.hfs.offset);
    return retval;
}

PRIVATE HVCB* vcbbyname(StringPtr name)
{
    HVCB* vcbp;

    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead); vcbp && !EqualString(vcbp->vcbVN, name, FALSE, TRUE);
         vcbp = (HVCB*)PPR(vcbp->qLink))
        ;
    return vcbp;
}

#if defined(CYGWIN32)
#define VCB_CMPN_FUNC strncasecmp
#define VCB_CMP_FUNC strcasecmp
#else
#define VCB_CMPN_FUNC strncmp
#define VCB_CMP_FUNC strcmp
#endif

PUBLIC HVCB* ROMlib_vcbbybiggestunixname(const char* name)
{
    HVCB *vcbp, *bestvcbp;
    int namesize, bestsize;

    if (strchr(name, '\\')) {
        int len;
        char *new_name, *op, c;
        const char* ip;

        len = strlen(name) + 1;
        new_name = alloca(len);
        for (op = new_name, ip = name; (c = *ip++);)
            *op++ = c == '\\' ? '/' : c;
        *op++ = 0;
        name = new_name;
    }

    bestvcbp = 0;
    bestsize = -1;
    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead); vcbp; vcbp = (HVCB*)PPR(vcbp->qLink)) {
        if (!vcbp->vcbCTRef) {
            namesize = strlen(((VCBExtra*)vcbp)->unixname);
            if (namesize > bestsize && VCB_CMPN_FUNC(((VCBExtra*)vcbp)->unixname, name, namesize) == 0) {
                bestsize = namesize;
                bestvcbp = vcbp;
            }
        }
    }
    return bestvcbp;
}

PUBLIC VCBExtra* ROMlib_vcbbyunixname(char* name)
{
    HVCB* vcbp;

    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead);
         vcbp && (vcbp->vcbCTRef || VCB_CMP_FUNC(((VCBExtra*)vcbp)->unixname, name) != 0);
         vcbp = (HVCB*)PPR(vcbp->qLink))
        ;
    return (VCBExtra*)vcbp;
}

PUBLIC HVCB* ROMlib_vcbbydrive(short vrefnum)
{
    HVCB* vcbp;

    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead); vcbp && Cx(vcbp->vcbDrvNum) != vrefnum; vcbp = (HVCB*)PPR(vcbp->qLink))
        ;
    return vcbp;
}

PUBLIC DrvQExtra* ROMlib_dqbydrive(short vrefnum)
{
    DrvQEl* dp;
    DrvQExtra* retval;
    short swapped_vrefnum;

    swapped_vrefnum = CW(vrefnum);
    retval = 0;
    for (dp = (DrvQEl*)PPR(DrvQHdr.qHead);
         dp && (retval = (DrvQExtra*)((char*)dp - sizeof(LONGINT)), retval->dq.dQDrive != swapped_vrefnum);
         dp = (DrvQEl*)PPR(dp->qLink))
        ;
    return dp ? retval : 0;
}

PUBLIC HVCB* ROMlib_vcbbyvrn(short vrefnum)
{
    HVCB* vcbp;

    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead); vcbp && Cx(vcbp->vcbVRefNum) != vrefnum; vcbp = (HVCB*)PPR(vcbp->qLink))
        ;
    return vcbp;
}

PUBLIC HVCB* ROMlib_findvcb(short vrefnum, StringPtr name, LONGINT* diridp, BOOLEAN usedefault)
{
    HVCB* vcbp;
    INTEGER namelen;
    Str255 tempname;
    char* colonp;
    wdentry* wdp;

    namelen = name ? name[0] : 0;
    vcbp = 0;
    if (namelen && name[1] != ':' && (colonp = ROMlib_indexn((char*)name + 2, ':', namelen - 1))) {
        tempname[0] = colonp - (char*)name - 1;
        memmove((char*)tempname + 1, (char*)name + 1, (LONGINT)tempname[0]);
        vcbp = vcbbyname(tempname);
        if (vcbp && diridp)
            *diridp = 1;
    }
    else {
        if (vrefnum > 0)
            vcbp = ROMlib_vcbbydrive(vrefnum);
        else if (vrefnum < 0) {
            if (ISWDNUM(vrefnum)) {
                wdp = WDNUMTOWDP(vrefnum);
                vcbp = PPR(wdp->vcbp);
                if (diridp)
                    *diridp = CL(wdp->dirid);
            }
            else
                vcbp = ROMlib_vcbbyvrn(vrefnum);
        }
        else if (usedefault || (!name && !vrefnum)) {
            vcbp = (HVCB*)MR(DefVCBPtr);
            if (diridp)
                *diridp = CL(DefDirID);
        }
    }
    return vcbp;
}

PRIVATE INTEGER drvtodref(INTEGER vref) /* TODO:  flesh this out */
{
    return OURHFSDREF;
}

PRIVATE INTEGER openxtnt(LONGINT filnum, LONGINT clpsize, LONGINT filsize, xtntrec xtr, HVCB* vcbp)
{
    filecontrolblock* fcbp;
    INTEGER retval;

    fcbp = ROMlib_getfreefcbp();
    if (fcbp) {
        fcbp->fcbFlNum = CL(filnum);
        fcbp->fcbMdRByt = 0;
        fcbp->fcbTypByt = 0;
        fcbp->fcbSBlk = 0;
        fcbp->fcbEOF = CL(filsize);
        fcbp->fcbPLen = CL(filsize);
        fcbp->fcbCrPs = 0;
        PP_RAW(fcbp->fcbVPtr) = RM(vcbp);
        PP_RAW(fcbp->fcbBfAdr) = 0;
        fcbp->fcbFlPos = 0;
        fcbp->fcbClmpSize = CL(clpsize);
        fcbp->fcbBTCBPtr = 0;
        memmove(fcbp->fcbExtRec, xtr, (LONGINT)sizeof(xtntrec));
        fcbp->fcbFType = 0;
        fcbp->fcbCatPos = 0;
        fcbp->fcbDirID = 0;
        fcbp->fcbCName[0] = 0;
        retval = (char*)fcbp - (char*)MR(FCBSPtr);
    }
    else
        retval = 0;
    return retval;
}

#define XTNUM 3
#define CTNUM 4

PUBLIC INTEGER ROMlib_nextvrn = 0; /* TODO: low memory global */

PUBLIC OSErr hfsPBMountVol(ParmBlkPtr pb, LONGINT floppyfd, LONGINT offset, LONGINT bsize, LONGINT maxbytes,
                           drive_flags_t flags, DrvQExtra* dqp)
{
    HVCB *vcbp, *vcbp2;
    OSErr err;
    volumeinfoPtr vip;
    BOOLEAN alreadythere;
    ULONGINT nblocks;
    THz saveZone;

    warning_fs_log("floppyfd = 0x%x, offset = %d, bsize = %d, maxbytes = %d "
                   "flags = 0x%x",
                   floppyfd, offset, bsize, maxbytes, flags);
    saveZone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    vcbp = ROMlib_vcbbydrive(CW(pb->volumeParam.ioVRefNum));
    if (vcbp)
        err = volOnLinErr;
    else {
        vcbp = (HVCB*)NewPtr((Size)sizeof(VCBExtra));
        if (!vcbp)
            err = MemError();
        else {
            ((VCBExtra*)vcbp)->custom_icons_disabled = FALSE;
            ((VCBExtra*)vcbp)->u.hfs.fd = floppyfd;
            ((VCBExtra*)vcbp)->u.hfs.offset = offset;
            ((VCBExtra*)vcbp)->u.hfs.bsize = bsize;
            ((VCBExtra*)vcbp)->u.hfs.maxbytes = maxbytes;
            err = readvolumeinfo(vcbp);
            if (err == noErr) {
                vip = (volumeinfoPtr)PPR(vcbp->vcbBufAdr);
                alreadythere = FALSE;
                for (vcbp2 = (HVCB*)PPR(VCBQHdr.qHead); vcbp2; vcbp2 = (HVCB*)PPR(vcbp2->qLink))
                    if (EqualString(vcbp2->vcbVN, vip->drVN, TRUE, TRUE) && vcbp2->vcbDrvNum == 0) {
#if 1
                        PP_RAW(vcbp2->vcbBufAdr) = PP_RAW(vcbp->vcbBufAdr);
                        PP_RAW(vcbp2->vcbMAdr) = PP_RAW(vcbp->vcbMAdr);
                        PP_RAW(vcbp2->vcbCtlBuf) = PP_RAW(vcbp->vcbCtlBuf);
#endif
                        ((VCBExtra*)vcbp2)->unixname = ((VCBExtra*)vcbp)->unixname;
                        ((VCBExtra*)vcbp2)->u.hfs.fd = ((VCBExtra*)vcbp)->u.hfs.fd;
                        DisposPtr((Ptr)vcbp);
                        alreadythere = TRUE;
                        vcbp = vcbp2;
                        break;
                    }
                memmove(&vcbp->vcbSigWord, &vip->drSigWord, (LONGINT)64);

                nblocks = (CL(vcbp->vcbAlBlkSiz) / PHYSBSIZE) * CW(vcbp->vcbNmAlBlks) + CW(vcbp->vcbAlBlSt) + 2;
                dqp->dq.dQDrvSz = CW(nblocks);
                dqp->dq.dQDrvSz2 = CW(nblocks >> 16);
                dqp->dq.qType = 1;

                vcbp->vcbDrvNum = pb->volumeParam.ioVRefNum;
                vcbp->vcbDRefNum = CW(drvtodref(Cx(pb->volumeParam.ioVRefNum)));
                vcbp->vcbFSID = 0;
                if (!alreadythere)
                    vcbp->vcbVRefNum = CW(--ROMlib_nextvrn);
                vcbp->vcbDirIndex = 0;
                vcbp->vcbDirBlk = 0;
                vcbp->vcbFlags = 0;
                memmove(&vcbp->vcbVolBkUp, &vip->drVolBkUp, (LONGINT)66);

                vcbp->vcbXTAlBlks = CW(Cx(vip->drXTFlSize) / Cx(vip->drAlBlkSiz));
                vcbp->vcbCTAlBlks = CW(Cx(vip->drCTFlSize) / Cx(vip->drAlBlkSiz));

                vcbp->vcbXTRef = CW(openxtnt(XTNUM, Cx(vip->drXTClpSiz), Cx(vip->drXTFlSize), vip->drXTExtRec, vcbp));
                vcbp->vcbCTRef = CW(openxtnt(CTNUM, Cx(vip->drCTClpSiz), Cx(vip->drCTFlSize), vip->drCTExtRec, vcbp));

                vcbp->vcbDirIDM = 0;
                vcbp->vcbOffsM = 0;
                vcbp->vcbAtrb = 0;
                if (flags & DRIVE_FLAGS_FIXED)
                    vcbp->vcbAtrb |= CW(VNONEJECTABLEBIT);

                if (!vcbp->vcbCTRef)
                    err = tmfoErr;
                if (err == noErr) {
                    if (!(flags & DRIVE_FLAGS_LOCKED)) {
                        OSErr err2;
                        signed char buffer[PHYSBSIZE + 3];
                        signed char* buf;

                        buf = (signed char*)(((uintptr_t)buffer + 3) & ~(uintptr_t)3);
                        err2 = ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE,
                                                   1, buf, reading, (LONGINT*)0);
                        if (err2 == noErr) {
                            err2 =
                                ROMlib_transphysblk(&((VCBExtra*)vcbp)->u.hfs, (ULONGINT)VOLUMEINFOBLOCKNO * PHYSBSIZE,
                                                    1, buf, writing, (LONGINT*)0);
                            if (err2 == noErr)
                                err2 = ROMlib_flushvcbp(vcbp);
                        }
                        if (err2 != noErr)
                            flags |= DRIVE_FLAGS_LOCKED;
                    }
                    if (flags & DRIVE_FLAGS_LOCKED)
                        vcbp->vcbAtrb |= CW(VHARDLOCKBIT);
                    if (!alreadythere)
                        Enqueue((QElemPtr)vcbp, &VCBQHdr);
                    pb->volumeParam.ioVRefNum = vcbp->vcbVRefNum;
                    if (!DefVCBPtr) {
                        HIDDEN_RAW_SET(DefVCBPtr_H, RM(vcbp));
                        DefVRefNum = vcbp->vcbVRefNum;
                        DefDirID = CLC(2);
                    }
                }
            }
        }
    }
    HIDDEN_RAW_SET(TheZone_H, saveZone);
    warning_fs_log("err = %d", err);
    PBRETURN((volumeParam*)pb, err);
}

PRIVATE void goofyclip(unsigned short* up)
{
    if (CW(*up) > 0x7C00) /* IMIV-130 */
        *up = CWC(0x7C00);
}

/*
 * getworkingdir returns the directory id associated with vrefnum
 */

PRIVATE LONGINT getworkingdir(INTEGER vrefnum)
{
    LONGINT retval;
    wdentry* wdp;

    if (ISWDNUM(vrefnum)) {
        wdp = WDNUMTOWDP(vrefnum);
        retval = CL(wdp->dirid);
    }
    else
        retval = 0;
    return retval;
}

/*
 * getnmfls finds a directory's valence
 */

PRIVATE unsigned short getnmfls(HVCB* vcbp, INTEGER workingdirnum)
{
    LONGINT dirid;
    catkey key;
    threadrec* thp;
    unsigned short retval;
    btparam btparamrec;
    OSErr err;

    dirid = getworkingdir(workingdirnum);
    err = ROMlib_makecatparam(&btparamrec, vcbp, dirid, 0, (Ptr)0);
    if (err == noErr)
        err = ROMlib_keyfind(&btparamrec);
    if (err == noErr && btparamrec.success) {
        thp = (threadrec*)DATAPFROMKEY(btparamrec.foundp);
        key.ckrParID = Cx(thp->thdParID);
        str255assign(key.ckrCName, thp->thdCName);
        key.ckrKeyLen = sizeof(LONGINT) + 2 + key.ckrCName[0];
        err = ROMlib_keyfind(&btparamrec);
        if (err == noErr && btparamrec.success)
            retval = ((directoryrec*)DATAPFROMKEY(btparamrec.foundp))->dirVal;
        else
            retval = 0;
    }
    else
        retval = 0;
    return retval;
}

#define RETURN return

PRIVATE OSErr commonGetVInfo(HVolumeParam* pb, BOOLEAN async, fstype fs)
{
    HVCB* vcbp;
    INTEGER workingdirnum;

    if (Cx(pb->ioVolIndex) > 0) {
        vcbp = (HVCB*)ROMlib_indexqueue(&VCBQHdr, Cx(pb->ioVolIndex));
        workingdirnum = 0;
    }
    else {
        if (pb->ioVolIndex == 0)
            vcbp = (HVCB*)ROMlib_findvcb(Cx(pb->ioVRefNum), (StringPtr)0, (LONGINT*)0, FALSE);
        else /* if (Cx(pb->ioVolIndex) < 0) */
            vcbp = (HVCB*)ROMlib_findvcb(Cx(pb->ioVRefNum), PPR(pb->ioNamePtr), (LONGINT*)0, TRUE);
        workingdirnum = getworkingdir(Cx(pb->ioVRefNum));
    }

    if (!vcbp)
        /*-->*/ PBRETURN(pb, nsvErr);

    if (/*CW (pb->ioVolIndex) >= 0 &&*/ PP_RAW(pb->ioNamePtr))
        str255assign(PPR(pb->ioNamePtr), (StringPtr)vcbp->vcbVN);
    pb->ioVCrDate = vcbp->vcbCrDate;
    pb->ioVAtrb = vcbp->vcbAtrb;

    if (workingdirnum)
        pb->ioVNmFls = CW(getnmfls(vcbp, workingdirnum));
    else
        pb->ioVNmFls = vcbp->vcbNmFls;

    pb->ioVNmAlBlks = vcbp->vcbNmAlBlks;
    pb->ioVAlBlkSiz = vcbp->vcbAlBlkSiz;
    pb->ioVClpSiz = vcbp->vcbClpSiz;
    pb->ioAlBlSt = vcbp->vcbAlBlSt;
    pb->ioVNxtCNID = vcbp->vcbNxtCNID;
    pb->ioVFrBlk = vcbp->vcbFreeBks;
    switch (fs) {
    case mfs:
        ((volumeParam*)pb)->ioVLsBkUp = vcbp->vcbVolBkUp;
        ((volumeParam*)pb)->ioVDirSt = 0;
        ((volumeParam*)pb)->ioVBlLn = 0;
        if (!workingdirnum)
            pb->ioVRefNum = vcbp->vcbVRefNum;
        goofyclip((unsigned short*)&pb->ioVNmAlBlks);
        goofyclip((unsigned short*)&pb->ioVFrBlk);
        break;
    case hfs:
        pb->ioVLsMod = vcbp->vcbLsMod;
        pb->ioVBitMap = vcbp->vcbVBMSt;
#if !defined(THINKCMESSED)
        pb->ioVAllocPtr = vcbp->vcbAllocPtr;
#else  /* THINKCMESSED */
        pb->ioAllocPtr = vcbp->vcbAllocPtr;
#endif /* THINKCMESSED */
        pb->ioVRefNum = vcbp->vcbVRefNum;
        pb->ioVSigWord = vcbp->vcbSigWord;
        pb->ioVDrvInfo = vcbp->vcbDrvNum;
        pb->ioVDRefNum = vcbp->vcbDRefNum;
        pb->ioVFSID = vcbp->vcbFSID;
        pb->ioVBkUp = vcbp->vcbVolBkUp;
        pb->ioVSeqNum = vcbp->vcbVSeqNum;
        pb->ioVWrCnt = vcbp->vcbWrCnt;
        pb->ioVFilCnt = vcbp->vcbFilCnt;
        pb->ioVDirCnt = vcbp->vcbDirCnt;
        memmove(pb->ioVFndrInfo, vcbp->vcbFndrInfo, (LONGINT)sizeof(pb->ioVFndrInfo));
        break;
    }
    PBRETURN(pb, noErr);
}

#undef RETURN

PUBLIC OSErr hfsPBGetVInfo(ParmBlkPtr pb, BOOLEAN async)
{
    return commonGetVInfo((HVolumeParam*)pb, async, mfs);
}

PUBLIC OSErr hfsPBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    return commonGetVInfo((HVolumeParam*)pb, async, hfs);
}

#define ATRBMASK VSOFTLOCKBIT

PUBLIC OSErr hfsPBSetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    OSErr err;
    HVCB* vcbp;

    vcbp = ROMlib_findvcb(Cx(pb->volumeParam.ioVRefNum), PPR(pb->volumeParam.ioNamePtr), (LONGINT*)0, FALSE);
    if (vcbp) {
        if (Cx(vcbp->vcbAtrb) & VHARDLOCKBIT)
            err = wPrErr;
        else {
            if (PP_RAW(pb->volumeParam.ioNamePtr))
                str255assign((StringPtr)vcbp->vcbVN, PPR(pb->volumeParam.ioNamePtr));
            vcbp->vcbCrDate = pb->volumeParam.ioVCrDate;
            vcbp->vcbLsMod = pb->volumeParam.ioVLsMod;
            vcbp->vcbAtrb = CW((Cx(vcbp->vcbAtrb) & ~ATRBMASK) | (Cx(pb->volumeParam.ioVAtrb) & ATRBMASK));
            vcbp->vcbClpSiz = pb->volumeParam.ioVClpSiz;
            vcbp->vcbVolBkUp = pb->volumeParam.ioVBkUp;
            vcbp->vcbVSeqNum = pb->volumeParam.ioVSeqNum;
            memmove(vcbp->vcbFndrInfo, pb->volumeParam.ioVFndrInfo, (LONGINT)32);
            vcbp->vcbFlags |= CW(VCBDIRTY);
            err = noErr;
        }
    }
    else
        err = nsvErr;
    PBRETURN((volumeParam*)pb, err);
}

PRIVATE OSErr getvolcommon(volumeParam* pb)
{
    OSErr err;

    if (!DefVCBPtr)
        err = nsvErr;
    else {
        err = noErr;
        if (PP_RAW(pb->ioNamePtr))
            str255assign(PPR(pb->ioNamePtr), (StringPtr)MR(DefVCBPtr)->vcbVN);
        pb->ioVRefNum = DefVRefNum;
    }
    return err;
}

PUBLIC OSErr hfsPBGetVol(ParmBlkPtr pb, BOOLEAN async)
{
    OSErr err;

    err = getvolcommon((volumeParam*)pb);
    PBRETURN((volumeParam*)pb, err);
}

PUBLIC LONGINT DefDirID = CLC(2);

PUBLIC OSErr hfsPBHGetVol(WDPBPtr pb, BOOLEAN async)
{
    wdentry* wdp;
    OSErr err;

    err = getvolcommon((volumeParam*)pb);
    pb->ioWDDirID = DefDirID;
    if (err == noErr) {
        if (ISWDNUM(Cx(DefVRefNum))) {
            wdp = WDNUMTOWDP(Cx(DefVRefNum));
            pb->ioWDProcID = wdp->procid;
            pb->ioWDVRefNum = PPR(wdp->vcbp)->vcbVRefNum;
        }
        else {
            pb->ioWDProcID = 0;
            pb->ioWDVRefNum = DefVRefNum;
        }
    }
    PBRETURN(pb, err);
}

/*
 * NOTE: Considerable change related to PBSetVol, PBHSetVol were made
 *	 just now (Sat Aug  1 16:13:35 MDT 1992).  These routines
 *	 have been giving us trouble for some time.  Tech Note 140
 *	 implies that there is a "DefDirID" buried somewhere in low
 *	 global space.  Sometime we should try to ferret it out.
 *
 */

PRIVATE OSErr setvolhelper(volumeParam* pb, BOOLEAN aysnc, LONGINT dirid, BOOLEAN convertzeros)
{
    HVCB *vcbp, *newDefVCBPtr;
    OSErr err, err1;
    LONGINT newdir, newDefDirID;
    INTEGER newDefVRefNum;
    CInfoPBRec cpb;

    /*
	 * CinemationCD hack ... they store a directory as a 2-byte quantity and
	 *	sign extend it.  This will only help us recover the sign bit.
	 */
    if (dirid < 0)
        dirid = 64 * 1024 + dirid;

    newdir = 0;
    vcbp = ROMlib_findvcb(Cx(pb->ioVRefNum), PPR(pb->ioNamePtr), &newdir, FALSE);
    if (!vcbp)
        err = nsvErr;
    else {
        err = noErr;
        newDefVCBPtr = RM(vcbp);
        newDefDirID = CLC(0);
        if (newdir > 2) { /* picked up working directory */
            newDefDirID = dirid ? CL(dirid) : CL(newdir);
            newDefVRefNum = pb->ioVRefNum;
        }
        else if (newdir == 1) { /* picked up by name */
            newDefDirID = CL(newdir);
            newDefVRefNum = vcbp->vcbVRefNum;
        }
        else {
            newDefVRefNum = pb->ioVRefNum;
            if (dirid == 0 && convertzeros)
                newDefDirID = CLC(2);
            else
                newDefDirID = CL(dirid);
        }

        if (!convertzeros && PP_RAW(pb->ioNamePtr)) { /* this could change things */
            if (PPR(pb->ioNamePtr)[0] == 0)
                PP_RAW(cpb.hFileInfo.ioNamePtr) = 0; /* otherwise we fill in */
            else
                PP_RAW(cpb.hFileInfo.ioNamePtr) = PP_RAW(pb->ioNamePtr);
            cpb.hFileInfo.ioVRefNum = pb->ioVRefNum;
            cpb.hFileInfo.ioFDirIndex = CWC(0);
            cpb.hFileInfo.ioDirID = CL(dirid);
            /*
			 * NOTE: the else case was added after seeing Excel 4 Installer do a setvol
			 *	 to a file, presumably with the intent to set it to the parent id.
			 *
			 *	 Also we make this try twice as a PM PMSP
			 */
            do {
                if ((err1 = PBGetCatInfo(&cpb, FALSE)) == noErr) {
                    if (cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
                        newDefDirID = cpb.dirInfo.ioDrDirID;
                    else
                        newDefDirID = cpb.hFileInfo.ioFlParID;
                }
            } while (err1 && cpb.hFileInfo.ioDirID == 0 && (cpb.hFileInfo.ioDirID = CLC(2)));
        }
        if (newDefDirID)
            DefDirID = newDefDirID;
        else if (DefVCBPtr != newDefVCBPtr || DefVRefNum != newDefVRefNum)
            DefDirID = CLC(2);
        HIDDEN_RAW_SET(DefVCBPtr_H, newDefVCBPtr);
        DefVRefNum = newDefVRefNum;
    }
    PBRETURN(pb, err);
}

PUBLIC OSErr hfsPBSetVol(ParmBlkPtr pb, BOOLEAN async)
{
    return setvolhelper((volumeParam*)pb, async, 0, TRUE);
}

PUBLIC OSErr hfsPBHSetVol(WDPBPtr pb, BOOLEAN async)
{
    return setvolhelper((volumeParam*)pb, async, Cx(pb->ioWDDirID), FALSE);
}

PUBLIC OSErr hfsPBFlushVol(ParmBlkPtr pb, BOOLEAN async)
{
    VCB* vcbp;
    OSErr err;

    vcbp = ROMlib_findvcb(Cx(pb->volumeParam.ioVRefNum), PPR(pb->volumeParam.ioNamePtr), (LONGINT*)0, TRUE);
    if (vcbp)
        err = ROMlib_flushvcbp(vcbp);
    else
        err = nsvErr;
    PBRETURN((volumeParam*)pb, err);
}

PRIVATE void closeallvcbfiles(HVCB* vcbp)
{
    filecontrolblock *fcbp, *efcbp;
    ioParam iopb;
    short length;

    length = CW(*(short*)MR(FCBSPtr));
    fcbp = (filecontrolblock*)((short*)MR(FCBSPtr) + 1);
    efcbp = (filecontrolblock*)((char*)MR(FCBSPtr) + length);
    for (; fcbp < efcbp; fcbp = (filecontrolblock*)((char*)fcbp + Cx(FSFCBLen)))
        if (fcbp->fcbFlNum && PPR(fcbp->fcbVPtr) == vcbp) {
            iopb.ioRefNum = CW((char*)fcbp - (char*)MR(FCBSPtr));
            /* my */ PBFlushFile((ParmBlkPtr)&iopb, FALSE);
        }
}

PUBLIC OSErr hfsPBUnmountVol(ParmBlkPtr pb)
{
    OSErr err;
    HVCB* vcbp;

    vcbp = ROMlib_findvcb(Cx(pb->volumeParam.ioVRefNum), PPR(pb->volumeParam.ioNamePtr), (LONGINT*)0, FALSE);
    if (vcbp) {
        filecontrolblock* system = ROMlib_refnumtofcbp(CW(SysMap));
        if (system && PPR(system->fcbVPtr) == vcbp)
            PBRETURN((volumeParam*)pb, fBsyErr);
        /* Reject open user forks. Internal catalog/extents forks are closed
           below before freeing the VCB they reference. */
        short length = CW(*(short*)MR(FCBSPtr));
        for (filecontrolblock* fcb = (filecontrolblock*)((short*)MR(FCBSPtr) + 1);
             (char*)fcb < (char*)MR(FCBSPtr) + length; fcb = (filecontrolblock*)((char*)fcb + Cx(FSFCBLen))) {
            INTEGER ref = CW((char*)fcb - (char*)MR(FCBSPtr));
            if (fcb->fcbFlNum && PPR(fcb->fcbVPtr) == vcbp && ref != vcbp->vcbXTRef && ref != vcbp->vcbCTRef)
                PBRETURN((volumeParam*)pb, fBsyErr);
        }
        closeallvcbfiles(vcbp);
        err = ROMlib_flushvcbp(vcbp);
        if (err)
            PBRETURN((volumeParam*)pb, err);
        if (ROMlib_image_volume_kind(vcbp)) {
            ioParam fork = {0};
            fork.ioRefNum = vcbp->vcbXTRef;
            err = PBClose((ParmBlkPtr)&fork, FALSE);
            if (err)
                PBRETURN((volumeParam*)pb, err);
            fork.ioRefNum = vcbp->vcbCTRef;
            err = PBClose((ParmBlkPtr)&fork, FALSE);
            if (err)
                PBRETURN((volumeParam*)pb, err);
            int fd = ((VCBExtra*)vcbp)->u.hfs.fd;
            DrvQExtra* drive = ROMlib_dqbydrive(CW(vcbp->vcbDrvNum));
            if (drive) {
                drive->hfs.fd = -1;
                /* Only remove the source after its last partition is put away. */
                boolean_t another = FALSE;
                for (HVCB* other = (HVCB*)PPR(VCBQHdr.qHead); other; other = (HVCB*)PPR(other->qLink)) {
                    DrvQExtra* other_drive = ROMlib_dqbydrive(CW(other->vcbDrvNum));
                    if (other != vcbp && other_drive && other_drive->devicename && drive->devicename &&
                        !strcasecmp((char*)other_drive->devicename, (char*)drive->devicename))
                        another = TRUE;
                }
                if (!another)
                    ROMlib_ecf_forget_mount((char*)drive->devicename);
            }
            ROMlib_image_forget_fd(fd);
            close(fd);
        }
        Dequeue((QElemPtr)vcbp, &VCBQHdr);
        if (DefVCBPtr && MR(DefVCBPtr) == (VCB*)vcbp) {
            HVCB* fallback = (HVCB*)PPR(VCBQHdr.qHead);
            HIDDEN_RAW_SET(DefVCBPtr_H, fallback ? RM(fallback) : 0);
            DefVRefNum = fallback ? fallback->vcbVRefNum : 0;
            DefDirID = CLC(2);
        }
        DisposPtr(PPR(vcbp->vcbMAdr));
        DisposPtr(PPR(vcbp->vcbBufAdr));
        DisposPtr(PPR(vcbp->vcbCtlBuf));
        DisposPtr((Ptr)vcbp);
    }
    else
        err = nsvErr;
    PBRETURN((volumeParam*)pb, err);
}

PRIVATE OSErr offlinehelper(volumeParam* pb, HVCB* vcbp)
{
    OSErr err, err1, err2;
    ioParam iop;

    err = /* my */ PBFlushVol((ParmBlkPtr)pb, FALSE);
    err1 = 0;
    err2 = 0;
    if (err == noErr) {
        if (vcbp) {
            iop.ioRefNum = vcbp->vcbXTRef;
            err1 = PBClose((ParmBlkPtr)&iop, FALSE);
            iop.ioRefNum = vcbp->vcbCTRef;
            err2 = PBClose((ParmBlkPtr)&iop, FALSE);
            DisposPtr(PPR(vcbp->vcbMAdr));
            PP_RAW(vcbp->vcbMAdr) = 0;
            DisposPtr(PPR(vcbp->vcbBufAdr));
            PP_RAW(vcbp->vcbBufAdr) = 0;
            DisposPtr(PPR(vcbp->vcbCtlBuf));
            PP_RAW(vcbp->vcbCtlBuf) = 0;
            vcbp->vcbDrvNum = 0;
            /* TODO:  look for offline flags in mpw equate files and set them */
        }
        else
            err = nsvErr;
    }
    if (err == noErr)
        err = err1;
    if (err == noErr)
        err = err2;
    return err;
}

PUBLIC OSErr hfsPBOffLine(ParmBlkPtr pb)
{
    OSErr err;
    HVCB* vcbp;

    vcbp = ROMlib_findvcb(Cx(pb->volumeParam.ioVRefNum), PPR(pb->volumeParam.ioNamePtr), (LONGINT*)0, FALSE);
    if (vcbp) {
        if (vcbp->vcbDrvNum) {
            vcbp->vcbDRefNum = CW(-Cx(vcbp->vcbDrvNum));
            err = offlinehelper((volumeParam*)pb, vcbp);
        }
        else
            err = noErr;
    }
    else
        err = nsvErr;
    PBRETURN((volumeParam*)pb, err);
}

PUBLIC OSErr hfsPBEject(ParmBlkPtr pb)
{
    OSErr err;
    HVCB* vcbp;
    INTEGER vref;

    vref = Cx(pb->volumeParam.ioVRefNum);
    vcbp = ROMlib_findvcb(vref, PPR(pb->volumeParam.ioNamePtr), (LONGINT*)0, FALSE);
    if (vcbp && ROMlib_image_volume_kind(vcbp))
        return hfsPBUnmountVol(pb);
    if (vcbp) {
        if (Cx(vcbp->vcbDrvNum)) {
            vcbp->vcbDRefNum = vcbp->vcbDrvNum;
            err = offlinehelper((volumeParam*)pb, vcbp);
        }
        else {
            if (Cx(vcbp->vcbDRefNum) < 0) /* offline */
                vcbp->vcbDRefNum = CW(Cx(vcbp->vcbDRefNum) * -1);
            err = noErr;
        }
    }
    else {
        if (vref == 1 || vref == 2)
            err = noErr; /* They're explicitly ejecting a particular drive */
        else
            err = nsvErr;
    }
#if !defined(MAC)
    if (err == noErr)
        err = ROMlib_ejectfloppy(vcbp ? ((VCBExtra*)vcbp)->u.hfs.fd : -1);
#endif
    PBRETURN((volumeParam*)pb, err);
}

PUBLIC OSErr ROMlib_pbvolrename(ioParam* pb, StringPtr newnamep)
{
    OSErr err;
    HParamBlockRec hpb;
    Str255 name_copy;

    str255assign(name_copy, PPR(pb->ioNamePtr));
    PP_RAW(hpb.volumeParam.ioNamePtr) = (StringPtr)CL((long)name_copy);
    hpb.volumeParam.ioVRefNum = pb->ioVRefNum;
    hpb.volumeParam.ioVolIndex = CWC(-1);
    err = /* my */ PBHGetVInfo((HParmBlkPtr)&hpb, FALSE);
    if (err == noErr) {
        PP_RAW(hpb.volumeParam.ioNamePtr) = RM(newnamep);
        err = /* my */ PBSetVInfo((HParmBlkPtr)&hpb, FALSE);
    }
    return err;
}
