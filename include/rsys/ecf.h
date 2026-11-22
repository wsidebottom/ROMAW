/*+===================================================================
  File:      ecf.h

  Summary:   Declare application launch profiles, persistent image selections and process handoffs.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_ECF_H
#define RSYS_ECF_H
#include "FileMgr.h"
#include "ProcessMgr.h"
#include <stddef.h>

extern OSErr ROMlib_filename_from_fsspec(char** path, FSSpec* file);
/* Host filename or classic volume-qualified pathname for process relaunch. */
extern OSErr ROMlib_launch_path_from_fsspec(char** path, FSSpec* file);
extern int ROMlib_config_syszone_size, ROMlib_config_applzone_size;
extern boolean_t ROMlib_ecf_parse_zone(const char* text, boolean_t system, int* value);
extern boolean_t ROMlib_ecf_zone_value(long value, boolean_t system, int* out);
extern const char* ROMlib_ecf_mount_operand(size_t index);
extern const char* ROMlib_ecf_mounted_operand(size_t index, int* mode);
extern boolean_t ROMlib_ecf_remember_mount(const char* path);
extern boolean_t ROMlib_ecf_remember_mount_mode(const char* path, int mode);
extern void ROMlib_ecf_forget_mount(const char* path);
extern void ROMlib_ecf_restore_mounts(const char* preferences);
extern const char* ROMlib_ecf_mount_operand_mode(size_t index, int* mode);
extern void ROMlib_ecf_remember_arguments(int argc, char** argv);
extern void ROMlib_ecf_remaining_arguments(int argc, char** argv);
extern void ROMlib_ecf_application_operand(boolean_t is_application);
extern void ROMlib_ecf_startup_memory(boolean_t sys_explicit, boolean_t app_explicit);
/* 1: replacement started, 0: no replacement needed, -1: failed. */
extern int ROMlib_ecf_restart_memory(const FSSpec* application);
/* Automatic heaps use SIZE preferred bytes, bounded by the 24-bit address
   space and minimum bytes. Explicit CLI/per-application settings take priority. */
extern int ROMlib_ecf_restart_mode_memory(const FSSpec* application, boolean_t mode32, uint64 framebuffer_end,
                                          int32 preferred_size, int32 minimum_size);
/* Retry a failed library startup in a fresh process with bounded classic OS
   and heap profiles. Returns 1 started, 0 exhausted, -1 unable to restart. */
extern int ROMlib_ecf_restart_system(const FSSpec* application, uint32 current_version);
extern int ROMlib_ecf_require_system(const FSSpec* application, uint32 required_version);
extern void ROMlib_ecf_apply_startup_profile(void);
extern void ROMlib_ecf_complete_startup(boolean_t success);
extern OSErr ROMlib_launch_desk_file(const FSSpec* file);
extern OSErr ROMlib_launch_desk_resource(const FSSpec* file, INTEGER resource);
extern OSErr ROMlib_launch_continue(const FSSpec* file, const ROMlib_AppParameters_t* parameters);
extern BOOLEAN ROMlib_launch_separate_desk_spec(const FSSpec* file, OSErr* result);

#endif
