/*+===================================================================
  File:      fileAccess.c

  Summary:   Implements file access support for Macintosh File Manager and host-file operations.

  Classes:   None.

  Functions: AllocContig, Allocate, FSClose, FSOpen, FSRead, FSReadAll, FSWrite, FSWriteAll, GetEOF, GetFPos, OpenRF,
             PBHCopyFile,
             ROMlib_breakoutioname, ROMlib_check_fork_sharing, ROMlib_destroy_blocks, ROMlib_fork_access_allowed,
             ROMlib_isresourcefork, ROMlib_maperrno, ROMlib_nami, ROMlib_record_fork_access, ROMlib_ufs_open_deny,
             ROMlib_newunixfrommac, ROMlib_open_internal_resource, ROMlib_resname, ROMlib_rewinddir,
             ROMlib_ufs_directory_count, ROMlib_ufs_volume_counts, SetEOF, SetFPos, fs_err_hook, ufsPBAllocContig,
             ufsPBAllocate, ufsPBClose, ufsPBFlushFile, ufsPBGetEOF, ufsPBGetFPos, ufsPBHOpen, ufsPBHOpenRF,
             ufsPBLockRange, ufsPBOpen, ufsPBOpenRF, ufsPBRead, ufsPBSetEOF, ufsPBSetFPos, ufsPBUnlockRange, ufsPBWrite,
             vlookupbyname

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below. The copy parameter block follows Universal Interfaces 3.4.2.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in FileMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"

#include "FileMgr.h"
#include "OSEvent.h"
#include "VRetraceMgr.h"
#include "OSUtil.h"
#include "DeviceMgr.h"
#include "MemoryMgr.h"

#include "rsys/hfs.h"

#include "rsys/file.h"
#include "rsys/glue.h"
#include "rsys/filedouble.h"
#include "rsys/segment.h"
#include "rsys/string.h"
#include "rsys/file.h"
#include "rsys/suffix_maps.h"
#include "rsys/lockunlock.h"
#include "rsys/time.h"

#include <ctype.h>
#include "rsys/system_resources.h"
#include "rsys/notmac.h"
#include "rsys/resource.h"
#include "rsys/user_profile.h"

#if defined(CYGWIN32)
#include "winfs.h"
#if defined(__MINGW32__)
#include <io.h>
#endif
#endif

int ROMlib_nosync = 0; /* if non-zero, we don't call sync () or fsync () */

#if !defined(NDEBUG)
PUBLIC void fs_err_hook(OSErr err)
{
}
#endif

PUBLIC int ROMlib_lasterrnomapped;

#define MAX_ERRNO 50

#define install_errno(uerr, merr)         \
    do {                                  \
        gui_assert(uerr < NELEM(xtable)); \
        xtable[uerr] = merr;              \
    } while (FALSE);

A0(PUBLIC, OSErr, ROMlib_maperrno) /* INTERNAL */
{
    OSErr retval;
    static OSErr xtable[MAX_ERRNO + 1];
    static char been_here = FALSE;
    int errno_save;

    if (!been_here) {
        int i;

        for (i = 0; i < (int)NELEM(xtable); ++i)
            xtable[i] = fsDSIntErr;

        install_errno(0, noErr);
        install_errno(EPERM, permErr);
        install_errno(ENOENT, fnfErr);
        install_errno(EIO, ioErr);
        install_errno(ENXIO, paramErr);
        install_errno(EBADF, fnOpnErr);
        install_errno(EAGAIN, fLckdErr);
        install_errno(ENOMEM, memFullErr);
        install_errno(EACCES, permErr);
        install_errno(EFAULT, paramErr);
        install_errno(EBUSY, fBsyErr);
        install_errno(EEXIST, dupFNErr);
        install_errno(EXDEV, fsRnErr);
        install_errno(ENODEV, nsvErr);
        install_errno(ENOTDIR, dirNFErr);
        install_errno(EINVAL, paramErr);
        install_errno(ENFILE, tmfoErr);
        install_errno(EMFILE, tmfoErr);
        install_errno(EFBIG, dskFulErr);
        install_errno(ENOSPC, dskFulErr);
        install_errno(ESPIPE, posErr);
        install_errno(EROFS, wPrErr);
        install_errno(EMLINK, dirFulErr);
#if !defined(CYGWIN32)
        install_errno(ETXTBSY, fBsyErr);
        install_errno(EWOULDBLOCK, permErr);
#endif

        been_here = TRUE;
    }

    errno_save = errno;
    ROMlib_lasterrnomapped = errno_save;

    if (errno_save < 0 || errno_save >= (int)NELEM(xtable))
        retval = fsDSIntErr;
    else
        retval = xtable[errno_save];

    if (retval == fsDSIntErr)
        warning_unexpected("fsDSIntErr errno = %d", errno_save);

    if (retval == dirNFErr)
        warning_trace_info("dirNFErr errno = %d", errno_save);

    fs_err_hook(retval);
    return retval;
}

A3(PUBLIC, OSErr, FSOpen, StringPtr, filen, INTEGER, vrn, /* IMIV-109 */
   INTEGER*, rn)
{
    ParamBlockRec pbr;
    OSErr temp;

    FIELD_ASSIGN_RAW(pbr.ioParam.ioNamePtr, RM(filen));
    pbr.ioParam.ioVRefNum = CW(vrn);
    pbr.ioParam.ioVersNum = 0;
    pbr.ioParam.ioPermssn = fsCurPerm;
    pbr.ioParam.ioMisc = 0;
    temp = PBOpen(&pbr, 0);
    *rn = CW(pbr.ioParam.ioRefNum);
    fs_err_hook(temp);
    return (temp);
}

A3(PUBLIC, OSErr, OpenRF, StringPtr, filen, INTEGER, vrn, /* IMIV-109 */
   INTEGER*, rn)
{
    ParamBlockRec pbr;
    OSErr temp;

    FIELD_ASSIGN_RAW(pbr.ioParam.ioNamePtr, RM(filen));
    pbr.ioParam.ioVRefNum = CW(vrn);
    pbr.ioParam.ioVersNum = 0;
    pbr.ioParam.ioPermssn = fsCurPerm;
    pbr.ioParam.ioMisc = 0;
    temp = PBOpenRF(&pbr, 0);
    *rn = CW(pbr.ioParam.ioRefNum);
    fs_err_hook(temp);
    return (temp);
}

A3(PUBLIC, OSErr, FSRead, INTEGER, rn, LONGINT*, count, /* IMIV-109 */
   Ptr, buffp)
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = CW(rn);
    FIELD_ASSIGN_RAW(pbr.ioParam.ioBuffer, RM(buffp));
    pbr.ioParam.ioReqCount = CL(*count);
    pbr.ioParam.ioPosMode = CWC(fsAtMark);
    temp = PBRead(&pbr, 0);
    *count = CL(pbr.ioParam.ioActCount);
    fs_err_hook(temp);
    return (temp);
}

PUBLIC OSErr FSReadAll(INTEGER rn, LONGINT* countp, Ptr buffp)
{
    LONGINT orig_count;
    OSErr retval;

    orig_count = *countp;
    retval = FSRead(rn, countp, buffp);
    if (retval == noErr && *countp != orig_count)
        retval = eofErr;
    return retval;
}

A3(PUBLIC, OSErr, FSWrite, INTEGER, rn, LONGINT*, count, /* IMIV-110 */
   Ptr, buffp)
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = CW(rn);
    FIELD_ASSIGN_RAW(pbr.ioParam.ioBuffer, RM(buffp));
    pbr.ioParam.ioReqCount = CL(*count);
    pbr.ioParam.ioPosMode = CWC(fsAtMark);
    temp = PBWrite(&pbr, 0);
    *count = CL(pbr.ioParam.ioActCount);
    fs_err_hook(temp);
    return (temp);
}

PUBLIC OSErr FSWriteAll(INTEGER rn, LONGINT* countp, Ptr buffp)
{
    LONGINT orig_count;
    OSErr retval;

    orig_count = *countp;
    retval = FSWrite(rn, countp, buffp);
    if (retval == noErr && *countp != orig_count)
        retval = ioErr;
    return retval;
}

A2(PUBLIC, OSErr, GetFPos, INTEGER, rn, LONGINT*, filep) /* IMIV-110 */
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = CW(rn);
    temp = PBGetFPos(&pbr, 0);
    *filep = CL(pbr.ioParam.ioPosOffset);
    fs_err_hook(temp);
    return (temp);
}

A3(PUBLIC, OSErr, SetFPos, INTEGER, rn, INTEGER, posmode, /* IMIV-110 */
   LONGINT, possoff)
{
    ParamBlockRec pbr;
    OSErr err;

    pbr.ioParam.ioRefNum = CW(rn);
    pbr.ioParam.ioPosMode = CW(posmode);
    pbr.ioParam.ioPosOffset = Cx(possoff);
    err = PBSetFPos(&pbr, 0);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, GetEOF, INTEGER, rn, LONGINT*, eof) /* IMIV-111 */
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = Cx(rn);
    temp = PBGetEOF(&pbr, 0);
    *eof = CL(pbr.ioParam.ioMisc);
    fs_err_hook(temp);
    return (temp);
}

A2(PUBLIC, OSErr, SetEOF, INTEGER, rn, LONGINT, eof) /* IMIV-111 */
{
    ParamBlockRec pbr;
    OSErr err;

    pbr.ioParam.ioRefNum = Cx(rn);
    pbr.ioParam.ioMisc = Cx(eof);
    err = PBSetEOF(&pbr, 0);
    fs_err_hook(err);
    return (err);
}

