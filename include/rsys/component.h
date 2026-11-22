/*+===================================================================
  File:      component.h

  Summary:   ifndef RSYS_COMPONENT_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_COMPONENT_H
#define RSYS_COMPONENT_H

#include "FileMgr.h"

/* Component and instance references are opaque guest tokens, not pointers. */
extern const char* ROMlib_component_signature(unsigned selector);
extern int ROMlib_component_selector(const char* name);
extern int ROMlib_component_service(unsigned selector, const uint32* args, uint32* result);
extern OSErr ROMlib_component_text_services(INTEGER count, const OSType* types, HIDDEN_Handle* output, LONGINT* seed);
extern int ROMlib_component_can_dispatch(uint32 instance);
extern int32 ROMlib_component_call(uint32 instance, const void* parameters);
extern void ROMlib_component_reset(void);
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
extern int ROMlib_ppc_component_entry(Handle code, const FSSpec* spec, ProcPtr* entry);
#endif

#endif
