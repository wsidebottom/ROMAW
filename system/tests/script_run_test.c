/*+===================================================================
  File:      script_run_test.c

  Summary:   Exercise the actual trap entry, including empty and negative runs.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Exercise the actual trap entry, including empty and negative runs. */
#define NDEBUG 1
#include "../../text/script.c"
#undef main
#undef NDEBUG
#include <assert.h>

int main(void)
{
    LONGINT used;
    const LONGINT lengths[] = {-2147483647, -1, 0, 1, 2, 32768, 2147483647};
    unsigned i;
    for (i = 0; i < NELEM(lengths); ++i) {
        used = CLC(-1);
        assert(C_FindScriptRun(NULL, lengths[i], &used) == 0);
        assert(CL(used) == (lengths[i] > 0 ? lengths[i] : 0));
    }
    puts("PASS: Roman script-run lengths and empty inputs");
    return 0;
}
