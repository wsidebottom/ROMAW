/*+===================================================================
  File:      hfsHelper.c

  Summary:   Implements hfs helper support for HFS volumes and catalog operations.

  Classes:   None.

  Functions: ROMlib_OurClose, ROMlib_hfsinit, ROMlib_indexn, ROMlib_indexqueue, ROMlib_mount_image,
             ROMlib_mount_image_mode, ROMlib_mount_image_dialog, ROMlib_openfloppy, ROMlib_openharddisk,
             ROMlib_priv_open, ROMlib_transphysblk, ROMlib_writefcbp, ROMlib_writevcbp, str255assign, try_to_mount_disk

  Origin:    Existing repository source in executor/src/hfs; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1992 - 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include "rsys/common.h"
#include "OSUtil.h"
#include "FileMgr.h"
#include "SegmentLdr.h"
#include "ToolboxEvent.h"
#include "MemoryMgr.h"
#include "ThinkC.h"
#include "SysErr.h"
#include "DialogMgr.h"
#include "ResourceMgr.h"

#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/partition.h"
#include "rsys/diskimage.h"
#include "romaw_images.h"
#include "rsys/hostdialog.h"
#include "rsys/ecf.h"
#ifdef _WIN32
#include <share.h>
#else
#include <sys/file.h>
#endif
#if defined(SDL)
#include "rsys/prefpanel.h"
#endif

#if defined(CYGWIN32)
#include "dosdisk.h"
#include "aspi.h"
#endif

#if !defined(MAC)

#define FLOPPYWORKSFS "/usr/filesystems/macintosh.fs"
#define NEWFLOPPYWORKSFS "/usr/filesystems/macintosh.fs.SAVE"

#define MAC30FS "/usr/filesystems/mac.fs"
#define NEWMAC30FS "/usr/filesystems/mac.fs.SAVE"

#define OURSOCK "/dev/HFS_XFer"

#define HFSXFERDOTFS "/usr/filesystems/HFS_XFer.fs"
#define HFSXFERUTIL HFSXFERDOTFS "/HFS_XFer.util"

PUBLIC LONGINT ROMlib_sock;
PUBLIC int ROMlib_pending_image_dialog;

PUBLIC void ROMlib_hfsinit(void)
{
}

/*
 * NOTE: The weird name constructed below is inherited from HFS_XFer.util.c,
 *	 which is an ARDI written NEXTSTEP atrocity.
 */

#if !defined(LINUX)
#define EJECTABLE(buf) FALSE
#else
/* #warning this is not the proper way to tell if something is ejectable */
#define EJECTABLE(buf) (buf[strlen(buf) - 3] == 'f' && buf[strlen(buf) - 2] == 'd')
#endif

#define ASSIGN_NAME_MODE_STRING(buf, lenp, filename, sbuf)                                   \
    do {                                                                                     \
        char ejectable;                                                                      \
                                                                                             \
        ejectable = EJECTABLE(filename);                                                     \
        *(lenp) = strlen((filename)) + 1 + 6 + 1 + 1 + 1;                                    \
        (buf) = alloca(*(lenp));                                                             \
        sprintf((buf), "%s%c%06o%c%c", (filename), 0, (sbuf).st_mode & 07777, 0, ejectable); \
    } while (0)

#define NRETRIES 5

PUBLIC long ROMlib_priv_open(const char* filename, long mode)
{
    long retval;

    retval = Uopen(filename, mode, 0);
    if (retval < 0)
        retval = ROMlib_maperrno();

    return retval;
}

PRIVATE void eject_floppy_notify(void)
{

    /*
	 * We make the test for 'ALRT' because when ROMAW shuts down, an ejectable
	 * volume may be unmounted after the volume with the System file has already
	 * been unmounted.  In the past, this caused a crash.  It may make sense to
	 * deliberately unmount last the volume containing System, but that's a fix
	 * for another day.
	 */

    if (WWExist == EXIST_YES && GetResource(TICK("ALRT"), EJECTALERTID))
        Alert(EJECTALERTID, (ProcPtr)0);
}

