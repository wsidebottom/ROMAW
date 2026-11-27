/*+===================================================================
  File:      mactcp_dns_host_test.c

  Summary:   Test real Windows resolver lifetime and deterministic A/PTR/HINFO/MX decoding and errors.

  Classes:   None.

  Functions: main; fixture DNS entry points when MACTCP_DNS_FIXTURE is defined.

  Origin:    ROMAW DNS regression suite. Synthetic records exercise the production decoder without changing host DNS.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/host_mactcp.h"
#include "rsys/host_mactcp_dns.h"

#ifdef MACTCP_DNS_FIXTURE
static DNS_RECORDA* record(WORD type, const char* name)
{
    DNS_RECORDA* item = calloc(1, sizeof *item);
    assert(item);
    item->wType = type;
    item->Flags.S.Section = DnsSectionAnswer;
    item->dwTtl = 300;
    item->pName = (char*)name;
    return item;
}

static DNS_STATUS WINAPI fixture_query(PCSTR name, WORD type, DWORD options, PVOID extra, PDNS_RECORDA* answer,
                                       PVOID* reserved)
{
    (void)extra;
    (void)reserved;
    *answer = NULL;
    if (!strcmp(name, "uncached.example") && (options & DNS_QUERY_CACHE_ONLY))
        return DNS_INFO_NO_RECORDS;
    if (!strcmp(name, "memory.example"))
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!strcmp(name, "missing.example"))
        return DNS_ERROR_RCODE_NAME_ERROR;
    DNS_RECORDA* item = record(type, name);
    *answer = item;
    if (type == DNS_TYPE_A) {
        item->wType = DNS_TYPE_CNAME;
        item->Data.CNAME.pNameHost = "canonical.example";
        item->pNext = record(DNS_TYPE_A, "canonical.example");
        item->pNext->Data.A.IpAddress = htonl(0x7f000001);
        item->pNext->pNext = record(DNS_TYPE_A, "canonical.example");
        item->pNext->pNext->Data.A.IpAddress = htonl(0x7f000002);
    }
    else if (type == DNS_TYPE_PTR)
        item->Data.PTR.pNameHost = "loopback.example";
    else if (type == DNS_TYPE_HINFO) {
        item->Data.HINFO.dwStringCount = !strcmp(name, "short.example") ? 1 : 2;
        item->Data.HINFO.pStringArray[0] = "m68k";
        item->Data.HINFO.pStringArray[1] = "MacOS";
    }
    else if (type == DNS_TYPE_MX) {
        item->Data.MX.wPreference = 20;
        item->Data.MX.pNameExchange = "secondary.example";
        item->pNext = record(DNS_TYPE_MX, name);
        item->pNext->Data.MX.wPreference = 10;
        item->pNext->Data.MX.pNameExchange = "primary.example";
    }
    return 0;
}

static void fixture_free(PDNS_RECORD record_list, DNS_FREE_TYPE type)
{
    (void)type;
    while (record_list) {
        PDNS_RECORD next = record_list->pNext;
        free(record_list);
        record_list = next;
    }
}

#undef DnsRecordListFree
#define DnsRecordListFree fixture_free
#define DnsQuery_A fixture_query
#include "../../platform/os/cygwin32/win_mactcp_dns.c"
#endif

int main(void)
{
    host_mactcp_dns_reply* reply = malloc(sizeof *reply);
    assert(reply);
    assert(!host_mactcp_dns_lookup("127.0.0.1", 1, reply) && reply->addresses[0] == 0x7f000001);
    assert(host_mactcp_dns_lookup("", 1, reply) == -23041);
    assert(host_mactcp_dns_lookup("bad name", 1, reply) == -23041);
#ifdef MACTCP_DNS_FIXTURE
    assert(!host_mactcp_dns_lookup("alias.example", 1, reply));
    assert(!strcmp(reply->canonical, "canonical.example") && reply->addresses[0] == 0x7f000001 &&
           reply->addresses[1] == 0x7f000002 && reply->record_count == 3 && reply->records[0].ttl == 300);
    assert(!host_mactcp_dns_lookup("1.0.0.127.in-addr.arpa", 12, reply) &&
           !strcmp(reply->canonical, "loopback.example"));
    assert(!host_mactcp_dns_lookup("cpu.example", 13, reply) && !strcmp(reply->cpu, "m68k") &&
           !strcmp(reply->os, "MacOS"));
    assert(!host_mactcp_dns_lookup("mail.example", 15, reply) && reply->preference == 10 &&
           !strcmp(reply->exchange, "primary.example"));
    assert(host_mactcp_dns_lookup("short.example", 13, reply) == -23046);
    assert(host_mactcp_dns_lookup("memory.example", 15, reply) == -23048);
    assert(host_mactcp_dns_lookup("missing.example", 15, reply) == -23045);
    assert(host_mactcp_dns_cached("uncached.example", 15, reply) == -23042);
    const char* query = "cpu.example";
    unsigned type = 13;
#else
    assert(!host_mactcp_dns_cached("localhost", 1, reply) && reply->addresses[0] == 0x7f000001);
    const char* query = "127.0.0.1";
    unsigned type = 1;
#endif
    int error;
    host_mactcp_dns_request* request = host_mactcp_dns_start(query, type, &error);
    assert(request && !error);
    DWORD deadline = GetTickCount() + 5000;
    do {
        error = host_mactcp_dns_result(request, reply);
        if (error == 1)
            Sleep(1);
    } while (error == 1 && (LONG)(deadline - GetTickCount()) > 0);
    assert(!error && !reply->error);
    host_mactcp_dns_dispose(request);
    for (unsigned i = 0; i < 100; ++i) {
        request = host_mactcp_dns_start(query, type, &error);
        assert(request && !error);
        host_mactcp_dns_dispose(request);
    }
    /* Workers retain their own references even after the guest abandons them. */
    Sleep(200);
    free(reply);
#ifdef MACTCP_DNS_FIXTURE
    puts(
        "PASS MacTCP DNS: CNAME/A/PTR/HINFO/MX records, preference, TTL/cache, malformed records, mapped errors and asynchronous cancellation");
#else
    puts(
        "PASS MacTCP DNS: real numeric/localhost resolution, asynchronous result publication and abandoned worker lifetime");
#endif
    return 0;
}
