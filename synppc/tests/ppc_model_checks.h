/*+===================================================================
  File:      ppc_model_checks.h

  Summary:   Declare the host checks driven by generated PowerPC arithmetic cases.

  Classes:   None.

  Functions: Declares the individual arithmetic model checks.

  Origin:    Shared entry points for the existing synppc model checks.

  Copyright and Legal notices: See synppc/COPYING.
===================================================================+*/

#ifndef PPC_MODEL_CHECKS_H
#define PPC_MODEL_CHECKS_H

#include <stdio.h>

int ppc_arithmetic_check(FILE* cases);
int ppc_rounding_status_check(FILE* cases);
int ppc_integer_check(FILE* cases);
int ppc_compare_check(FILE* cases);
int ppc_fused_status_check(FILE* cases);

#endif
