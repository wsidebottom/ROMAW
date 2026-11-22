/*+===================================================================
  File:      system_resources.h

  Summary:   Shared System resources and the combined ROMAW preferences file.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef ROMAW_SYSTEM_RESOURCES_H
#define ROMAW_SYSTEM_RESOURCES_H

#define ROMAW_PREFERENCES_NAME "\005ROMAW"

extern OSErr ROMlib_romaw_preferences_spec(FSSpec* spec);
extern INTEGER ROMlib_open_resource_descriptor(INTEGER refnum);
extern INTEGER ROMlib_open_data_resource_descriptor(INTEGER refnum);
extern OSErr ROMlib_open_data_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference);
extern OSErr ROMlib_create_data_resource_file(const FSSpec* spec);
extern OSErr ROMlib_resource_chain_refnum(int top, INTEGER current, INTEGER* reference);
extern OSErr ROMlib_detach_resource_file(INTEGER reference);
extern OSErr ROMlib_insert_resource_file(INTEGER reference, INTEGER location);
extern OSErr ROMlib_open_orphan_resource_file(const FSSpec* spec, SignedByte permission, INTEGER* reference);
extern OSErr ROMlib_initialize_resource_descriptor(INTEGER refnum);
extern int ROMlib_resource_file_already_open(const FSSpec* spec, Boolean* in_chain, INTEGER* reference);
extern INTEGER ROMlib_open_internal_resource(int fd, LONGINT length);
extern INTEGER ROMlib_open_embedded_system(void);
extern void ROMlib_load_system_preferences(void);
extern OSErr ROMlib_save_system_preferences(INTEGER refnum);

#endif
