/*+===================================================================
  File:      fileMisc.c

  Summary:   Implements file misc support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: PRNTOFPERR, ROMlib_addtodq, ROMlib_automount, ROMlib_fileinit, ROMlib_mount_compatible_directories,
             ROMlib_volume_alias, convert_slashs_to_backslashs, copystr, ufsPBGetFCBInfo

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1997 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in FileMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "rsys/configprefs.h"
#include "rsys/user_profile.h"
#include "rsys/host_names.h"
#include "QuickDraw.h"
#include "FileMgr.h"
#include "OSEvent.h"
#include "VRetraceMgr.h"
#include "OSUtil.h"
#include "MemoryMgr.h"
#include "StdFilePkg.h"

#include "rsys/hfs.h"
#include "rsys/file.h"
#include "rsys/notmac.h"
#include "rsys/stdfile.h"
#include "rsys/string.h"
#include "rsys/segment.h"
#include "rsys/suffix_maps.h"

#if defined(CYGWIN32)
#include "rsys/checkpoint.h"
#endif

#if !defined(CYGWIN32)
#include <pwd.h>
#else
#include "winfs.h"
#include "dosdisk.h"
#endif

#include <ctype.h>

/* NOTE:  calling most of the routines here is a sign that the user may
	  be depending on the internal layout of things a bit too much */

A0(PUBLIC trap, void, FInitQueue) /* IMIV-128 */
{
}

A0(PUBLIC trap, QHdrPtr, GetFSQHdr) /* IMIV-175 */
{
    return (&FSQHdr); /* in UNIX domain, everything is synchronous */
}

A0(PUBLIC trap, QHdrPtr, GetVCBQHdr) /* IMIV-178 */
{
    return (&VCBQHdr);
}

A0(PUBLIC trap, QHdrPtr, GetDrvQHdr) /* IMIV-182 */
{
    return (&DrvQHdr);
}

A2(PUBLIC, OSErr, ufsPBGetFCBInfo, FCBPBPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    int rn;
    OSErr err;
    fcbrec* fp;
    int i, count;

#if !defined(LETGCCWAIL)
    rn = -1;
    fp = 0;
#endif /* LETGCCWAIL */

    err = noErr;
    if (pb->ioFCBIndx == 0) {
        rn = Cx(pb->ioRefNum);
        fp = PRNTOFPERR(rn, &err);
    }
    else if (Cx(pb->ioFCBIndx) > 0) {
        for (count = 0, i = 0; i < NFCB && count < Cx(pb->ioFCBIndx); i++)
            if (ROMlib_fcblocks[i].fdfnum &&
                (!pb->ioVRefNum || PPR(ROMlib_fcblocks[i].fcvptr)->vcbVRefNum == pb->ioVRefNum))
                count++;
        if (count == Cx(pb->ioFCBIndx)) {
            fp = ROMlib_fcblocks + i - 1;
            rn = (Ptr)fp - MR(FCBSPtr);
        }
        else
            err = paramErr;
    }
    else
        err = paramErr;
    if (err == noErr) {
        if (PP_RAW(pb->ioNamePtr))
            str255assign(PPR(pb->ioNamePtr), fp->fcname);
        pb->ioVRefNum = PPR(fp->fcvptr)->vcbVRefNum;
        pb->ioRefNum = CW(rn);
        pb->ioFCBFlNm = fp->fdfnum;
        pb->ioFCBFlags = (fp->fcflags << 8) | (unsigned char)fp->fcbTypByt;
        pb->ioFCBStBlk = 0;
        pb->ioFCBEOF = fp->fcleof;
        pb->ioFCBPLen = fp->fcleof;
        pb->ioFCBCrPs = lseek(fp->fcfd, 0, L_INCR) - FORKOFFSET(fp);
        pb->ioFCBVRefNum = PPR(fp->fcvptr)->vcbVRefNum; /* what's this? */
        pb->ioFCBClpSiz = PPR(fp->fcvptr)->vcbClpSiz;
        pb->ioFCBParID = fp->fcparid;
    }
    return err;
}

PRIVATE boolean_t charcmp(char c1, char c2)
{
    boolean_t retval;

    if (c1 == c2)
        retval = TRUE;
    else if (c1 == '/')
        retval = c2 == '\\';
    else if (c1 == '\\')
        retval = c2 == '/';
    else
        retval = tolower(c1) == tolower(c2);
    return retval;
}

