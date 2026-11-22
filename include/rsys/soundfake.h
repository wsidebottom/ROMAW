/*+===================================================================
  File:      soundfake.h

  Summary:   Declares soundfake support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(_RSYS_SOUNDFAKE_H_)
#define _RSYS_SOUNDFAKE_H_

#include "rsys/sounddriver.h"

extern boolean_t sound_fake_init(sound_driver_t*);

#endif /* !_RSYS_SOUNDFAKE_H_ */
