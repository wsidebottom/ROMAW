/*+===================================================================
  File:      application_event.h

  Summary:   Declares application event support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef ROMAW_APPLICATION_EVENT_H
#define ROMAW_APPLICATION_EVENT_H
#include "FileMgr.h"
extern OSErr ROMlib_send_application_open_event(int count);
extern OSErr ROMlib_send_document_open_event(int count, const FSSpec* files);
extern OSErr ROMlib_decode_launch_parameters(const void* parameters, Handle* converted);
#endif