PUBLIC OSErr ROMlib_ejectfloppy(LONGINT floppyfd)
{
    OSErr err;

    err = noErr;
#if defined(CYGWIN32)
    if (floppyfd == -1 || (floppyfd & (DOSFDBIT | ASPIFDBIT))) {
        if (floppyfd != -1) {
            if (floppyfd & DOSFDBIT) {
                floppyfd &= ~DOSFDBIT;
                dosdisk_close(floppyfd, TRUE);
            }
        }
        eject_floppy_notify();
    }
    else {
#endif
        if (floppyfd != -1) {
            ROMlib_image_forget_fd(floppyfd);
            close(floppyfd);
        }
#if defined(LINUX)
        eject_floppy_notify();
#endif
#if defined(CYGWIN32)
    }
#endif
    return err;
}

PUBLIC void ROMlib_OurClose(void)
{
    HVCB *vcbp, *next;
    ParamBlockRec pbr;

    for (vcbp = (HVCB*)PPR(VCBQHdr.qHead); vcbp; vcbp = next) {
        next = (HVCB*)PPR(vcbp->qLink);
        FIELD_ASSIGN_RAW(pbr.ioParam.ioNamePtr, 0);
        if (Cx(vcbp->vcbCTRef)) {
            pbr.ioParam.ioVRefNum = vcbp->vcbVRefNum;
            PBUnmountVol(&pbr);
        }
        else
            ROMlib_dbm_close((VCBExtra*)vcbp);
    }
}

/*
 * NOTE: messp below points to the longint that will be filled in with
 *	 the status of the first disk insert event that we create, but
 *	 because we can have multiple drives mounted on one physical
 *	 drive, we wind put using PostEvent for all but the first.
 *
 *	 The above method sounds pretty hacky to me, and should probably
 *	 be replaced once we find a cheaper way to be notified that
 *	 a disk can be read (probably use a signal on the pipe).
 */

PRIVATE LONGINT try_to_open_disk(const char* dname, LONGINT* bsizep, LONGINT* maxbytesp, drive_flags_t* flagsp,
                                 uint32* offsetp)
{
    LONGINT floppyfd;
    int len;

    *flagsp = 0;
    len = strlen(dname) + 1;        /* first component: name */
    len += strlen(dname + len) + 1; /* second component: permission */
    if (!dname[len])
        *flagsp |= DRIVE_FLAGS_FIXED; /* third component: ejectable */

#if !defined(CYGWIN32)
#define EXTRA_BITS O_EXCL
#else
#define EXTRA_BITS 0
#endif

    if ((floppyfd = Uopen(dname, O_BINARY | O_RDWR | EXTRA_BITS, 0000)) < 0 &&
        (*flagsp |= DRIVE_FLAGS_LOCKED, (floppyfd = Uopen(dname, O_BINARY | O_RDONLY | EXTRA_BITS, 0000)) < 0))
        /* fprintf(stderr, "can't open %s\n", dname) */;
    else {
        *bsizep = PHYSBSIZE;
        *maxbytesp = 1024L * 1024;
    }

    if (floppyfd >= 0) {
        struct stat sbuf;

        if (fstat(floppyfd, &sbuf) >= 0 && (S_IFREG & sbuf.st_mode))
            *offsetp = sbuf.st_size % PHYSBSIZE;
        else
            *offsetp = 0;
    }

    return floppyfd;
}

PRIVATE LONGINT read_driver_block_size(LONGINT fd, LONGINT bsize, LONGINT maxbytes, char aligned_buf[])
{
    LONGINT retval;

    retval = PHYSBSIZE;
    if (ROMlib_readwrite(fd, aligned_buf, PHYSBSIZE, 0, reading, bsize, maxbytes) == noErr) {
        if (aligned_buf[0] == 0x45 && aligned_buf[1] == 0x52) {
            retval = (unsigned short)CW(*(unsigned short*)&aligned_buf[2]);
            warning_fs_log("fd = 0x%x, block size = %d", fd, retval);
        }
    }
    return retval;
}

