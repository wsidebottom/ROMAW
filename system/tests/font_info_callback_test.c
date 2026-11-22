/*+===================================================================
  File:      font_info_callback_test.c

  Summary:   Verify GetFontInfo's metric-only text-measure callback contract.

  Classes:   None.

  Functions: ROMlib_CALLTXMEAS, main.

  Origin:    Original regression fixture; GetFontInfo's null text argument is documented by SuperMario
             QuickDraw/Text.a and Inside Macintosh's text measurement contract.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#define NDEBUG 1
#include "rsys/common.h"
#include "QuickDraw.h"
#include "../../quickdraw/qText.c"
#undef main
#undef NDEBUG
#include <assert.h>

static int calls, zero_denominator;

INTEGER ROMlib_CALLTXMEAS(INTEGER count, Ptr text, Point* numerator, Point* denominator, FontInfo* info)
{
    assert(count == 0 && text == NULL);
    assert(CW(numerator->h) == 1 && CW(numerator->v) == 1);
    assert(CW(denominator->h) == 1 && CW(denominator->v) == 1);
    numerator->v = CWC(3);
    denominator->v = CW(zero_denominator ? 0 : 2);
    info->ascent = CWC(4);
    info->descent = CWC(2);
    info->leading = CWC(6);
    info->widMax = CWC(7);
    ++calls;
    return 0;
}

int main(void)
{
    FontInfo info;
    C_GetFontInfo(&info);
    assert(calls == 1 && CW(info.ascent) == 6 && CW(info.descent) == 3 && CW(info.leading) == 9);
    zero_denominator = 1;
    C_GetFontInfo(&info);
    assert(calls == 2 && CW(info.ascent) == 12 && CW(info.descent) == 6 && CW(info.leading) == 18);
    return 0;
}
