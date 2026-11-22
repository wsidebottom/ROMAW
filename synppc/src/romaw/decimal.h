/*+===================================================================
  File:      decimal.h

  Summary:   Declares bounded PowerPC MathLib decimal formatting adapters.

  Classes:   None.

  Functions: Declarations of ROMlib_ppc_num2dec, ROMlib_ppc_dec2str, ROMlib_ppc_str2dec and ROMlib_ppc_dec2num.

  Origin:    Original adapters following Inside Macintosh: PowerPC Numerics.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/
#ifndef ROMAW_PPC_DECIMAL_H
#define ROMAW_PPC_DECIMAL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ROMLIB_PPC_DECIMAL_SIZE = 42, ROMLIB_PPC_DECIMAL_STRING_SIZE = 80 };

int ROMlib_ppc_num2dec(int style, int digits, double value, unsigned char output[ROMLIB_PPC_DECIMAL_SIZE]);
int ROMlib_ppc_dec2str(int style, int digits, const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                       char output[ROMLIB_PPC_DECIMAL_STRING_SIZE]);
int ROMlib_ppc_str2dec(const char* text, size_t length, int* index, unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                       int* valid_prefix);
int ROMlib_ppc_dec2num(const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE], double* output);
#ifdef __cplusplus
}
#endif
#endif