PUBLIC void try_to_mount_disk(const char* dname, LONGINT floppyfd, LONGINT* messp, LONGINT bsize, LONGINT maxbytes,
                              drive_flags_t flags, uint32 offset_in)
{
    INTEGER partition;
    ParamBlockRec pb;
    char *tbuf, *buf;
    INTEGER drivenum;
    LONGINT offset;
    BOOLEAN foundmap, first;
    oldblock1_t* oldmapp;
    partmapentry_t* partp;
    int i;
    LONGINT mess;
    OSErr err;
    DrvQExtra* dqp;
    hfs_access_t hfs;
    LONGINT driver_block_size;

    *messp = 0;
    tbuf = alloca(bsize + 3);
    buf = (char*)(((uintptr_t)tbuf + 3) & ~(uintptr_t)3);
    partition = 0;

    if (floppyfd >= 0)
        driver_block_size = read_driver_block_size(floppyfd, bsize, maxbytes, buf);
    else
        driver_block_size = PHYSBSIZE;

    hfs.fd = (flags & DRIVE_FLAGS_FLOPPY) ? floppyfd : -1;
    hfs.offset = offset_in;
    hfs.bsize = bsize;
    hfs.maxbytes = maxbytes;

    dqp = ROMlib_addtodq(2048L * 2, dname, partition, OURHFSDREF, flags, &hfs);

    if (floppyfd < 0)
        /*-->*/ return;

    drivenum = CW(dqp->dq.dQDrive);

    pb.ioParam.ioVRefNum = CW(drivenum);

    foundmap = FALSE;
    first = TRUE;
    offset = hfs.offset + PARTOFFSET * driver_block_size;
    if (ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset, reading, bsize, maxbytes) == noErr) {
        if (buf[0] == PARMAPSIG0 && buf[1] == PARMAPSIG1) {
            warning_fs_log("found partition sig at %d, floppyfd = 0x%x", offset, floppyfd);
            partp = (partmapentry_t*)buf;
            do {
                if (strncmp((char*)partp->pmPartType, HFSPARTTYPE, 32) == 0) {
                    foundmap = TRUE;
                    if (!first) {
                        ++partition;
                        dqp = ROMlib_addtodq(2048L * 2, dname, partition, OURHFSDREF, flags, &hfs);
                        drivenum = CW(dqp->dq.dQDrive);
                        pb.ioParam.ioVRefNum = CW(drivenum);
                    }
                    dqp->hfs.offset = hfs.offset + (CL(partp->pmPyPartStart) * driver_block_size);
                    err = hfsPBMountVol(&pb, floppyfd, dqp->hfs.offset, bsize, maxbytes, flags, dqp);
                    mess = ((LONGINT)err << 16) | drivenum;
                    if (first) {
                        *messp = CL(mess);
                        first = FALSE;
                    }
                    else
                        PPostEvent(diskEvt, mess, (HIDDEN_EvQElPtr*)0);
                }
                offset += driver_block_size;
            } while (ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset, reading, bsize, maxbytes) == noErr &&
                     buf[0] == PARMAPSIG0 && buf[1] == PARMAPSIG1);
        }
        else if (buf[0] == OLDMAPSIG0 && buf[1] == OLDMAPSIG1) {
            oldmapp = (oldblock1_t*)buf;
            for (i = 0; i < NOLDENTRIES &&
                        (Cx(oldmapp->oldmapentry[i].pdStart) != 0 || Cx(oldmapp->oldmapentry[i].pdSize) != 0 ||
                         Cx(oldmapp->oldmapentry[i].pdFSID) != 0);
                 ++i) {
                /*
				 * NOTE: We initially tried looking for 'TFS1' in pdFSID, but our Cirrus 80
				 *	 didn't use that id.
				 */
                if (!first) {
                    ++partition;
                    dqp = ROMlib_addtodq(2048L * 2, dname, partition, OURHFSDREF, flags, &hfs);
                    drivenum = CW(dqp->dq.dQDrive);
                    pb.ioParam.ioVRefNum = CW(drivenum);
                }
                dqp->hfs.offset = hfs.offset + (CL(oldmapp->oldmapentry[i].pdStart) * driver_block_size);
                err = hfsPBMountVol(&pb, floppyfd, dqp->hfs.offset, bsize, maxbytes, flags, dqp);
                mess = ((LONGINT)err << 16) | drivenum;
                if (first) {
                    *messp = CL(mess);
                    first = FALSE;
                }
                else
                    PPostEvent(diskEvt, mess, (HIDDEN_EvQElPtr*)0);
            }
            foundmap = TRUE;
        }
    }

    if (!foundmap) {
        for (offset = hfs.offset + VOLUMEINFOBLOCKNO * PHYSBSIZE, i = 4; --i >= 0; offset += PHYSBSIZE) {
            if (i == 0) {
                if (ROMlib_magic_offset == -1)
                    /*-->*/ continue;
                else
                    offset = ROMlib_magic_offset;
            }
            err = ROMlib_readwrite(floppyfd, buf, PHYSBSIZE, offset, reading, bsize, maxbytes);
            if (err != noErr) {
                warning_unexpected("fd = 0x%x err = %d, offset = %d, "
                                   "bsize = %d, maxbytes = %d",
                                   floppyfd, err, offset, bsize, maxbytes);
                /*-->*/ break;
            }
            if (buf[0] == 'B' && buf[1] == 'D') {
                warning_fs_log("Found HFS volume on 0x%x %d", floppyfd, offset);
                offset -= VOLUMEINFOBLOCKNO * PHYSBSIZE;
                foundmap = TRUE;
                /*-->*/ break;
            }
            else if (buf[0] == 'H' && buf[1] == '+') {
                warning_fs_log("Found HFS+ volume on 0x%x %d", floppyfd, offset);
            }
            else {
                warning_fs_log("fd = 0x%x, offset = %d, sig = 0x%02x%02x", floppyfd, offset, buf[0], buf[1]);
            }
        }
        if (foundmap) {
            dqp->hfs.offset = offset;
            err = hfsPBMountVol(&pb, floppyfd, offset, bsize, maxbytes, flags, dqp);
            *messp = CL(((LONGINT)err << 16) | drivenum);
        }
    }
}

