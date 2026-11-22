/*+===================================================================
  File:      licensetext.h

  Summary:   Declares licensetext support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_LICENSETEXT_H_)
#define _RSYS_LICENSETEXT_H_

typedef struct {
    const char* heading;
    const char* body;
} license_text_page_t;

extern license_text_page_t ROMlib_license[];

#endif /* _RSYS_LICENSETEXT_H_ */