A2(PUBLIC, OSErr, Allocate, INTEGER, rn, LONGINT*, count) /* IMIV-112 */
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = Cx(rn);
    pbr.ioParam.ioReqCount = *count;
    temp = PBAllocate(&pbr, 0);
    *count = CL(pbr.ioParam.ioActCount);
    fs_err_hook(temp);
    return (temp);
}

A2(PUBLIC, OSErr, AllocContig, INTEGER, rn, LONGINT*, count)
{
    ParamBlockRec pbr;
    OSErr temp;

    pbr.ioParam.ioRefNum = Cx(rn);
    pbr.ioParam.ioReqCount = *count;
    temp = PBAllocContig(&pbr, 0);
    *count = CL(pbr.ioParam.ioActCount);
    fs_err_hook(temp);
    return (temp);
}

A1(PUBLIC, OSErr, FSClose, INTEGER, rn) /* IMIV-112 */
{
    ParamBlockRec pbr;
    OSErr err;

    pbr.ioParam.ioRefNum = Cx(rn);
    err = PBClose(&pbr, 0);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, VCB*, vlookupbyname, const char*, namep, const char*, endp)
{
    VCB* retval;
    int n;

    n = endp - namep;
    for (retval = (VCB*)PPR(VCBQHdr.qHead); retval; retval = (VCB*)PPR(retval->qLink))
        if (n == retval->vcbVN[0] && strncmp(namep, (char*)retval->vcbVN + 1, n) == 0)
            return retval;

#if defined(CYGWIN32)
    for (retval = (VCB*)PPR(VCBQHdr.qHead); retval; retval = (VCB*)PPR(retval->qLink))
        if ((n == retval->vcbVN[0] && strncasecmp(namep, (char*)retval->vcbVN + 1, n) == 0)
            /* Preserve drive-letter and historical C/ aliases after renaming. */
            || (!((HVCB*)retval)->vcbCTRef && (n == 1 || (n == 2 && namep[1] == '/')) &&
                strlen(((VCBExtra*)retval)->unixname) == 3 && ((VCBExtra*)retval)->unixname[1] == ':' &&
                ((VCBExtra*)retval)->unixname[2] == '/' &&
                toupper((unsigned char)namep[0]) == toupper((unsigned char)((VCBExtra*)retval)->unixname[0])))
            return retval;
#endif
    return 0;
}

A1(PRIVATE, VCB*, vlookupbydrive, INTEGER, drive)
{
    VCB* retval;

    for (retval = (VCB*)PPR(VCBQHdr.qHead); retval; retval = (VCB*)PPR(retval->qLink))
        if (drive == Cx(retval->vcbDrvNum))
            return retval;
    return 0;
}

PRIVATE LONGINT cacheindex = 0x7fffffff;

A0(PUBLIC, void, ROMlib_rewinddir)
{
    cacheindex = 0x7fffffff;
}

/*
 * Blech!  I've finally caved in and am going to look for the magic
 * number in the obnoxious case of a file that starts with %XX where XX
 * are two upper case hex digits.
 */

PUBLIC BOOLEAN ROMlib_isresourcefork(const char* fullname)
{
    LONGINT fd;
    LONGINT magic;
    BOOLEAN retval;
    const char* filename;

    filename = strrchr(fullname, '/');
    filename = filename ? filename + 1 : fullname;
    /* The legacy drive-root record remains metadata under every sidecar
     convention, including netatalk's subdirectory convention. */
    if (!strcmp(filename, "%%2F")) {
        fd = Uopen(fullname, O_BINARY | O_RDONLY, 0);
        retval = fd >= 0 && read(fd, &magic, sizeof magic) == sizeof magic &&
                 (magic == CLC(DOUBLEMAGIC) || magic == DOUBLEMAGIC);
        if (fd >= 0)
            Uclose(fd);
        return retval;
    }
    if (!strncmp(filename, "._", 2)) {
        fd = Uopen(fullname, O_BINARY | O_RDONLY, 0);
        retval = fd >= 0 && read(fd, &magic, sizeof(magic)) == sizeof(magic) && magic == CLC(DOUBLEMAGIC);
        if (fd >= 0)
            Uclose(fd);
        return retval;
    }

    /* Percent companions may coexist with subdirectory fork conventions. */

    if (filename[0] != '%')
        retval = FALSE;
    else {
        filename = strrchr(fullname, '/');
        if (!filename)
            filename = fullname;
        else
            ++filename;
        if (filename[0] != '%')
            retval = FALSE;
        else if (!isxdigit(filename[1]) || !isxdigit(filename[2]))
            retval = TRUE;
        else {
            fd = -1;
            retval = (fd = Uopen(fullname, O_BINARY | O_RDONLY, 0)) >= 0 &&
                     read(fd, (void*)&magic, sizeof(magic)) == sizeof(magic) &&
                     (magic == CLC(DOUBLEMAGIC) || magic == DOUBLEMAGIC);
            if (fd >= 0)
                Uclose(fd);
        }
    }
    return retval;
}

int ROMlib_catalog_skip_counts;

static boolean_t dislike_name(char* namep)
{
    boolean_t retval;

    if (strcmp(namep, ".") == 0 || strcmp(namep, "..") == 0 || strcmp(namep, ".AppleDouble") == 0 ||
        strcmp(namep, ".AppleDesktop") == 0 || strcmp(namep, ".finf") == 0 || strcmp(namep, ".rsrc") == 0 ||
        strcmp(namep, ".DS_Store") == 0 || namep[0] == '.')
        retval = TRUE;
    else {
        char* buf;
        int namelen;

        namelen = strlen(namep);
        buf = alloca(namelen);
        memcpy(buf, namep, namelen);
        namelen -= ROMlib_UNIX7_to_Mac(buf, namelen);
        retval = namelen > 31;
    }
    return retval;
}

/* Count catalog entries rather than statfs.f_files (an inode capacity on Unix
   and historically the constant ten on Windows). Finder uses ioVFilCnt as
   the denominator of its Desktop rebuild progress bar. Independent streams
   preserve active indexed catalog enumeration. Never follow host symlinks or
   Windows junctions back into an ancestor. */
static void volume_counts(const char* directory, unsigned depth, uint32* files, uint32* directories)
{
    if (depth >= 128)
        return;
    struct stat info;
    ROMlib_orphan_scan* orphans = ROMlib_orphan_scan_open(directory);
    const char* name;
    while ((name = ROMlib_orphan_scan_next(orphans, &info)))
        if (!dislike_name((char*)name) && *files < INT32_MAX)
            ++*files;
    ROMlib_orphan_scan_close(orphans);
    DIR* stream = Uopendir(directory);
    if (!stream)
        return;
#if defined(USE_STRUCT_DIRECT)
    struct direct* entry;
#else
    struct dirent* entry;
#endif
    size_t length = strlen(directory);
    while ((entry = readdir(stream))) {
        if (dislike_name(entry->d_name))
            continue;
        size_t name_length = strlen(entry->d_name);
        if (length > SIZE_MAX - name_length - 2)
            break;
        char* path = malloc(length + name_length + 2);
        if (!path)
            break;
        memcpy(path, directory, length);
        path[length] = '/';
        memcpy(path + length + 1, entry->d_name, name_length + 1);
        if (!ROMlib_isresourcefork(path) && Ustat(path, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                if (*directories < INT32_MAX)
                    ++*directories;
                boolean_t follow;
#if defined(__MINGW32__)
                struct _finddata_t attributes;
                intptr_t search = _findfirst(path, &attributes);
                follow = search != -1 && !(attributes.attrib & 0x400);
                if (search != -1)
                    _findclose(search);
#else
                struct stat link_info;
                follow = lstat(path, &link_info) == 0 && !S_ISLNK(link_info.st_mode);
#endif
                if (follow)
                    volume_counts(path, depth + 1, files, directories);
            }
            else if (*files < INT32_MAX)
                ++*files;
        }
        free(path);
    }
    closedir(stream);
}

PUBLIC void ROMlib_ufs_volume_counts(const char* directory, uint32* files, uint32* directories)
{
    *files = *directories = 0;
    volume_counts(directory, 0, files, directories);
}

/* Count the same visible entries accepted by dirindex, using an independent
   stream so catalog queries do not disturb an active indexed enumeration. */
PUBLIC uint16 ROMlib_ufs_directory_count(const char* directory)
{
    uint16 orphans = 0;
    struct stat orphan_info;
    ROMlib_orphan_scan* scan = ROMlib_orphan_scan_open(directory);
    const char* orphan;
    while (orphans < 65535 && (orphan = ROMlib_orphan_scan_next(scan, &orphan_info)))
        if (!dislike_name((char*)orphan))
            ++orphans;
    ROMlib_orphan_scan_close(scan);
    if (orphans == 65535)
        return orphans;
#if defined(__MINGW32__)
    /* Windows directory enumeration already supplies existence and attributes.
     Calling stat for every ordinary entry repeats network metadata requests
     for information returned by the directory server in the same batch. */
    size_t length = strlen(directory);
    if (length > SIZE_MAX - 3)
        return orphans;
    char* pattern = malloc(length + 3);
    if (!pattern)
        return 0;
    memcpy(pattern, directory, length);
    memcpy(pattern + length, "/*", 3);
    struct _finddata_t entry;
    intptr_t stream = _findfirst(pattern, &entry);
    free(pattern);
    if (stream == -1)
        return 0;
    uint16 count = orphans;
    char* path = NULL;
    size_t capacity = 0;
    do {
        if (dislike_name(entry.name))
            continue;
        size_t needed = length + strlen(entry.name) + 2;
        if (needed > capacity) {
            char* grown = realloc(path, needed);
            if (!grown)
                break;
            path = grown;
            capacity = needed;
        }
        memcpy(path, directory, length);
        path[length] = '/';
        strcpy(path + length + 1, entry.name);
        if (ROMlib_isresourcefork(path))
            continue;
        /* Reparse targets may be dangling: retain stat's existing semantics. */
        struct stat info;
        if (!(entry.attrib & 0x400) || Ustat(path, &info) == 0)
            ++count;
    } while (count < 65535 && _findnext(stream, &entry) == 0);
    free(path);
    _findclose(stream);
    return count;
#else
    DIR* stream = Uopendir(directory);
#if defined(USE_STRUCT_DIRECT)
    struct direct* entry;
#else
    struct dirent* entry;
#endif
    uint16 count = orphans;
    char* path;
    size_t length = strlen(directory);
    if (!stream)
        return 0;
    while (count < 65535 && (entry = readdir(stream)))
        if (!dislike_name(entry->d_name)) {
            struct stat info;
            size_t name_length = strlen(entry->d_name);
            path = malloc(length + name_length + 2);
            if (!path)
                break;
            memcpy(path, directory, length);
            path[length] = '/';
            memcpy(path + length + 1, entry->d_name, name_length + 1);
            if (!ROMlib_isresourcefork(path) && Ustat(path, &info) == 0)
                ++count;
            free(path);
        }
    closedir(stream);
    return count;
#endif
}