PRIVATE int slashstrcmp(const char* p1, const char* p2)
{
    int retval;

    retval = 0;

    while (*p1 || *p2) {
        if (!charcmp(*p1, *p2)) {
            retval = -1;
            break;
        }
        ++p1;
        ++p2;
    }
    return retval;
}

PRIVATE INTEGER ROMlib_driveno = 3;
PRIVATE INTEGER ROMlib_ejdriveno = 2;

/*
 * NOTE: The way we handle drive information is pretty messed up right now.
 * In general the correct information is in the VCBExtra; we only recently
 * began putting it in the DriveExtra and right now we only use the info
 * in the DriveExtra to allow us to format floppies -- no other formatting
 * is currently permitted.  The problem is there's no easy way to map drive
 * characteristics from the non-Mac host into Mac type information unless
 * we can pull the information out of the Mac filesystem.
 */

PUBLIC DrvQExtra* ROMlib_addtodq(ULONGINT drvsize, const char* devicename, INTEGER partition, INTEGER drefnum,
                                 drive_flags_t flags, hfs_access_t* hfsp)
{
    INTEGER dno;
    DrvQExtra* dqp;
    DrvQEl* dp;
    int strl;
    THz saveZone;
    static boolean_t seen_floppy = FALSE;

    saveZone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
#if !defined(LETGCCWAIL)
    dqp = (DrvQExtra*)0;
#endif
    dno = 0;
    for (dp = (DrvQEl*)PPR(DrvQHdr.qHead); dp; dp = (DrvQEl*)PPR(dp->qLink)) {
        dqp = (DrvQExtra*)((char*)dp - sizeof(LONGINT));
        if (dqp->partition == CW(partition) && slashstrcmp((char*)dqp->devicename, devicename) == 0) {
            dno = Cx(dqp->dq.dQDrive);
            /*-->*/ break;
        }
    }
    if (!dno) {
        if ((flags & DRIVE_FLAGS_FLOPPY) && !seen_floppy) {
            dno = 1;
            seen_floppy = TRUE;
        }
        else {
            if ((flags & DRIVE_FLAGS_FIXED) || ROMlib_ejdriveno == 3)
                dno = ROMlib_driveno++;
            else
                dno = ROMlib_ejdriveno++;
        }
        dqp = (DrvQExtra*)NewPtr(sizeof(DrvQExtra));
        if (!dqp)
            return NULL;
        dqp->flags = CL(1 << 7); /* is not single sided */
        if (flags & DRIVE_FLAGS_LOCKED)
            dqp->flags = CL(CL(dqp->flags) | 1L << 31);
        if (flags & DRIVE_FLAGS_FIXED)
            dqp->flags = CL(CL(dqp->flags) | 8L << 16);
        else
            dqp->flags = CL(CL(dqp->flags) | 2); /* IMIV-181 says
								it can be 1 or 2 */

        /*	dqp->dq.qLink will be set up when we Enqueue this baby */
        dqp->dq.dQDrvSz = CW(drvsize);
        dqp->dq.dQDrvSz2 = CW(drvsize >> 16);
        dqp->dq.qType = CWC(1);
        dqp->dq.dQDrive = CW(dno);
        dqp->dq.dQRefNum = CW(drefnum);
        dqp->dq.dQFSID = 0;
        if (!devicename)
            dqp->devicename = 0;
        else {
            strl = strlen(devicename);
            dqp->devicename = NewPtr(strl + 1);
            strcpy((char*)dqp->devicename, devicename);
        }
        dqp->partition = CW(partition);
        if (hfsp)
            dqp->hfs = *hfsp;
        else {
            memset(&dqp->hfs, 0, sizeof(dqp->hfs));
            dqp->hfs.fd = -1;
        }
        Enqueue((QElemPtr)&dqp->dq, &DrvQHdr);
    }
    HIDDEN_RAW_SET(TheZone_H, saveZone);
    return dqp;
}

PRIVATE boolean_t root_directory_p(char* path, dev_t our_dev)
{
    char* slash;
    boolean_t retval;

    /* we used to just compare our_inode to 2, but that doesn't work with
	   NFS mounted filesystems that aren't mounted at the root directory or
	   with DOS filesystems mounted under Linux */

    slash = strrchr(path, '/');
    if (!slash || ((slash == path + SLASH_CHAR_OFFSET) && !slash[1]))
        retval = TRUE;
    else {
        struct stat sbuf;
        char save_char;

        if (slash == path + SLASH_CHAR_OFFSET)
            ++slash;
        save_char = *slash;
        *slash = 0;
        if (Ustat(path, &sbuf) != 0)
            retval = TRUE;
        else
            retval = sbuf.st_dev != our_dev;
        *slash = save_char;
    }
    return retval;
}

