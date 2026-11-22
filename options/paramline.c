/*+===================================================================
  File:      paramline.c

  Summary:   Implements paramline support for ROMAW options and preferences.

  Classes:   None.

  Functions: ROMlib_argv_to_string, ROMlib_help_requested, ROMlib_version_requested, count_params, get_param,
             paramline_to_argcv

  Origin:    Existing repository source in executor/src/options; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1988 - 1998 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "paramline.h"

typedef enum { FALSE, TRUE } boolean_t;

int ROMlib_help_requested(int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--"))
            break;
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-help"))
            return TRUE;
    }
    return FALSE;
}

int ROMlib_version_requested(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--"))
            break;
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-version"))
            return TRUE;
    }
    return FALSE;
}

/* Scan one argument, optionally copying its unquoted bytes.  Backslashes
 * are literal; single and double quotes can join adjacent word fragments. */
static int scan_param(const char** bufpp, int* nleftp, char* output)
{
    const char* p = *bufpp;
    int n = *nleftp, length = 0;
    char literal = 0;

    while (n > 0 && isspace((unsigned char)*p))
        ++p, --n;
    if (!n) {
        *bufpp = p;
        *nleftp = n;
        return -1;
    }
    while (n > 0) {
        char c = *p++;
        --n;
        if (literal && c == literal)
            literal = 0;
        else if (!literal && isspace((unsigned char)c))
            break;
        else if (!literal && (c == '\'' || c == '"'))
            literal = c;
        else {
            if (output)
                output[length] = c;
            ++length;
        }
    }
    if (output)
        output[length] = 0;
    *bufpp = p;
    *nleftp = n;
    return length;
}

int count_params(const char* p, int n)
{
    int count = 0;
    while (n > 0 && scan_param(&p, &n, NULL) >= 0)
        ++count;
    return count;
}

char* get_param(const char** bufpp, int* nleftp)
{
    const char* end = *bufpp;
    int remaining = *nleftp;
    int length = scan_param(&end, &remaining, NULL);
    char* result;

    if (length < 0) {
        *bufpp = end;
        *nleftp = remaining;
        return NULL;
    }
    result = malloc((size_t)length + 1);
    if (result)
        scan_param(bufpp, nleftp, result);
    return result;
}

void paramline_to_argcv(const char* cmdp, int* argcp, char*** argvp)
{
    size_t length = strlen(cmdp);
    int len, argc, i;
    char** argv;

    *argcp = -1;
    *argvp = NULL;
    if (length > INT_MAX)
        return;
    len = length;
    argc = count_params(cmdp, len);
    if ((size_t)argc >= (size_t)-1 / sizeof *argv)
        return;
    argv = malloc(((size_t)argc + 1) * sizeof *argv);
    if (!argv)
        return;
    for (i = 0; i < argc; ++i) {
        argv[i] = get_param(&cmdp, &len);
        if (!argv[i]) {
            while (i > 0)
                free(argv[--i]);
            free(argv);
            return;
        }
    }
    argv[argc] = NULL;
    *argcp = argc;
    *argvp = argv;
}

/* Diagnostic text only: preserve the existing space-separated format. */
char* ROMlib_argv_to_string(int argc, char* const* argv)
{
    size_t size = 1;
    char *result, *out;
    int i;

    for (i = 0; i < argc; ++i) {
        size_t length = strlen(argv[i]);
        size_t separator = i != 0;
        if (separator > (size_t)-1 - size || length > (size_t)-1 - size - separator)
            return NULL;
        size += length + separator;
    }
    result = malloc(size);
    if (!result)
        return NULL;
    out = result;
    for (i = 0; i < argc; ++i) {
        size_t length = strlen(argv[i]);
        if (i)
            *out++ = ' ';
        memcpy(out, argv[i], length);
        out += length;
    }
    *out = 0;
    return result;
}