A4(PRIVATE, char*, dirindex, char*, dir, LONGINT, index, BOOLEAN, nodirectories, struct stat*, sbufp)
{
    THz saveZone;
    static char *cachedir = 0, *cachedirend;
    static DIR* dirp = 0;
    static ROMlib_orphan_scan* orphans;
    static char result[MAXPATHLEN];
    const char* orphan;
#if defined(USE_STRUCT_DIRECT)
    struct direct* dp;
#else
    struct dirent* dp;
#endif
    int dirnamelen;
    struct stat sbuf;

#if defined(CYGWIN32)
    while (dir[0] == '/' && dir[1] == '/')
        ++dir;
#endif

    if (!sbufp)
        sbufp = &sbuf;
    if (index <= cacheindex || !cachedir || strcmp(dir, cachedir) != 0) {
        if (cachedir)
            free(cachedir);
        if (dirp)
            closedir(dirp);
        ROMlib_orphan_scan_close(orphans);
        orphans = ROMlib_orphan_scan_open(dir);
        dirp = Uopendir(dir);
        if (!dirp) {
            dp = 0;
            goto DONE;
        }
        cacheindex = 0;
        dirnamelen = strlen(dir);

#if !defined(MAXNAMLEN)
#define MAXNAMLEN 1024
#endif /* !defined(MAXNAMLEN) */

        saveZone = TheZone;
        HIDDEN_RAW_SET(TheZone_H, SysZone);
        cachedir = malloc(dirnamelen + 1 + MAXNAMLEN + 1);
        HIDDEN_RAW_SET(TheZone_H, saveZone);
        if (!cachedir)
            /*-->*/ return 0;
        strcpy(cachedir, dir);
        cachedirend = cachedir + dirnamelen;
    }
    *cachedirend = '/';
    do {
        if ((dp = readdir(dirp))) {
            strcpy(cachedirend + 1, dp->d_name);
            if (!dislike_name(dp->d_name) && !ROMlib_isresourcefork(cachedir) && Ustat(cachedir, sbufp) == 0) {
                if (!nodirectories || !S_ISDIR(sbufp->st_mode))
                    ++cacheindex;
            }
        }
    } while (dp && cacheindex != index);
    *cachedirend = 0;
    if (!dp)
        while ((orphan = ROMlib_orphan_scan_next(orphans, sbufp)))
            if (!dislike_name((char*)orphan) && ++cacheindex == index) {
                strcpy(result, orphan);
                return result;
            }
DONE:
    return dp ? dp->d_name : 0;
}

/*
 * NOTE: we must use capital letters in tohex below.  Code depends on it.
 */

A1(PRIVATE, unsigned char, tohex, unsigned char, c)
{
    unsigned char retval;

    retval = (unsigned char)(netatalk_conventions_p ? "0123456789abcdef" : "0123456789ABCDEF")[c & 0xF];
    return retval;
}

A3(PRIVATE, int, Mac_to_UNIX7, unsigned char*, name, INTEGER, length, unsigned char*, out)
{
    unsigned char c;
    int retval;
    boolean_t last_character_was_colon;
#if defined(CYGWIN32)
    const unsigned char *end = name + length, *tail = name;
#endif

    retval = length;
    last_character_was_colon = TRUE;
    while (--length >= 0) {
#if defined(CYGWIN32)
        if (last_character_was_colon) {
            /* Win32 trims terminal spaces/dots, including in parent components.
             Find each component's terminal run once, keeping this linear even
             for a long name consisting entirely of spaces and periods. */
            tail = memchr(name, ':', end - name);
            if (!tail)
                tail = end;
            while (tail > name && (tail[-1] == ' ' || tail[-1] == '.'))
                --tail;
        }
#endif
        c = *name++;
        if (NEEDTOMAP(c)
#if defined(CYGWIN32)
            || ((c == ' ' || c == '.') && name > tail)
#endif
        ) {
            *out++ = apple_double_quote_char;
            *out++ = tohex(c >> 4);
            *out++ = tohex(c);
            retval += 2;
        }
        else if (c == ':') {
            if (last_character_was_colon) {
                *out++ = '.';
                *out++ = '.';
                retval += 2;
            }
            *out++ = '/';
        }
        else
            *out++ = c;
        last_character_was_colon = c == ':';
    }
    return retval;
}

A2(PUBLIC, char*, ROMlib_newunixfrommac, char*, ip, INTEGER, n)
{
    char* retval;

    if (n < 0 || (!ip && n))
        return NULL;
    if (!n)
        return strdup("");
    if ((retval = malloc(3 * n + 1))) /* worst case numbers */
        retval[Mac_to_UNIX7((unsigned char*)ip, n, (unsigned char*)retval)] = 0;
    return retval;
}

/*
 * ROMlib_breakoutioname's role is to examine ioNamePtr and vRefNum
 * to determine the vcbp, but since it has to look at ioNamePtr
 * anyway it makes sense to make a unix partial pathname for the
 * rest.  While we're at it, if we get passed in a working-director
 * instead of a TRUE vrn we might as well note the directory id
 * that is associated with it.
 */

/*
 * BUG Fix TODO: newunixfrommac needs to be more complex; specifically
 *		 it needs to handle :: properly (which is to go up
 *		 a directory, UNLESS it's already at the top).
 */

A5(PUBLIC, VCB*, ROMlib_breakoutioname, ParmBlkPtr, pb, /* INTERNAL */
   LONGINT*, diridp, char**, therestp, BOOLEAN*, fullpathp, BOOLEAN, usedefault)
{
    VCB* retval;
    char* colon;
    unsigned char* p;
    INTEGER v;
    wdentry* wdp;

    retval = 0;
    *diridp = 0;
    *therestp = 0;
    if (fullpathp)
        *fullpathp = FALSE;
    if ((p = (unsigned char*)PPR(pb->ioParam.ioNamePtr))) {
        if (p[0] > 1 && p[1] != ':') {
            /* A Macintosh volume name can now be one drive letter (D:).
                   Skip the drive colon only for a native Windows D:/ path. */
            int colon_start = 0;
#if defined(CYGWIN32)
            if (p[0] >= 3 && p[2] == ':' && (p[3] == '/' || p[3] == '\\'))
                colon_start = 2;
#endif
            if ((colon = pstr_index_after(p, ':', colon_start))) {
                retval = vlookupbyname((char*)p + 1, colon);
                if (colon - ((char*)p + 1) < p[0])
                    *therestp = ROMlib_newunixfrommac(colon + 1, p[0] + (char*)p - colon);
                if (fullpathp)
                    *fullpathp = TRUE;
            }
            else
                *therestp = ROMlib_newunixfrommac((char*)p + 1, p[0]);
        }
        else if (p[0] > 0) {
            if (p[1] == ':' || (p[0] == 1 && p[1] == '/'))
                *therestp = ROMlib_newunixfrommac((char*)p + 2, p[0] - 1);
            else
                *therestp = ROMlib_newunixfrommac((char*)p + 1, p[0]);
        }
    }
    if (!retval) {
        if ((v = Cx(pb->ioParam.ioVRefNum)) > 0) /* drive number */
            retval = vlookupbydrive(v);
        else if (v < 0) {
            if (ISWDNUM(v)) {
                wdp = WDNUMTOWDP(v);
                *diridp = CL(wdp->dirid);
                retval = PPR(wdp->vcbp);
            }
            else
                retval = ROMlib_vcbbyvrn(v);
        }
        if (!retval && (usedefault || (!PP_RAW(pb->ioParam.ioNamePtr) && !pb->ioParam.ioVRefNum))) {
            retval = MR(DefVCBPtr);
            *diridp = CL(DefDirID);
        }
    }
    return retval;
}

/*
 * myscandir looks through a directory for a filename match with case not
 *	     significant.  It returns TRUE if a match was found; FALSE
 *	     otherwise.  Note, the filename specified in "rest" starts
 *	     out smashed and needs the character stored in "save" to
 *	     be filled in before it can be used, but this has to be
 *	     done after "dirname" has been used.
 */

