/*+===================================================================
  File:      main.c

  Summary:   Implements main support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: ROMlib_command_line_help, romaw_dir_fopen, romaw_dir_remove, parse_drive_opt

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1988 - 2005 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/*
 * Errors should be handled more cleanly.
 */

#include "rsys/common.h"
#include "rsys/mixed_mode.h"
#include "rsys/ecf.h"
#include "rsys/desk.h"
#include "rsys/configprefs.h"
#include "rsys/options.h"

#include "QuickDraw.h"
#include "CQuickDraw.h"
#include "SegmentLdr.h"
#include "rsys/notmac.h"
#include "rsys/prefpanel.h"
#include "rsys/user_profile.h"
#include "MemoryMgr.h"
#include "FontMgr.h"
#include "SysErr.h"
#include "OSUtil.h"
#include "ResourceMgr.h"
#include "TextEdit.h"
#include "FileMgr.h"
#include "DialogMgr.h"
#include "StdFilePkg.h"
#include "MenuMgr.h"
#include "ScrapMgr.h"
#include "SoundMgr.h"
#include "ScriptMgr.h"
#include "SANE.h"
#include "DeskMgr.h"
#include "DeviceMgr.h"
#include "ToolboxUtil.h"
#include "Gestalt.h"
#include "AppleTalk.h"
#include "AppleEvents.h"

#include "rsys/cquick.h"
#include "rsys/tesave.h"
#include "rsys/mman.h"
#include "rsys/osutil.h"
#include "rsys/menu.h"
#include "rsys/pstuff.h"
#include "rsys/prefs.h"
#include "rsys/misc.h"
#include "rsys/flags.h"
#include "rsys/option.h"
#include "rsys/host.h"
#include "rsys/syncint.h"
#include "rsys/vdriver.h"
#include "rsys/vbl.h"
#include "rsys/time.h"
#include "rsys/segment.h"
#include "rsys/keycode.h"
#include "rsys/version.h"
#include "rsys/m68kint.h"
#include "rsys/misc.h"
#include "rsys/blockinterrupts.h"
#include "rsys/rgbutil.h"
#include "rsys/refresh.h"
#include "rsys/romaw.h"
#include "rsys/wind.h"
#include "rsys/osevent.h"
#include "rsys/image.h"
#include "rsys/dump.h"
#include "rsys/file.h"
#include "rsys/ctl.h"
#include "rsys/parseopt.h"
#include "rsys/hfs.h"
#include "rsys/macinfo.h"
#include "rsys/print.h"
#include "rsys/memsize.h"
#include "rsys/stdfile.h"
#include "rsys/autorefresh.h"
#include "rsys/sounddriver.h"
#include "rsys/desperate.h"
#include "rsys/dcache.h"
#include "rsys/system_error.h"
#include "rsys/filedouble.h"

#include "rsys/os.h"
#include "rsys/arch.h"
#include "rsys/checkpoint.h"
#include "rsys/cleanup.h"
#include "rsys/gestalt.h"
#include "rsys/keyboards.h"
#include "rsys/launch.h"
#include "rsys/desktop.h"
#include "rsys/text.h"
#include "rsys/appearance.h"
#include "rsys/hfs_plus.h"

#include "rsys/check_structs.h"

#include "paramline.h"

#include <ctype.h>

#if defined(CYGWIN32)
#include "winfs.h"
#include "dosdisk.h"
#include "win_except.h"
#include "win_queue.h"
#include "win_clip.h"
#include "win_print.h"
#endif

#if defined(X)
#include "x.h"
#endif

PUBLIC int ROMlib_noclock = 0;

#if defined(NOMOUSE_COMMAND_LINE_OPTION)
PUBLIC int ROMlib_no_mouse = 1;
#endif

/* optional resolution other than 72dpix72dpi for printing */
PUBLIC INTEGER ROMlib_optional_res_x, ROMlib_optional_res_y;

/* A string approximating the original command line. */
PUBLIC const char* ROMlib_command_line;

/* Set to TRUE if there was any error parsing arguments. */
PRIVATE boolean_t bad_arg_p = FALSE;

/* Set to FALSE when we know that the command line switches will result
   in no graphics. */

PRIVATE boolean_t graphics_p = TRUE;

/* for a description of flags declared here, see <rsys/flags.h> */

/* Initial screen size.  This can be changed dynamically. */
INTEGER flag_width = 0, flag_height = 0; /* 0 means "use default". */

/* Initial bits per pixel.  The screen depth can be changed dynamically. */
int flag_bpp = 0; /* 0 means "use default". */

INTEGER ROMlib_shadow_screen_p = TRUE;

INTEGER ROMlib_no_windows;

#if defined(CYGWIN32)
int ROMlib_drive_check = 0;
#endif

#if defined(SYN68K)
static boolean_t use_native_code_p = TRUE;
#endif

/* the system version that romaw is currently reporting to
   applications, set through the `-system' options.  contains the
   version number in the form `0xABC' which corresponds to system
   version A.B.C */
uint32 system_version = 0x711; /* default: Macintosh System 7.1.1 */

