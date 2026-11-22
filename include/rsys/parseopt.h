/*+===================================================================
  File:      parseopt.h

  Summary:   Declares parseopt support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_PARSEOPT_H_)
#define _RSYS_PARSEOPT_H_

extern boolean_t parse_system_version(const char* vers);
extern boolean_t parse_size_opt(const char* opt, const char* arg);
extern boolean_t parse_prres_opt(INTEGER* outx, INTEGER* outy, const char* arg);
/* Decimal major[.minor[.patch]]; output remains unchanged on failure. */
extern boolean_t ROMlib_parse_version(const char* vers, uint32* version_out);

#define CREATE_SYSTEM_VERSION(a, b, c) ((((a) & 0xF) << 8) | (((b) & 0xF) << 4) | ((c) & 0xF))

#endif /* !_RSYS_PARSEOPT_H_ */
