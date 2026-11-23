/*+===================================================================
  File:      process.h

  Summary:   Declares process support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#if !defined(__rsys_process_h__)
#define __rsys_process_h__

#include "ProcessMgr.h"

typedef struct PACKED size_resource {
    int16 flags;
    int32 pref_size;
    int32 min_size;
} size_resource_t;

extern void* ROMlib_process_identity(void);
extern void ROMlib_process_enable_scripts(uint32 mask, BOOLEAN unicode);
extern OSErr ROMlib_tsm_registration(BOOLEAN registering);
extern OSErr ROMlib_get_system_client_process(ProcessSerialNumber* serial);
extern OSErr ROMlib_begin_system_mode(void);
extern OSErr ROMlib_end_system_mode(void);
extern BOOLEAN ROMlib_tsm_use_input_window(void);
extern OSErr ROMlib_tsm_new_document(INTEGER count, const OSType* types, HIDDEN_Handle* result, LONGINT refcon);
extern OSErr ROMlib_tsm_delete_document(Handle id);
extern OSErr ROMlib_tsm_set_active(Handle id, BOOLEAN active);
extern Handle ROMlib_tsm_active_document(void);
extern OSErr ROMlib_tsm_fix_document(Handle id);
extern OSErr ROMlib_tsm_set_input_window(Handle id, BOOLEAN use);
extern BOOLEAN ROMlib_tsm_document_input_window(Handle id);
extern void ROMlib_tsm_dispose_documents(void);
extern BOOLEAN ROMlib_process_take_wakeup(void);

#endif /* !defined (__rsys_process_h__) */