static option_t common_opts[] = {
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    {"ppc", "prefer the PowerPC application fragment when available", opt_no_arg, NULL},
    {"ppc-jit", "enable experimental PowerPC native integer blocks", opt_no_arg, NULL},
    {"ppc-aot", "pretranslate and cache PowerPC code across launches", opt_no_arg, NULL},
    {"ppc-system", "use the experimental PowerPC 750 CPU/MMU core", opt_no_arg, NULL},
    {"no-ppc-jit", "interpret PowerPC code without native integer blocks", opt_no_arg, NULL},
    {"ppc-startup-retry", "internal PowerPC startup retry marker", opt_no_arg, NULL},
#endif
    {"sticky", "sticky menus", opt_no_arg, NULL},
    {"pceditkeys", "have Delete key delete one character forward", opt_no_arg, NULL},
    {"bpp", "default screen depth", opt_sep, NULL},
    {"size", "startup client size WIDTHxHEIGHT; overrides saved application size", opt_sep, NULL},
    {"window-size", "startup client size WIDTHxHEIGHT (minimum 512x342); overrides saved size", opt_sep, NULL},
#if defined(SDL)
    {"fixed-window-size", "lock client size to WIDTHxHEIGHT; disables resizing and maximizing", opt_sep, NULL},
    {"display-scale", "scale the host window by PERCENT (25..1600; 100=1x, 150=1.5x, 200=2x)", opt_sep, NULL},
#endif
    {"debug",
     ("enable certain debugging output and consistency checks.  This "
      "is primarily used by ARDI developers, but we are making it "
      "available during the pre-beta period to expert users.  The next "
      "argument must be a list of comma-separated words describing which "
      "debug options you want enabled.  You can abbreviate the debug "
      "options as long as the abbreviation is unambiguous.  Here is a "
      "list of the options (some of which will may do nothing):  "
      "\"all\" enables all debugging options, "
      "\"fslog\" enables filesystem call logging, "
      "\"memcheck\" enables heap consistency checking (slow!), "
      "\"textcheck\" enables text record consistency checking (slow!), "
      "\"trace\" enables miscellaneous trace information, "
      "\"sound\" enables miscellaneous sound logging information, "
      "\"trapfailure\" enables warnings when traps return error codes, "
      "\"errno\" enables some C library-related warnings, "
      "\"unexpected\" enables warnings for unexpected events, "
      "\"unimplemented\" enables warnings for unimplemented traps.  "
      "Example: \"ROMAW -debug unimp,trace\""),

     opt_sep, NULL},
    {"mac-info", "show an item in ROMAW Finder Mac Get Info", opt_sep, NULL},
    {"desk-resource", "open the specified DRVR resource in an accessory file", opt_sep, NULL},
    {"mount-image", "choose and mount a Macintosh disk image read-only", opt_no_arg, NULL},
    {"mount", "mount a disk image (optical images default to read-only)", opt_sep, NULL},
    {"mount-ro", "mount a disk image read-only", opt_sep, NULL},
    {"mount-rw", "request read/write disk image access (including optical images)", opt_sep, NULL},
    {"nodiskcache", "disable internal disk cache.", opt_no_arg, NULL},
    {"ramlog",
     "log debugging information to RAM; alt-shift-7 dumps the "
     "accrued error log out via the normal mechanism.",
     opt_no_arg, NULL},

#if defined(CYGWIN32)
    {"macdrives", "drive letters that represent Mac formatted media", opt_sep, NULL},
    {"dosdrives", "drive letters that represent DOS formatted media", opt_sep, NULL},
    {"skipdrives", "drive letters that represent drives to avoid", opt_sep, NULL},
#endif
    {"keyboards", "list available keyboard mappings", opt_no_arg, NULL},
    {"keyboard", "choose a specific keyboard map", opt_sep, NULL},
    {"print",
     "tell program to print file; not useful unless you also "
     "specify a program to run and one or more documents to print.",
     opt_no_arg, NULL},

#if defined(NOMOUSE_COMMAND_LINE_OPTION)
    /* Hack Dr. Chung wanted */
    {"nomouse", "ignore missing mouse", opt_no_arg, NULL},
#endif
    {"noautorefresh", "turns off automatic detection of programs that bypass QuickDraw.", opt_no_arg, NULL},
    {
        "refresh",
        "handle programs that bypass QuickDraw, at a performance penalty.  "
        "Follow -refresh with an number indicating how many 60ths of a second "
        "to wait between each screen refresh, e.g. \"ROMAW -refresh 10\".",
        opt_optional,
        "10",
    },
    {
        "help",
        "print all options and exit",
        opt_no_arg,
        NULL,
    },
    {
        "version",
        "print the ROMAW version",
        opt_no_arg,
        NULL,
    },
#if defined(MALLOC_MAC_MEMORY)
    {
        "memory",
        "specify the total memory you want reserved for use by the programs "
        "run under ROMAW and for ROMAW's internal system software.  "
        "For example, \"ROMAW -memory 5.5M\" would "
        "make five and a half megabytes available to the virtual machine.  "
        "ROMAW will require extra memory above and beyond this amount "
        "for other uses.",
        opt_sep,
        NULL,
    },
    {
        "applzone",
        "specify the memory to allocate for the application being run, "
        "e.g. \"ROMAW -applzone 4M\" would make four megabytes "
        "of RAM available to the application.  \"applzone\" stands for "
        "\"application zone\".",
        opt_sep,
        NULL,
    },
    {
        "stack",
        "like -applzone, but specifies the amount of stack memory to allocate.",
        opt_sep,
        NULL,
    },
    {
        "syszone",
        "like -applzone, but specifies the amount of memory to make "
        "available to ROMAW's internal system software.",
        opt_sep,
        NULL,
    },
#if defined(MM_MANY_APPLZONES)
    {"napplzones", "debugging flag, specifies the number of applzones to use", opt_sep, NULL},
#endif /* MM_MANY_APPLZONES */
#endif /* MALLOC_MAC_MEMORY */
    {"system", "specify the system version that ROMAW reports to applications (default 7.1.1)", opt_sep, NULL},
#if defined(SYN68K)
    {"24bit", "force 24-bit Macintosh addressing", opt_no_arg, NULL},
    {"32bit", "force 32-bit Macintosh addressing", opt_no_arg, NULL},
    {"notnative", "don't use native code in syn68k", opt_no_arg, NULL},
#endif

#if defined(CYGWIN32)
    {"logerr", "log diagnostic output to error log file", opt_no_arg, NULL},
#endif

    {"grayscale", "\
specify that romaw should run in grayscale mode even if it is \
capable of color.",
     opt_no_arg, NULL},
    {"netatalk", "use .AppleDouble/name for new sidecars (default ._name)", opt_no_arg, NULL},
    {"afpd", "use afpd conventions for AppleDouble files (implies -netatalk)", opt_no_arg, NULL},
    {"basiliskii", "use .finf/name and .rsrc/name for new sidecars in unmarked directories", opt_no_arg, NULL},
    {"executor", "use legacy %name AppleDouble files for new sidecars in unmarked directories", opt_no_arg, NULL},

    {"cities",
     "Don't use Helvetica for Geneva, Courier for Monaco and Times "
     "for New York when generating PostScript",
     opt_no_arg, NULL},

#if defined(CYGWIN32)
    {"die", "allow ROMAW to die instead of catching trap", opt_no_arg, NULL},
    {"noautoevents", "disable timer driven event checking", opt_no_arg, NULL},
#endif

#if defined(CYGWIN32)
    {"nocheckpoint", "disable \"failure.txt\" checkpointing", opt_no_arg, NULL},
#endif

    {"prvers", "specify the printer version that ROMAW reports to applications", opt_sep, NULL},

    {"prres",
     "specify an additional resolution available for printing, e.g. "
     "\"ROMAW -prres 600x600\" will make 600dpi resolution available "
     "in addition to the standard 72dpi.  Not all apps will be able to use "
     "this additional resolution.",
     opt_sep, NULL},

#if defined(SDL)
    {"fullscreen", "try to run in full-screen mode", opt_no_arg, NULL},
#if defined(CYGWIN)
    {
        "sdlaudio",
        "specify the audio driver to attempt to use first, e.g. "
        "\"ROMAW -sdlaudio waveout\" will tell ROMAW to use the old "
        "windows sound drivers and not use DirectSound.",
        opt_sep,
        NULL,
    },
#else
    {
        "sdlaudio",
        "specify the audio driver to attempt to use first, e.g. "
        "\"ROMAW -sdlaudio esd\" will tell ROMAW to use esound, "
        "the Enlightened Sound Daemon instead of /dev/dsp.",
        opt_sep,
        NULL,
    },
#endif
#endif

#if defined(X) || defined(SDL)
    {"scancodes",
     "different form of key mapping (may be useful in "
     "conjunction with -keyboard)",
     opt_no_arg, NULL},
#endif

    {"desperate", /* Handled specially; here for documentation purposes only. */
     "run in \"desperation mode\".  This will cause ROMAW "
     "to use as few system features as possible, which is handy for "
     "troubleshooting if ROMAW is having serious problems with "
     "your system.",
     opt_no_arg, NULL},

#if defined(CYGWIN32)
    {"realmodecd", "try to use real-mode cd-rom driver", opt_no_arg, NULL},
#endif

    {"appearance",
     "(mac or windows) specify the appearance of windows and "
     "menus.  For example \"ROMAW -appearance windows\" will make each "
     "window have a blue title bar",
     opt_sep, NULL},

    {"hfsplusro", "unsupported -- do not use", opt_no_arg, NULL},
};

opt_database_t* common_db;

/* Prints the specified value in a representation appropriate for command
 * line switches.
 */
static void print_command_line_value(FILE* fp, int v)
{
    if (v > 0 && v % 1024 == 0) {
        if (v % (1024 * 1024) == 0)
            fprintf(fp, "%dM", v / (1024 * 1024));
        else
            fprintf(fp, "%dK", v / 1024);
    }
    else
        fprintf(fp, "%d", v);
}

static void check_arg(char* argname, int* arg, int min, int max)
{
    if (*arg < min || *arg > max) {
        fprintf(stderr, "%s: invalid value for `%s': must be between ", program_name, argname);
        print_command_line_value(stderr, min);
        fputs(" and ", stderr);
        print_command_line_value(stderr, max);
        fputs(" inclusive.\n", stderr);
        bad_arg_p = TRUE;
    }
}

