/*+===================================================================
  File:      win_mactcp.c

  Summary:   Use Windows IP Helper for IPv4 address discovery and asynchronous ICMP echo.

  Classes:   None.

  Functions: host_mactcp_address, host_mactcp_echo, host_mactcp_result, host_mactcp_dispose,
             host_mactcp_resolver_open, host_mactcp_resolver_close, host_mactcp_resolve.

  Origin:    ROMAW Windows networking backend; uses the public Windows IP Helper API.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/host_mactcp.h"

struct host_mactcp_request {
    HANDLE icmp, event;
    void* reply;
    unsigned char* payload;
    unsigned char options[40];
    DWORD reply_size;
    uint16_t payload_length;
    int ready;
};

static unsigned resolver_users;
static SRWLOCK resolver_lock = SRWLOCK_INIT;

int host_mactcp_resolver_open(void)
{
    AcquireSRWLockExclusive(&resolver_lock);
    if (!resolver_users) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data)) {
            ReleaseSRWLockExclusive(&resolver_lock);
            return -23044;
        }
    }
    ++resolver_users;
    ReleaseSRWLockExclusive(&resolver_lock);
    return 0;
}

void host_mactcp_resolver_close(void)
{
    AcquireSRWLockExclusive(&resolver_lock);
    if (resolver_users && !--resolver_users)
        WSACleanup();
    ReleaseSRWLockExclusive(&resolver_lock);
}

int host_mactcp_resolve(const char* name, uint32_t addresses[4], char canonical[255])
{
    AcquireSRWLockShared(&resolver_lock);
    if (!resolver_users) {
        ReleaseSRWLockShared(&resolver_lock);
        return -23047;
    }
    struct addrinfo hints = {0}, *answer = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    int status = getaddrinfo(name, NULL, &hints, &answer);
    if (status) {
        ReleaseSRWLockShared(&resolver_lock);
        return status == EAI_NONAME ? -23045 : -23046;
    }
    unsigned count = 0;
    memset(addresses, 0, sizeof(uint32_t) * 4);
    memset(canonical, 0, 255);
    for (const struct addrinfo* item = answer; item && count < 4; item = item->ai_next) {
        const struct sockaddr_in* address = (const struct sockaddr_in*)item->ai_addr;
        uint32_t value = ntohl(address->sin_addr.s_addr);
        unsigned i;
        for (i = 0; i < count && addresses[i] != value; ++i)
            ;
        if (i == count)
            addresses[count++] = value;
    }
    const char* text = answer && answer->ai_canonname ? answer->ai_canonname : name;
    strncpy(canonical, text, 254);
    freeaddrinfo(answer);
    ReleaseSRWLockShared(&resolver_lock);
    return count ? 0 : -23046;
}

static int map_error(DWORD status)
{
    switch (status) {
    case IP_SUCCESS:
        return 0;
    case IP_REQ_TIMED_OUT:
        return -23035;
    case IP_DEST_NET_UNREACHABLE:
    case IP_DEST_HOST_UNREACHABLE:
    case IP_BAD_ROUTE:
        return -23037;
    case IP_PACKET_TOO_BIG:
        return -23032;
    case ERROR_NOT_ENOUGH_MEMORY:
    case IP_NO_RESOURCES:
        return -23009;
    default:
        return -23033;
    }
}

int host_mactcp_address(uint32_t* address, uint32_t* mask)
{
    ULONG size = 0;
    DWORD best = 0;
    GetBestInterface(htonl(0x08080808), &best);
    if (GetIpAddrTable(NULL, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return -23004;
    MIB_IPADDRTABLE* table = malloc(size);
    if (!table)
        return -23009;
    int result = -23004;
    if (GetIpAddrTable(table, &size, FALSE) == NO_ERROR) {
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            MIB_IPADDRROW* row = &table->table[i];
            if (!row->dwAddr || (row->wType & MIB_IPADDR_DISCONNECTED))
                continue;
            if (result || row->dwIndex == best) {
                *address = ntohl(row->dwAddr);
                *mask = ntohl(row->dwMask);
                result = 0;
            }
            if (row->dwIndex == best)
                break;
        }
    }
    free(table);
    return result;
}

host_mactcp_request* host_mactcp_echo(uint32_t destination, const void* data, uint16_t length, uint32_t timeout,
                                      const void* options, uint16_t option_length, int* error)
{
    *error = -23006;
    if (length > 65507 || option_length > 40 || (length && !data) || (option_length && !options))
        return NULL;
    *error = -23009;
    host_mactcp_request* request = calloc(1, sizeof *request);
    if (!request)
        return NULL;
    request->icmp = IcmpCreateFile();
    request->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    request->reply_size = sizeof(ICMP_ECHO_REPLY) + length + 8;
    request->reply = calloc(1, request->reply_size);
    request->payload = malloc(length ? length : 1);
    if (request->icmp == INVALID_HANDLE_VALUE || !request->event || !request->reply || !request->payload) {
        host_mactcp_dispose(request);
        return NULL;
    }
    if (length)
        memcpy(request->payload, data, length);
    IP_OPTION_INFORMATION info = {0};
    info.Ttl = 128;
    if (option_length) {
        memcpy(request->options, options, option_length);
        info.OptionsSize = option_length;
        info.OptionsData = request->options;
    }
    DWORD count = IcmpSendEcho2(request->icmp, request->event, NULL, NULL, htonl(destination), request->payload, length,
                                &info, request->reply, request->reply_size, timeout);
    request->payload_length = length;
    request->ready = count != 0;
    if (!count && GetLastError() != ERROR_IO_PENDING) {
        *error = map_error(GetLastError());
        host_mactcp_dispose(request);
        return NULL;
    }
    *error = 0;
    return request;
}

int host_mactcp_result(host_mactcp_request* request, const void** data, uint16_t* length, uint32_t* milliseconds)
{
    if (!request->ready && WaitForSingleObject(request->event, 0) != WAIT_OBJECT_0)
        return 1;
    if (!request->ready) {
        if (!IcmpParseReplies(request->reply, request->reply_size))
            return map_error(GetLastError());
        request->ready = 1;
    }
    ICMP_ECHO_REPLY* reply = request->reply;
    int error = map_error(reply->Status);
    if (!error && reply->DataSize > request->payload_length)
        return -23006;
    if (!error) {
        *data = reply->Data;
        *length = reply->DataSize;
        *milliseconds = reply->RoundTripTime;
    }
    return error;
}

void host_mactcp_dispose(host_mactcp_request* request)
{
    if (!request)
        return;
    /* Closing the ICMP handle cancels pending I/O before freeing its buffers. */
    if (request->icmp && request->icmp != INVALID_HANDLE_VALUE)
        IcmpCloseHandle(request->icmp);
    if (request->event)
        CloseHandle(request->event);
    free(request->reply);
    free(request->payload);
    free(request);
}
