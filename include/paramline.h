/*+===================================================================
  File:      paramline.h

  Summary:   Declares paramline support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__PARAMLINE_H__)
#define __PARAMLINE_H__

extern int count_params(const char* p, int n);

extern char* get_param(const char** bufpp, int* nleftp);

/* On allocation failure, returns argc = -1 and argv = NULL. */
extern void paramline_to_argcv(const char* cmdp, int* argcp, char*** argvp);

/* Space-separated diagnostic text; caller owns the result, NULL on failure. */
extern char* ROMlib_argv_to_string(int argc, char* const* argv);

/* Help and version requests precede emulator or SDL initialization. */
extern int ROMlib_help_requested(int argc, char** argv);
extern int ROMlib_version_requested(int argc, char** argv);
extern const char* ROMlib_command_line_help(void);

#endif