PUBLIC void ROMlib_openfloppy(const char* dname, LONGINT* messp)
{
    LONGINT floppyfd;
    LONGINT bsize, maxbytes;
    drive_flags_t flags;
    uint32 offset;

    *messp = 0;
    floppyfd = try_to_open_disk(dname, &bsize, &maxbytes, &flags, &offset);
    if (floppyfd >= 0)
        try_to_mount_disk(dname, floppyfd, messp, bsize, maxbytes, flags, offset);
}

PUBLIC void ROMlib_openharddisk(const char* dname, LONGINT* messp)
{
    char* newbuf;
    long len;
    struct stat sbuf;

    *messp = 0;
    if (Ustat(dname, &sbuf) == 0) {
        ASSIGN_NAME_MODE_STRING(newbuf, &len, dname, sbuf);
        ROMlib_openfloppy(newbuf, messp);
    }
}

/* The probe validates every HFS
   partition before the legacy filesystem sees any of its allocation fields. */
static OSErr mount_image(const char* path, int mode)
{
    romaw_image_plan plan;
    if (!path || !*path)
        return paramErr;
    int fd = Uopen(path, O_BINARY | O_RDONLY, 0);
    if (fd < 0)
        return ROMlib_maperrno();
    struct stat image_attributes;
    if (fstat(fd, &image_attributes) || !S_ISREG(image_attributes.st_mode)) {
        close(fd);
        return paramErr;
    }
    FILE* file = fdopen(fd, "rb");
    if (!file) {
        close(fd);
        return ioErr;
    }
    int valid = romaw_image_probe(file, &plan);
    fclose(file);
    if (!valid)
        return ROMlib_mount_modern_image(path);
    const char* suffix = strrchr(path, '.');
    int optical =
        suffix && (!strcasecmp(suffix, ".iso") || !strcasecmp(suffix, ".cdr") || !strcasecmp(suffix, ".toast"));
    int writable = mode >= 0 && (!optical || mode > 0) && romaw_images_ndif_writable(path);
    int shared_fd = -1;
    drive_flags_t shared_flags = DRIVE_FLAGS_FIXED;
    for (unsigned i = 0; i < plan.count; ++i) {
        hfs_access_t access = {.fd = -1, .offset = plan.offset[i], .bsize = 512, .maxbytes = 1024 * 1024};
        drive_flags_t flags = shared_flags;
        if (shared_fd >= 0)
            fd = dup(shared_fd);
        else {
#ifdef _WIN32
            fd = writable ? _sopen(path, O_BINARY | O_RDWR | O_NOINHERIT, _SH_DENYWR, 0) : -1;
#else
            struct stat attributes;
            fd = writable && stat(path, &attributes) == 0 && (attributes.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))
                     ? Uopen(path, O_BINARY | O_RDWR, 0)
                     : -1;
            if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB)) {
                close(fd);
                fd = -1;
            }
