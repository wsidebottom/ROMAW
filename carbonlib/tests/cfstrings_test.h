/*+===================================================================
  File:      cfstrings_test.h

  Summary:   ifndef ROMAW_CARBON_CFSTRINGS_TEST_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFSTRINGS_TEST_H
#define ROMAW_CARBON_CFSTRINGS_TEST_H
int ROMlib_carbon_cfstrings_test_reference(const char* library, const char* name, ROMlib_carbon_reference* reference);
int ROMlib_carbon_cfstrings_test_callback(void* procedure, const uint32* arguments, unsigned count, uint32* result);
void ROMlib_carbon_cfstrings_tests(void);
#endif
