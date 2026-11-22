/*+===================================================================
  File:      application_registry.h

  Summary:   ifndef RSYS_APPLICATION_REGISTRY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_APPLICATION_REGISTRY_H
#define RSYS_APPLICATION_REGISTRY_H
OSErr ROMlib_application_spec(uint32 creator, uint32 type, unsigned index, FSSpec* spec);
void ROMlib_application_registry_reset(void);
/* Drawing may query the persistent Desktop/FTM index, but must not trigger a
   recursive Apps scan. Normal launch lookup retains discovery as a fallback. */
OSErr ROMlib_application_spec_cached(uint32 creator, uint32 type, FSSpec* spec);
int ROMlib_application_registry_idle(void);
int ROMlib_application_registry_pending(void);
#endif
