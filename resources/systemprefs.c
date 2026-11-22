/*+===================================================================
  File:      systemprefs.c

  Summary:   Bounded reading and atomic writing of AppleDouble resource files.

  Classes:   None.

  Functions: system_resource_read, system_resource_write

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded reading and atomic writing of AppleDouble resource files. */
#include "systemprefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define getpid _getpid
#define fsync _commit
#include <process.h>
#endif

#define MAX_PREFS (32U * 1024 * 1024)

static uint32_t read_be32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | p[2] << 8 | p[3];
}

static void write_be32(unsigned char* p, uint32_t n)
{
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}

static char* resource_sidecar_path(const char* path)
{
    const char* leaf = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (!leaf || (backslash && backslash > leaf))
        leaf = backslash;
    leaf = leaf ? leaf + 1 : path;
    size_t prefix = leaf - path, length = strlen(path);
    if (length > SIZE_MAX - 3)
        return NULL;
    char* sidecar = malloc(length + 3);
    if (sidecar) {
        memcpy(sidecar, path, prefix);
        memcpy(sidecar + prefix, "._", 2);
        strcpy(sidecar + prefix + 2, leaf);
    }
    return sidecar;
}

/* A real resource fork and Finder information, in the standard OS X sidecar
   understood by ROMAW, ResEdit and Macintosh file transfers. */
int system_resource_read(const char* path, unsigned char** bytes, size_t* length)
{
    unsigned char header[26], entry[12];
    int ok = 0, found = 0;
    *bytes = NULL;
    *length = 0;
    char* sidecar = resource_sidecar_path(path);
    if (!sidecar)
        return 0;
    FILE* file = fopen(sidecar, "rb");
    free(sidecar);
    if (!file)
        return errno == ENOENT;
    if (fseek(file, 0, SEEK_END))
        goto done;
    long size = ftell(file);
    if (size < 26 || (size_t)size > MAX_PREFS || fseek(file, 0, SEEK_SET) ||
        fread(header, 1, sizeof header, file) != sizeof header || read_be32(header) != 0x00051607 ||
        read_be32(header + 4) != 0x00020000)
        goto done;
    unsigned count = header[24] << 8 | header[25];
    size_t table_end = 26 + 12 * (size_t)count;
    if (table_end > (size_t)size)
        goto done;
    uint32_t fork_offset = 0, fork_length = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (fread(entry, 1, sizeof entry, file) != sizeof entry)
            goto done;
        uint32_t offset = read_be32(entry + 4);
        uint32_t n = read_be32(entry + 8);
        if (offset < table_end || offset > (size_t)size || n > (size_t)size - offset)
            goto done;
        if (read_be32(entry) == 2) {
            if (found || n > MAX_PREFS / 2)
                goto done;
            found = 1;
            fork_offset = offset;
            fork_length = n;
        }
    }
    if (!found || !fork_length) {
        ok = 1; /* A PRAM file can initially have no resource payload. */
        goto done;
    }
    *bytes = malloc(fork_length);
    if (!*bytes || fseek(file, fork_offset, SEEK_SET) || fread(*bytes, 1, fork_length, file) != fork_length)
        goto done;
    *length = fork_length;
    ok = 1;
done:
    if (fclose(file))
        ok = 0;
    if (!ok) {
        free(*bytes);
        *bytes = NULL;
        *length = 0;
    }
    return ok;
}

int system_resource_write(const char* path, const unsigned char* bytes, size_t length)
{
    unsigned char header[82] = {0};
    char *sidecar = NULL, *temporary = NULL;
    int fd = -1, ok = 0;
    FILE* file;
    if (!bytes || length < 16 || length > MAX_PREFS / 2)
        return 0;
    sidecar = resource_sidecar_path(path);
    if (!sidecar || strlen(sidecar) > SIZE_MAX - 64)
        goto done;
    temporary = malloc(strlen(sidecar) + 64);
    if (!temporary)
        goto done;
    for (unsigned attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        sprintf(temporary, "%s.tmp.%lu.%u", sidecar, (unsigned long)getpid(), attempt);
        fd = open(temporary,
                  O_WRONLY | O_CREAT | O_EXCL
#ifdef O_BINARY
                      | O_BINARY
#endif
                  ,
                  0600);
        if (fd < 0 && errno != EEXIST)
            break;
    }
    if (fd < 0)
        goto done;
    file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        goto remove_temp;
    }
    write_be32(header, 0x00051607);
    write_be32(header + 4, 0x00020000);
    header[25] = 2;
    write_be32(header + 26, 9); /* Finder information. */
    write_be32(header + 30, 50);
    write_be32(header + 34, 32);
    write_be32(header + 38, 2); /* Resource fork. */
    write_be32(header + 42, sizeof header);
    write_be32(header + 46, length);
    memcpy(header + 50, "rsrcRSED", 8);
    ok = fwrite(header, 1, sizeof header, file) == sizeof header && fwrite(bytes, 1, length, file) == length;
    if (fflush(file) || fsync(fileno(file)))
        ok = 0;
    if (fclose(file))
        ok = 0;
    if (!ok)
        goto remove_temp;
    /* Do not truncate an existing data fork. ResEdit uses the resource fork. */
    fd = open(path,
              O_WRONLY | O_CREAT
#ifdef O_BINARY
                  | O_BINARY
#endif
              ,
              0600);
    if (fd < 0) {
        ok = 0;
        goto remove_temp;
    }
    if (close(fd)) {
        ok = 0;
        goto remove_temp;
    }
#ifdef _WIN32
    ok = MoveFileExA(temporary, sidecar, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = rename(temporary, sidecar) == 0;
#endif
remove_temp:
    if (!ok)
        remove(temporary);
done:
    free(temporary);
    free(sidecar);
    return ok;
}
