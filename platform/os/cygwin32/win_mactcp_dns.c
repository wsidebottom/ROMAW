/*+===================================================================
  File:      win_mactcp_dns.c

  Summary:   Resolve A, PTR, HINFO and MX records with bounded replies and cancellable guest ownership.

  Classes:   None.

  Functions: host_mactcp_dns_lookup, host_mactcp_dns_cached, host_mactcp_dns_start,
             host_mactcp_dns_result, host_mactcp_dns_dispose.

  Origin:    ROMAW Windows DNS backend. Workers never access guest memory or call guest procedures.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rsys/host_mactcp.h"
#include "rsys/host_mactcp_dns.h"

struct host_mactcp_dns_request {
    LONG references;
    HANDLE ready;
    uint16_t type;
    char name[255];
    host_mactcp_dns_reply reply;
};

static void copy_string(char* destination, size_t capacity, const char* source)
{
    if (!capacity)
        return;
    size_t length = source ? strlen(source) : 0;
    if (length >= capacity)
        length = capacity - 1;
    if (length)
        memcpy(destination, source, length);
    destination[length] = 0;
}

static int valid_name(const char* name)
{
    if (!name || !*name || strlen(name) > 253)
        return 0;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p)
        if (*p <= 32 || *p == 127)
            return 0;
    return 1;
}

static int dns_error(DNS_STATUS status)
{
    switch (status) {
    case ERROR_SUCCESS:
        return 0;
    case DNS_ERROR_RCODE_NAME_ERROR:
        return -23045;
    case DNS_INFO_NO_RECORDS:
    case ERROR_TIMEOUT:
        return -23046;
    case DNS_ERROR_NO_DNS_SERVERS:
        return -23044;
    case ERROR_NOT_ENOUGH_MEMORY:
        return -23048;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
        return -23041;
    default:
        return -23047;
    }
}

static void remember(host_mactcp_dns_reply* reply, const DNS_RECORDA* source)
{
    if (reply->record_count == HOST_MACTCP_DNS_RECORDS ||
        (source->wType != DNS_TYPE_A && source->wType != DNS_TYPE_CNAME && source->wType != DNS_TYPE_PTR &&
         source->wType != DNS_TYPE_NS))
        return;
    host_mactcp_dns_record* record = &reply->records[reply->record_count++];
    record->type = source->wType;
    record->ttl = source->dwTtl;
    copy_string(record->name, sizeof record->name, source->pName);
    if (source->wType == DNS_TYPE_A)
        record->address = ntohl(source->Data.A.IpAddress);
    else
        copy_string(record->target, sizeof record->target, source->Data.PTR.pNameHost);
}

static int lookup(const char* name, uint16_t type, int cache_only, host_mactcp_dns_reply* reply)
{
    memset(reply, 0, sizeof *reply);
    reply->error = -23041;
    if (!valid_name(name) ||
        (type != DNS_TYPE_A && type != DNS_TYPE_PTR && type != DNS_TYPE_HINFO && type != DNS_TYPE_MX))
        return reply->error;
    copy_string(reply->canonical, sizeof reply->canonical, name);
    int error = host_mactcp_resolver_open();
    if (error)
        return reply->error = error;
    struct in_addr numeric;
    if (type == DNS_TYPE_A && InetPtonA(AF_INET, name, &numeric) == 1) {
        reply->addresses[0] = ntohl(numeric.s_addr);
        host_mactcp_resolver_close();
        return reply->error = 0;
    }
    PDNS_RECORDA records = NULL;
    DNS_STATUS status =
        DnsQuery_A(name, type, cache_only ? DNS_QUERY_CACHE_ONLY : DNS_QUERY_STANDARD, NULL, &records, NULL);
    unsigned addresses = 0, found = 0;
    if (!status) {
        for (const DNS_RECORDA* item = records; item; item = item->pNext) {
            remember(reply, item);
            if (item->wType == DNS_TYPE_CNAME && item->Data.CNAME.pNameHost)
                copy_string(reply->canonical, sizeof reply->canonical, item->Data.CNAME.pNameHost);
            if (item->wType != type)
                continue;
            if (type == DNS_TYPE_A) {
                uint32_t address = ntohl(item->Data.A.IpAddress);
                unsigned i;
                for (i = 0; i < addresses && reply->addresses[i] != address; ++i)
                    ;
                if (i == addresses && addresses < 4)
                    reply->addresses[addresses++] = address;
                copy_string(reply->canonical, sizeof reply->canonical, item->pName);
                found = 1;
            }
            else if (type == DNS_TYPE_PTR && item->Data.PTR.pNameHost) {
                copy_string(reply->canonical, sizeof reply->canonical, item->Data.PTR.pNameHost);
                found = 1;
            }
            else if (type == DNS_TYPE_HINFO && item->Data.HINFO.dwStringCount >= 2) {
                copy_string(reply->cpu, sizeof reply->cpu, item->Data.HINFO.pStringArray[0]);
                copy_string(reply->os, sizeof reply->os, item->Data.HINFO.pStringArray[1]);
                found = 1;
            }
            else if (type == DNS_TYPE_MX && item->Data.MX.pNameExchange &&
                     (!found || item->Data.MX.wPreference < reply->preference)) {
                copy_string(reply->exchange, sizeof reply->exchange, item->Data.MX.pNameExchange);
                reply->preference = item->Data.MX.wPreference;
                found = 1;
            }
        }
    }
    if (records)
        DnsRecordListFree((PDNS_RECORD)records, DnsFreeRecordList);
    error = found ? 0 : cache_only ? -23042 : status ? dns_error(status) : -23046;
    /* Preserve the host's Hosts-file and local-name resolution paths as well as DNS.
       Only localhost may use this fallback for a cache-only query. */
    if (error && type == DNS_TYPE_A && (!cache_only || !_stricmp(name, "localhost"))) {
        struct addrinfo hints = {0}, *answer = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_CANONNAME;
        if (!getaddrinfo(name, NULL, &hints, &answer)) {
            for (const struct addrinfo* item = answer; item && addresses < 4; item = item->ai_next) {
                const struct sockaddr_in* address = (const struct sockaddr_in*)item->ai_addr;
                uint32_t value = ntohl(address->sin_addr.s_addr);
                unsigned i;
                for (i = 0; i < addresses && reply->addresses[i] != value; ++i)
                    ;
                if (i == addresses)
                    reply->addresses[addresses++] = value;
            }
            if (answer && answer->ai_canonname)
                copy_string(reply->canonical, sizeof reply->canonical, answer->ai_canonname);
            freeaddrinfo(answer);
            if (addresses)
                error = 0;
        }
    }
    host_mactcp_resolver_close();
    return reply->error = error;
}