#endif
            if (fd < 0) {
                flags |= DRIVE_FLAGS_LOCKED;
                fd = Uopen(path, O_BINARY | O_RDONLY, 0);
            }
        }
        if (fd < 0)
            return ROMlib_maperrno();
        DrvQExtra* drive = ROMlib_addtodq(4096, path, i, OURHFSDREF, flags, &access);
        if (!drive) {
            close(fd);
            return memFullErr;
        }
        INTEGER number = CW(drive->dq.dQDrive);
        HVCB* existing = ROMlib_vcbbydrive(number);
        if (existing) {
            close(fd);
            shared_fd = ((VCBExtra*)existing)->u.hfs.fd;
            shared_flags = DRIVE_FLAGS_FIXED | (((uint32)CL(drive->flags) & 0x80000000U) ? DRIVE_FLAGS_LOCKED : 0);
            continue;
        }
        access.fd = fd;
        drive->hfs = access;
        /* A reused drive entry may have had a different access mode. */
        uint32 drive_flags = CL(drive->flags) & ~0x80000000U;
        if (flags & DRIVE_FLAGS_LOCKED)
            drive_flags |= 0x80000000U;
        drive->flags = CL(drive_flags);
        ParamBlockRec pb = {0};
        pb.ioParam.ioVRefNum = CW(number);
        OSErr error = hfsPBMountVol(&pb, fd, access.offset, access.bsize, access.maxbytes, flags, drive);
        if (error) {
            close(fd);
            return error;
        }
        shared_fd = fd;
        shared_flags = flags;
        PPostEvent(diskEvt, number, (HIDDEN_EvQElPtr*)0);
    }
    return noErr;
}

PUBLIC OSErr ROMlib_mount_image_mode(const char* path, int mode)
{
    /* Remember the outermost selection so application relaunches reconstruct
       the session from the original source container. */
    static unsigned depth;
    if (!path || !*path)
        return paramErr;
#ifdef _WIN32
    char* absolute = _fullpath(NULL, path, 0);
#else
    char* absolute = realpath(path, NULL);
#endif
    if (!absolute)
        return ROMlib_maperrno();
    ++depth;
#if defined(SDL)
    if (depth == 1)
        ROMlib_sdl_image_progress_begin();
#endif
    OSErr error = mount_image(absolute, mode);
#if defined(SDL)
    if (depth == 1)
        ROMlib_sdl_image_progress_end();
#endif
    --depth;
    if (!error && !depth && !ROMlib_ecf_remember_mount_mode(absolute, mode))
        error = memFullErr;
    free(absolute);
    return error;
}

PUBLIC OSErr ROMlib_mount_image(const char* path)
{
    return ROMlib_mount_image_mode(path, 0);
}

