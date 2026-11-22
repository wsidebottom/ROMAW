/*+===================================================================
  File:      jumpvectors.h

  Summary:   Declares jumpvectors support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__RSYS_JUMPVECTORS__)
#define __RSYS_JUMPVECTORS__

#if !defined(JFLUSH_H)
extern HIDDEN_ProcPtr JFLUSH_H, JResUnknown1_H, JResUnknown2_H;
#endif

#define JFLUSH (HIDDEN_RAW(JFLUSH_H))
#define JResUnknown1 (HIDDEN_RAW(JResUnknown1_H))
#define JResUnknown2 (HIDDEN_RAW(JResUnknown2_H))

#endif /* !defined(__RSYS_JUMPVECTORS__) */
