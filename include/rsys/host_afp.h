/*+===================================================================
  File:      host_afp.h

  Summary:   Native AFP session interface.

  Classes:   None.

  Functions: Session lifecycle and AFP command declarations.

  Origin:    ROMAW native Apple Filing Protocol client.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_HOST_AFP_H
#define RSYS_HOST_AFP_H
#include <stdint.h>

#define ROMAW_AFP_TRANSPORT_ERROR INT32_MIN

typedef struct romaw_afp_session romaw_afp_session;
romaw_afp_session* romaw_afp_connect(uint32_t address, uint16_t port, int32_t* error);
int32_t romaw_afp_command(romaw_afp_session* session, const void* request, unsigned size, void* reply,
                          unsigned capacity, unsigned* received);
int32_t romaw_afp_open_volume(romaw_afp_session* session, const char* name, uint16_t* volume);
int32_t romaw_afp_close_volume(romaw_afp_session* session, uint16_t volume);
void romaw_afp_close(romaw_afp_session* session);
#endif
