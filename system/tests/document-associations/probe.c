/*+===================================================================
  File:      probe.c

  Summary:   Implements probe support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests/document-associations; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include <Quickdraw.h>
#include <Files.h>
#include <Fonts.h>
#include <Resources.h>
#include <Memory.h>
#include <Processes.h>
#include <Gestalt.h>
#include <Windows.h>
#include <ToolUtils.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
_Static_assert(offsetof(DTPBRec, ioFileCreator) == 52, "Desktop Manager ABI");
_Static_assert(offsetof(DTPBRec, ioAPPLParID) == 100, "Desktop Manager ABI");

int main(void)
{
    const char* document = "D:Source:ROMAW:build:document-association-check:Apps:Sample.ftst";
    Str255 name;
    FSSpec spec;
    FInfo info;
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    name[0] = strlen(document);
    memcpy(name + 1, document, name[0]);
    if (FSMakeFSSpec(0, 0, name, &spec) || FSpGetFInfo(&spec, &info) || info.fdType != 'TEST' ||
        info.fdCreator != 'fAst') {
        SysBeep(-1);
        return 1;
    }
    long callback = 0;
    FSSpec application;
    if (Gestalt('exAI', &callback) || !callback) {
        SysBeep(-8);
        return 8;
    }
    typedef long (*ApplicationLookup)(unsigned long, OSType, OSType, FSSpec*);
    ApplicationLookup lookup = (ApplicationLookup)callback;
    OSErr lookup_error = fnfErr;
    for (unsigned attempt = 0; attempt < 10000 && lookup_error == fnfErr; attempt++) {
        lookup_error = lookup(0, 'none', 'TEST', &application);
        if (lookup_error == fnfErr)
            lookup(1, 0, 0, NULL);
    }
    if (lookup_error || application.name[0] != 14 || memcmp(application.name + 1, "DocumentTarget", 14)) {
        SysBeep(-8);
        return 8;
    }
    short saved_resources = CurResFile();
    short application_resources = FSpOpenResFile(&application, fsRdPerm);
    Handle icon = application_resources == -1 ? NULL : Get1Resource('ICN#', 129);
    int icon_ok = icon && GetHandleSize(icon) == 256 && (unsigned char)(*icon)[0] == 0x55;
    if (application_resources != -1)
        CloseResFile(application_resources);
    UseResFile(saved_resources);
    if (!icon_ok) {
        SysBeep(-2);
        return 2;
    }
    FSSpec missing;
    if (lookup(0, 'none', 'MISS', &missing) != fnfErr) {
        SysBeep(-9);
        return 9;
    }
    const char* explicit_metadata = "D:Source:ROMAW:build:document-association-check:Apps:Explicit.ftst";
    name[0] = strlen(explicit_metadata);
    memcpy(name + 1, explicit_metadata, name[0]);
    if (FSMakeFSSpec(0, 0, name, &spec) || FSpGetFInfo(&spec, &info) || info.fdType != 'EXPL' ||
        info.fdCreator != 'ownr') {
        SysBeep(-3);
        return 3;
    }
    const char* legacy = "D:Source:ROMAW:build:document-association-check:Apps:Legacy.ftst";
    name[0] = strlen(legacy);
    memcpy(name + 1, legacy, name[0]);
    if (FSMakeFSSpec(0, 0, name, &spec) || FSpGetFInfo(&spec, &info) || info.fdType != 'TEST' ||
        info.fdCreator != 'fAst') {
        SysBeep(-5);
        return 5;
    }
    const char* forked = "D:Source:ROMAW:build:document-association-check:Apps:Forked.ftst";
    name[0] = strlen(forked);
    memcpy(name + 1, forked, name[0]);
    short resource;
    if (FSMakeFSSpec(0, 0, name, &spec) || FSpOpenRF(&spec, fsRdWrPerm, &resource)) {
        SysBeep(-6);
        return 6;
    }
    long count = 8;
    if (FSWrite(resource, &count, "resource") || FSClose(resource) || FSpGetFInfo(&spec, &info) ||
        info.fdType != 'TEST' || info.fdCreator != 'fAst') {
        SysBeep(-7);
        return 7;
    }
    name[0] = strlen(document);
    memcpy(name + 1, document, name[0]);
    OSErr result = FSMakeFSSpec(0, 0, name, &spec);
    Handle parameters = NULL;
    short app_resource;
    GetAppParms(name, &app_resource, &parameters);
    WDPBRec working_directory = {0};
    working_directory.ioVRefNum = spec.vRefNum;
    working_directory.ioWDDirID = spec.parID;
    working_directory.ioWDProcID = 'fAsP';
    if (!result)
        result = PBOpenWDSync(&working_directory);
    if (!result && !parameters)
        result = paramErr;
    if (!result) {
        AppFile file = {0};
        file.vRefNum = working_directory.ioVRefNum;
        file.fType = 'TEST';
        memcpy(file.fName, spec.name, spec.name[0] + 1);
        SetHandleSize(parameters, 4 + sizeof file);
        result = MemError();
        if (!result) {
            short header[2] = {appOpen, 1};
            memcpy(*parameters, header, sizeof header);
            memcpy(*parameters + sizeof header, &file, sizeof file);
            LaunchParamBlockRec launch = {0};
            launch.launchBlockID = extendedBlock;
            launch.launchEPBLength = extendedBlockLen;
            launch.launchAppSpec = &application;
            result = LaunchApplication(&launch);
        }
    }
    char error[256];
    snprintf(error, sizeof error, "Macintosh launch error %d", (int)result);
    FILE* log = fopen("D:Source:ROMAW:build:document-association-check:launch-error.txt", "w");
    if (log) {
        fputs(error, log);
        fclose(log);
    }
    SysBeep(-4);
    return 4;
}