A4(PRIVATE, BOOLEAN, myscandir, char*, dirname, char, save, char*, rest, INTEGER, restlen)
{
    DIR* dirp;
#if defined(USE_STRUCT_DIRECT)
    struct direct* directp;
#else
    struct dirent* directp;
#endif
    int retval;
    LONGINT d0;
#if defined(CYGWIN32)
    char *wanted = NULL, *candidate = NULL, *actual = NULL;
    int wanted_length = 0, matches = 0;
#endif

#if defined(CYGWIN32)
    while (dirname[0] == '/' && dirname[1] == '/')
        ++dirname;
#endif

    retval = FALSE;
#if defined(CYGWIN32)
    /* A preceding component may just have been recovered from an escaped
     name. Retry the current prefix, including the Unicode Icon<CR> alias,
     before consulting ANSI directory entries that cannot name that file. */
    {
        struct stat info;
        char boundary = rest[restlen];
        *rest = save;
        rest[restlen] = 0;
        int exists = Ustat(dirname, &info) == 0;
        rest[restlen] = boundary;
        if (exists)
            return TRUE;
        *rest = 0;
    }
#endif
    if ((dirp = Uopendir(dirname))) {
        *rest = save;
        d0 = ((LONGINT)restlen << 16) | (unsigned short)restlen;
#if defined(CYGWIN32)
        wanted = malloc(restlen + 1);
        candidate = malloc(restlen + 1);
        actual = malloc(restlen + 1);
        if (wanted && candidate && actual) {
            memcpy(wanted, rest, restlen);
            wanted_length = restlen - ROMlib_UNIX7_to_Mac(wanted, restlen);
        }
#endif
        while ((directp = readdir(dirp))) {
            int name_length = strlen(directp->d_name);
            if (restlen == name_length &&
                ROMlib_RelString((unsigned char*)rest, (unsigned char*)directp->d_name, FALSE, TRUE, d0) == 0) {
                retval = TRUE;
                memcpy(rest, directp->d_name, restlen);
                break;
            }
#if defined(CYGWIN32)
            /* Compare display names, but retain the exact host spelling. Never
             guess between two entries that normalize to the same name. The
             recovered component must fit the existing escaped path buffer. */
            if (wanted_length && name_length <= restlen) {
                memcpy(candidate, directp->d_name, name_length);
                int length = name_length - ROMlib_UNIX7_to_Mac(candidate, name_length);
                if (length == wanted_length && !ROMlib_RelString((unsigned char*)wanted, (unsigned char*)candidate,
                                                                 FALSE, TRUE, (length << 16) | length)) {
                    ++matches;
                    memcpy(actual, directp->d_name, name_length + 1);
                }
            }
#endif
        }
#if defined(CYGWIN32)
        if (!retval && matches == 1) {
            size_t length = strlen(actual);
            memmove(rest + length, rest + restlen, strlen(rest + restlen) + 1);
            memcpy(rest, actual, length);
            retval = TRUE;
        }
        free(actual);
        free(candidate);
        free(wanted);
#endif
        closedir(dirp);
    }
    else
        *rest = save;
    if (!retval) {
        *rest = 0;
        ROMlib_orphan_scan* scan = ROMlib_orphan_scan_open(dirname);
        struct stat info;
        const char* name;
        *rest = save;
        while ((name = ROMlib_orphan_scan_next(scan, &info)))
            if (strlen(name) == restlen && !ROMlib_RelString((unsigned char*)rest, (unsigned char*)name, FALSE, TRUE,
                                                             ((LONGINT)restlen << 16) | restlen)) {
                memcpy(rest, name, restlen);
                retval = TRUE;
                break;
            }
        ROMlib_orphan_scan_close(scan);
    }
    return retval;
}

A2(PRIVATE, LONGINT, weasel, char*, fullpathname, struct stat*, sbufp)
{
    char *start, *nextslash, save;
    INTEGER length;
    LONGINT retval;

    gui_assert(fullpathname[SLASH_CHAR_OFFSET] == '/');

    if (ROMlib_ufs_stat(fullpathname, sbufp) < 0) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:
            start = fullpathname + SLASH_CHAR_OFFSET + 1;
            break;
        default:
            return errno;
            break;
        }
    }
    else
        return 0;
    do {
        nextslash = strchr(start, '/');
        if (nextslash)
            length = nextslash - start;
        else
            length = strlen(start);
        save = *start;
        *start = 0;
        if (!myscandir(fullpathname, save, start, length)) {
            if (!nextslash && ROMlib_ufs_stat(fullpathname, sbufp) == 0)
                return 0;
            return nextslash ? ENOTDIR : ENOENT;
        }
        nextslash = strchr(start, '/');
        if (nextslash)
            start = nextslash + 1;
    } while (nextslash);
    if (ROMlib_ufs_stat(fullpathname, sbufp) < 0) {
        retval = errno;
        return retval;
    }
    else
        return 0;
}

/*
 * NOTE: As of today, ROMlib_nami always either returns an error, or leaves
 *	 a valid result in *sbufp (if sbufp is non-zero).  That means that
 *	 you can call ROMlib_nami and not have to do a stat afterward.
 */

A9(PUBLIC, OSErr, ROMlib_nami, ParmBlkPtr, pb, LONGINT, dir, /* INTERNAL */
   IndexType, indextype, char**, pathname, char**, filename, char**, endname, BOOLEAN, nodirs, VCBExtra**, vcbpp,
   struct stat*, sbufp)
{
    char *temp1, *temp2, *temp3;
    INTEGER index;
    StringPtr fnamep;
    Str255 fname;
    LONGINT dirid;
    char* therest;
    char* fn;
    VCBExtra* vcbp;
    datum content;
    char* vcbanddir;
    int vcbstrlen, therestlen;
    StringPtr savenamep;
    BOOLEAN fullp;
    struct stat sbuf;
    INTEGER badness;
    OSErr err;
    ALLOCABEGIN

    if (!pathname)
        pathname = &temp1;
    if (!filename)
        filename = &temp2;
    if (!endname)
        endname = &temp3;
    fnamep = 0;
    if (indextype != NoIndex) {
        savenamep = PPR(pb->ioParam.ioNamePtr);
        PP_RAW(pb->ioParam.ioNamePtr) = 0;
    }
#if !defined(LETGCCWAIL)
    else
        savenamep = (StringPtr)-1;
#endif /* LETGCCWAIL */
    vcbp = (VCBExtra*)ROMlib_breakoutioname(pb, &dirid, &therest, &fullp, TRUE);
    if (vcbpp)
        *vcbpp = vcbp;
    /* A root FSSpec uses parent ID 1 plus the volume name, not a child
       under the mounted host root. Treat it like directory ID 2. */
    if (vcbp && dir == 1 && indextype == NoIndex && !fullp) {
        StringPtr root_name = PPR(pb->ioParam.ioNamePtr);
        int n = root_name ? root_name[0] : 0;
        if (n && n == vcbp->vcb.vcbVN[0] &&
            ROMlib_RelString(root_name + 1, vcbp->vcb.vcbVN + 1, FALSE, TRUE, (n << 16) | n) == 0) {
            free(therest);
            therest = NULL;
            dir = 2;
        }
    }

    if (fullp)
        dir = 0;
    else {
        if (dir <= 0)
            dir = dirid;
    }
    content.dsize = 0;
    /*
     * Retry strategy:
     *	first time things don't work we try closing and reopening
     *	the database (badness == 1); second time we try closing,
     *	rebuilding and reopening the database (badness == 2).
     */
    badness = 0;
    /* do { */
loop:
    if (dir > 2) {
        content = ROMlib_dbm_fetch(vcbp, dir);
        if (!content.dptr) {
            content.dsize = 0;
        }
    }
    vcbstrlen = strlen(vcbp->unixname);
    vcbanddir = ALLOCA(vcbstrlen + content.dsize + 2);
    BlockMove((Ptr)vcbp->unixname, (Ptr)vcbanddir, (Size)vcbstrlen + 1);
    if (content.dsize) {
        if (vcbanddir[vcbstrlen - 1] != '/') {
            vcbanddir[vcbstrlen] = '/';
            ++vcbstrlen;
        }
        BlockMove((Ptr)content.dptr, (Ptr)vcbanddir + vcbstrlen, (Size)content.dsize);
        vcbanddir[vcbstrlen + content.dsize] = 0;
        if (Ustat(vcbanddir, &sbuf) < 0) {
            warning_unexpected("vcbanddir = '%s', dir = 0x%x", vcbanddir, dir);
            ROMlib_dbm_delete_inode(vcbp, dir);
            /*-->*/ goto loop;
        }
        else if (ST_INO(sbuf) != dir) {
#if 1
            warning_unexpected("ST_INO(sbuf) = 0x%lx, dir = 0x%x", (unsigned long)ST_INO(sbuf), dir);
            ROMlib_dbm_delete_inode(vcbp, dir);
            {
                LONGINT templong;

                templong = ST_INO(sbuf);
                ROMlib_dbm_store(vcbp, vcbanddir, &templong, TRUE);
            }
            /*-->*/ goto loop;
#else
            fprintf(stderr, "ino mismatch %s %ld %d\n", vcbanddir, (long)ST_INO(sbuf), dir);
            ++badness;
#endif
        }
        else
            badness = 0;
    }
    else if (dir > 2)
        ++badness;
    if (badness == 1 && dirid) {
        dir = dirid;
        badness = 2;
        goto loop;
    }
    if (badness && badness <= 1)
        goto loop;
    /* } while (badness && badness <= 1); */
    if (badness) {
        err = dirNFErr;
        warning_trace_info("badness = %d, dirid = %d, dir = %d, "
                           "vcbp->unixname = '%s', therest = '%s', "
                           "fullp = %d, vcbanddir = '%s'",
                           badness, dirid, dir, vcbp->unixname, therest, fullp, vcbanddir);
        fs_err_hook(err);
        free(therest);
        /*-->*/ return err;
    }
    if (indextype != NoIndex)
        PP_RAW(pb->ioParam.ioNamePtr) = RM(savenamep);
    if (indextype == FDirIndex && (index = Cx(pb->fileParam.ioFDirIndex)) > 0) {
        free(therest);
        therest = 0;
        if (!(fn = dirindex(vcbanddir, index, nodirs, sbufp))) {
            ALLOCAEND
            err = fnfErr;
            fs_err_hook(err);
            /*-->*/ return err;
        }
        if (PP_RAW(pb->ioParam.ioNamePtr))
            fnamep = PPR(pb->ioParam.ioNamePtr);
        else
            fnamep = fname;
        fnamep[0] = strlen(fn);
        strcpy((char*)fnamep + 1, fn);
        therest = malloc((Size)fnamep[0] + 1);
        BlockMove((Ptr)fnamep + 1, (Ptr)therest, (Size)fnamep[0]);
        therest[fnamep[0]] = 0;
    }
    therestlen = therest ? strlen(therest) : 0;
    *pathname = malloc(therestlen + strlen(vcbanddir) + 2);
    *filename = *pathname + strlen(vcbanddir) + 1;
    *endname = *filename + therestlen + 1;
    BlockMove((Ptr)vcbanddir, (Ptr)*pathname, (Size)strlen(vcbanddir));
    if (therest) {
        if (vcbanddir[1 + SLASH_CHAR_OFFSET]) /* i.e. test for "/" */
            (*filename)[-1] = '/';
        else {
            --*filename;
            --*endname;
        }
        BlockMove((Ptr)therest, (Ptr)*filename, (Size)therestlen);
        (*endname)[-1] = 0;
    }
    else {
        *endname = *filename;
        (*endname)[-1] = 0;
    }
    if (strcmp(*pathname + SLASH_CHAR_OFFSET, "/") != 0) {
        while ((*endname)[-2] == '/') { /* remove trailing slashes */
            (*endname)[-2] = 0;
            --*endname;
        }
        if ((*filename = strrchr(*pathname, '/')))
            ++*filename;
        else
            *filename = *pathname;
    }
    else
        *filename = *pathname;
    if (therest)
        free(therest);
    ALLOCAEND
    if (indextype == NoIndex && sbufp) {
        errno = weasel(*pathname, sbufp);
        err = ROMlib_maperrno();
        if (err == noErr) {
            /* Host-name recovery can shorten an escaped component, moving
             every subsequent component and its terminating NUL. */
            char* slash = strrchr(*pathname, '/');
            *filename = slash && slash[1] ? slash + 1 : *pathname;
            *endname = *pathname + strlen(*pathname) + 1;
        }
    }
    else if (indextype == IGNORENAME && sbufp) {
        if (Ustat(*pathname, sbufp) < 0) {
            err = ROMlib_maperrno();
        }
        else
            err = noErr;
    }
    else
        err = noErr;
    fs_err_hook(err);
    if (err == noErr && fullp)
        ROMlib_automount(*pathname);
    return err;
}

