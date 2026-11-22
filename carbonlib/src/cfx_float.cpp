/*+===================================================================
  File:      cfx_float.cpp

  Summary:   Preserve guest rounding, exception flags and classifications for native date arithmetic.

  Classes:   None.

  Functions: ROMlib_cfx_fp_arithmetic, ROMlib_cfx_fp_compare, ROMlib_cfx_fp_integer.

  Origin:    Original C bridge to synppc's native SoftFloat-backed PPC arithmetic; no guest instructions are executed.

  Copyright and Legal notices: See COPYING and synppc/COPYING.
===================================================================+*/

#include "cfx_float.h"
#include "../../synppc/src/ppc-arithmetic.h"

int ROMlib_cfx_fp_arithmetic(unsigned operation, uint64_t a, uint64_t b, uint64_t c, uint32_t* fpscr, uint64_t* output)
{
    const uint32_t opcode = (63U << 26) | (operation << 1);
    if (operation >= 28 && operation <= 31)
        return romaw_ppc_fused_value(opcode, a, b, c, fpscr, output);
    return romaw_ppc_arithmetic_value(opcode, a, b, c, fpscr, output);
}

int ROMlib_cfx_fp_integer(uint64_t input, uint32_t* fpscr, int32_t* output)
{
    uint64_t result = 0;
    if (!romaw_ppc_integer_value(input, 1, fpscr, &result))
        return 0;
    *output = static_cast<int32_t>(result);
    return 1;
}

unsigned ROMlib_cfx_fp_compare(uint64_t a, uint64_t b, int ordered, uint32_t* fpscr)
{
    return romaw_ppc_compare_value(a, b, ordered != 0, fpscr);
}
