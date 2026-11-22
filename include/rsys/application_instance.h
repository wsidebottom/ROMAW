/*+===================================================================
  File:      application_instance.h

  Summary:   Coordinate one Windows process per classic application and queue document-open requests.

  Classes:   None.

  Functions: Interface declarations; see below.

  Origin:    Executor Windows application launch integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_APPLICATION_INSTANCE_H
#define ROMAW_APPLICATION_INSTANCE_H
#include <stdint.h>

/* The identity must describe the application file, not its display name.
   A zero status means acquired or forwarded; reused distinguishes them.
   Windows messages only queue host memory. Guest work happens in poll. */
uint32_t ROMlib_instance_claim(const char* identity, const char* const* documents, uint32_t count, int* reused);
char* ROMlib_instance_file_identity(const char* host_path, const char* suffix);
char* ROMlib_instance_take(uint32_t* count);
void ROMlib_instance_close(void);

/* Toolbox-facing integration, called on the emulation thread. */
void ROMlib_poll_application_instance(void);
#endif