/*
 * Old algorithm:
 *
 * The new code is a little bit tricky; it would be just prepend a %, except
 * if someone has a name that begins with two hex digits ("feed me") you would
 * get something that could be a data fork, so we make sure that if the first
 * char is a % and the next two are hex and the hex would make something that
 * we would have wanted to map then we map the second character even though
 * we normally wouldn't map it.
 *
 * New algorithm:
 *
 * We just prepend the bloody "%" -- We get into trouble with other providers
 * of Apple Double if we do it differently.  What a crock.
 */

A3(PUBLIC, char*, ROMlib_resname, char*, pathname, /* INTERNAL */
   char*, filename, char*, endname)
{
    int pathnamesize;
    char* newname;

    pathnamesize = filename - pathname;
    if (pathnamesize)
        newname = ROMlib_sidecar_name(pathname, filename);
    else {
        newname = romaw_root_metadata_path(pathname);
    }
    return newname;
}

A3(PRIVATE, char*, old_percent_resname, char*, pathname, char*, filename, char*, endname)
{
    int pathnamesize, filenamesize, newsize;
    char* newname;

    pathnamesize = filename - pathname;
    filenamesize = endname - filename;
    if (pathnamesize) {
        newsize = pathnamesize + filenamesize + 1;
        newname = malloc(newsize);
        memcpy(newname, pathname, pathnamesize);
        newname[pathnamesize] = '%';
        memcpy(newname + pathnamesize + 1, filename, filenamesize);
    }
    else {
        newname = romaw_root_metadata_path(pathname);
    }
    return newname;
}

/*
 * macopen opens the file using mac permissions, returning an OSErr
 * the file descriptor of the opened file is filled in where fdp points
 */

A5(PRIVATE, OSErr, macopen, char*, file, short, perm, LONGINT*, fdp, BOOLEAN, isres, Byte*, flagp)
{
    int newperm;
    int flockret;
    boolean_t open_should_fail_p;
    OSErr err;

    switch (perm) {
    default:
        warning_unexpected("perm = %d", perm);
    /* FALL THROUGH */
    case fsCurPerm:
        newperm = O_RDWR; /* start out here, downgrade if necessary */
        *flagp |= fcwriteperm;
        break;
    case fsRdPerm:
        newperm = O_RDONLY;
        break;
    case fsWrPerm:
        *flagp |= fcwriteperm;
        newperm = isres ? O_RDWR : O_WRONLY;
        break;
    case fsRdWrPerm:
        *flagp |= fcwriteperm;
        newperm = O_RDWR;
        break;
    case fsRdWrShPerm:
        *flagp |= fcwriteperm;
        newperm = O_RDWR;
        break;
    }

#if defined(BROKEN_OPEN)
    {
        struct stat sbuf;

        open_should_fail_p =
            ((newperm == O_WRONLY || newperm == O_RDWR) && Ustat(file, &sbuf) == 0 && (!(sbuf.st_mode & S_IWUSR)));
    }
#else
    open_should_fail_p = FALSE;
#endif
    if (open_should_fail_p)
        *fdp = -1;
    else
        *fdp = Uopen(file, O_BINARY | newperm, 0);
    if (*fdp == -1 && perm == fsCurPerm) {
        *fdp = Uopen(file, O_BINARY | O_RDONLY, 0);
        *flagp &= ~fcwriteperm;
    }
    flockret = 0;
    err = *fdp == -1 ? ROMlib_maperrno() : noErr;
    fs_err_hook(err);
    return err;
}

enum { fork_access_recorded = 0x40, fork_access_explicit = 0x80, fork_access_exclusive = 4, fork_access_mask = 3 };
/* Guest FCB layouts remain fixed. Access metadata is indexed by the reference
   number, and a slot is ignored as soon as the FCB is closed. */
static Byte fork_access[INT16_MAX + 1];

static unsigned permission_access(SignedByte permission)
{
    if (permission == fsRdPerm)
        return file_access_read;
    if (permission == fsWrPerm)
        return file_access_write;
    return file_access_read | file_access_write;
}

static OSErr fork_sharing_conflict(VCB* volume, LONGINT file_id, bool resource, unsigned requested, INTEGER* existing)
{
    const Byte* base = (const Byte*)MR(FCBSPtr);
    const uint16 size = CW(*(const uint16*)base);
    const uint16 stride = CW(FSFCBLen);
    if (stride < sizeof(filecontrolblock))
        return noErr;
    for (unsigned ref = 2; ref + stride <= size && ref < NELEM(fork_access); ref += stride) {
        const filecontrolblock* file = (const filecontrolblock*)(base + ref);
        if (!file->fcbFlNum || file->fcbFlNum != file_id || (VCB*)PPR(file->fcbVPtr) != volume ||
            ((file->fcbMdRByt & RESOURCEBIT) != 0) != resource)
            continue;
        const unsigned active = fork_access[ref] & fork_access_recorded
                                    ? fork_access[ref]
                                    : file_access_read | ((file->fcbMdRByt & WRITEBIT) ? file_access_write : 0);
        if (((active >> 4) & requested & fork_access_mask) ||
            ((requested >> 4) & active & fork_access_mask))
            return permErr;
        if (existing && (requested & file_access_write) && (active & fork_access_exclusive)) {
            *existing = ref;
            return opWrErr;
        }
    }
    return noErr;
}

OSErr ROMlib_check_fork_sharing(VCB* volume, LONGINT file_id, bool resource, int modes, SignedByte* permission,
                               INTEGER* existing)
{
    if (modes != file_open_regular)
        return fork_sharing_conflict(volume, file_id, resource, modes, existing);
    OSErr error = fork_sharing_conflict(volume, file_id, resource, permission_access(*permission), NULL);
    if (error && *permission == fsCurPerm) {
        error = fork_sharing_conflict(volume, file_id, resource, file_access_read, NULL);
        if (!error)
            *permission = fsRdPerm;
    }
    return error;
}

void ROMlib_record_fork_access(INTEGER reference, int modes, SignedByte permission, bool writable)
{
    if (reference <= 0)
        return;
    if (modes != file_open_regular)
        fork_access[reference] = fork_access_recorded | fork_access_explicit | modes;
    else {
        const unsigned access = writable ? permission_access(permission) : file_access_read;
        const unsigned exclusive = writable && permission != fsRdWrShPerm ? fork_access_exclusive : 0;
        fork_access[reference] = fork_access_recorded | access | exclusive;
    }
}

bool ROMlib_fork_access_allowed(INTEGER reference, unsigned access)
{
    return reference <= 0 || !(fork_access[reference] & fork_access_explicit) ||
           !ROMlib_refnumtofcbp(reference) || (fork_access[reference] & access);
}

A1(PRIVATE, void, freeprn, fcbrec*, fp)
{
    ROMlib_forget_fork_offset(fp);
    ROMlib_close_empty_fork(fp);
    fp->fdfnum = 0;
}

