/*+===================================================================
  File:      host_mactcp.h

  Summary:   Host IPv4 address discovery and asynchronous ICMP echo interface.

  Classes:   None.

  Functions: Interface declarations for host IPv4 discovery, ICMP requests and address resolution.

  Origin:    ROMAW Windows networking backend.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_HOST_MACTCP_H
#define RSYS_HOST_MACTCP_H
#include <stdint.h>
typedef struct host_mactcp_request host_mactcp_request;
int host_mactcp_resolver_open(void);
void host_mactcp_resolver_close(void);
int host_mactcp_resolve(const char* name, uint32_t addresses[4], char canonical[255]);
int host_mactcp_address(uint32_t* address, uint32_t* mask);
host_mactcp_request* host_mactcp_echo(uint32_t destination, const void* data, uint16_t length, uint32_t timeout,
                                      const void* options, uint16_t option_length, int* error);
int host_mactcp_result(host_mactcp_request* request, const void** data, uint16_t* length, uint32_t* milliseconds);
void host_mactcp_dispose(host_mactcp_request* request);
#endif
