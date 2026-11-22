/*+===================================================================
  File:      flags.h

  Summary:   FALSE if we are blitting straight to screen memory, TRUE if we are blitting to a shadow screen

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* FALSE if we are blitting straight to screen memory, TRUE if we are
   blitting to a shadow screen */
extern INTEGER ROMlib_shadow_screen_p;

/* TRUE if the cursor needs to be saved when calling `ROMlib_blt_rgn ()',
   otherwise the cursor is saved when the shadow buffer is
   flushed */
extern INTEGER ROMlib_bltrgn_save_cursor_p;

/* do not check for diskette when under dos */
extern int ROMlib_drive_check;

#if defined(NOMOUSE_COMMAND_LINE_OPTION)
/* no mouse is installed; keyboard only */
extern int ROMlib_no_mouse;
#endif

extern INTEGER ROMlib_no_windows;

extern int ROMlib_nosplash;

/* 0 means "use default". */
extern INTEGER flag_width, flag_height;

/* 0 means "use default". */
extern int flag_bpp;

/* *argv; name romaw was invoked with */
extern char* program_name;

/* Approximate command line; argv[] elements separated by spaces. */
extern const char* ROMlib_command_line;

/* 0 means try running browser, 1 means don't */

/* 0 means normal, 1 means special gestalt values for photoshop */
extern int ROMlib_photoshop_hack;

/* 1 means map '\n' to '\r' in newline mode */
extern int ROMlib_newlinetocr;

/* TRUE if there is a version skew between the system file version and
   the required system version.  set by `InitResources ()', and used
   by `InitWindows ()' */
extern boolean_t system_file_version_skew_p;
