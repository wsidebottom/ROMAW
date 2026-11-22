/*+===================================================================
  File:      safe_alloca.h

  Summary:   Declares safe alloca support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__safe_alloca__)
#define __safe_alloca__

#if !defined(NDEBUG)

#define FIREWALL_START 0x940504FE
#define FIREWALL_STOP_0 0xEE
#define FIREWALL_STOP_1 0x41
#define FIREWALL_STOP_2 0x51
#define FIREWALL_STOP_3 0x59

/*
 * I don't like this implementation, but it works.
 */

#define SAFE_DECL()           \
    unsigned long ___newsize; \
    unsigned char* ___tempptr

#define SAFE_alloca(size)                                                                                            \
    (___newsize = (size) + 3 * sizeof(long), ___tempptr = alloca(___newsize), ((uint32*)___tempptr)[0] = ___newsize, \
     ((uint32*)___tempptr)[1] = FIREWALL_START, ___tempptr[___newsize - 4] = FIREWALL_STOP_0,                        \
     ___tempptr[___newsize - 3] = FIREWALL_STOP_1, ___tempptr[___newsize - 2] = FIREWALL_STOP_2,                     \
     ___tempptr[___newsize - 1] = FIREWALL_STOP_3, (void*)(___tempptr + 2 * sizeof(long)))

#define ASSERT_SAFE(var)                                           \
    do {                                                           \
        ___tempptr = (unsigned char*)(var);                        \
        ___tempptr -= 2 * sizeof(long);                            \
        gui_assert(((uint32*)___tempptr)[1] == FIREWALL_START);    \
        ___newsize = ((uint32*)___tempptr)[0];                     \
        gui_assert(___tempptr[___newsize - 4] == FIREWALL_STOP_0); \
        gui_assert(___tempptr[___newsize - 3] == FIREWALL_STOP_1); \
        gui_assert(___tempptr[___newsize - 2] == FIREWALL_STOP_2); \
        gui_assert(___tempptr[___newsize - 1] == FIREWALL_STOP_3); \
    } while (0)

#else /* defined(NDEBUG) */

#define SAFE_DECL()
#define SAFE_alloca(size) alloca(size)
#define ASSERT_SAFE(var) \
    do {                 \
    } while (0)

#endif /* defined(NDEBUG) */

#endif /* !defined(__safe_alloca__) */
