/*+===================================================================
  File:      string.h

  Summary:   Declares string support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_STRING_H_)
#define _RSYS_STRING_H_

#define TEMP_C_STRING_FROM_STR255(str)                      \
    ({                                                      \
        char* retval;                                       \
        int str_len;                                        \
                                                            \
        str_len = *(unsigned char*)(str);                   \
        retval = alloca(str_len + 1);                       \
        memcpy(retval, (unsigned char*)(str) + 1, str_len); \
        retval[str_len] = 0;                                \
        retval;                                             \
    })

extern void pstr_from_c_string(StringPtr dest, size_t capacity, const char* source);
extern void str255_from_c_string(Str255 str255, const char* c_stringp);
extern char* pstr_index_after(StringPtr p, char c, int i);
extern void str63assign(Str63 new, const StringPtr old);
extern void str31assign(Str63 new, const StringPtr old);
extern StringHandle stringhandle_from_c_string(const char* c_stringp);

#endif /* !_RSYS_STRING_H_ */