/*
 * ROMlib_volumename is a magic global variable that tells MountVol
 * the name of the volume that you're mounting (since there is no
 * way to map the "drive number" into such a string)
 */

PUBLIC char* ROMlib_volumename;

PRIVATE void ROMlib_automount_helper(char* path, char* aliasp)
{
    struct stat sbuf;
    char *oldsavep, *savep, save;
    ParamBlockRec pb;
    int sret;
    int i;
    LONGINT dirid;
    INTEGER retval;
    HVCB* vcbp;
    DrvQExtra* dqp;

#if defined(CYGWIN32)
    {
        char *temppath, *op, c;
        int len;

        len = strlen(path) + 1;

        /* If we don't have x:/ then we need to prepend the start drive */
        if (path[0] && (path[1] != ':' || path[2] != '/'))
            len += 2;
        temppath = alloca(len);
        if (path[0] && (path[1] != ':' || path[2] != '/')) {
            temppath[0] = ROMlib_start_drive;
            temppath[1] = ':';
            temppath += 2;
        }

        /* convert backslashes to slashes */
        op = temppath;
        while ((c = *path++))
            *op++ = c == '\\' ? '/' : c;
        *op = 0;
        path = temppath;
    }
#endif

    retval = 0;
    oldsavep = NULL;
#if !defined(LETGCCWAIL)
    save = 0;
#endif
    if (path[0] == '/'
#if defined(CYGWIN32)
        || (path[1] == ':' && path[2] == '/')
#endif
    ) {
        ROMlib_undotdot(path);
        /* Make two passes:  On the first pass (i == 0) we identify
		   filesystems and mount them.  On the second pass (i == 1) we
		   store away intermediate directory numbers */

        for (i = 0; i < 2; ++i) {
            boolean_t done;
            sret = Ustat(path, &sbuf);
            savep = 0;
            oldsavep = 0;
            done = FALSE;
            do {
                if (sret == 0 && S_ISDIR(sbuf.st_mode)) {
                    if (root_directory_p(path, sbuf.st_dev) || aliasp) {
                        if (i == 0) {
                            ROMlib_volumename = path;
                            dqp = ROMlib_addtodq(2048L * 50, ROMlib_volumename, 0, OURUFSDREF, DRIVE_FLAGS_FIXED, 0);
                            if (!dqp) {
                                retval = memFullErr;
                                goto mount_failed;
                            }
                            pb.ioParam.ioVRefNum = dqp->dq.dQDrive;
                            retval = ufsPBMountVol(&pb);
                            if (retval != noErr && retval != volOnLinErr)
                                goto mount_failed;
                            if (aliasp) {
                                VCBExtra* vcbp;

                                vcbp = ROMlib_vcbbyunixname(path);
                                if (!vcbp) {
                                    retval = nsvErr;
                                    goto mount_failed;
                                }
                                const char* label = aliasp;
#if defined(CYGWIN32)
                                char mac_label[256];
                                size_t length = strlen(aliasp);
                                int converted =
                                    length < sizeof mac_label ? ROMlib_host_name_to_mac(aliasp, length, mac_label) : -1;
                                if (converted >= 0) {
                                    mac_label[converted] = 0;
                                    label = mac_label;
                                }
#endif
                                pstr_from_c_string(vcbp->vcb.vcbVN, sizeof vcbp->vcb.vcbVN, label);
                                ROMlib_restore_host_volume_name(vcbp);
                                /* hack in name */
                                if (oldsavep)
                                    *oldsavep = save;
                                /*-->*/ return;
                            }
                        }
                    }
                    else {
                        if (i == 1) {
                            vcbp = ROMlib_vcbbybiggestunixname(path);
                            if (!vcbp) {
                                retval = nsvErr;
                                goto mount_failed;
                            }
                            dirid = ST_INO(sbuf);
                            ROMlib_dbm_store((VCBExtra*)vcbp, path, &dirid, FALSE);
                        }
                    }
                }
                if (savep == path + SLASH_CHAR_OFFSET + 1)
                    done = TRUE;
                else {
                    savep = strrchr(path, '/');
                    if (savep == path + SLASH_CHAR_OFFSET)
                        ++savep;
                    if (oldsavep)
                        *oldsavep = save;
                    save = *savep;
                    *savep = 0;
                    oldsavep = savep;
                    sret = Ustat(path, &sbuf);
                }
            } while (!done);
            if (oldsavep)
                *oldsavep = save;
        }
    }
    return;

mount_failed:
    if (oldsavep)
        *oldsavep = save;
    fprintf(stderr, "Unable to mount host folder '%s' (error %d)\n", path, retval);
}

