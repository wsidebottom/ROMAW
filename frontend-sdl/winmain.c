/*+===================================================================
  File:      winmain.c

  Summary:   WINMAIN.C, placed in the public domain by Sam Lantinga 4/13/98

  Classes:   None.

  Functions: WinMain

  Origin:    Existing repository source in executor/src/config/front-ends/sdl; original attribution and provenance
             comments are preserved below.

  Copyright and Legal notices: Original public-domain and warranty notices are retained below.
===================================================================+*/

/*
    WINMAIN.C, placed in the public domain by Sam Lantinga  4/13/98

    The WinMain function -- calls your program's main() function
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <windows.h>

#include "paramline.h"
#include "generated/build_version.h"
#include "rsys/desktop.h"

#include "SDL/SDL.h"

extern int romaw_finder_main(void);

/* Decide before SDL or the emulator can write startup diagnostics. */
static int debug_output_requested(int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0)
            break;
        if (strcmp(argv[i], "-debug") == 0)
            return 1;
    }
    return 0;
}

static void setup_output(int debug)
{
    if (debug) {
        FILE* console;

        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            AllocConsole();
        console = fopen("CONOUT$", "w");
        if (console) {
            fprintf(console, "ROMAW debug output: stdout.txt and stderr.txt\n");
            fclose(console);
        }
        if (!freopen("stdout.txt", "w", stdout))
            freopen("CONOUT$", "w", stdout);
        if (!freopen("stderr.txt", "w", stderr))
            freopen("CONOUT$", "w", stderr);
    }
    else {
        freopen("NUL", "w", stdout);
        freopen("NUL", "w", stderr);
    }
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
}

/* Preserve redirected stdout; otherwise write to the invoking terminal. */
static int print_output(const char* text)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE console = INVALID_HANDLE_VALUE;
    size_t remaining = strlen(text);
    int result = 0;

    if (!output || output == INVALID_HANDLE_VALUE || GetFileType(output) == FILE_TYPE_UNKNOWN) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            return 0;
        console =
            CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        output = console;
    }
    while (remaining) {
        DWORD written;
        if (!WriteFile(output, text, (DWORD)remaining, &written, NULL) || !written) {
            result = 1;
            break;
        }
        text += written;
        remaining -= written;
    }
    if (console != INVALID_HANDLE_VALUE)
        CloseHandle(console);
    return result;
}

/* This is where execution begins */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR szCmdLine, int sw)
{
    char* appname;
    char* ptr;
    int i;
    char** argv;
    int argc;

    paramline_to_argcv(GetCommandLine(), &argc, &argv);
    if (argc < 1)
        return 1;
    if (ROMlib_help_requested(argc, argv))
        return print_output(ROMlib_command_line_help());
    if (ROMlib_version_requested(argc, argv))
        return print_output(ROMAW_VERSION "\n");

    /* The built-in desktop uses the same initialized QuickDraw/Window/Menu
       Managers as applications. SDL's presentation window stays hidden; the
       Finder keeps the outer Windows frame and executes native x64 code. */
    SetProcessDPIAware();
    /* Argument parsing disables this default when an application is
       supplied. Options such as -debug trace still open the native desktop. */
    ROMlib_desktop_set_entry(romaw_finder_main);

    /* A PATH launch can leave argv[0] as just "romaw".  Runtime
	 * folders must be located relative to the executable, not that name
	 * or the caller's working directory. */
    {
        char executable_path[MAX_PATH];
        DWORD length;
        char* resolved;

        length = GetModuleFileNameA(NULL, executable_path, sizeof executable_path);
        if (!length || length >= sizeof executable_path)
            return 1;
        resolved = malloc(length + 1);
        if (!resolved)
            return 1;
        memcpy(resolved, executable_path, length + 1);
        free(argv[0]);
        argv[0] = resolved;
    }

    /*
	 * Direct Draw has a nasty bug where a file that is opened before
	 * Direct Draw is invoked, *stays* open until the system is rebooted.
	 * So we need to get Direct Draw loaded before we open any files.
	 */

    {
        HANDLE h;

        h = LoadLibrary("DDRAW.DLL");
        if (h)
            FreeLibrary(h);
    }
    setup_output(debug_output_requested(argc, argv));

    /* Get the class name from argv[0] */
    /* Basename... */
    if ((ptr = strrchr(argv[0], '\\')) == NULL)
        appname = argv[0];
    else
        appname = ptr + 1;
    /* minus extension... */
    if ((ptr = strrchr(appname, '.')) == NULL)
        i = strlen(appname);
    else
        i = (ptr - appname);
    /* equals appname! */
    ptr = (char*)alloca(i + 1);
    strncpy(ptr, appname, i);
    ptr[i] = '\0';
    appname = ptr;

    /* Load SDL dynamic link library */
    if (SDL_Init(0) < 0) {
        fprintf(stderr, "WinMain() error: %s\n", SDL_GetError());
        return (FALSE);
    }
    atexit(SDL_Quit);

    /* Create and register our class, then run main code */
    if (SDL_RegisterApp(appname, CS_BYTEALIGNCLIENT, hInst) < 0) {
        fprintf(stderr, "WinMain() error: %s\n", SDL_GetError());
        return (FALSE);
    }
    SDL_main(argc, argv);

    exit(0);
}