PUBLIC int ROMlib_mount_image_dialog(void)
{
    char* path = NULL;
    int result = host_image_dialog(&path);
    if (result <= 0) {
        if (result < 0)
            host_dialog_error("Cannot open the disk-image picker.");
        return result;
    }
    OSErr error = ROMlib_mount_image_mode(path, result == 2 ? 1 : -1);
    free(path);
    if (error == -128) /* userCanceledErr */
        return 0;
    if (error) {
        char message[512];
        snprintf(message, sizeof message, "Cannot mount this disk image (error %d). %s", error, ROMlib_image_error());
        host_dialog_error(message);
        return -1;
    }
    return 1;
}

#endif

#define JUMPTODONEIF(x) \
    if ((x)) {          \
        err = ioErr;    \
        goto DONE;      \
    }

PUBLIC OSErr ROMlib_readwrite(LONGINT fd, char* buffer, LONGINT count, LONGINT offset, accesstype rw, LONGINT blocksize,
                              LONGINT maxtransfer)
{
    OSErr image_error;
    if (ROMlib_image_io(fd, buffer, count, offset, rw != reading, &image_error))
        return image_error;
    char* newbuffer;
    LONGINT remainder, totransfer;
    BOOLEAN needlseek;
    OSErr err;
    int (*readfp)(int fd, void* buf, int nbytes);
    int (*writefp)(int fd, const void* buf, int nbytes);
    off_t (*seekfp)(int fd, off_t where, int how);

    if (blocksize > 18 * 1024) {
        warning_unexpected("fd = 0x%x, block size = %d", fd, blocksize);
        return fsDSIntErr;
    }

    if (blocksize == 0) {
        blocksize = 2048;
        warning_unexpected("fd = 0x%x, zero block size", fd);
    }
#if defined(CYGWIN32)
    if (fd & DOSFDBIT) {
        fd &= ~DOSFDBIT;
        seekfp = dosdisk_seek;
        readfp = dosdisk_read;
        writefp = dosdisk_write;
    }
    else {
#endif
        seekfp = (off_t(*)(int, off_t, int))lseek;
        readfp = (int (*)(int, void*, int))read;
        writefp = (int (*)(int, const void*, int))write;
#if defined(CYGWIN32)
    }
#endif
    err = noErr;
    newbuffer = 0;
    needlseek = TRUE;
    if ((remainder = offset % blocksize)) { /* |xxxDATA| */
        remainder = blocksize - remainder;
        totransfer = MIN(count, remainder);
        newbuffer = (char*)(((uintptr_t)alloca(blocksize + 3) + 3) & ~(uintptr_t)3);
        offset = offset / blocksize * blocksize;
        JUMPTODONEIF(seekfp(fd, offset, L_SET) < 0)
        needlseek = FALSE;
        JUMPTODONEIF(readfp(fd, newbuffer, blocksize) != blocksize)
        if (rw == reading) {
            memmove(buffer, newbuffer + blocksize - remainder, totransfer);
        }
        else {
            memmove(newbuffer + blocksize - remainder, buffer, totransfer);
            JUMPTODONEIF(seekfp(fd, offset, L_SET) < 0)
            JUMPTODONEIF(writefp(fd, newbuffer, blocksize) != blocksize)
        }
        buffer += totransfer;
        count -= totransfer;
        offset += blocksize;
    }
    if (count >= blocksize) { /* |DATADATADATA...| */
        remainder = count % blocksize;
        count -= remainder;
        if (needlseek) {
            JUMPTODONEIF(seekfp(fd, offset, L_SET) < 0)
            needlseek = FALSE;
        }
        while (count) {
            totransfer = MIN(maxtransfer, count);
            if (rw == reading) {
                JUMPTODONEIF(readfp(fd, buffer, totransfer) != totransfer)
            }
            else {
                JUMPTODONEIF(writefp(fd, buffer, totransfer) != totransfer)
            }
            buffer += totransfer;
            count -= totransfer;
            offset += totransfer;
        }
        count = remainder;
    }
    if (count) { /* |DATAxxx| */
        if (!newbuffer)
            newbuffer = (char*)(((uintptr_t)alloca(blocksize + 3) + 3) & ~(uintptr_t)3);
        if (needlseek)
            JUMPTODONEIF(seekfp(fd, offset, L_SET) < 0)
        JUMPTODONEIF(readfp(fd, newbuffer, blocksize) != blocksize)
        if (rw == reading) {
            memmove(buffer, newbuffer, count);
        }
        else {
            memmove(newbuffer, buffer, count);
            JUMPTODONEIF(seekfp(fd, offset, L_SET) < 0)
            JUMPTODONEIF(writefp(fd, newbuffer, blocksize) != blocksize)
        }
    }
DONE:
    return err;
}