PUBLIC void ROMlib_automount(char* path)
{
    ROMlib_automount_helper(path, NULL);
}

PUBLIC void ROMlib_volume_alias(const char* path, const char* alias_name)
{
    ROMlib_automount_helper((char*)path, (char*)alias_name);
}

PUBLIC void ROMlib_mount_compatible_directories(void)
{
    for (size_t i = 0; i < config_drive_directory_count(); ++i) {
        const char* folder = config_drive_directory(i);
        if (ROMlib_vcbbyunixname(folder))
            continue;
        if (strlen(folder) == 3 && folder[1] == ':' && folder[2] == '/') {
            ROMlib_automount((char*)folder);
            continue;
        }
        const char* leaf = strrchr(folder, '/');
        leaf = leaf && leaf[1] ? leaf + 1 : folder;
        ROMlib_volume_alias(folder, leaf);
    }
}

static void host_path_memory_error(void)
{
    fputs("Unable to allocate expanded host path.\n", stderr);
    exit(1);
}

static char* combine_str(const char* str1, const char* str2)
{
    size_t len1, len2;
    char* retval;

    if (!str1 || !str2)
        return NULL;
    len1 = strlen(str1);
    len2 = strlen(str2);
    if (len2 == (size_t)-1 || len1 > (size_t)-1 - len2 - 1)
        return NULL;
    retval = malloc(len1 + len2 + 1);
    if (retval) {
        memcpy(retval, str1, len1);
        memcpy(retval + len1, str2, len2 + 1);
    }
    return retval;
}

PUBLIC void convert_slashs_to_backslashs(char* p)
{
    if (p) {
        while (*p) {
            if (*p == '/')
                *p = '\\';
            ++p;
        }
    }
}

#if defined(LINUX)

PRIVATE char* substr(const char* source_string, const char* source_substring, const char* dest_substring)
{
    int source_string_len;
    int source_substring_len;
    int dest_substring_len;
    int max_len;
    char* retval;

    source_string_len = strlen(source_string);
    source_substring_len = strlen(source_substring);
    dest_substring_len = strlen(dest_substring);
    if (dest_substring_len > source_substring_len)
        max_len = ((source_string_len + source_substring_len - 1) / source_substring_len * dest_substring_len) + 1;
    else
        max_len = source_string_len + 1;
    retval = malloc(max_len);
    if (retval) {
        const char* ip;
        char* op;

        for (op = retval, ip = source_string; *ip;) {
            if (strncmp(ip, source_substring, source_substring_len) != 0)
                *op++ = *ip++;
            else {
                memcpy(op, dest_substring, dest_substring_len);
                ip += source_substring_len;
                op += dest_substring_len;
            }
        }
        *op = 0;
    }
    return retval;
}

PRIVATE char* convert_romaws_to_appnames(char* str)
{
    char* retval;
    int appname_len;
    char* appname;
    char* dash;

    if (!str)
        retval = NULL;
    else {
        appname_len = strlen(ROMlib_appname);
        appname = alloca(appname_len + 1);
        memcpy(appname, ROMlib_appname, appname_len + 1);
        dash = strchr(appname, '-');
        if (dash)
            *dash = 0;
        if (strcmp(appname, "romaw") == 0)
            retval = str;
        else {
            retval = substr(str, "romaw", appname);
            if (retval)
                free(str);
            else
                retval = str;
        }
    }
    return retval;
}
#endif

/* Mutable per-user runtime state is separate from installed read-only assets. */
PUBLIC char ROMlib_userdir[MAXPATHLEN];

PUBLIC char* copystr(const char* name)
{
    char* retval;

    if (!name)
        retval = 0;
    else {
        switch (name[0]) {
        case '@':
            retval = combine_str(ROMlib_userdir, name + 1);
            break;
        case '+':
#if !defined(LINUX)
            retval = combine_str(ROMlib_startdir, name + 1);
#else
            retval = combine_str("/home/romaw", name + 1);
#endif
            break;
#if !defined(CYGWIN32)
        case '~': {
            struct passwd* pwp;

#if defined(LINUX)
            {
                char* home;

                home = getenv("HOME");
                if (home) {
                    retval = combine_str(home, name + 1);
                    break;
                }
            }
#endif
            pwp = getpwuid(getuid());
            if (pwp) {
                retval = combine_str(pwp->pw_dir, name + 1);
                break;
            }
        }
        /* FALL THROUGH */
#endif
        default:
            retval = combine_str("", name);
            break;
        }
    }

#if defined(CYGWIN32)
    convert_slashs_to_backslashs(retval);
#endif

#if defined(LINUX)
    retval = convert_romaws_to_appnames(retval);
#endif

    if (name && !retval)
        host_path_memory_error();

    return retval;
}