A1(PRIVATE, OSErr, getprn, INTEGER*, pprn)
{
    INTEGER length;
    fcbrec *fcbp, *efcbp;
    OSErr err;

    /* #define	NASTYTEMPHAXK */
    length = CW(*(short*)MR(FCBSPtr));
    fcbp = (fcbrec*)((short*)MR(FCBSPtr) + 1);
    efcbp = (fcbrec*)((char*)MR(FCBSPtr) + length);
    for (; fcbp < efcbp && fcbp->fdfnum; fcbp = (fcbrec*)((char*)fcbp + Cx(FSFCBLen)))
        ;
    if (fcbp < efcbp) {
        *pprn = (char*)fcbp - (char*)MR(FCBSPtr);
        err = noErr;
    }
    else
        err = tmfoErr;
    fs_err_hook(err);
    return err;
}

/* Adopt a private raw resource fork; the File Manager owns fd on success. */
INTEGER
ROMlib_open_internal_resource(int fd, LONGINT length)
{
    INTEGER refnum;
    HVCB* volume = ROMlib_vcbbybiggestunixname(ROMlib_startdir);
    fcbrec* fp;
    OSErr err;
    err = volume ? noErr : nsvErr;
    if (err == noErr)
        err = getprn(&refnum);
    if (err != noErr) {
        close(fd);
        ROMlib_setreserr(err);
        return -1;
    }
    fp = (fcbrec*)(MR(FCBSPtr) + refnum);
    memset(fp, 0, sizeof *fp);
    fp->fdfnum = CL(0x7FFF0000L + refnum);
    ROMlib_record_fork_access(refnum, file_open_regular, fsRdWrPerm, true);
    fp->fcflags = fcwriteperm | fcfisres;
    fp->fcfd = fd;
    fp->hiddenfd = -1; /* Raw fork, with no AppleDouble preamble. */
    fp->fcleof = fp->fcPLen = CL(length);
    FIELD_ASSIGN_RAW(fp->fcvptr, RM((VCB*)volume));
    fp->fcparid = CLC(2);
    fp->fcbClmpSize = CLC(1);
    fp->fcbFType = TICKX("zsys");
    memcpy(fp->fcname, SYSMACNAME, sizeof SYSMACNAME);
    /* The descriptor loader closes fd itself if it cannot attach the map. */
    return ROMlib_open_resource_descriptor(refnum);
}

static OSErr PBOpenForkD(ParmBlkPtr pb, BOOLEAN a, ForkType fork, LONGINT dir, int modes)
{
    OSErr err;
    int temp;
    INTEGER prn;
    char *pathname, *filename, *endname, *newname;
    struct stat sbuf;
    VCBExtra* vcbp;
    int namelen;
    fcbrec* fp;
    THz savezone;
    int save_index;
    char save_char;
    char* need_to_free;

    need_to_free = 0;
    pb->ioParam.ioRefNum = CWC(0); /* in case some goofy program,
                      like StuffIt Lite decides to
                      ignore error codes */
    savezone = TheZone;
    HIDDEN_RAW_SET(TheZone_H, SysZone);
    pathname = 0;
    /*
     * We have to pass in sbuf below or we won't be weaseled like we should be.
     */
    if ((err = ROMlib_nami(pb, dir, NoIndex, &pathname, &filename, &endname, TRUE, &vcbp, (struct stat*)&sbuf)) ==
        noErr) {
        mode_t file_mode = sbuf.st_mode;
        SignedByte permission = pb->ioParam.ioPermssn;
        INTEGER existing = 0;
        err = ROMlib_check_fork_sharing((VCB*)vcbp, CL((LONGINT)ST_INO(sbuf)), fork == ResourceFork, modes,
                                       &permission, &existing);
        if (err) {
            pb->ioParam.ioRefNum = CW(existing);
            goto openfork_done;
        }
        if (modes != file_open_regular && (modes & file_access_write) && !(file_mode & S_IWUSR)) {
            err = fLckdErr;
            goto openfork_done;
        }
        err = getprn(&prn);
        if (err == noErr) {
            fp = (fcbrec*)(MR(FCBSPtr) + prn);
            fp->fdfnum = CL((LONGINT)ST_INO(sbuf));
            fp->fcfd = -1;
            fp->hiddenfd = -1;
            fp->fcflags = 0;
            fp->fcbTypByt = 0;
            fp->fcbCrPs = 0;
            if ((filename == pathname + 1) || (filename == pathname + SLASH_CHAR_OFFSET + 1))
                save_index = 0; /* root ... don't scorch leading "/" */
            else
                save_index = -1;
            save_char = filename[save_index];
            filename[save_index] = 0;
            if (Ustat(pathname, &sbuf) < 0)          /* NOTE: this is a stat of */
                err = ROMlib_maperrno();             /* the parent's directory, */
            else {                                   /* so ROMlib_nami won't do */
                if (ST_INO(sbuf) == vcbp->u.ufs.ino) /* the stat for us */
                    fp->fcparid = CLC(2);
                else
                    fp->fcparid = CL((LONGINT)ST_INO(sbuf));
                filename[save_index] = save_char;
                if (fork == ResourceFork) {
                    newname = ROMlib_resname(pathname, filename, endname);
                    if (!newname) {
                        err = errno == ENOMEM ? memFullErr : ioErr;
                        freeprn(fp);
                        goto openfork_done;
                    }
                    {
                        char* resource = ROMlib_sidecar_resource_name(newname);
                        free(newname);
                        newname = resource;
                    }
                    temp = Ustat(newname, &sbuf);
                    if (temp < 0 && errno != ENOENT)
                        err = ioErr;
                    need_to_free = pathname; /* can't free it now, because
                                filename is drawn from
                                here */
                    pathname = newname;
                }
            }
            if (err == noErr) {
                err = macopen(pathname, permission, &fp->fcfd, fork == ResourceFork, &fp->fcflags);
                if (err != noErr && fork == ResourceFork && netatalk_conventions_p) {
                    char* oldname;
                    LONGINT oldfd;
                    Byte oldflags;
                    OSErr olderr;

                    oldname = old_percent_resname(need_to_free, filename, endname);
                    if (!oldname) {
                        err = errno == ENOMEM ? memFullErr : ioErr;
                        freeprn(fp);
                        goto openfork_done;
                    }
                    oldflags = fp->fcflags;
                    olderr = macopen(oldname, permission, &oldfd, TRUE, &oldflags);
                    if (olderr == noErr) {
                        free(pathname);
                        pathname = oldname;
                        fp->fcfd = oldfd;
                        fp->fcflags = oldflags;
                        err = noErr;
                    }
                    else
                        free(oldname);
                }
                if (err == noErr && fork == ResourceFork && fstat(fp->fcfd, &sbuf) == 0 && sbuf.st_size == 0) {
                    Uclose(fp->fcfd);
                    fp->fcfd = -1;
                    err = fnfErr;
                }
                if (err == fnfErr) {
                    if (!(file_mode & 0200) && permission != fsCurPerm && permission != fsRdPerm)
                        err = wPrErr;
                    else
                        err = ROMlib_open_empty_fork(fp, pathname);
                    if (err == noErr && permission == fsCurPerm && (file_mode & 0200))
                        fp->fcflags |= fcwriteperm;
                }
                if (err != noErr)
                    freeprn(fp);
                else {
                    if (fork == ResourceFork) {
                        fp->fcflags |= fcfisres;
                        if (!ROMlib_empty_fork_p(fp))
                            fp->hiddenfd = fp->fcfd;
                        newname = ROMlib_resname(need_to_free, filename, endname);
                        if (!newname) {
                            err = errno == ENOMEM ? memFullErr : ioErr;
                            if (!ROMlib_empty_fork_p(fp))
                                Uclose(fp->fcfd);
                            freeprn(fp);
                            goto openfork_done;
                        }
                        if (ROMlib_basilisk_sidecar_p(newname))
                            fp->hiddenfd = Uopen(newname, O_BINARY | O_RDONLY, 0);
                        else if (!ROMlib_empty_fork_p(fp) && !ROMlib_resource_fork_present(fp->fcfd)) {
                            Uclose(fp->fcfd);
                            freeprn(fp);
                            free(newname);
                            err = fnfErr;
                            goto openfork_done;
                        }
                        free(newname);
                        lseek(fp->fcfd, ROMlib_FORKOFFSET(fp), L_SET);
                    }
                    else {
                        newname = ROMlib_resname(pathname, filename, endname);
                        if (!newname) {
                            err = errno == ENOMEM ? memFullErr : ioErr;
                            if (!ROMlib_empty_fork_p(fp))
                                Uclose(fp->fcfd);
                            freeprn(fp);
                            goto openfork_done;
                        }
                        fp->hiddenfd = Uopen(newname, O_BINARY | O_RDWR, 0666L);
                        if (fp->hiddenfd < 0) {
                            Byte dummy_byte = -1;

                            if (errno == ENOENT) {
                                err = noErr;
                                fp->hiddenfd = -1;
                            }
                            else
                                macopen(newname, permission, &fp->hiddenfd, TRUE, &dummy_byte);
                        }
                        free(newname);
                    }
                    if (err == noErr)
                        err = ROMlib_geteofostype(fp);
                    FIELD_ASSIGN_RAW(fp->fcvptr, RM((VCB*)vcbp));
                    pb->ioParam.ioRefNum = CW(prn);
                    ROMlib_record_fork_access(prn, modes, permission, (fp->fcflags & fcwriteperm) != 0);
                    namelen = strlen(filename);
                    namelen -= ROMlib_UNIX7_to_Mac(filename, namelen);
                    namelen = MIN(namelen, 31);
                    fp->fcname[0] = namelen;
                    BlockMove((Ptr)filename, (Ptr)fp->fcname + 1, (Size)namelen);
                }
            }
        }
    }

openfork_done:
    if (need_to_free)
        free(need_to_free);

    if (pathname)
        free(pathname);
    HIDDEN_RAW_SET(TheZone_H, savezone);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBOpen, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    OSErr err;

    err = PBOpenForkD(pb, a, DataFork, 0, file_open_regular);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBHOpen, HParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)                                 /* IMIV-136 */
{
    OSErr err;

    err = PBOpenForkD((ParmBlkPtr)pb, a, DataFork, CL(pb->fileParam.ioDirID), file_open_regular);
    fs_err_hook(err);
    return err;
}

