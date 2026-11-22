/*+===================================================================
  File:      prefs.h

  Summary:   Declares prefs support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_PREFS__)
#define __RSYS_PREFS__

typedef enum {
    WriteAlways,
    WriteInBltrgn,
    WriteInOSEvent,
    WriteAtEndOfTrap,
    WriteNever
} WriteWhenType; /* This is an extension */

extern WriteWhenType ROMlib_when;
extern int ROMlib_PretendSound;
extern int ROMlib_cacheheuristic;
extern int ROMlib_clock;
extern int ROMlib_directdiskaccess;
extern int ROMlib_flushoften;
extern int ROMlib_fontsubstitution;
extern int ROMlib_newlinetocr;
extern int ROMlib_nowarn32;
extern int ROMlib_passpostscript;
extern int ROMlib_refresh;
extern int ROMlib_delay;
extern int ROMlib_noclock;

extern int ROMlib_pretend_help;
extern int ROMlib_pretend_alias;
extern int ROMlib_pretend_script;
extern int ROMlib_pretend_edition;

extern char* ROMlib_configfilename;
extern char ROMlib_configsection[800];
extern FILE* configfile;

extern uint32 system_version;

#define ROMLIB_DEBUG_BIT (1 << 1)

extern void ROMlib_WriteWhen(WriteWhenType when);

#endif /* !defined(__RSYS_PREFS__) */
