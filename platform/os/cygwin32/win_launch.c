/*+===================================================================
  File:      win_launch.c

  Summary:   Implements win launch support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_launch_native_app, ROMlib_win_relaunch

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 2001 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES

#include "rsys/common.h"
#include "rsys/error.h"

#include <windows.h>

#include "win_win.h"

enum { noErr = 0, paramErr = -50 };

/* Quote each document for Windows argument parsing. Backslashes before a
 * quote or the closing quote must be doubled. Normalize host separators
 * before quoting so a final slash cannot escape the closing quote. */
static char* construct_arguments(int count, char** filenames, int normalize)
{
    size_t capacity = 1;
    char *result, *out;
    int i;

    for (i = 0; i < count; ++i) {
        size_t length;
        if (!filenames[i])
            return NULL;
        length = strlen(filenames[i]);
        if (capacity > (size_t)-1 - 3 || length > ((size_t)-1 - capacity - 3) / 2)
            return NULL;
        capacity += length * 2 + 3;
    }
    result = malloc(capacity);
    if (!result)
        return NULL;
    out = result;
    for (i = 0; i < count; ++i) {
        const unsigned char* p = (const unsigned char*)filenames[i];
        size_t slashes = 0;
        if (i)
            *out++ = ' ';
        *out++ = '"';
        for (;; ++p) {
            unsigned char c = normalize && *p == '/' ? '\\' : *p;
            size_t emit;
            if (c == '\\') {
                ++slashes;
                continue;
            }
            emit = (c == '"' || !c) ? slashes * 2 : slashes;
            while (emit--)
                *out++ = '\\';
            slashes = 0;
            if (!c)
                break;
            if (c == '"')
                *out++ = '\\';
            *out++ = c;
        }
        *out++ = '"';
    }
    *out = 0;
    return result;
}

static char* construct_command_line(int count, char** filenames)
{
    return construct_arguments(count, filenames, 1);
}

/* Supply launch-only values in a private child environment. No handles are
   inherited, and the parent's environment is never modified. */
static char* ecf_environment(int system, int application)
{
    char *source = GetEnvironmentStringsA(), *result, *out;
    const char* p;
    size_t length = 1;
    int n;
    if (!source)
        return NULL;
    for (p = source; *p; p += strlen(p) + 1) {
        size_t item = strlen(p) + 1;
        if (length > SIZE_MAX - 256 - item) {
            FreeEnvironmentStringsA(source);
            return NULL;
        }
        length += item;
    }
    result = malloc(length + 256);
    if (!result) {
        FreeEnvironmentStringsA(source);
        return NULL;
    }
    out = result;
    for (p = source; *p; p += strlen(p) + 1)
        if (_strnicmp(p, "ROMAW_ECF_SYSZONE=", sizeof "ROMAW_ECF_SYSZONE=" - 1) &&
            _strnicmp(p, "ROMAW_ECF_APPLZONE=", sizeof "ROMAW_ECF_APPLZONE=" - 1) &&
            _strnicmp(p, "ROMAW_ECF_BROWSER=", sizeof "ROMAW_ECF_BROWSER=" - 1)) {
            size_t size = strlen(p) + 1;
            memcpy(out, p, size);
            out += size;
        }
    FreeEnvironmentStringsA(source);
    n = sprintf(out, "ROMAW_ECF_SYSZONE=%d", system);
    out += n + 1;
    n = sprintf(out, "ROMAW_ECF_APPLZONE=%d", application);
    out += n + 1;
    *out = 0;
    return result;
}

int ROMlib_win_relaunch(int count, char** arguments, int system, int application)
{
    char *command = NULL, *environment = NULL, *executable = NULL;
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD capacity = 512, length;
    int result = 0;
    if (count <= 0 || !arguments || !arguments[0] || system <= 0 || application <= 0)
        return 0;
    for (;;) {
        executable = malloc(capacity);
        if (!executable)
            goto done;
        length = GetModuleFileNameA(NULL, executable, capacity);
        if (length && length < capacity - 1)
            break;
        free(executable);
        executable = NULL;
        if (!length || capacity >= 32768)
            goto done;
        capacity *= 2;
    }
    command = construct_arguments(count, arguments, 0);
    environment = ecf_environment(system, application);
    if (!command || !environment)
        goto done;
    startup.cb = sizeof startup;
    result = !!CreateProcessA(executable, command, NULL, NULL, FALSE, 0, environment, NULL, &startup, &process);
    if (result) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
done:
    free(command);
    free(environment);
    free(executable);
    return result;
}

/*
 * NOTE: when you use ROMAW to launch a native application under win32,
 * the files involved all have to be .lnk files.  This is ugly.
 */

PUBLIC int ROMlib_launch_native_app(int n_filenames, char** filenames)
{
    HINSTANCE hi;
    size_t length;
    char *command_buf, *args_buf, *p;

    if (n_filenames <= 0 || !filenames || !filenames[0] || !*filenames[0])
        return paramErr;
    length = strlen(filenames[0]);
    if (length == (size_t)-1)
        return paramErr;
    command_buf = malloc(length + 1);
    if (!command_buf)
        return paramErr;
    memcpy(command_buf, filenames[0], length + 1);
    for (p = command_buf; *p; ++p)
        if (*p == '/')
            *p = '\\';
    args_buf = construct_command_line(n_filenames - 1, filenames + 1);
    if (!args_buf) {
        free(command_buf);
        return paramErr;
    }
    hi = ShellExecuteA(cygwin_sdlwindow(), NULL, command_buf, args_buf, NULL, SW_SHOWNORMAL);
    free(args_buf);
    free(command_buf);
    return (INT_PTR)hi > 32 ? noErr : paramErr;
}