PUBLIC char ROMlib_startdir[MAXPATHLEN];
PUBLIC INTEGER ROMlib_startdirlen;
#if defined(CYGWIN32)
PUBLIC char ROMlib_start_drive;
#endif
PUBLIC char* ROMlib_appname;

ULONGINT ROMlib_ourmtime;

#if defined(CYGWIN32)
PUBLIC char ROMlib_savecwd[MAXPATHLEN];

PRIVATE void cd_back(void)
{
    if (ROMlib_savecwd[0])
        Uchdir(ROMlib_savecwd);
}
#endif

PRIVATE void set_appname(char* argv0)
{
    ROMlib_appname = strrchr(argv0, '/');
#if defined(CYGWIN32)
    if (!ROMlib_appname)
        ROMlib_appname = strrchr(argv0, '\\');
#endif
    if (ROMlib_appname)
        ++ROMlib_appname;
    else
        ROMlib_appname = argv0;
}

A1(PRIVATE, void, setstartdir, char*, argv0)
{
#if defined(CYGWIN32)
    atexit(call_cleanup_bat);
#endif

    /*
 * When run under DOS, argv[0] contains a full path name, including
 * a dos-drive and colon at the beginning (e.g. C:/fratzand/romaw).
 * However, you get neither when running under DOS under gdb.  Yahoo.
 */
    if (!getcwd(ROMlib_savecwd, sizeof(ROMlib_savecwd)))
        ROMlib_savecwd[0] = 0;
    else
        atexit(cd_back);
    if (argv0[1] == ':') {
        char* lastslash;
        static char cd_to[] = "C:/";

        ROMlib_start_drive = argv0[0];
        cd_to[0] = argv0[0];
        Uchdir(cd_to);
        /* We now include the drive letter in with ROMlib_startdir. */

        strcpy(ROMlib_startdir, argv0);

        lastslash = strrchr(ROMlib_startdir, '/');
#if defined(CYGWIN32)
        if (!lastslash)
            lastslash = strrchr(ROMlib_startdir, '\\');
#endif
        if (lastslash)
            *lastslash = 0;
    }
    else {
        ROMlib_start_drive = 0;
        strcpy(ROMlib_startdir, ".");
    }
    ROMlib_startdirlen = strlen(ROMlib_startdir);
}

PUBLIC BOOLEAN ROMlib_startupscreen = TRUE;

char* program_name;

#if defined(SYN68K)
static syn68k_addr_t unhandled_trap(syn68k_addr_t callback_address, void* arg)
{
    static const char* trap_description[] = {/* I've only put the really interesting ones in. */
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             "Illegal instruction",
                                             "Integer divide by zero",
                                             "CHK/CHK2",
                                             "FTRAPcc, TRAPcc, TRAPV",
                                             "Privilege violation",
                                             "Trace",
                                             "A-line",
                                             "FPU",
                                             NULL,
                                             NULL,
                                             "Format error"};
    int trap_num;
    char pc_str[128];

    trap_num = (long)arg;

    switch (trap_num) {
    case 4:  /* Illegal instruction. */
    case 8:  /* Privilege violation. */
    case 10: /* A-line. */
    case 11: /* F-line. */
    case 14: /* Format error. */
    case 15: /* Uninitialized interrupt. */
    case 24: /* Spurious interrupt. */
    case 25: /* Level 1 interrupt autovector. */
    case 26: /* Level 2 interrupt autovector. */
    case 27: /* Level 3 interrupt autovector. */
    case 28: /* Level 4 interrupt autovector. */
    case 29: /* Level 5 interrupt autovector. */
    case 30: /* Level 6 interrupt autovector. */
    case 31: /* Level 7 interrupt autovector. */
    case 32: /* TRAP #0 vector. */
    case 33: /* TRAP #1 vector. */
    case 34: /* TRAP #2 vector. */
    case 35: /* TRAP #3 vector. */
    case 36: /* TRAP #4 vector. */
    case 37: /* TRAP #5 vector. */
    case 38: /* TRAP #6 vector. */
    case 39: /* TRAP #7 vector. */
    case 40: /* TRAP #8 vector. */
    case 41: /* TRAP #9 vector. */
    case 42: /* TRAP #10 vector. */
    case 43: /* TRAP #11 vector. */
    case 44: /* TRAP #12 vector. */
    case 45: /* TRAP #13 vector. */
    case 46: /* TRAP #14 vector. */
    case 47: /* TRAP #15 vector. */
    case 3:  /* Address error. */
    case 6:  /* CHK/CHK2 instruction. */
    case 7:  /* FTRAPcc, TRAPcc, TRAPV instructions. */
    case 9:  /* Trace. */
    case 5:  /* Integer divide by zero. */
        sprintf(pc_str, "0x%lX", (unsigned long)READUL(EM_A7 + 2));
        break;
    default:
        strcpy(pc_str, "<unknown>");
        break;
    }

    if (trap_num < (int)NELEM(trap_description) && trap_description[trap_num] != NULL)
        gui_fatal("Fatal error:  unhandled trap %d at pc %s (%s)", trap_num, pc_str, trap_description[trap_num]);
    else
        gui_fatal("Fatal error:  unhandled trap %d at pc %s", trap_num, pc_str);

    /* It will never get here; this is just here to quiet gcc. */
    return callback_address;
}

static void setup_trap_vectors(void)
{
    syn68k_addr_t timer_callback;
    int i;

    /* Set up the trap vector for the timer interrupt. */
    timer_callback = callback_install(catchalarm, NULL);
    *(syn68k_addr_t*)SYN68K_TO_US(M68K_TIMER_VECTOR * 4) = CL(timer_callback);

    /* Fill in unhandled trap vectors so they cause graceful deaths.
   * Skip over those trap vectors which are known to have legitimate
   * values.
   */
    for (i = 1; i < 64; i++)
        if (i != 10 /* A line trap.       */
#if defined(M68K_TIMER_VECTOR)
            && i != M68K_TIMER_VECTOR /* timer interrupt.   */
#endif

#if defined(M68K_WATCHDOG_VECTOR)
            && i != M68K_WATCHDOG_VECTOR /* watchdog timer interrupt. */
#endif

#if defined(M68K_EVENT_VECTOR)
            && i != M68K_EVENT_VECTOR
#endif

#if defined(M68K_MOUSE_MOVED_VECTOR)
            && i != M68K_MOUSE_MOVED_VECTOR
#endif

#if defined(M68K_SOUND_VECTOR)
            && i != M68K_SOUND_VECTOR
#endif

            && i != 0x58 / 4) /* Magic ARDI vector. */
        {
            syn68k_addr_t c;
            c = callback_install(unhandled_trap, (void*)i);
            *(syn68k_addr_t*)SYN68K_TO_US(i * 4) = CL(c);
        }
}
#endif /* SYN68K */

static void illegal_mode(void) __attribute__((noreturn));

static void illegal_mode(void)
{
    const vdriver_modes_t* vm;
    int num_sizes, max_width, max_height, max_bpp;

    vm = vdriver_mode_list;
    num_sizes = vm->num_sizes;
    max_bpp = vdriver_max_bpp;
    if (num_sizes) {
        max_width = vm->size[num_sizes - 1].width;
        max_height = vm->size[num_sizes - 1].height;
    }
    else
        max_width = max_height = 0; /* quiet gcc */

    vdriver_shutdown(); /* Just to be safe. */
    if (num_sizes == 0)
        fprintf(stderr, "Unable to find legal graphics mode.\n");
    else {
        fprintf(stderr,
                "Invalid graphics mode.  Largest allowable "
                "screen %dx%d, %d bits per pixel.\n",
                max_width, max_height, max_bpp);
    }

    /* Don't call ExitToShell here; ROMlib isn't set up enough yet to do that. */
    exit(-1);
}

#if defined(CYGWIN32)
PUBLIC uint32 ROMlib_macdrives; /* default computed at runtime */
PUBLIC uint32 ROMlib_dosdrives = ~0;
PUBLIC uint32 ROMlib_skipdrives = 0;

