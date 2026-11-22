/*+===================================================================
  File:      segment.h

  Summary:   Declares segment support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SEGMENT_H_)
#define _SEGMENT_H_

extern char ROMlib_exit;

#if !defined(USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES)

#include <SegmentLdr.h>

typedef struct PACKED {
    INTEGER message;
    INTEGER count;
    AppFile files[1];
} finderinfo;

extern int ROMlib_print;
extern void flushcache(void);
extern void SFSaveDisk_Update(INTEGER vrefnum, Str255 filename);

extern char* ROMlib_undotdot(char* origp);

extern void ROMlib_seginit(LONGINT argc, char** argv);
extern void empty_timer_queues(void);
extern BOOLEAN ROMlib_startupscreen;
#endif

#endif /* !_SEGMENT_H_ */
