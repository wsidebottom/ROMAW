/*+===================================================================
  File:      native040-test.c

  Summary:   Compare published 68040 arithmetic roots with maintained C arithmetic across rounding and precision modes.

  Classes:   None.

  Functions: main

  Origin:    Original ARDI Executor closed native-code regression vectors.

  Copyright and Legal notices: Applicable license: syn68k/COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native040.h"
#include "fpu_arithmetic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int equal(Syn68kNative040Float a, floatx80 b)
{
    return a.high == b.high && a.low == b.low;
}

int main(void)
{
    static const unsigned operations[] = {0x22, 0x28, 0x23, 0x20, 4, 0, 0x18, 0x1a, 0x24, 0x27};
    static const floatx80 vectors[] = {{0, 0},
                                       {0x8000, 0},
                                       {0x3fff, UINT64_C(0x8000000000000000)},
                                       {0xbfff, UINT64_C(0x8000000000000000)},
                                       {0x4000, UINT64_C(0xc000000000000000)},
                                       {0x3ffd, UINT64_C(0xaaaaaaaaaaaaaaaa)},
                                       {0x3fff, UINT64_MAX},
                                       {0x3f80, UINT64_C(0x8000000000000000)},
                                       {0x3c00, UINT64_MAX},
                                       {0x407e, UINT64_MAX},
                                       {0x43fe, UINT64_MAX},
                                       {1, UINT64_C(0x8000000000000000)},
                                       {0, UINT64_C(0x8000000000000000)},
                                       {0x7ffe, UINT64_MAX},
                                       {0x7fff, UINT64_C(0x8000000000000000)},
                                       {0xffff, UINT64_C(0x8000000000000000)},
                                       {0x7fff, UINT64_C(0xc0123456789abcde)}};
    unsigned count = 0;
    for (unsigned op = 0; op < sizeof operations / sizeof *operations; ++op) {
        Syn68kNative040Entry entry = syn68k_native040_entry(op);
        assert(entry);
        for (unsigned rounding = 0; rounding < 4; ++rounding)
            for (unsigned precision = 0; precision < 3; ++precision)
                for (unsigned a = 0; a < sizeof vectors / sizeof *vectors; ++a)
                    for (unsigned b = 0; b < sizeof vectors / sizeof *vectors; ++b) {
                        const unsigned widths[] = {80, 32, 64};
                        Syn68kNative040FPU frame = {.source = {vectors[a].high, vectors[a].low},
                                                    .target = {vectors[b].high, vectors[b].low},
                                                    .rounding = rounding,
                                                    .precision = widths[precision]};
                        float_rounding_mode = rounding;
                        floatx80_rounding_precision = widths[precision];
                        float_detect_tininess = float_tininess_after_rounding;
                        float_exception_flags = 0;
                        FPUdivision expected = fpu_calculate(vectors[b], vectors[a], operations[op], widths[precision]);
                        unsigned flags = float_exception_flags;
                        // Template-local state must not alter the compiled runtime's state.
                        float_exception_flags = 0x5a;
                        float_rounding_mode = 3 - rounding;
                        entry(&frame);
                        assert(frame.flags == flags && equal(frame.result, expected.result) &&
                               equal(frame.condition, expected.condition) &&
                               equal(frame.writeback, expected.writeback));
                        assert(frame.status == expected.status && frame.grs == expected.grs &&
                               frame.sticky == expected.sticky && frame.exponent15 == expected.exponent15 &&
                               frame.finite == expected.finite);
                        assert(float_exception_flags == 0x5a && float_rounding_mode == 3 - rounding);
                        ++count;
                    }
    }
    assert(!syn68k_native040_entry(NATIVE040_COUNT) && !syn68k_native040_entry(-1));
    printf("%u published native 68040 arithmetic/rounding/precision/state-isolation cases passed\n", count);
    return 0;
}
