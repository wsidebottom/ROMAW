/*+===================================================================
  File:      system_error.h

  Summary:   Declares system error support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_SYSTEM_ERROR_H_)
#define _SYSTEM_ERROR_H_

enum { GENERIC_COMPLAINT_ID = -3000 };

typedef void (*system_error_callback_t)(void);

extern int system_error(const char* _message, int _default_button, const char* button0, const char* button1,
                        const char* button2, system_error_callback_t func0, system_error_callback_t func1,
                        system_error_callback_t func2);

/* Called on the emulator thread to dismiss an active system error dialog. */
extern boolean_t ROMlib_system_error_request_close(void);

#endif /* !_SYSTEM_ERROR_H_ */