OSErr ROMlib_ufs_open_deny(HParmBlkPtr pb, bool resource, int modes)
{
    return PBOpenForkD((ParmBlkPtr)pb, FALSE, resource ? ResourceFork : DataFork, CL(pb->fileParam.ioDirID), modes);
}

A2(PUBLIC, OSErr, ufsPBOpenRF, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err;

    err = PBOpenForkD(pb, a, ResourceFork, 0, file_open_regular);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBHOpenRF, HParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err;

    err = PBOpenForkD((ParmBlkPtr)pb, a, ResourceFork, CL(pb->fileParam.ioDirID), file_open_regular);
    fs_err_hook(err);
    return err;
}

#define POSMASK 0x3 /* IMIV-121 */

PRIVATE OSErr pbfpos(ParmBlkPtr pb, LONGINT* toseekp, boolean_t can_go_past_eof)
{
    OSErr err;
    LONGINT leof;
    LONGINT forkoffset;
    LONGINT pos;
    int fd;
    fcbrec* fp;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        forkoffset = FORKOFFSET(fp);
        leof = Cx(fp->fcleof);
        fd = fp->fcfd;

        switch (Cx(pb->ioParam.ioPosMode) & POSMASK) {
        case fsAtMark:
            if ((pos = lseek(fd, 0L, L_INCR)) == -1) {
                switch (errno) {
                case EBADF:
                    err = fnOpnErr;
                    break;
                case EINVAL:
                    err = paramErr;
                    break;
                default:
                    err = ioErr;
                }
            }
            else
                *toseekp = pos - forkoffset;
            break;
        case fsFromStart:
            *toseekp = Cx(pb->ioParam.ioPosOffset);
            break;
        case fsFromLEOF:
            *toseekp = leof + Cx(pb->ioParam.ioPosOffset);
            break;
        case fsFromMark:
            if ((pos = lseek(fd, 0L, L_INCR)) == -1) {
                switch (errno) {
                case EBADF:
                    err = fnOpnErr;
                    break;
                case EINVAL:
                    err = paramErr;
                    break;
                default:
                    err = ioErr;
                }
            }
            else
                *toseekp = pos + Cx(pb->ioParam.ioPosOffset) - forkoffset;
            break;
        }
        if (*toseekp + forkoffset < 0) {
            err = posErr;
            *toseekp = 0;
        }
        else if ((*toseekp > leof) && !can_go_past_eof) {
            err = eofErr;
            *toseekp = leof;
        }
        *toseekp += forkoffset;
    }
    fs_err_hook(err);
    return err;
}

A3(PRIVATE, OSErr, PBLockUnlockRange, ParmBlkPtr, pb, BOOLEAN, a, lockunlock_t, op)
{
    OSErr err;
    LONGINT toseek;
    int fd;
    LONGINT curseek;
    fcbrec* fp;

    err = noErr;
    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        if ((err = pbfpos(pb, &toseek, TRUE)) == noErr) {
            OSErr (*verify)(int fd, uint32 start, uint32 count);
            OSErr (*cleanup)(int fd, uint32 start, uint32 count);

            fd = fp->fcfd;
            curseek = lseek(fd, 0, L_SET);

            if (op == lock) {
                verify = ROMlib_fd_range_overlap;
                cleanup = ROMlib_fd_add_range;
            }
            else {
                verify = ROMlib_find_fd_start_count;
                cleanup = ROMlib_fd_remove_range;
            }

            err = verify(fd, toseek, CL(pb->ioParam.ioReqCount));
            if (err == noErr && lseek(fd, toseek, L_SET) == -1)
                err = ROMlib_maperrno();
            if (err == noErr)
                err = ROMlib_lockunlockrange(fd, toseek, CL(pb->ioParam.ioReqCount), op);
            if (err == noErr)
                err = cleanup(fd, toseek, CL(pb->ioParam.ioReqCount));
            lseek(fd, curseek, L_SET);
        }
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBLockRange, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err;

    err = PBLockUnlockRange(pb, a, lock);
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBUnlockRange, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err;

    err = PBLockUnlockRange(pb, a, unlock);
    fs_err_hook(err);
    return err;
}

PRIVATE OSErr pbsetfpos(ParmBlkPtr pb, boolean_t can_go_past_eof)
{
    OSErr err;
    LONGINT forkoffset;
    LONGINT toseek;
    int fd;
    fcbrec* fp;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        forkoffset = FORKOFFSET(fp);
        err = pbfpos(pb, &toseek, can_go_past_eof);
        fd = fp->fcfd;
        pb->ioParam.ioPosOffset = CL(lseek(fd, toseek, L_SET) - forkoffset);
    }
    fs_err_hook(err);
    return err;
}

PUBLIC int ROMlib_newlinetocr = TRUE;

/*
 * NOTE: ROMlib_destroy_blocks is a wrapper routine that either destroys
 *	 a limited range or flushes the entire cache.  Apple's semantics
 *	 say that you must flush the entire cache in certain circumstances.
 *	 However, 99% of all programs need to destroy only a range of
 *	 addresses.
 */

PUBLIC int ROMlib_flushoften = 0;

PUBLIC unsigned long ROMlib_destroy_blocks(syn68k_addr_t start, uint32 count, BOOLEAN flush_only_faulty_checksums)
{
    unsigned long num_blocks_destroyed;

    if (ROMlib_flushoften) {
        start = 0;
        count = ~0;
    }
    if (count) {
        if (flush_only_faulty_checksums)
            num_blocks_destroyed = destroy_blocks_with_checksum_mismatch(start, count);
        else
            num_blocks_destroyed = destroy_blocks(start, count);
    }
    else
        num_blocks_destroyed = 0;

    return num_blocks_destroyed;
}

A2(PUBLIC, OSErr, ufsPBRead, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    OSErr err;
    LONGINT nread;
    LONGINT forkoffset;
    fcbrec* fp;

    if (Cx(pb->ioParam.ioRefNum) < 0) {
        err = ROMlib_dispatch(pb, a, Prime, aRdCmd);
        fs_err_hook(err);
        return err;
    }
    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr && ((err = pbsetfpos(pb, FALSE)) == noErr || err == eofErr)) {
        forkoffset = FORKOFFSET(fp);
        if (Cx(pb->ioParam.ioReqCount) < 0)
            err = paramErr;
        else {
            int fd = fp->fcfd;
            LONGINT rc = Cx(pb->ioParam.ioReqCount);

            if (Cx(pb->ioParam.ioPosOffset) + rc > Cx(fp->fcleof))
                rc = Cx(fp->fcleof) - Cx(pb->ioParam.ioPosOffset);
            nread = read(fd, (char*)(PPR(pb->ioParam.ioBuffer)), rc);
            ROMlib_destroy_blocks((syn68k_addr_t)(long)US_TO_SYN68K(PPR(pb->ioParam.ioBuffer)), rc, TRUE);
            if (nread == -1) {
                pb->ioParam.ioActCount = 0;
                err = ioErr;
            }
            else {
                pb->ioParam.ioActCount = CL(nread);
                pb->ioParam.ioPosOffset = CL(lseek(fd, 0L, L_INCR) - forkoffset);
                if (rc != Cx(pb->ioParam.ioReqCount))
                    err = eofErr;
            }
        }
    }
    fs_err_hook(err);

    return err;
}

A2(PUBLIC, OSErr, ufsPBWrite, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    OSErr err;
    LONGINT nwrite, rc;
    LONGINT forkoffset;
    fcbrec* fp;

    if (Cx(pb->ioParam.ioRefNum) < 0) {
        err = ROMlib_dispatch(pb, a, Prime, aWrCmd);
        fs_err_hook(err);
        return err;
    }
    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr && !(fp->fcflags & fcwriteperm))
        err = wrPermErr;
    if (err == noErr && ((err = pbsetfpos(pb, TRUE)) == noErr || err == eofErr)) {
        forkoffset = FORKOFFSET(fp);
        if ((rc = Cx(pb->ioParam.ioReqCount)) < 0)
            err = paramErr;
        else {
            int fd = fp->fcfd;
#if defined(WEHAVEENOUGHSWAPSPACETORUNSUNSGOOFYLITTLERPCTHINGYS)
#if defined(SUN)
            if (lockf(fd, F_TLOCK, rc))
                err = fLckdErr;
#endif /* SUN */
#endif /* WEHAVEENOUGHSWAPSPACETORUNSUNSGOOFYLITTLERPCTHINGYS */
            if (err == noErr && Cx(pb->ioParam.ioPosOffset) + rc > Cx(fp->fcleof)) {
                fp->fcleof = CL(CL(pb->ioParam.ioPosOffset) + rc);
                err = ROMlib_seteof(fp);
            }
            if (err == noErr) {
                fd = fp->fcfd; /* Growth can materialize an absent fork. */
                forkoffset = FORKOFFSET(fp);
                nwrite = write(fd, (char*)(PPR(pb->ioParam.ioBuffer)), rc);
                if (nwrite == -1) {
                    pb->ioParam.ioActCount = 0;
                    err = ioErr;
                }
                else {
                    err = noErr;
                    pb->ioParam.ioActCount = CL(nwrite);
                    pb->ioParam.ioPosOffset = CL(lseek(fd, 0L, L_INCR) - forkoffset);
                    if (Cx(pb->ioParam.ioPosOffset) > Cx(fp->fcleof)) {
                        fp->fcleof = pb->ioParam.ioPosOffset;
                        err = ROMlib_seteof(fp);
                    }
                }
            }
#if defined(WEHAVEENOUGHSWAPSPACETORUNSUNSGOOFYLITTLERPCTHINGYS)
#if defined(SUN)
            lockf(fd, F_ULOCK, Cx(pb->ioParam.ioReqCount));
#endif /* SUN */
#endif /* WEHAVEENOUGHSWAPSPACETORUNSUNSGOOFYLITTLERPCTHINGYS */
        }
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBGetFPos, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err;
    LONGINT forkoffset;
    fcbrec* fp;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        forkoffset = FORKOFFSET(fp);
        pb->ioParam.ioPosOffset = CL(lseek(fp->fcfd, 0L, L_INCR) - forkoffset);
        pb->ioParam.ioReqCount = pb->ioParam.ioActCount = pb->ioParam.ioPosMode = 0;
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBSetFPos, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    OSErr err = pbsetfpos(pb, FALSE);

    if (err == noErr)
        pb->ioParam.ioReqCount = pb->ioParam.ioActCount = 0;
    fs_err_hook(err);

    return err;
}

