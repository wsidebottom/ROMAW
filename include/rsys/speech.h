/*+===================================================================
  File:      speech.h

  Summary:   ifndef RSYS_SPEECH_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_SPEECH_H
#define RSYS_SPEECH_H
#include <stdint.h>

const char* ROMlib_speech_signature(const char* name, uint32_t* selector);
int ROMlib_speech_service(uint32_t selector, const uint32_t* arguments, uint32_t* result);
int ROMlib_speech_dispatch(uint32_t selector, uint32_t* return_address);
void ROMlib_speech_poll(void);
void ROMlib_speech_reset(void);
#endif