#if defined(CYGWIN32)
PUBLIC boolean_t cd_mounted_by_trickery_p = FALSE;

#define MACCDROM "DOS/EXTRA/LIBRARY/MACCDROM.HFV"

#if defined(CYGWIN32)
PRIVATE char* cd_big_hfv = 0;

PRIVATE void check_for_romaw_cd(const char* drive)
{
    if (!cd_big_hfv) {
        struct stat sbuf;

        cd_big_hfv = malloc(strlen(drive) + strlen(MACCDROM) + 1);
        sprintf(cd_big_hfv, "%s%s", drive, MACCDROM);
        if (stat(cd_big_hfv, &sbuf) != 0) {
            free(cd_big_hfv);
            cd_big_hfv = 0;
        }
    }
}

PRIVATE boolean_t e2_is_mounted(void)
{
    boolean_t retval;
    const char e2_name[] = "ROMAW2";

    retval = !!vlookupbyname(e2_name, e2_name + strlen(e2_name));
    return retval;
}
#endif

#endif

PUBLIC StringPtr ROMlib_exefname;
PUBLIC char* ROMlib_exeuname;

PUBLIC char* ROMlib_SystemFolder;
PUBLIC char* ROMlib_DefaultFolder;
PUBLIC char* ROMlib_PublicDirectoryMap;
PUBLIC char* ROMlib_PrivateDirectoryMap;
PUBLIC char* ROMlib_MacVolumes;
PRIVATE char* ROMlib_OffsetFile;

PUBLIC LONGINT ROMlib_magic_offset = -1;

PRIVATE void skip_comments(FILE* fp)
{
    int c;

    while ((c = getc(fp)) == '#') {
        while (c != '\n')
            c = getc(fp);
    }
    ungetc(c, fp);
}

PRIVATE void parse_offset_file(void)
{
    FILE* fp;

    fp = Ufopen(ROMlib_OffsetFile, "r");
    if (!fp) {
    }
    else {
        int n_found;

        skip_comments(fp);
        n_found = fscanf(fp, "0x%08x", &ROMlib_magic_offset);
        if (n_found != 1)
            warning_unexpected("n_found = %d", n_found);
        fclose(fp);
    }
}

PRIVATE boolean_t is_unix_path(const char* pathname)
{
    boolean_t retval;

#if defined(CYGWIN32)
    if (pathname[0] && pathname[1] == ':' && (pathname[2] == '/' || pathname[2] == '\\'))
        pathname += 3;
#endif
    retval = strchr(pathname, ':') == 0;
    return retval;
}

static void bless_host_system_folder(HVCB* volume, LONGINT directory)
{
    /* HFS drFndrInfo[0] is the blessed System Folder CNID. The catalog result
     and VCB field are both already in guest byte order. Image volumes retain
     their own on-disk blessing; only the configured host folder is updated. */
    if (volume && !volume->vcbCTRef && volume->vcbFndrInfo[0] != directory) {
        volume->vcbFndrInfo[0] = directory;
        if (romaw_volume_finder_info(((VCBExtra*)volume)->unixname, volume->vcbFndrInfo, TRUE) < 0)
            warning_unexpected("Could not save the host System Folder blessing");
    }
}

