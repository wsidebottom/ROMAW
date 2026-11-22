/*+===================================================================
  File:      cleanup.c

  Summary:   Implements cleanup support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: add_to_cleanup, call_cleanup_bat

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1998 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include <rsys/common.h>
#include <rsys/cleanup.h>
#include <rsys/file.h>
#include <rsys/notmac.h>

#include <stdarg.h>

#if defined(CYGWIN32)

#if defined(CYGWIN32)
#include <windows.h>
#endif

PUBLIC void add_to_cleanup(const char* s, ...)
{
    char* batch_file;
    FILE* fp;
    struct stat sbuf;

    batch_file = copystr(CLEANUP_BATCH_FILE_NAME);
    if (stat(batch_file, &sbuf) == 0)
        fp = fopen(batch_file, "a");
    else {
        fp = fopen(batch_file, "w");
        if (fp) {
            fprintf(fp, "@echo off\n");
            if (ROMlib_start_drive)
                fprintf(fp, "%c:\n", ROMlib_start_drive);
        }
    }
    free(batch_file);
    if (fp) {
        va_list ap;

        va_start(ap, s);
        vfprintf(fp, s, ap);
        va_end(ap);
        fclose(fp);
    }
}

PUBLIC void call_cleanup_bat(void)
{
    char* batch_file;
    struct stat sbuf;

    batch_file = copystr(CLEANUP_BATCH_FILE_NAME);
    if (stat(batch_file, &sbuf) == 0) {
        add_to_cleanup("del \"%s\"\n", batch_file);
        {
            STARTUPINFO si;
            PROCESS_INFORMATION pi;

            memset(&si, 0, sizeof si);
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            if (CreateProcess(batch_file, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    free(batch_file);
}

#endif
