/*+===================================================================
  File:      host_mactcp_dns.h

  Summary:   Bounded DNS replies and worker-owned asynchronous Windows resolver requests.

  Classes:   None.

  Functions: host_mactcp_dns_lookup, host_mactcp_dns_cached, host_mactcp_dns_start,
             host_mactcp_dns_result, host_mactcp_dns_dispose.

  Origin:    ROMAW implementation using public Windows DNS and Winsock resolver APIs.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_HOST_MACTCP_DNS_H
#define RSYS_HOST_MACTCP_DNS_H
#include <stdint.h>

enum { HOST_MACTCP_DNS_RECORDS = 64 };

typedef struct {
    uint16_t type, preference;
    uint32_t address, ttl;
    char name[255], target[255];
} host_mactcp_dns_record;

typedef struct {
    int error;
    char canonical[255], cpu[30], os[30], exchange[255];
    uint16_t preference;
    uint32_t addresses[4];
    unsigned record_count;
    host_mactcp_dns_record records[HOST_MACTCP_DNS_RECORDS];
} host_mactcp_dns_reply;
typedef struct host_mactcp_dns_request host_mactcp_dns_request;
int host_mactcp_dns_lookup(const char* name, uint16_t type, host_mactcp_dns_reply* reply);
int host_mactcp_dns_cached(const char* name, uint16_t type, host_mactcp_dns_reply* reply);
host_mactcp_dns_request* host_mactcp_dns_start(const char* name, uint16_t type, int* error);
int host_mactcp_dns_result(host_mactcp_dns_request* request, host_mactcp_dns_reply* reply);
void host_mactcp_dns_dispose(host_mactcp_dns_request* request);
#endif
