/*+===================================================================
  File:      typeselect.h

  Summary:   Roman-script type-selection record used by classic Finder. ifndef RSYS_TYPESELECT_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Roman-script type-selection record used by classic Finder. */
#ifndef RSYS_TYPESELECT_H
#define RSYS_TYPESELECT_H
#include "EventMgr.h"

typedef struct PACKED {
    uint32 last_key_time;
    int16 script;
    unsigned char keys[64];
} ROMlib_type_select_record;

extern void ROMlib_type_select_clear(ROMlib_type_select_record*);
extern BOOLEAN ROMlib_type_select_key(EventRecord*, ROMlib_type_select_record*);
extern INTEGER ROMlib_type_select_compare(const ROMlib_type_select_record*, INTEGER, StringPtr);
extern INTEGER ROMlib_type_select_find(const ROMlib_type_select_record*, INTEGER, INTEGER, ProcPtr, void*);
#endif
