/*+===================================================================
  File:      user_profile.h

  Summary:   Declares user profile support for public and internal interfaces.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#ifndef ROMAW_USER_PROFILE_H
#define ROMAW_USER_PROFILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native paths use UTF-8. Only a single filename component is accepted.
   The user profile is always .romaw in the user's home directory.
   Installation settings and master templates seed missing ROMAW.prefs and ROMAW.ftm files. */
int romaw_profile_path(const char* leaf, char* out, size_t capacity, char* error, size_t error_capacity);
int romaw_profile_migrate(const char* installation, char* error, size_t error_capacity);
int romaw_profile_legacy_path(char* out, size_t capacity, char* error, size_t error_capacity);
/* malloc-owned legacy File Manager path; migrates old root AppleDouble
     files without replacing current metadata. NULL sets errno on failure. */
char* romaw_root_metadata_path(const char* root);
/* Read/write the 32 Finder bytes stored in an HFS volume header.
     Host directories keep this volume state beside their root metadata. */
int romaw_volume_finder_info(const char* root, void* bytes, int write);
/* 1 local, 2 network, 3 floppy, 4 optical; shared with FinderVolumeKind. */
int romaw_host_volume_kind(const char* path);
/* UTF-8 native picker history shared by ROMAW and native Finder. */
int romaw_dialog_directory(char* out, size_t capacity);
int romaw_dialog_remember(const char* directory);
#ifdef __cplusplus
}
#endif
#endif
