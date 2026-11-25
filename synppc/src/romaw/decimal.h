/*+===================================================================
  File:      decimal.h

  Summary:   Declares bounded PowerPC MathLib decimal formatting adapters.

  Classes:   None.

  Functions: Declarations for PPC MathLib decimal parsing and formatting.

  Origin:    Original adapters following Inside Macintosh: PowerPC Numerics.

  Copyright and Legal notices: Applicable repository license: synppc/COPYING.
===================================================================+*/
#ifndef ROMAW_PPC_DECIMAL_H
#define ROMAW_PPC_DECIMAL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ROMLIB_PPC_DECIMAL_SIZE = 42, ROMLIB_PPC_DECIMAL_STRING_SIZE = 80, ROMLIB_PPC_DECIMAL_INPUT_SIZE = 64 };

int ROMlib_ppc_num2dec(int style, int digits, double value, unsigned char output[ROMLIB_PPC_DECIMAL_SIZE]);
int ROMlib_ppc_num2decl(int style, int digits, __float128 value, unsigned char output[ROMLIB_PPC_DECIMAL_SIZE]);
int ROMlib_ppc_decimal_text(const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                            char output[ROMLIB_PPC_DECIMAL_INPUT_SIZE]);
int ROMlib_ppc_dec2str(int style, int digits, const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                       char output[ROMLIB_PPC_DECIMAL_STRING_SIZE]);
int ROMlib_ppc_str2dec(const char* text, size_t length, int* index, unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE],
                       int* valid_prefix);
int ROMlib_ppc_dec2num(const unsigned char decimal[ROMLIB_PPC_DECIMAL_SIZE], double* output);
#ifdef __cplusplus
}
#endif
#endif
