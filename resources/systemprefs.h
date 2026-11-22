/*+===================================================================
  File:      systemprefs.h

  Summary:   Bounded resource-fork storage for ROMAW preferences.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/resources; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_SYSTEMPREFS_H
#define ROMAW_SYSTEMPREFS_H
#include <stddef.h>

int system_resource_read(const char* path, unsigned char** bytes, size_t* length);
int system_resource_write(const char* path, const unsigned char* bytes, size_t length);
#endif
