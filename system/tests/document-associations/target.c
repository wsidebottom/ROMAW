/*+===================================================================
  File:      target.c

  Summary:   Implements target support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests/document-associations; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <Files.h>
#include <ToolUtils.h>
#include <string.h>

int main(void)
{
    short message, count, ref;
    AppFile document;
    FSSpec spec;
    CountAppFiles(&message, &count);
    if (message != appOpen || count != 1) {
        SysBeep(-20);
        return 20;
    }
    GetAppFiles(1, &document);
    if (document.fType != 'TEST' || document.fName[0] != 11 || memcmp(document.fName + 1, "Sample.ftst", 11)) {
        SysBeep(-21);
        return 21;
    }
    if (FSMakeFSSpec(document.vRefNum, 0, document.fName, &spec) || FSpOpenDF(&spec, fsRdPerm, &ref)) {
        SysBeep(-22);
        return 22;
    }
    char text[11];
    long length = sizeof text;
    if (FSRead(ref, &length, text) || length != sizeof text || memcmp(text, "mapped data", 11)) {
        SysBeep(-23);
        return 23;
    }
    FSClose(ref);
    SysBeep(777);
    return 0;
}
