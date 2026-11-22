/*+===================================================================
  File:      cfx_float.h

  Summary:   C interfaces to the PPC floating-point arithmetic used by native date services.

  Classes:   None.

  Functions: Interface declarations; see the declarations below.

  Origin:    Original CarbonLib adapter to the existing synppc native arithmetic implementation.

  Copyright and Legal notices: See COPYING and synppc/COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CFX_FLOAT_H
#define ROMAW_CARBON_CFX_FLOAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ROMlib_cfx_fp_arithmetic(unsigned operation, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output);
int ROMlib_cfx_fp_integer(uint64_t input, uint32_t* fpscr, int32_t* output);
unsigned ROMlib_cfx_fp_compare(uint64_t a, uint64_t b, int ordered, uint32_t* fpscr);

#ifdef __cplusplus
}
#endif

#endif
