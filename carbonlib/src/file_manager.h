/*+===================================================================
  File:      file_manager.h

  Summary:   Declares native PPC File Manager marshaling and trap dispatch.

  Classes:   None.

  Functions: Internal declarations for the classic File Manager entries.

  Origin:    System 9.2.1 InterfaceLib File Manager overlay (fovr 50) and Universal Interfaces 3.4.2 Files.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_FILE_MANAGER_H
#define ROMAW_CARBON_FILE_MANAGER_H

#include "internal.h"
#include "Finder.h"

int ROMlib_carbon_file_parameter_block(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector,
                                       size_t extent);
int ROMlib_carbon_file_trap(uint16_t trap_word, int selector, uint32_t address, uint32_t* result);
int ROMlib_carbon_PBUnmountVol(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_UnmountVol(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_file_io(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word);
int ROMlib_carbon_file_hierarchy(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector);
int ROMlib_carbon_file_spec(ROMlib_carbon_ppc_frame* call_frame, uint16_t trap_word, int selector);
int ROMlib_carbon_file_make_spec(ROMlib_carbon_ppc_frame* call_frame);
int ROMlib_carbon_PBWaitIOComplete(ROMlib_carbon_ppc_frame* call_frame);

#endif