PRIVATE boolean_t drive_number_from_letter(char c, int* nump)
{
    boolean_t retval;

    if (isupper(c) || ((c >= '[' && c <= '`'))) {
        *nump = c - 'A';
        retval = TRUE;
    }
    else if (islower(c)) {
        *nump = c - 'a';
        retval = TRUE;
    }
    else
        retval = FALSE;
    return retval;
}

PRIVATE void drive_error(const char* opt)
{
    fprintf(stderr,
            "Invalid drive specification.\nUse something like "
            "\"-%s A-EJ\" (for drives A, B, C, D, E and J).\n",
            opt);
    bad_arg_p = TRUE;
}

PRIVATE const char* munch_next_char(const char* p, uint32* destp, const char* opt)
{
    const char* retval;
    int d;

    if (drive_number_from_letter(p[0], &d)) {
        if (p[1] == '-') {
            int d2;
            if (drive_number_from_letter(p[2], &d2)) {
                int lower, upper, i;

                lower = MIN(d, d2);
                upper = MAX(d, d2);
                for (i = lower; i <= upper; ++i)
                    *destp |= (1 << i);
                retval = p + 3;
            }
            else {
                drive_error(opt);
                retval = "";
            }
        }
        else {
            *destp |= 1 << d;
            retval = p + 1;
        }
    }
    else {
        drive_error(opt);
        retval = "";
    }
    return retval;
}

PUBLIC uint32 parse_drive_opt(const char* opt_name, const char* opt_value)
{
    uint32 retval;
    const char* next_charp;

    retval = 0;
    for (next_charp = opt_value; *next_charp;)
        next_charp = munch_next_char(next_charp, &retval, opt_name);
    return retval;
}
#endif

/* This is to tell people about the switch from "-applzone 4096" to
 * "-applzone 4M".
 */
static void note_memory_syntax_change(const char* arg, unsigned val)
{
    /* Make sane error messages when the supplied value is insane.
   * Otherwise, try to print an example that illustrates how to
   * use the value they are trying to generate.
   */
    if (val < 100 || val > 1000000)
        val = 2048;

    fprintf(stderr,
            "Specified %s is too small.  The syntax "
            "has changed; now, for a %u.%02u\nmegabyte %s you would "
            "say \"-%s ",
            arg, val / 1024, (val % 1024) * 100 / 1024, arg, arg);

    if (val % 1024 == 0)
        fprintf(stderr, "%uM", val / 1024);
    else if (val < 1024)
        fprintf(stderr, "%uK", val);
    else
        fprintf(stderr, "%u.%02uM", val / 1024, (val % 1024) * 100 / 1024);

    fputs("\"\n", stderr);

    bad_arg_p = TRUE;
}

PRIVATE int zap_comments(char* buf, int n_left)
{
    char *ip, *op;
    boolean_t last_was_cr;
    int retval;

    ip = buf;
    op = buf;
    retval = 0;
    last_was_cr = TRUE;
    while (n_left > 0) {
        while (n_left > 0 && (*ip != '#' || !last_was_cr)) {
            last_was_cr = *ip == '\n' || *ip == '\r';
            *op++ = *ip++;
            ++retval;
            --n_left;
        }
        if (n_left > 0) {
            while (n_left > 0 && *ip != '\n' && *ip != '\r') {
                ++ip;
                --n_left;
            }
        }
    }
    return retval;
}

#define ROMAW_DIR(filename)                                                  \
    ({                                                                       \
        char* retval;                                                        \
                                                                             \
        retval = alloca(strlen(ROMlib_startdir) + 1 + strlen(filename) + 1); \
        sprintf(retval, "%s/%s", ROMlib_startdir, (filename));               \
        retval;                                                              \
    })

PUBLIC FILE* romaw_dir_fopen(const char* file, const char* perm)
{
    char* pathname;
    FILE* retval;

    pathname = ROMAW_DIR(file);
    retval = fopen(pathname, perm);
    return retval;
}

PUBLIC int romaw_dir_remove(const char* file)
{
    char* pathname;
    int retval;

    pathname = ROMAW_DIR(file);
    retval = remove(pathname);
    return retval;
}

PRIVATE void read_args_from_file(const char* filename, int* argcp, char*** argvpp)
{
    FILE* fp;

    fp = romaw_dir_fopen(filename, "r");
    if (fp) {
        int nread, n_extra_params, i;
        char buf[8192], *bufp;
        char** saveargv;

        nread = fread(buf, 1, sizeof buf, fp);
        fclose(fp);
        nread = zap_comments(buf, nread);
        n_extra_params = count_params(buf, nread);
        saveargv = malloc(((size_t)*argcp + n_extra_params + 1) * sizeof *saveargv);
        if (!saveargv) {
            fprintf(stderr, "Unable to allocate arguments from %s\n", filename);
            exit(1);
        }
        bufp = buf;
        memcpy(saveargv, *argvpp, *argcp * sizeof *saveargv);
        for (i = 0; i < n_extra_params; ++i) {
            saveargv[*argcp + i] = get_param((const char**)&bufp, &nread);
            if (!saveargv[*argcp + i]) {
                while (i > 0)
                    free(saveargv[*argcp + --i]);
                free(saveargv);
                fprintf(stderr, "Unable to allocate arguments from %s\n", filename);
                exit(1);
            }
        }
        *argcp += n_extra_params;
        saveargv[*argcp] = NULL;
        *argvpp = saveargv;
    }
}

#if defined(CYGWIN32)
PRIVATE uint32 win_drive_to_bit(const char* drive_namep)
{
    uint32 retval;

    if (drive_namep[1] == ':')
        retval = 1 << (tolower(drive_namep[0]) - 'a');
    else {
        warning_unexpected("drive name = '%s'", drive_namep);
        retval = 0;
    }
    return retval;
}
#endif

static int parse_display_scale(const char* text)
{
    unsigned value = 0;
    if (!text || !*text)
        return 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (1600U - (*p - '0')) / 10)
            return 0;
        value = value * 10 + *p - '0';
    }
    return value >= 25 ? value : 0;
}

static void register_command_line_options(void)
{
    opt_register("common", common_opts, NELEM(common_opts));
    opt_register_pre_note("welcome to the ROMAW help message.");
    opt_register_pre_note("usage: `ROMAW [option...] "
                          "[program [document1 document2 ...]]'");
    opt_register_pre_note("Use -help to list options and exit.");
    vdriver_opt_register();
}

const char* ROMlib_command_line_help(void)
{
    opt_init();
    register_command_line_options();
    return opt_help_message();
}

