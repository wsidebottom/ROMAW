/*+===================================================================
  File:      native_launch_test.c

  Summary:   Implements native launch test support for regression tests and test fixtures.

  Classes:   None.

  Functions: cygwin_sdlwindow, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <shellapi.h>
#include <assert.h>
static int fail_after = -1, live, calls, expected_count;
static char** expected;
static INT_PTR shell_result = 33;

static void* launch_malloc(size_t size)
{
    void* p;
    if (!fail_after)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    p = malloc(size);
    assert(p);
    ++live;
    return p;
}

static void launch_free(void* p)
{
    if (p)
        --live;
    free(p);
}

HWND cygwin_sdlwindow(void)
{
    return NULL;
}

static HINSTANCE WINAPI capture_shell(HWND window, LPCSTR operation, LPCSTR command, LPCSTR parameters,
                                      LPCSTR directory, INT show)
{
    int argc, i, size;
    wchar_t *wide, **args;
    char* line;
    ++calls;
    assert(!strcmp(command, "C:\\Apps\\Native.lnk") && parameters);
    line = malloc(strlen(parameters) + 16);
    assert(line);
    sprintf(line, "native.exe %s", parameters);
    size = MultiByteToWideChar(CP_ACP, 0, line, -1, NULL, 0);
    wide = malloc(size * sizeof *wide);
    assert(wide);
    assert(MultiByteToWideChar(CP_ACP, 0, line, -1, wide, size));
    args = CommandLineToArgvW(wide, &argc);
    assert(args && argc == expected_count + 1);
    for (i = 0; i < expected_count; ++i) {
        char* actual;
        int j;
        size = WideCharToMultiByte(CP_ACP, 0, args[i + 1], -1, NULL, 0, NULL, NULL);
        actual = malloc(size);
        assert(actual);
        assert(WideCharToMultiByte(CP_ACP, 0, args[i + 1], -1, actual, size, NULL, NULL));
        assert(strlen(actual) == strlen(expected[i]));
        for (j = 0; actual[j]; ++j)
            assert(actual[j] == (expected[i][j] == '/' ? '\\' : expected[i][j]));
        free(actual);
    }
    LocalFree(args);
    free(wide);
    free(line);
    return (HINSTANCE)shell_result;
}

static int relaunch_calls, closed_handles, process_success = 1;

static BOOL WINAPI capture_process(LPCSTR executable, LPSTR command, LPSECURITY_ATTRIBUTES process,
                                   LPSECURITY_ATTRIBUTES thread, BOOL inherit, DWORD flags, LPVOID environment,
                                   LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION result)
{
    const char* entry;
    int system = 0, application = 0;
    ++relaunch_calls;
    assert(executable && *executable && !inherit && !flags && !directory);
    assert(startup->cb == sizeof *startup);
    assert(strstr(command, "\"romaw\" \"-syszone\" \"8m\" \"D:/app name\" \"tail\\\\\""));
    for (entry = environment; *entry; entry += strlen(entry) + 1) {
        if (!strncmp(entry, "ROMAW_ECF_SYSZONE=", sizeof "ROMAW_ECF_SYSZONE=" - 1)) {
            assert(!strcmp(entry, "ROMAW_ECF_SYSZONE=8388608"));
            ++system;
        }
        if (!strncmp(entry, "ROMAW_ECF_APPLZONE=", sizeof "ROMAW_ECF_APPLZONE=" - 1)) {
            assert(!strcmp(entry, "ROMAW_ECF_APPLZONE=16777216"));
            ++application;
        }
        assert(strncmp(entry, "ROMAW_ECF_BROWSER=", sizeof "ROMAW_ECF_BROWSER=" - 1));
    }
    assert(system == 1 && application == 1);
    result->hThread = (HANDLE)1;
    result->hProcess = (HANDLE)2;
    return process_success;
}

static BOOL WINAPI capture_close(HANDLE handle)
{
    assert(handle == (HANDLE)1 || handle == (HANDLE)2);
    ++closed_handles;
    return TRUE;
}

#define CreateProcessA capture_process
#define CloseHandle capture_close
#define malloc launch_malloc
#define free launch_free
#define ShellExecuteA capture_shell
#include "../../platform/os/cygwin32/win_launch.c"
#undef malloc
#undef free
#undef main

int main(void)
{
    char* names[] = {"C:/Apps/Native.lnk",       "D:/folder with spaces/", "",
                     "C:\\test\\\"quoted\".txt", "\\\\server\\share\\",    "ordinary.txt"};
    char* bad[] = {"C:/Apps/Native.lnk", NULL};
    char *large, *quoted;
    int i;
    expected_count = 0;
    assert(!ROMlib_launch_native_app(1, names) && calls == 1 && !live);
    expected_count = 5;
    expected = names + 1;
    assert(!ROMlib_launch_native_app(6, names) && calls == 2 && !live);
    shell_result = 32;
    assert(ROMlib_launch_native_app(6, names) == paramErr && calls == 3 && !live);
    assert(ROMlib_launch_native_app(0, NULL) == paramErr);
    assert(ROMlib_launch_native_app(-1, names) == paramErr);
    assert(ROMlib_launch_native_app(1, NULL) == paramErr);
    assert(ROMlib_launch_native_app(2, bad) == paramErr && calls == 3 && !live);
    for (i = 0; i < 2; ++i) {
        fail_after = i;
        assert(ROMlib_launch_native_app(6, names) == paramErr && calls == 3 && !live);
    }
    fail_after = -1;
    large = malloc(1000001);
    assert(large);
    memset(large, '\\', 1000000);
    large[1000000] = 0;
    quoted = construct_command_line(1, &large);
    assert(quoted && strlen(quoted) == 2000002);
    launch_free(quoted);
    free(large);
    assert(!live);

    {
        char* arguments[] = {"romaw", "-syszone", "8m", "D:/app name", "tail\\", NULL};
        _putenv("ROMAW_ECF_SYSZONE=old");
        _putenv("ROMAW_ECF_APPLZONE=old");
        _putenv("ROMAW_ECF_BROWSER=old");
        assert(ROMlib_win_relaunch(5, arguments, 8388608, 16777216));
        assert(relaunch_calls == 1 && closed_handles == 2 && !live);
        assert(!strcmp(getenv("ROMAW_ECF_SYSZONE"), "old"));
        process_success = 0;
        assert(!ROMlib_win_relaunch(5, arguments, 8388608, 16777216));
        assert(relaunch_calls == 2 && closed_handles == 2 && !live);
        for (i = 0; i < 3; ++i) {
            fail_after = i;
            assert(!ROMlib_win_relaunch(5, arguments, 8388608, 16777216));
            assert(!live && relaunch_calls == 2);
        }
        fail_after = -1;
        _putenv("ROMAW_ECF_SYSZONE=");
        _putenv("ROMAW_ECF_APPLZONE=");
        _putenv("ROMAW_ECF_BROWSER=");
    }
    puts(
        "PASS: empty native arguments, Windows quoting round trips, allocation failures and long inputs; no programs launched");
    return 0;
}