A0(PUBLIC, void, ROMlib_fileinit) /* INTERNAL */
{
    INTEGER i;
    CInfoPBRec cpb;
    WDPBRec wpb;
    INTEGER wdlen;
    HVCB* vcbp;
    LONGINT m;
    THz savezone;
    struct stat sbuf;
    char* sysname;
    int sysnamelen;
    char *p, *ep, *newp;

    static struct dangerstr { /* DANGER DANGER DANGER */
        char* name;           /* this is taken from <defaults.h> */
        char* value;          /* instead of us including it ... */
    } defvec[] = {

    /* if <defaults.h> changes we're SOL */
#if defined(LINUX)
        {"SystemFolder", "@/Apps/System Folder"},
        {"PublicDirectoryMap", "@/cache/directorymap-public"},
        {"PrivateDirectoryMap", "@/cache/directorymap"},
        {"DefaultFolder", "@/Apps"},
        {"MacVolumes", "/var/opt/romaw/exsystem.hfv;/var/opt/romaw"},
        {"OffsetFile", "@/offset_file"},

        {NULL, "@/Apps/System Folder"},
        {NULL, "@/Apps"},

#elif !defined(CYGWIN32)
        {"SystemFolder", "@/Apps/System Folder"},
        {"PublicDirectoryMap", "@/cache/directorymap-public"},
        {"PrivateDirectoryMap", "@/cache/directorymap"},
        {"DefaultFolder", "@/Apps"},
        {"MacVolumes", "+/exsystem.hfv;+"},
        {"OffsetFile", "@/offset_file"},
#else

        {"SYSTEMFOLDER", "@/Apps/System Folder"},

        {"PUBLICDIRECTORYMAP", "@/cache/directorymap-public"},
        {"PRIVATEDIRECTORYMAP", "@/cache/directorymap"},

        {"DEFAULTFOLDER", "@/Apps"},

        {"MACVOLUMES", "+/exsystem.hfv;+"},
        {"OFFSETFILE", "@/offset.txt"},
#endif
        {0, 0},
    };

#if !defined(LINUX)
#define AFPD(m, n) (m)
#else
#define AFPD(m, n) (afpd_conventions_p ? (n) : (m))
#endif

#define SYSTEMFOLDER defvec[0].name
#define PUBLICDIRECTORYMAP defvec[1].name
#define PRIVATEDIRECTORYMAP defvec[2].name
#define DEFAULTFOLDER defvec[3].name
#define MACVOLUMES defvec[4].name
#define OFFSETFILE defvec[5].name

#define SYSTEMFOLDER_DEF defvec[AFPD(0, 6)].value
#define PUBLICDIRECTORYMAP_DEF defvec[1].value
#define PRIVATEDIRECTORYMAP_DEF defvec[2].value
#define DEFAULTFOLDER_DEF defvec[AFPD(3, 7)].value
#define MACVOLUMES_DEF defvec[4].value
#define OFFSETFILE_DEF defvec[5].value

    CurDirStore = CLC(2);

    savezone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    HIDDEN_RAW_SET(FCBSPtr_H, RM(NewPtr((Size)sizeof(fcbhidden))));
    ((fcbhidden*)MR(FCBSPtr))->nbytes = CW(sizeof(fcbhidden));

    for (i = 0; i < NFCB; i++) {
        ROMlib_fcblocks[i].fdfnum = 0;
        ROMlib_fcblocks[i].fcleof = i + 1;
        ROMlib_fcblocks[i].fcbTypByt = 0;
        ROMlib_fcblocks[i].fcbSBlk = 0;
        ROMlib_fcblocks[i].fcPLen = 0;
        ROMlib_fcblocks[i].fcbCrPs = 0;
        PP_RAW(ROMlib_fcblocks[i].fcbBfAdr) = 0;
        ROMlib_fcblocks[i].fcbFlPos = 0;
        ROMlib_fcblocks[i].fcbClmpSize = CLC(1);
        ROMlib_fcblocks[i].fcbFType = 0;
        ROMlib_fcblocks[i].zero[0] = 0;
        ROMlib_fcblocks[i].zero[1] = 0;
        ROMlib_fcblocks[i].zero[2] = 0;
        ROMlib_fcblocks[i].fcname[0] = 0;
    }
    ROMlib_fcblocks[NFCB - 1].fcleof = -1;

#define NWDENTRIES 40
    wdlen = NWDENTRIES * sizeof(wdentry) + sizeof(INTEGER);
    HIDDEN_RAW_SET(WDCBsPtr_H, RM(NewPtr((Size)wdlen)));
    HIDDEN_RAW_SET(TheZone_H, savezone);
    memset(MR(WDCBsPtr), 0, wdlen);
    *(INTEGER*)MR(WDCBsPtr) = CW(wdlen);

    ROMlib_SystemFolder = copystr(getenv(SYSTEMFOLDER));
    ROMlib_PublicDirectoryMap = copystr(getenv(PUBLICDIRECTORYMAP));
    ROMlib_PrivateDirectoryMap = copystr(getenv(PRIVATEDIRECTORYMAP));
    ROMlib_DefaultFolder = copystr(getenv(DEFAULTFOLDER));
    ROMlib_MacVolumes = copystr(getenv(MACVOLUMES));
    ROMlib_OffsetFile = copystr(getenv(OFFSETFILE));

    if (!ROMlib_SystemFolder)
        ROMlib_SystemFolder = copystr(SYSTEMFOLDER_DEF);
    if (!ROMlib_PublicDirectoryMap)
        ROMlib_PublicDirectoryMap = copystr(PUBLICDIRECTORYMAP_DEF);
    if (!ROMlib_PrivateDirectoryMap)
        ROMlib_PrivateDirectoryMap = copystr(PRIVATEDIRECTORYMAP_DEF);
    if (!ROMlib_DefaultFolder)
        ROMlib_DefaultFolder = copystr(DEFAULTFOLDER_DEF);
    if (!ROMlib_MacVolumes)
        ROMlib_MacVolumes = copystr(MACVOLUMES_DEF);
    if (!ROMlib_OffsetFile)
        ROMlib_OffsetFile = copystr(OFFSETFILE_DEF);

    parse_offset_file();

    /*
	 * NOTE: The following is a hack that will remain in place until we have
	 *     a replacement for using the ndbm routines which apparently can't
	 *     share files between machines of different endianness.
	 */

#if defined(LITTLEENDIAN)

#define STR_APPEND(str, suffix)                      \
    {                                                \
        char* _combined_ = combine_str(str, suffix); \
        if (!_combined_)                             \
            host_path_memory_error();                \
        free(str);                                   \
        str = _combined_;                            \
    }

#define LITTLE_ENDIAN_SUFFIX "-le"

    STR_APPEND(ROMlib_PublicDirectoryMap, LITTLE_ENDIAN_SUFFIX);
    STR_APPEND(ROMlib_PrivateDirectoryMap, LITTLE_ENDIAN_SUFFIX);

#endif /* defined(LITTLEENDIAN) */

    ROMlib_hfsinit();
    ROMlib_automount(ROMlib_SystemFolder);
    {
        char* apps = copystr("@/Apps");
        if (!apps)
            host_path_memory_error();
        ROMlib_volume_alias(apps, "ROMAW");
        free(apps);
    }
    /* Register the System Folder in the newly mounted ROMAW volume's
     catalog as well as in the enclosing host drive. */
    ROMlib_automount(ROMlib_SystemFolder);
    /* A compatible folder on a DOS drive needs its own volume capabilities. */
    ROMlib_mount_compatible_directories();

    m = 0;
    p = ROMlib_MacVolumes;
    while (p && *p) {
        ep = strchr(p, ';');
        if (ep)
            *ep = 0;
        newp = copystr(p);
        if (Ustat(newp, &sbuf) == 0) {
            if (!S_ISDIR(sbuf.st_mode))
                ROMlib_openharddisk(newp, &m);
            else {
                DIR* dirp;

                dirp = Uopendir(newp);
                if (dirp) {
#if defined(USE_STRUCT_DIRECT)
                    struct direct* direntp;
#else
                    struct dirent* direntp;
#endif

                    while ((direntp = readdir(dirp))) {
                        int namelen;

                        namelen = strlen(direntp->d_name);
                        if (namelen >= 4 && (strcasecmp(direntp->d_name + namelen - 4, ".hfv") == 0 ||
                                             strcasecmp(direntp->d_name + namelen - 4, ".hda") == 0 ||
                                             strcasecmp(direntp->d_name + namelen - 4, ".ima") == 0)) {
                            char* tempname;

                            tempname = alloca(strlen(newp) + 1 + namelen + 1);
                            sprintf(tempname, "%s/%s", newp, direntp->d_name);
                            ROMlib_openharddisk(tempname, &m);
                        }
                    }
                    closedir(dirp);
                }
            }
        }
        free(newp);
        if (ep) {
            *ep = ';';
            p = ep + 1;
        }
        else
            p = 0;
    }

    ROMlib_automount(ROMlib_startdir);
    ROMlib_automount(ROMlib_userdir);
    ROMlib_automount(ROMlib_DefaultFolder);
    if (is_unix_path(ROMlib_DefaultFolder) && Ustat(ROMlib_DefaultFolder, &sbuf) == 0) {
        CurDirStore = CL((LONGINT)ST_INO(sbuf));
        vcbp = ROMlib_vcbbybiggestunixname(ROMlib_DefaultFolder);
        if (!vcbp) {
            fprintf(stderr, "Unable to find mounted volume for default folder '%s'\n", ROMlib_DefaultFolder);
            exit(1);
        }
        SFSaveDisk = CW(-CW(vcbp->vcbVRefNum));
    }
    if (is_unix_path(ROMlib_SystemFolder)) {
        if (Ustat(ROMlib_SystemFolder, &sbuf) < 0) {
            fprintf(stderr, "Couldn't find '%s'\n", ROMlib_SystemFolder);
            exit(1);
        }
        PP_RAW(cpb.hFileInfo.ioNamePtr) = RM((StringPtr) "\001:");
        vcbp = ROMlib_vcbbybiggestunixname(ROMlib_SystemFolder);
        cpb.hFileInfo.ioVRefNum = vcbp ? vcbp->vcbVRefNum : CWC(-1);
        cpb.hFileInfo.ioDirID = CL((LONGINT)ST_INO(sbuf));
    }
    else {
        sysnamelen = 1 + strlen(ROMlib_SystemFolder) + 1 + strlen(SYSMACNAME + 1) + 1;
        sysname = alloca(sysnamelen);
        *sysname = sysnamelen - 2; /* don't count first byte or nul */
        sprintf(sysname + 1, "%s:", ROMlib_SystemFolder);
        *sysname = strlen(sysname + 1);
        PP_RAW(cpb.hFileInfo.ioNamePtr) = (StringPtr)RM(sysname);
        cpb.hFileInfo.ioVRefNum = 0;
        cpb.hFileInfo.ioDirID = 0;
    }
    cpb.hFileInfo.ioFDirIndex = CWC(0);
    if (PBGetCatInfo(&cpb, FALSE) == noErr) {
        bless_host_system_folder(ROMlib_vcbbyvrn(CW(cpb.hFileInfo.ioVRefNum)), cpb.dirInfo.ioDrDirID);
        PP_RAW(wpb.ioNamePtr) = 0;
        wpb.ioVRefNum = cpb.hFileInfo.ioVRefNum;
        wpb.ioWDProcID = TICKX("unix");
        wpb.ioWDDirID = cpb.dirInfo.ioDrDirID;
        if (PBOpenWD(&wpb, FALSE) == noErr)
            BootDrive = wpb.ioVRefNum;
    }
    else {
        fprintf(stderr, "Couldn't open System: '%s'\n", ROMlib_SystemFolder);
        exit(1);
    }
#if defined(CYGWIN32)
    {
        static char drive_to_mount[4] = "x:/";

        {
            int i;

            for (i = 0; i <= 31; ++i) {
                uint32 bit;

                bit = 1 << i;
                if (ROMlib_dosdrives & bit) {
                    drive_to_mount[0] = 'a' + i;
                    checkpoint_dosdrives(checkpointp, begin, bit);
#if defined(CYGWIN32)
                    drive_to_mount[0] += 'A' - 'a';
                    if (win_access(drive_to_mount)) {
#endif
                        ROMlib_automount(drive_to_mount);
#if defined(CYGWIN32)
                        check_for_romaw_cd(drive_to_mount);
#endif
#if defined(CYGWIN32)
                    }
#endif
                    checkpoint_dosdrives(checkpointp, end, bit);
                }
            }
        }
    }
#endif

#if defined(CYGWIN32)
    if (ROMlib_dosdrives)
#endif
        futzwithdosdisks();

#if defined(CYGWIN32)
    if (!e2_is_mounted() && cd_big_hfv) {
        LONGINT m;

        ROMlib_openharddisk(cd_big_hfv, &m);
        if (m)
            cd_mounted_by_trickery_p = TRUE;
    }
#endif

    {
        size_t length = strlen(ROMlib_userdir);
        char* map = malloc(length + sizeof "/ROMAW.ftm");
        if (map) {
            sprintf(map, "%s/ROMAW.ftm", ROMlib_userdir);
            ROMlib_load_filetype_map(map);
            free(map);
        }
    }
}

fcbrec* PRNTOFPERR(INTEGER prn, OSErr* errp)
{
    fcbrec* retval;
    OSErr err;

    if (prn < 0 || prn >= CW(*(short*)MR(FCBSPtr)) || (prn % 94) != 2) {
        retval = 0;
        err = rfNumErr;
    }
    else {
        retval = (fcbrec*)((char*)MR(FCBSPtr) + prn);
        if (!retval->fdfnum) {
            retval = 0;
            err = rfNumErr;
        }
        else
            err = noErr;
    }
    *errp = err;
    return retval;
}
