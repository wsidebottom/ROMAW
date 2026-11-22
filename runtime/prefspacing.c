/*+===================================================================
  File:      prefspacing.c

  Summary:   Implements prefspacing support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: prefs_space_sections

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "rsys/prefspacing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

int prefs_space_sections(const char* path)
{
    FILE *input = NULL, *output = NULL;
    char* temporary;
    int fd = -1, ok = 0, changed = 0;
    int beginning = 1, blank = 1, previous_blank = 1;
    size_t total = 0;
    if (strlen(path) > SIZE_MAX - 64)
        return 0;
    temporary = malloc(strlen(path) + 64);
    if (!temporary)
        return 0;
#ifdef _WIN32
    WritePrivateProfileStringA(NULL, NULL, NULL, path);
    unsigned long pid = (unsigned long)_getpid();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    input = fopen(path, "rb");
    if (!input) {
        free(temporary);
        return errno == ENOENT;
    }
    for (unsigned attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        sprintf(temporary, "%s.spacing.%lu.%u", path, pid, attempt);
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
    output = fdopen(fd, "wb");
    if (!output) {
        close(fd);
        goto remove_temp;
    }
    int c;
    while ((c = fgetc(input)) != EOF) {
        if (++total > 32U * 1024 * 1024)
            goto close_output;
        if (beginning && c == '[' && !previous_blank) {
            if (fputs("\r\n", output) == EOF)
                goto close_output;
            changed = 1;
        }
        if (fputc(c, output) == EOF)
            goto close_output;
        beginning = c == '\n';
        if (beginning) {
            previous_blank = blank;
            blank = 1;
        }
        else if (c != '\r' && c != ' ' && c != '\t')
            blank = 0;
    }
    ok = !ferror(input);
close_output:
    if (fclose(output))
        ok = 0;
    output = NULL;
    if (fclose(input))
        ok = 0;
    input = NULL;
    if (ok && changed) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        WritePrivateProfileStringA(NULL, NULL, NULL, path);
#else
        ok = rename(temporary, path) == 0;
#endif
    }
remove_temp:
    if (!ok || !changed)
        remove(temporary);
done:
    if (input)
        fclose(input);
    free(temporary);
    return ok;
}
