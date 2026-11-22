/*+===================================================================
  File:      setup.c

  Summary:   Implements setup support for syn68k 68K translation and runtime support.

  Classes:   None.

  Functions: fully_randomize_regs, randint, randnum, randomize_mem, randomize_regs

  Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             syn68k/COPYING.
===================================================================+*/

#include "syn68k_public.h"
#include "setup.h"
#include "driver.h"
#include <assert.h>
extern int random(void);

uint8* mem;

static inline int32 my_random()
{
    static unsigned n = 9;
    n = (n * 1233143127) ^ (n >> 13);
    return n & 0x7FFFFFFF;
}

int32 randint(int32 low, int32 high)
{
    assert(high >= low);

    return low + (my_random() % (high - low + 1));
}

/* Choose a random 32 bit value.  Half the time, choose one
 * of the "important" values at random.  The important values are more
 * likely to elicit rare cc behavior, so we artificially make them more
 * likely.  This also increases the chances of two values being equal.
 */
int32 randnum()
{
    static const uint32 important_values[] = {0x00000000, 0x00000001, 0x00000080, 0x000000FF, 0x00000100,
                                              0x00007FFF, 0x00008000, 0x0000FFFF, 0x00010000, 0x7FFFFFFF,
                                              0x80000000, 0xFFFFFFFF, 0x00000101, 0x00010001, 0x10000001};

    if (randint(0, 1)) /* 50-50 chance. */
    {
        return important_values[randint(0, ((sizeof important_values / sizeof important_values[0]) - 1))];
    }
    else {
        return my_random();
    }
}

void randomize_mem()
{
    int32* m = (int32*)mem;
    int i;

    for (i = MEM_SIZE / sizeof(int32); i > 0; i--) {
        int32 n = randint(-1, 6);
        if (n <= 0)
            *m++ = n; /* 0 or -1 some of the time, no need to byte swap. */
        else
            *m++ = SWAPUL_IFLE(my_random());
    }
}

void randomize_regs(int low_reg, int high_reg, uint32 low_val, uint32 high_val, uint32 bits_to_mask)
{
    int i;
    for (i = low_reg; i <= high_reg; i++)
        cpu_state.regs[i].sl.n = randint(low_val, high_val) & ~bits_to_mask;
}

void fully_randomize_regs()
{
    M68kReg* r;
    int i;

    for (i = 15, r = cpu_state.regs; i >= 0; r++, i--)
        r->sl.n = randnum();
}