PUBLIC OSErr ROMlib_transphysblk(hfs_access_t* hfsp, LONGINT physblock, short nphysblocks, Ptr bufp, accesstype rw,
                                 LONGINT* actp)
{
    LONGINT fd;
    OSErr err;
    Ptr newbufp;

#if defined(MAC)
    ioParam pb;

    pb.ioVRefNum = vcbp->vcbDrvNum;
    pb.ioRefNum = vcbp->vcbDRefNum;
    pb.ioBuffer = CL(bufp);
    pb.ioReqCount = CL(PHYSBSIZE * (LONGINT nphysblocks));
    pb.ioPosMode = CW(fsFromStart);
    pb.ioPosOffset = CL(physblock);
    err = rw == reading ? PBRead((ParmBlkPtr)&pb, FALSE) : PBWrite((ParmBlkPtr)&pb, FALSE);
    if (actp)
        *actp = pb.ioActCount;
#else
    newbufp = bufp;
    fd = hfsp->fd;

    err = ROMlib_readwrite(fd, (char*)newbufp, (LONGINT)nphysblocks * PHYSBSIZE, physblock + hfsp->offset, rw,
                           hfsp->bsize, hfsp->maxbytes);
    if (actp)
        *actp = err != noErr ? 0 : CL((LONGINT)nphysblocks * PHYSBSIZE);

#endif
    if (err != noErr)
        warning_unexpected("fd = 0x%x, err in transphysblock (err = %d)", fd, err);
    return err;
}

PUBLIC char* ROMlib_indexn(char* str, char tofind, INTEGER length)
{
    while (--length >= 0)
        if (*str++ == tofind)
            return str - 1;
    return 0;
}

#if !defined(str255assign)
PUBLIC void str255assign(StringPtr dstp, StringPtr srcp)
{
    memmove(dstp, srcp, (size_t)srcp[0] + 1);
}
#endif /* !defined(str255assign) */

/*
 * ROMlib_indexqueue returns a pointer to the n'th entry on a queue.
 * ROMlib_indexqueue is one based; not zero based.
 */

PUBLIC void* ROMlib_indexqueue(QHdr* qp, short index)
{
    QElemPtr p;

#if defined(MAC)
    for (p = CL(qp->qHead); (--index > 0) && p; p = CL(p->qLink))
        ;
#else
    for (p = PPR(qp->qHead); (--index > 0) && p; p = PPR(p->vcbQElem.qLink))
        ;
#endif
    return p;
}

PUBLIC OSErr ROMlib_writefcbp(filecontrolblock* fcbp)
{
    Byte flags;
    OSErr retval;

    flags = fcbp->fcbMdRByt;
    if (!(flags & WRITEBIT))
        retval = wrPermErr;
    else if (flags & FLOCKEDBIT)
        retval = fLckdErr;
    else
        retval = noErr;
    return retval;
}

PUBLIC OSErr ROMlib_writevcbp(HVCB* vcbp)
{
    INTEGER vflags;
    OSErr retval;

    vflags = Cx(vcbp->vcbAtrb);
    if (vflags & VSOFTLOCKBIT)
        retval = vLckdErr;
    else if (vflags & VHARDLOCKBIT)
        retval = wPrErr;
    else
        retval = noErr;
    return retval;
}
