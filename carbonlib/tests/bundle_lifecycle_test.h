/*+===================================================================
  File:      bundle_lifecycle_test.h

  Summary:   Native bundle lifecycle test hooks. ifndef ROMAW_CARBON_BUNDLE_LIFECYCLE_TEST_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native bundle lifecycle test hooks. */
#ifndef ROMAW_CARBON_BUNDLE_LIFECYCLE_TEST_H
#define ROMAW_CARBON_BUNDLE_LIFECYCLE_TEST_H
#include "carbonlib.h"
int ROMlib_carbon_bundle_lifecycle_reference(const char* library, const char* name, ROMlib_carbon_reference* reference);
int ROMlib_carbon_bundle_lifecycle_callback(void* procedure, const uint32_t* arguments, unsigned count,
                                            uint32_t* result);
void ROMlib_carbon_bundle_lifecycle_tests(void);
#endif