A2(PUBLIC, int, main, int, argc, char**, argv)
{
    if (ROMlib_help_requested(argc, argv)) {
        fputs(ROMlib_command_line_help(), stdout);
        return 0;
    }
    if (ROMlib_version_requested(argc, argv)) {
        fprintf(stdout, "%s\n", ROMAW_VERSION);
        return 0;
    }
    check_structs();
    fprintf(stderr, "trace: check_structs complete\n");
    fflush(stderr);

    INTEGER i;
    static unsigned short jmpl_to_ResourceStub[3] = {
        CWC(0x4EF9), 0, 0 /* Filled in below. */
    };
    long l;
    ULONGINT save_trap_vectors[64];
    virtual_int_state_t int_state;
    THz saveSysZone, saveApplZone;
    Ptr saveApplLimit;
    char* arg;

    int grayscale_p = FALSE;

    ROMlib_command_line = ROMlib_argv_to_string(argc, argv);
    if (!ROMlib_command_line) {
        fprintf(stderr, "Unable to allocate command-line diagnostic text.\n");
        exit(1);
    }

    if (!arch_init()) {
        fprintf(stderr, "Unable to initialize CPU information.\n");
        exit(-100);
    }
    fprintf(stderr, "trace: arch_init complete\n");
    fflush(stderr);

    if (!os_init()) {
        fprintf(stderr, "Unable to initialize operating system features.\n");
        exit(-101);
    }
    fprintf(stderr, "trace: os_init complete\n");
    fflush(stderr);

    {
        char* t;

        t = strrchr(*argv, '/');
        if (t)
            program_name = &t[1];
        else
            program_name = *argv;
    }

    /* Guarantee various time variables are set up properly. */
    msecs_elapsed();

    ROMlib_WriteWhen(WriteInOSEvent);

    setstartdir(argv[0]);
    set_appname(argv[0]);
    fprintf(stderr, "trace: startdir/appname complete\n");
    fflush(stderr);

#define COMMANDS "commands.txt"

    ROMlib_ecf_remember_arguments(argc, argv);
    read_args_from_file(COMMANDS, &argc, &argv);
#if defined(CYGWIN32)
    read_args_from_file(CHECKPOINT_FILE, &argc, &argv);
    checkpointp = checkpoint_init();
#endif

    /* Replace "-desperate" switch with what it implies.  We must do
   * this before normal command line processing.
   */
    if (!handle_desperate_switch(&argc, &argv))
        exit(-1);

    opt_init();
    common_db = opt_alloc_db();
    register_command_line_options();

    if (!bad_arg_p)
        bad_arg_p = opt_parse(common_db, common_opts, NELEM(common_opts), &argc, argv);

    if (opt_val(common_db, "version", NULL)) {
        fprintf(stdout, "%s\n", ROMAW_VERSION);
        exit(0);
    }

    /*
 * If they ask for help, it's not an error -- it should go to stdout
 */

    if (opt_val(common_db, "help", NULL)) {
        fprintf(stdout, "%s", opt_help_message());
        exit(0);
    }

    /* Verify that the user input a legal bits per pixel.  "0" is a legal
   * value here; that means "use the vdriver's default."
   */
    opt_int_val(common_db, "bpp", &flag_bpp, &bad_arg_p);
    if (flag_bpp != 0 && flag_bpp != 1 && flag_bpp != 2 && flag_bpp != 4 && flag_bpp != 8 && flag_bpp != 16 &&
        flag_bpp != 32) {
        fprintf(stderr, "Bits per pixel must be 1, 2, 4, 8, 16 or 32.\n");
        bad_arg_p = TRUE;
    }

#if defined(SDL)
    if (opt_val(common_db, "fullscreen", NULL))
        ROMlib_fullscreen_p = TRUE;
#endif

#if defined(X) || (defined(CYGWIN32) && defined(SDL))
    if (opt_val(common_db, "scancodes", NULL))
        ROMlib_set_use_scancodes(TRUE);
#endif

#if defined(SDL)

    if (opt_val(common_db, "sdlaudio", &arg))
        ROMlib_set_sdl_audio_driver_name(arg);

#endif

    if (opt_val(common_db, "ppc", NULL))
        ROMlib_set_ppc(TRUE);

#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    if (argc > 1)
        ROMlib_desktop_set_entry(NULL);
    ROMlib_ppc_set_jit((opt_val(common_db, "ppc-jit", NULL) || opt_val(common_db, "ppc-aot", NULL)) &&
                       !opt_val(common_db, "no-ppc-jit", NULL));
    ROMlib_ppc_set_aot(opt_val(common_db, "ppc-aot", NULL) && !opt_val(common_db, "no-ppc-jit", NULL));
    ROMlib_ppc_set_system(opt_val(common_db, "ppc-system", NULL));
#endif

    if (opt_val(common_db, "hfsplusro", NULL))
        ROMlib_hfs_plus_support = TRUE;

#if defined(CYGWIN32)
    if (opt_val(common_db, "realmodecd", NULL))
        ROMlib_set_realmodecd(TRUE);
#endif

    if (opt_val(common_db, "size", &arg))
        if (!parse_size_opt("size", arg))
            exit(-10);
    if (opt_val(common_db, "window-size", &arg))
        if (!parse_size_opt("window-size", arg))
            exit(-10);
#if defined(SDL)
    if (opt_val(common_db, "display-scale", &arg)) {
        ROMlib_display_scale_override = parse_display_scale(arg);
        if (!ROMlib_display_scale_override) {
            fprintf(stderr, "Display scale must be an integer percentage from 25 to 1600.\n");
            exit(-10);
        }
        ROMlib_display_scale = ROMlib_display_scale_override;
    }
    if (opt_val(common_db, "fixed-window-size", &arg)) {
        if (!parse_size_opt("fixed-window-size", arg))
            exit(-10);
        ROMlib_fixed_window_p = TRUE;
    }
    if (ROMlib_fixed_window_p && (ROMlib_fullscreen_p || getenv("SDL_FULLSCREEN"))) {
        fprintf(stderr, "Fixed window size cannot be combined with fullscreen.\n");
        exit(-10);
    }
#endif

    if (opt_val(common_db, "prres", &arg))
        bad_arg_p |= !parse_prres_opt(&ROMlib_optional_res_x, &ROMlib_optional_res_y, arg);

    if (opt_val(common_db, "debug", &arg))
        bad_arg_p |= !error_parse_option_string(arg);

#if defined(CYGWIN32)
    if (opt_val(common_db, "macdrives", &arg))
        ROMlib_macdrives = parse_drive_opt("macdrives", arg);
    else {
#if !defined(CYGWIN32)
        ROMlib_macdrives = 3; /* A: + B: */
        {
            int cdrom;

            cdrom = dosdisk_find_cdrom();
            if (cdrom != -1)
                ROMlib_macdrives |= (1 << cdrom);
        }
#else
        char buf[512];

        if (win_GetLogicalDriveStrings(sizeof buf - 1, buf)) {
            char* p;

            for (p = buf; *p; p += strlen(p) + 1)
                if (win_direct_accessible_disk(p))
                    ROMlib_macdrives |= win_drive_to_bit(p);
        }
#endif
    }

    if (opt_val(common_db, "dosdrives", &arg))
        ROMlib_dosdrives = parse_drive_opt("dosdrives", arg);

    if (opt_val(common_db, "skipdrives", &arg)) {
        ROMlib_skipdrives = parse_drive_opt("skipdrives", arg);
        ROMlib_macdrives &= ~ROMlib_skipdrives;
        ROMlib_dosdrives &= ~ROMlib_skipdrives;
    }

#endif

    {
        int nocache;
        nocache = 0;
        opt_int_val(common_db, "nodiskcache", &nocache, &bad_arg_p);
        dcache_set_enabled(!nocache);
    }

#if defined(SYN68K)
    use_native_code_p = !opt_val(common_db, "notnative", NULL);
    if (opt_val(common_db, "24bit", NULL) && opt_val(common_db, "32bit", NULL)) {
        fprintf(stderr, "Choose only one of -24bit and -32bit\n");
        exit(1);
    }
    ROMlib_addressing_mode = opt_val(common_db, "24bit", NULL) ? 0 : opt_val(common_db, "32bit", NULL) ? 1 : -1;
#if defined(CYGWIN32) && defined(SDL)
    /* Native Finder uses offscreen QuickDraw surfaces, not a 24-bit app's
       partition. Leave ample room for several full-colour child windows. */
    if (ROMlib_desktop_active())
        ROMlib_addressing_mode = 1;
    /* Leave room below 16 MB for 24-bit heaps, the native callback window,
     stack and framebuffer. Explicit 32-bit mode has no such constraint. */
    if (!opt_val(common_db, "memory", NULL) && !opt_val(common_db, "applzone", NULL))
        ROMlib_applzone_size = (ROMlib_desktop_active() ? 512 : ROMlib_addressing_mode == 1 ? 64 : 8) * 1024 * 1024;
    if (!opt_val(common_db, "memory", NULL) && !opt_val(common_db, "syszone", NULL))
        ROMlib_syszone_size = (ROMlib_addressing_mode == 1 ? 8 : 2) * 1024 * 1024;
#endif
#endif

    afpd_conventions_p = opt_val(common_db, "afpd", NULL);
    boolean_t netatalk_sidecars = afpd_conventions_p || opt_val(common_db, "netatalk", NULL);
    boolean_t basilisk_sidecars = opt_val(common_db, "basiliskii", NULL);
    boolean_t executor_sidecars = opt_val(common_db, "executor", NULL);
    if (netatalk_sidecars + basilisk_sidecars + executor_sidecars > 1) {
        fprintf(stderr, "Choose only one sidecar format: -netatalk (or -afpd), -basiliskii, or -executor.\n");
        exit(-10);
    }
    /* Retain legacy-fork fallback for every modern layout. */
    netatalk_conventions_p = 1;
    apple_double_quote_char = ':';
    if (netatalk_sidecars)
        apple_double_fork_prefix = ".AppleDouble/";
    else if (basilisk_sidecars)
        apple_double_fork_prefix = ".finf/";
    else if (executor_sidecars)
        apple_double_fork_prefix = "%";
    else
        apple_double_fork_prefix = "._";
#if defined(CYGWIN32)
    /* Colons introduce alternate streams on Windows and cannot name folders.
     Filename escaping is independent of the selected AppleDouble layout. */
    apple_double_quote_char = '%';
#endif
    apple_double_fork_prefix_length = strlen(apple_double_fork_prefix);

    substitute_fonts_p = !opt_val(common_db, "cities", NULL);

#if defined(CYGWIN32)
    if (opt_val(common_db, "die", NULL))
        uninstall_exception_handler();
    if (opt_val(common_db, "noautoevents", NULL))
        set_timer_driven_events(FALSE);
#endif

#if defined(CYGWIN32)
    if (opt_val(common_db, "nocheckpoint", NULL))
        disable_checkpointing();
#endif

    /* Parse the "-memory" option. */
    {
        int total_memory;
        if (opt_int_val(common_db, "memory", &total_memory, &bad_arg_p)) {
            check_arg("memory", &total_memory, (MIN_APPLZONE_SIZE + DEFAULT_SYSZONE_SIZE + DEFAULT_STACK_SIZE),
                      (MAX_APPLZONE_SIZE + DEFAULT_SYSZONE_SIZE + DEFAULT_STACK_SIZE));

            /* Set up the three memory sizes appropriately.  For now we
	 * just allocate the defaults for syszone and stack, and
	 * put everything else in -applzone.
	 */
            ROMlib_syszone_size = DEFAULT_SYSZONE_SIZE;
            ROMlib_stack_size = DEFAULT_STACK_SIZE;
            ROMlib_applzone_size = (total_memory - ROMlib_syszone_size - ROMlib_stack_size);
        }
    }

    /* I bumped the minimal ROMlib_applzone to 512, since Loser needs
     more than 256.  I guess it's a little unfair to people who bypass
     Loser, but it will prevent confusion.  */
    opt_int_val(common_db, "applzone", &ROMlib_applzone_size, &bad_arg_p);
    if (ROMlib_applzone_size < 65536)
        note_memory_syntax_change("applzone", ROMlib_applzone_size);
    else
        check_arg("applzone", &ROMlib_applzone_size, MIN_APPLZONE_SIZE, MAX_APPLZONE_SIZE);

    opt_int_val(common_db, "syszone", &ROMlib_syszone_size, &bad_arg_p);
    if (ROMlib_syszone_size < 65536)
        note_memory_syntax_change("syszone", ROMlib_syszone_size);
    else
        check_arg("syszone", &ROMlib_syszone_size, MIN_SYSZONE_SIZE, MAX_SYSZONE_SIZE);

    opt_int_val(common_db, "stack", &ROMlib_stack_size, &bad_arg_p);
    if (ROMlib_stack_size < 32768)
        note_memory_syntax_change("stack", ROMlib_stack_size);
    else
        check_arg("stack", &ROMlib_stack_size, MIN_STACK_SIZE, MAX_STACK_SIZE);

#if defined(MM_MANY_APPLZONES)
    opt_int_val(common_db, "napplzones", &mm_n_applzones, &bad_arg_p);
    check_arg("napplzones", &mm_n_applzones, 1, /* random */ 255);
#endif /* !MM_MANY_APPLZONES */

    ROMlib_ecf_startup_memory(opt_val(common_db, "memory", NULL) || opt_val(common_db, "syszone", NULL),
                              opt_val(common_db, "memory", NULL) || opt_val(common_db, "applzone", NULL));
    ROMlib_InitZones(offset_big);

    {
        uint32 save_a7;

        save_a7 = EM_A7;
#if defined(SYN68K)
        /* Set up syn68k. */
        fprintf(stderr, "trace: initialize_68k_emulator begin\n");
        fflush(stderr);
        initialize_68k_emulator(vdriver_system_busy,
#if defined(__CHECKER__)
                                FALSE,
#else
                                use_native_code_p,
#endif
                                (uint32*)SYN68K_TO_US(0), 0);
        syn68k_bus_read = ROMlib_powerpc_cache_read;
        fprintf(stderr, "trace: initialize_68k_emulator complete\n");
        fflush(stderr);
#endif /* SYN68K */

        EM_A7 = save_a7;
    }

    if (opt_val(common_db, "keyboards", NULL))
        graphics_p = FALSE;

    {
        char error[1024];
        if (!romaw_profile_legacy_path(ROMlib_userdir, MAXPATHLEN, error, sizeof error) ||
            !romaw_profile_migrate(ROMlib_startdir, error, sizeof error)) {
            fprintf(stderr, "Cannot initialize the ROMAW user profile: %s\n", error);
            exit(1);
        }
    }

    /* Block virtual interrupts, until the system is fully set up. */
    int_state = block_virtual_ints();
    fprintf(stderr, "trace: virtual interrupts blocked\n");
    fflush(stderr);

    if (graphics_p) {
        fprintf(stderr, "trace: vdriver_init begin\n");
        fflush(stderr);
    }

    if (graphics_p && !vdriver_init(0, 0, 0, FALSE, &argc, argv)) {
        fprintf(stderr, "Unable to initialize video driver.\n");
        exit(-12);
    }
    if (graphics_p) {
        fprintf(stderr, "trace: vdriver_init complete\n");
        fflush(stderr);
    }

#if defined(SYN68K)
    /* Save the trap vectors away. */
    memcpy(save_trap_vectors, SYN68K_TO_US(0), sizeof save_trap_vectors);
#endif

    opt_int_val(common_db, "sticky", &ROMlib_sticky_menus_p, &bad_arg_p);

    opt_int_val(common_db, "pceditkeys", &ROMlib_forward_del_p, &bad_arg_p);

    {
        char* path = malloc(strlen(ROMlib_userdir) + sizeof "/ROMAW.prefs");
        if (path) {
            sprintf(path, "%s/ROMAW.prefs", ROMlib_userdir);
            char* master = malloc(strlen(ROMlib_startdir) + sizeof "/ROMAW.prefs.master");
            if (master) {
                sprintf(master, "%s/ROMAW.prefs.master", ROMlib_startdir);
                if (!config_prefs_seed(path, master))
                    fprintf(stderr, "Cannot initialize preferences in %s\n", path);
                free(master);
            }
#if defined(CYGWIN32)
            char* apps = malloc(strlen(ROMlib_userdir) + sizeof "/Apps");
            if (apps) {
                sprintf(apps, "%s/Apps", ROMlib_userdir);
                int configured = config_drive_prefs_load(path, apps);
                free(apps);
                if (configured < 0)
                    bad_arg_p = TRUE;
                else if (!configured && graphics_p && !bad_arg_p)
                    ROMlib_win_drive_preferences(path);
            }
            else
                bad_arg_p = TRUE;
#endif
            free(path);
        }
        else
            bad_arg_p = TRUE;
    }

    opt_int_val(common_db, "print", &ROMlib_print, &bad_arg_p);

#if defined(NOMOUSE_COMMAND_LINE_OPTION)
    opt_int_val(common_db, "nomouse", &ROMlib_no_mouse, &bad_arg_p);
#endif

    {
        int no_auto = FALSE;
        opt_int_val(common_db, "noautorefresh", &no_auto, &bad_arg_p);
        do_autorefresh_p = !no_auto;
    }

    ROMlib_preferences_set_cli_overrides((opt_val(common_db, "sticky", NULL) ? ROMAW_PREF_CLI_STICKY : 0) |
                                         (opt_val(common_db, "pceditkeys", NULL) ? ROMAW_PREF_CLI_FORWARD_DELETE : 0) |
                                         (opt_val(common_db, "noautorefresh", NULL) ? ROMAW_PREF_CLI_AUTO_REFRESH : 0) |
                                         (opt_val(common_db, "cities", NULL) ? ROMAW_PREF_CLI_SUBSTITUTE_FONTS : 0));
    opt_int_val(common_db, "refresh", &ROMlib_refresh, &bad_arg_p);
    check_arg("refresh", &ROMlib_refresh, 0, 60);

    opt_int_val(common_db, "grayscale", &grayscale_p, &bad_arg_p);

    {
        char* str;

        if (opt_val(common_db, "prvers", &str)) {
            uint32 vers;

            if (!ROMlib_parse_version(str, &vers))
                bad_arg_p = TRUE;
            else
                ROMlib_PrDrvrVers = (vers >> 8) * 10 + ((vers >> 4) & 0xF);
        }
    }

    {
        int log;
        log = 0;
        opt_int_val(common_db, "ramlog", &log, &bad_arg_p);
        log_err_to_ram_p = (log != 0);
    }

    /* If we failed to parse our arguments properly, exit now.
   * I don't think we should call ExitToShell yet because the
   * rest of the system isn't initialized.
   */
    if (argc >= 2) {
        int a;

        /* Only complain if we see something with a leading dash; anything
	* else might be a file to launch.
	*/
        for (a = 1; a < argc; a++)
            if (argv[a][0] == '-') {
                fprintf(stderr, "%s: unknown option `%s'\n", program_name, argv[a]);
                bad_arg_p = TRUE;
            }
    }

    filltables();

    l = (long)ostraptable[0x0FC];
    ((unsigned char*)jmpl_to_ResourceStub)[2] = l >> 24;
    ((unsigned char*)jmpl_to_ResourceStub)[3] = l >> 16;
    ((unsigned char*)jmpl_to_ResourceStub)[4] = l >> 8;
    ((unsigned char*)jmpl_to_ResourceStub)[5] = l;
    ostraptable[0xFC] = (void*)US_TO_SYN68K(jmpl_to_ResourceStub);
    osstuff[0xFC].orig = (void*)US_TO_SYN68K(jmpl_to_ResourceStub);

    saveSysZone = SysZone;
    saveApplZone = ApplZone;
    saveApplLimit = ApplLimit;
    memset(&nilhandle_H, ~0, (char*)&lastlowglobal - (char*)&nilhandle_H);

    Ticks_UL.u = 0;
    HIDDEN_RAW_SET(nilhandle_H, 0); /* so nil dereferences "work" */

    memset(&EventQueue, 0, sizeof(EventQueue));
    HIDDEN_RAW_SET(GetNextEventFilter_H, NULL);
    memset(&VBLQueue, 0, sizeof(VBLQueue));
    memset(&DrvQHdr, 0, sizeof(DrvQHdr));
    memset(&VCBQHdr, 0, sizeof(VCBQHdr));
    memset(&FSQHdr, 0, sizeof(FSQHdr));
    TESysJust = 0;
    HIDDEN_RAW_SET(SysZone_H, saveSysZone);
    HIDDEN_RAW_SET(ApplZone_H, saveApplZone);
    HIDDEN_RAW_SET(ApplLimit_H, saveApplLimit);
    BootDrive = 0;
    HIDDEN_RAW_SET(DefVCBPtr_H, 0);
    CurMap = 0;
    HIDDEN_RAW_SET(TopMapHndl_H, 0);
    HIDDEN_RAW_SET(DSAlertTab_H, 0);
    HIDDEN_RAW_SET(ResumeProc_H, 0);
    SFSaveDisk = 0;
    HIDDEN_RAW_SET(GZRootHnd_H, 0);
    ANumber = 0;
    HIDDEN_RAW_SET(ResErrProc_H, 0);
    FractEnable = 0;
    SEvtEnb = 0;
    HIDDEN_RAW_SET(MenuList_H, 0);
    MBarEnable = 0;
    MenuFlash = 0;
    TheMenu = 0;
    HIDDEN_RAW_SET(MBarHook_H, 0);
    HIDDEN_RAW_SET(MenuHook_H, 0);
    HIDDEN_RAW_SET(MenuCInfo_H, NULL);
    HIDDEN_RAW_SET(HeapEnd_H, 0);
    HIDDEN_RAW_SET(ApplLimit_H, 0);
    SoundActive = soundactiveoff;
    PortBUse = 2; /* configured for Serial driver */
    memset(KeyMap, 0, sizeof_KeyMap);
    if (vdriver_grayscale_p || grayscale_p) {
        /* Choose a nice light gray hilite color. */
        HiliteRGB.red = CWC(0xAAAA);
        HiliteRGB.green = CWC(0xAAAA);
        HiliteRGB.blue = CWC(0xAAAA);
    }
    else {
        /* how about a nice yellow hilite color? */
        HiliteRGB.red = CWC(0xFFFF);
        HiliteRGB.green = CWC(0xFFFF);
        HiliteRGB.blue = CWC(0);
    }

    {
        static uint16 ret = CWC(0x4E75);

        HIDDEN_RAW_SET(JCrsrTask_H, (ProcPtr)RM(&ret));
    }

    SET_HILITE_BIT();
    HIDDEN_RAW_SET(TheGDevice_H, HIDDEN_RAW_SET(MainDevice_H, HIDDEN_RAW_SET(DeviceList_H, CLC_NULL)));

    OneOne = CLC(0x00010001);
    Lo3Bytes = CLC(0xFFFFFF);
    HIDDEN_RAW_SET(DragHook_H, 0);
    HIDDEN_RAW_SET(TopMapHndl_H, 0);
    HIDDEN_RAW_SET(SysMapHndl_H, 0);
    HIDDEN_RAW_SET(MBDFHndl_H, 0);
    HIDDEN_RAW_SET(MenuList_H, 0);
    HIDDEN_RAW_SET(MBSaveLoc_H, 0);

    SysVersion = CW(system_version);
    FSFCBLen = CWC(94);
    ScrapState = CWC(-1);
    /* Finder inspects ScrapHandle directly before calling the Scrap Manager.
     Uninitialized low memory is filled with 0xff; a nonnull -1 handle is not
     a valid representation of an uninitialized scrap. */
    HIDDEN_RAW_SET(ScrapHandle_H, NULL);
    ScrapSize = 0;
    ScrapCount = 0;

    HIDDEN_RAW_SET(TheZone_H, SysZone);
    HIDDEN_RAW_SET(UTableBase_H, (DCtlHandlePtr)(long)RM(NewPtr(sizeof(HIDDEN_RAW(UTableBase[0])) * NDEVICES)));
    memset(MR(UTableBase), 0, sizeof(HIDDEN_RAW(UTableBase[0])) * NDEVICES);
    UnitNtryCnt = CW(NDEVICES);
    HIDDEN_RAW_SET(TheZone_H, ApplZone);

    if (graphics_p) {
        /* Set up the current graphics mode appropriately. */
        if (!vdriver_set_mode(flag_width, flag_height, flag_bpp, grayscale_p))
            illegal_mode();

        /* initialize the mac rgb_spec's */
        make_rgb_spec(&mac_16bpp_rgb_spec, 16, TRUE, 0, 5, 10, 5, 5, 5, 0, CL(GetCTSeed()));

        make_rgb_spec(&mac_32bpp_rgb_spec, 32, TRUE, 0, 8, 16, 8, 8, 8, 0, CL(GetCTSeed()));

        gd_allocate_main_device();
    }

    ROMlib_eventinit(graphics_p);
    hle_init();

    ROMlib_fileinit();

    InitUtil();

#if !defined(X) && !defined(SDL)
/* #warning "Hack so we don't smash mouse/keyboard m68k interrupt vectors." */
#if defined(M68K_EVENT_VECTOR)
    save_trap_vectors[M68K_EVENT_VECTOR] = *(syn68k_addr_t*)SYN68K_TO_US(M68K_EVENT_VECTOR * 4);
#endif
#if defined(M68K_MOUSE_MOVED_VECTOR)
    save_trap_vectors[M68K_MOUSE_MOVED_VECTOR] = *(syn68k_addr_t*)SYN68K_TO_US(M68K_MOUSE_MOVED_VECTOR * 4);
#endif
#endif

    {
        char* appearance_str;

        if (opt_val(common_db, "appearance", &appearance_str))
            bad_arg_p |= !ROMlib_parse_appearance(appearance_str);
    }

    InitResources();
#if defined(SDL)
    ROMlib_sdl_load_panel_font();
#endif
    ROMlib_desk_resource_selected = opt_int_val(common_db, "desk-resource", &ROMlib_desk_resource_id, &bad_arg_p);
    if (ROMlib_desk_resource_selected &&
        (bad_arg_p || ROMlib_desk_resource_id < -32768 || ROMlib_desk_resource_id > 32767))
        exit(2);
    if (opt_val(common_db, "mac-info", &arg) && !ROMlib_mac_info_request(arg))
        exit(2);
    {
        char preferences[MAXPATHLEN];
        snprintf(preferences, sizeof preferences, "%s/ROMAW.prefs", ROMlib_userdir);
        ROMlib_ecf_restore_mounts(preferences);
    }
    if (opt_val(common_db, "mount", &arg) || opt_val(common_db, "mount-ro", &arg) ||
        opt_val(common_db, "mount-rw", &arg))
        for (size_t i = 0;; ++i) {
            int mode = 0;
            const char* image = ROMlib_ecf_mount_operand_mode(i, &mode);
            if (!image) {
                if (i)
                    break;
                image = arg;
            }
            OSErr error = ROMlib_mount_image_mode(image, mode);
            if (error == -128) /* userCanceledErr */
                continue;
            if (error) {
                gui_fatal("Cannot mount disk image '%s' (error %d). %s", image, error, ROMlib_image_error());
                exit(2);
            }
        }
    /* Host panels need the initialized application window as their owner. */
    ROMlib_pending_image_dialog = opt_val(common_db, "mount-image", NULL);

    /* parse the `-system' option */
    {
        char* system_str;

        if (opt_val(common_db, "system", &system_str))
            bad_arg_p |= !parse_system_version(system_str);
        else
            ROMlib_set_system_version(system_version);
    }

    if (bad_arg_p) {
        fprintf(stderr, "Type \"%s -help\" for a list of command-line options.\n", program_name);
        exit(-10);
    }

    {
        boolean_t keyboard_set_failed;

        if (opt_val(common_db, "keyboard", &arg)) {
            keyboard_set_failed = !ROMlib_set_keyboard(arg);
            if (keyboard_set_failed)
                printf("``%s'' is not an available keyboard\n", arg);
        }
        else
            keyboard_set_failed = FALSE;

        if (keyboard_set_failed || opt_val(common_db, "keyboards", NULL))
            display_keyboard_choices();
    }

    fprintf(stderr, "trace: ROMlib_seginit begin\n");
    fflush(stderr);
    ROMlib_ecf_remaining_arguments(argc, argv);
    ROMlib_seginit(argc, argv);
    fprintf(stderr, "trace: ROMlib_seginit complete\n");
    fflush(stderr);

    fprintf(stderr, "trace: InitFonts begin\n");
    fflush(stderr);
    InitFonts();
    fprintf(stderr, "trace: InitFonts complete\n");
    fflush(stderr);

#if !defined(NDEBUG)
    dump_init(NULL);
#endif

    /* see qColorutil.c */
    ROMlib_color_init();
    fprintf(stderr, "trace: ROMlib_color_init complete\n");
    fflush(stderr);

    wind_color_init();
    fprintf(stderr, "trace: wind_color_init complete\n");
    fflush(stderr);
    /* must be called after `ROMlib_color_init ()' */
    image_inits();
    fprintf(stderr, "trace: image_inits complete\n");
    fflush(stderr);

    /* must be after `image_inits ()' */
    sb_ctl_init();
    fprintf(stderr, "trace: sb_ctl_init complete\n");
    fflush(stderr);

    AE_init();
    fprintf(stderr, "trace: AE_init complete\n");
    fflush(stderr);

    {
        INTEGER env = 0;
        ROMlib_Fsetenv(&env, 0);
    }

    HIDDEN_RAW_SET(TEDoText_H, RM((ProcPtr)P_ROMlib_dotext)); /* where should this go ? */

#if defined(SYN68K)
    {
        LONGINT save58;

        save58 = *(LONGINT*)SYN68K_TO_US(0x58);
        /* Replace the trap vectors which got smashed during initialization. */
        memcpy(SYN68K_TO_US(0), save_trap_vectors, sizeof save_trap_vectors);
        *(LONGINT*)SYN68K_TO_US(0x58) = save58;

        setup_trap_vectors();
        /* Install after the final vector reset. Keep the software part of the
       040 FPU outside application heaps so it also survives Launch. */
        Ptr fpsp = NewPtrSys(syn68k_fpsp_size());
        if (!fpsp || !syn68k_fpsp_install(US_TO_SYN68K(fpsp))) {
            if (fpsp)
                DisposPtr(fpsp);
            fprintf(stderr, "Cannot install Motorola FPSP in system memory\n");
        }
        else
            fprintf(stderr, "trace: Motorola FPSP installed at 0x%08lx (%lu bytes)\n",
                    (unsigned long)US_TO_SYN68K(fpsp), (unsigned long)syn68k_fpsp_size());
    }

    /* Set up timer interrupts.  We need to do this after everything else
   * has been initialized.
   */
    fprintf(stderr, "trace: syncint_init begin\n");
    fflush(stderr);
    if (!syncint_init()) {
        vdriver_shutdown();
        fputs("Fatal error:  unable to initialize timer.\n", stderr);
        exit(-11);
    }
    fprintf(stderr, "trace: syncint_init complete\n");
    fflush(stderr);

#endif /* SYN68K */

    sound_init();
    fprintf(stderr, "trace: sound_init complete\n");
    fflush(stderr);

    set_refresh_rate(ROMlib_refresh);
    fprintf(stderr, "trace: set_refresh_rate complete\n");
    fflush(stderr);

    restore_virtual_ints(int_state);
    fprintf(stderr, "trace: restore_virtual_ints complete\n");
    fflush(stderr);

    WWExist = QDExist = EXIST_NO;

    fprintf(stderr, "trace: romaw_main begin\n");
    fflush(stderr);
    romaw_main();
    fprintf(stderr, "trace: romaw_main returned\n");
    fflush(stderr);

    if (!ROMlib_no_windows)
        ExitToShell();
    else
        exit(0);
    /* NOT REACHED */
    return 0;
}
