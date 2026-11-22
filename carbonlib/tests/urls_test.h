/*+===================================================================
  File:      urls_test.h

  Summary:   Native URL test hooks. ifndef ROMAW_CARBON_URLS_TEST_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native URL test hooks. */
#ifndef ROMAW_CARBON_URLS_TEST_H
#define ROMAW_CARBON_URLS_TEST_H
#include "carbonlib.h"
int ROMlib_carbon_url_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference);
int ROMlib_carbon_url_test_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result);
void ROMlib_carbon_url_tests(void);
#endif