A2(PUBLIC, OSErr, ufsPBGetEOF, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    fcbrec* fp;
    OSErr err;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr)
        pb->ioParam.ioMisc = fp->fcleof;
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBSetEOF, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    fcbrec* fp;
    OSErr err;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        fp->fcleof = pb->ioParam.ioMisc;
        err = ROMlib_seteof(fp);
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBAllocate, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    fcbrec* fp;
    OSErr err;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (err == noErr) {
        fp->fcleof = CL(CL(fp->fcleof) + CL(pb->ioParam.ioReqCount));
        err = ROMlib_seteof(fp);
        pb->ioParam.ioActCount = pb->ioParam.ioReqCount;
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBAllocContig, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    /* A host fork uses the same reservation path. PBAllocContig performs
       the caller's single completion after this backend returns. */
    return ufsPBAllocate(pb, FALSE);
}

A2(PUBLIC, OSErr, ufsPBFlushFile, ParmBlkPtr, pb, /* INTERNAL */
   BOOLEAN, a)
{
    fcbrec* fp;
    OSErr err;

    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);
    if (!ROMlib_nosync)
        sync();
    fs_err_hook(err);
    return err;
}

A2(PUBLIC, OSErr, ufsPBClose, ParmBlkPtr, pb, BOOLEAN, a) /* INTERNAL */
{
    OSErr err;
    fcbrec* fp;

    if (Cx(pb->ioParam.ioRefNum) < 0) {
        err = ROMlib_dispatch(pb, a, Close, 0);
        fs_err_hook(err);
        return err;
    }
    fp = PRNTOFPERR(Cx(pb->ioParam.ioRefNum), &err);

    if (err == noErr) {
        if (ROMlib_empty_fork_p(fp))
            ROMlib_close_empty_fork(fp);
        else if (Uclose(fp->fcfd) < 0) {
            err = ioErr;
        }
        if (fp->hiddenfd >= 0 && fp->hiddenfd != fp->fcfd)
            Uclose(fp->hiddenfd);
        freeprn(fp);
    }
    fs_err_hook(err);
    return err;
}

A2(PUBLIC trap, OSErrRET, PBHGetLogInInfo, HParmBlkPtr, pb, BOOLEAN, a)
{
    OSErrRET retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}

A2(PUBLIC trap, OSErrRET, PBHGetDirAccess, HParmBlkPtr, pb, BOOLEAN, a)
{
    OSErrRET retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}

static OSErr copy_file_open(const FSSpec* file, int resource, uint16_t mode, INTEGER* reference)
{
    ForkAccessParam request = {0};
    request.ioVRefNum = file->vRefNum;
    request.ioDirID = file->parID;
    PP_ASSIGN(request.ioNamePtr, (StringPtr)file->name);
    request.ioDenyModes = CW(mode);
    const OSErr error = resource ? OpenRFDeny((HParmBlkPtr)&request, FALSE) : OpenDeny((HParmBlkPtr)&request, FALSE);
    if (!error)
        *reference = CW(request.ioRefNum);
    return error;
}

static OSErr copy_file_fork(const FSSpec* source, const FSSpec* destination, int resource, Ptr buffer, LONGINT capacity)
{
    INTEGER input;
    OSErr error = copy_file_open(source, resource, 0x21, &input); /* Read and deny write. */
    if (error)
        return error;
    INTEGER output;
    error = copy_file_open(destination, resource, 0x32, &output); /* Write and deny read/write. */
    if (!error) {
        for (;;) {
            LONGINT count = capacity;
            const OSErr read_error = FSRead(input, &count, buffer);
            if (read_error && read_error != eofErr) {
                error = read_error;
                break;
            }
            if (count < 0 || count > capacity) {
                error = ioErr;
                break;
            }
            if (count)
                error = FSWriteAll(output, &count, buffer);
            if (error || read_error == eofErr || !count)
                break;
        }
        const OSErr close_error = FSClose(output);
        if (!error)
            error = close_error;
    }
    const OSErr close_error = FSClose(input);
    return error ? error : close_error;
}

static OSErr copy_file_catalog(FSSpec* file, CInfoPBRec* info, int write)
{
    PP_ASSIGN(info->hFileInfo.ioNamePtr, (StringPtr)file->name);
    info->hFileInfo.ioVRefNum = file->vRefNum;
    info->hFileInfo.ioFDirIndex = 0;
    info->hFileInfo.ioDirID = file->parID;
    return write ? PBSetCatInfo(info, FALSE) : PBGetCatInfo(info, FALSE);
}

static OSErr copy_file(CopyParam* request)
{
    const StringPtr input = PPR(request->ioNamePtr);
    if (!input || !input[0])
        return bdNamErr;
    Str255 source_path;
    str255assign(source_path, input);
    FSSpec source;
    OSErr error = FSMakeFSSpec(CW(request->ioVRefNum), CL(request->ioDirID), source_path, &source);
    if (error)
        return error;
    CInfoPBRec metadata = {0};
    error = copy_file_catalog(&source, &metadata, FALSE);
    if (error)
        return error;
    if (metadata.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
        return paramErr;

    const StringPtr path = PPR(request->ioNewName);
    Str255 directory_name = {0};
    if (path)
        str255assign(directory_name, path);
    FSSpec directory;
    error = FSMakeFSSpec(CW(request->ioDstVRefNum), CL(request->ioNewDirID), directory_name, &directory);
    if (error)
        return error == fnfErr ? dirNFErr : error;
    CInfoPBRec directory_info = {0};
    const int named_directory = directory_name[0] != 0;
    PP_ASSIGN(directory_info.dirInfo.ioNamePtr, (StringPtr)directory_name);
    directory_info.dirInfo.ioVRefNum = request->ioDstVRefNum;
    directory_info.dirInfo.ioDrDirID = request->ioNewDirID;
    directory_info.dirInfo.ioFDirIndex = CW((int16)(named_directory ? 0 : -1));
    error = PBGetCatInfo(&directory_info, FALSE);
    if (error)
        return error;
    if (!(directory_info.dirInfo.ioFlAttrib & ATTRIB_ISADIR))
        return dirNFErr;

    const StringPtr leaf = PPR(request->ioCopyName);
    Str255 name;
    str255assign(name, leaf ? leaf : source.name);
    if (!name[0] || memchr(name + 1, ':', name[0]))
        return bdNamErr;
    FSSpec destination;
    error = FSMakeFSSpec(CW(directory.vRefNum), CL(directory_info.dirInfo.ioDrDirID), name, &destination);
    if (error != fnfErr)
        return error ? error : dupFNErr;

    enum { buffer_size = 32 * 1024 };

    Ptr buffer = NewPtr(buffer_size);
    if (!buffer)
        return memFullErr;
    FSSpec temporary = destination;
    HParamBlockRec create = {0};
    create.fileParam.ioVRefNum = temporary.vRefNum;
    create.fileParam.ioDirID = temporary.parID;
    PP_ASSIGN(create.fileParam.ioNamePtr, (StringPtr)temporary.name);
    const unsigned long stamp = msecs_elapsed();
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        temporary.name[0] =
            snprintf((char*)temporary.name + 1, sizeof temporary.name - 1, "ROMAW Copy %08lx.%02x", stamp, attempt);
        error = PBHCreate(&create, FALSE);
        if (error != dupFNErr)
            break;
    }
    if (!error) {
        error = copy_file_fork(&source, &temporary, 0, buffer, buffer_size);
        if (!error && metadata.hFileInfo.ioFlRLgLen)
            error = copy_file_fork(&source, &temporary, 1, buffer, buffer_size);
        if (!error)
            error = copy_file_catalog(&temporary, &metadata, TRUE);
        if (!error) {
            error = FSpRename(&temporary, name);
            if (!error)
                temporary = destination;
        }
        if (!error && (metadata.hFileInfo.ioFlAttrib & ATTRIB_ISLOCKED))
            error = FSpSetFLock(&temporary);
        if (error)
            FSpDelete(&temporary);
    }
    DisposPtr(buffer);
    return error;
}

A2(PUBLIC trap, OSErrRET, PBHCopyFile, HParmBlkPtr, pb, BOOLEAN, a)
{
    if (!pb)
        return paramErr;
    const OSErr error = copy_file((CopyParam*)pb);
    FAKEASYNC(pb, a, error);
}

A2(PUBLIC trap, OSErrRET, PBHMapName, HParmBlkPtr, pb, BOOLEAN, a)
{
    OSErrRET retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}

A2(PUBLIC trap, OSErrRET, PBHMapID, HParmBlkPtr, pb, BOOLEAN, a)
{
    OSErrRET retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}

A2(PUBLIC trap, OSErrRET, PBHSetDirAccess, HParmBlkPtr, pb, BOOLEAN, a)
{
    OSErrRET retval;

    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}
