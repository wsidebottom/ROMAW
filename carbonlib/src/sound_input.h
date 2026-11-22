/*+===================================================================
  File:      sound_input.h

  Summary:   Declares native Carbon Sound Input operations.

  Classes:   None.

  Functions: Declares ROMlib_carbon_sound_input.

  Origin:    Universal Interfaces 3.4.2 Sound.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_SOUND_INPUT_H
#define ROMAW_CARBON_SOUND_INPUT_H
#include "carbonlib.h"

enum sound_input_operation {
    input_index,
    input_open,
    input_close,
    input_get,
    input_set,
    input_record,
    input_stop,
    input_pause,
    input_resume,
    input_status,
    input_to_bytes,
    input_to_ms
};

int ROMlib_carbon_sound_input(ROMlib_carbon_ppc_frame* call, enum sound_input_operation operation);

#endif
