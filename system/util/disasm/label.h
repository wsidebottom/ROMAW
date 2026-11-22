/*+===================================================================
  File:      label.h

  Summary:   ifndef _LABEL_H_

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/util/disasm; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef _LABEL_H_
#define _LABEL_H_

extern char* add_labels(char* code, unsigned long entry_point, int keep_addrs_p);

#endif /* !_LABEL_H_ */
