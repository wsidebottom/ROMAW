/*+===================================================================
  File:      build-host.h

  Summary:   WSL build helpers and isolated host-side tests, not an ROMAW platform. ifndef ROMAW_BUILD_HOST_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/tools; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* WSL build helpers and isolated host-side tests, not an ROMAW platform. */
#ifndef ROMAW_BUILD_HOST_H
#define ROMAW_BUILD_HOST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#define LINUX
#define PRIVATE static
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define DB_DBM_HSEARCH 1
#include <db.h>
#endif
