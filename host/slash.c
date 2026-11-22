/*+===================================================================
  File:      slash.c

  Summary:   Implements slash support for host operating-system support.

  Classes:   None.

  Functions: Uaccess, Uchdir, Uchmod, Uclose, Ufopen, Umkdir, Uopen, Uopendir, Urename, Urmdir, Ustat, Uunlink,
             Uutimes

  Origin:    Existing repository source in executor/src/host; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/*
 * The DOS file manipulation routines don't like to see "//", which is
 * a pain for us because we like the format of a file to be the filesystem
 * (volume) name, a slash, and then the rest of the path.  That means that
 * anything that is in the root directory gets a leading slash.  Stubbing
 * out these routines is much easier than rewriting the other routines.
 */

#include "rsys/common.h"
#include "rsys/lockunlock.h"

#if defined(CYGWIN32)

#if defined(CYGWIN32)
#include "winfs.h"
#include "win_stat.h"
#include "rsys/host_names.h"
#endif

#include "rsys/slash.h"

#define DOUBLE_SLASH_REMOVE(str)                                        \
    ({                                                                  \
        char *retval, *op;                                              \
        const char* ip;                                                 \
        boolean_t last_was_slash_p;                                     \
                                                                        \
        /* char *from;                                                \
    asm ("movl %%ebp, %0" : "=g" (from));                         \
    from = *(char **)(from+4);                                    \
    warning_unexpected ("name = \"%s\" from = %p", str, from); */ \
        retval = alloca(strlen(str) + 1);                               \
        last_was_slash_p = FALSE;                                       \
        for (ip = str, op = retval; *ip; ++ip) {                        \
            if (*ip != '/' || !last_was_slash_p)                        \
                *op++ = *ip;                                            \
            last_was_slash_p = *ip == '/';                              \
        }                                                               \
        *op = 0;                                                        \
        retval;                                                         \
    })

int Uaccess(const char* path, int mode)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return access(path, mode);
}

int Uchdir(const char* path)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return chdir(path);
}

int Uchmod(const char* path, int mode)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return chmod(path, mode);
}

FILE* Ufopen(const char* path, const char* type)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return fopen(path, type);
}

int Umkdir(const char* path, int mode)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return mkdir(path
#if !defined(CYGWIN32)
                 ,
                 mode
#endif
    );
}

DIR* Uopendir(const char* path)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return opendir((char*)path);
}

int Urename(const char* path, const char* new)
{
    path = DOUBLE_SLASH_REMOVE(path);
    new = DOUBLE_SLASH_REMOVE(new);
    return rename(path, new);
}

int Urmdir(const char* path)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return rmdir(path);
}

int Ustat(const char* path, struct stat* buf)
{
    int retval;

    path = DOUBLE_SLASH_REMOVE(path);
    retval = stat(path, buf);
    if (retval < 0 && (errno == ENOENT || errno == ENOTDIR || errno == EINVAL))
        retval = ROMlib_win_icon_stat(path, buf);
    if (strlen(path) == 2 && path[1] == ':') {
#if defined(CYGWIN32)
        buf->st_ino = 0;
        buf->st_rdev = 1;
#else
        buf->st_ino = 1;
#endif
    }
#if defined(CYGWIN32)
    else {
        uint32 ino;

        ino = ino_from_name(path);
        buf->st_ino = ino >> 16; /* See definition of ST_INO */
        buf->st_rdev = ino;
    }
#endif
    return retval;
}

int Uunlink(const char* path)
{
    path = DOUBLE_SLASH_REMOVE(path);
    return unlink(path);
}

int Uutimes(const char* path, struct timeval tvp[2])
{
#warning "Blowing off utimes"
    return -1;
}

#else

#define DOUBLE_SLASH_REMOVE(path) path

#endif

int Uopen(const char* path, int flags, int mode)
{
    int retval;

    path = DOUBLE_SLASH_REMOVE(path);
    retval = open(path, flags, mode);
#if defined(CYGWIN32)
    if (retval < 0 && (errno == ENOENT || errno == ENOTDIR || errno == EINVAL))
        retval = ROMlib_win_icon_open(path, flags, mode);
    if (retval >= 0)
        ROMlib_win_hide_desktop(retval, path);
#endif
    ROMlib_fd_clear_locks_after_open(retval, TRUE);
    return retval;
}

int Uclose(int fd)
{
    int retval;

    ROMlib_fd_release_locks_for_close(fd);
    retval = close(fd);
    return retval;
}