int host_mactcp_dns_lookup(const char* name, uint16_t type, host_mactcp_dns_reply* reply)
{
    return lookup(name, type, 0, reply);
}

int host_mactcp_dns_cached(const char* name, uint16_t type, host_mactcp_dns_reply* reply)
{
    return lookup(name, type, 1, reply);
}

static void release_request(host_mactcp_dns_request* request)
{
    if (!InterlockedDecrement(&request->references)) {
        CloseHandle(request->ready);
        free(request);
    }
}

static DWORD WINAPI dns_worker(void* argument)
{
    host_mactcp_dns_request* request = argument;
    host_mactcp_dns_lookup(request->name, request->type, &request->reply);
    SetEvent(request->ready);
    host_mactcp_resolver_close();
    release_request(request);
    return 0;
}

host_mactcp_dns_request* host_mactcp_dns_start(const char* name, uint16_t type, int* error)
{
    *error = -23041;
    if (!valid_name(name))
        return NULL;
    *error = host_mactcp_resolver_open();
    if (*error)
        return NULL;
    host_mactcp_dns_request* request = calloc(1, sizeof *request);
    if (!request) {
        host_mactcp_resolver_close();
        *error = -23048;
        return NULL;
    }
    request->references = 2;
    request->type = type;
    copy_string(request->name, sizeof request->name, name);
    request->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!request->ready || !QueueUserWorkItem(dns_worker, request, WT_EXECUTEDEFAULT)) {
        if (request->ready)
            CloseHandle(request->ready);
        free(request);
        host_mactcp_resolver_close();
        *error = -23048;
        return NULL;
    }
    *error = 0;
    return request;
}

int host_mactcp_dns_result(host_mactcp_dns_request* request, host_mactcp_dns_reply* reply)
{
    DWORD status = WaitForSingleObject(request->ready, 0);
    if (status == WAIT_TIMEOUT)
        return 1;
    if (status != WAIT_OBJECT_0)
        return -23047;
    memcpy(reply, &request->reply, sizeof *reply);
    return 0;
}

void host_mactcp_dns_dispose(host_mactcp_dns_request* request)
{
    if (request)
        release_request(request);
}
