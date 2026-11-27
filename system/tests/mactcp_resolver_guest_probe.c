/*+===================================================================
  File:      mactcp_resolver_guest_probe.c

  Summary:   Exercise every DNR selector, legacy layouts, Hosts initialization, cache callbacks and async lifetime.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW real 68K guest resolver regression using public MacTCP DNR interfaces.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <Files.h>
#include <Resources.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef long (*resolver_proc)(long, ...);
static resolver_proc dnr;
static short report;
static unsigned checks, failures, callback_failures, cache_count, close_in_enum;
static volatile unsigned result_count;
static unsigned char result[520], cancelled[520];

static uint16_t get16(const void* p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static uint32_t get32(const void* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void check(int passed, const char* name)
{
    char line[200];
    long length = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    failures += !passed;
}

static pascal void enum_result(unsigned char* record, uint32_t user)
{
    const char* name = (const char*)get32(record);
    uint16_t type = get16(record + 4);
    if (user != 0x89abcdef || !name || !*name || get16(record + 6) != 1 ||
        (type != 1 && type != 2 && type != 5 && type != 12))
        ++callback_failures;
    if (type != 1) {
        const char* target = (const char*)get32(record + 12);
        if (!target || !*target)
            ++callback_failures;
    }
    ++cache_count;
    if (close_in_enum)
        dnr(2);
}

static pascal void result_proc(unsigned char* reply, uint32_t user)
{
    if (reply != result || user != 0x89abcdef || (int32_t)get32(reply) >= 0 || (int32_t)get32(reply) == -23042)
        ++callback_failures;
    ++result_count;
}

static long short_addr_to_str(Ptr entry, uint32_t address, char* text)
{
    register Ptr target __asm__("a2") = entry;
    long result_code;
    __asm__ volatile("move.l %[text],-(%%sp)\n\t"
                     "move.l %[address],-(%%sp)\n\t"
                     "move.w #4,-(%%sp)\n\t"
                     "jsr (%[target])\n\t"
                     "lea 10(%%sp),%%sp\n\t"
                     "move.l %%d0,%[answer]"
                     : [answer] "=d"(result_code)
                     : [target] "a"(target), [address] "d"(address), [text] "a"(text)
                     : "d0", "d1", "a0", "a1", "cc", "memory");
    return result_code;
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:mactcp:resolver-guest-results.txt";
    HCreate(0, 0, path, 'MTPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    Handle resource = GetIndResource('dnrp', 1);
    check(resource && GetHandleSize(resource) == 10, "DNR resource and native veneer");
    if (!resource)
        return 1;
    HLock(resource);
    dnr = (resolver_proc)*resource;
    char text[16];
    check(!short_addr_to_str(*resource, 0x7f000001, text) && !strcmp(text, "127.0.0.1"),
          "packed two-byte legacy selector and caller-cleaned C stack");
    Handle relocated = NewHandle(10);
    check(relocated != NULL, "allocate relocated resolver entry");
    if (relocated) {
        HLock(relocated);
        memcpy(*relocated, *resource, 10);
        check(!((resolver_proc)*relocated)(4, (uint32_t)0x7f000002, text) && !strcmp(text, "127.0.0.2"),
              "relocated DNR veneer retains the public entry ABI");
        HUnlock(relocated);
        DisposeHandle(relocated);
    }
    const char* hosts = "D:Source:ROMAW:build:mactcp:resolver-hosts.txt";
    const char* cycle = "D:Source:ROMAW:build:mactcp:resolver-cycle.txt";
    check(dnr(1, "D:Source:ROMAW:build:mactcp:resolver-never-created.txt") == -43,
          "missing Hosts file fails initialization");
    check(dnr(1, "D:Source:ROMAW:build:mactcp:resolver-malformed.txt") == -23041, "embedded NUL Hosts file rejected");
    check(dnr(1, "D:Source:ROMAW:build:mactcp:resolver-too-large.txt") == -23006, "oversized Hosts file rejected");
    check(!dnr(1, hosts), "OpenResolver loads actual optional Hosts file");
    check(!dnr(1, hosts) && !dnr(2), "balanced nested opens preserve active resolver");

    memset(result, 0xa5, sizeof result);
    check(!dnr(3, "ALIAS.fixture", result, (void*)0, (uint32_t)0) && !get32(result) &&
              get32(result + 260) == 0x7f000001 && !strcmp((char*)result + 4, "loopback.fixture") &&
              result[276] == 0xa5,
          "StrToAddr resolves case-insensitive aliases into packed hostInfo");
    check(!dnr(3, "ALIAS.fixture.", result, (void*)0, (uint32_t)0) && get32(result + 260) == 0x7f000001,
          "fully-qualified trailing root label matches Hosts records");
    check(!dnr(3, "cname.fixture", result, (void*)0, (uint32_t)0) && get32(result + 260) == 0x7f000003 &&
              !strcmp((char*)result + 4, "canonical.fixture"),
          "CNAME resolution");
    check(!dnr(6, (uint32_t)0x7f000001, result, (void*)0, (uint32_t)0) && get32(result + 260) == 0x7f000001 &&
              !strcmp((char*)result + 4, "loopback.fixture"),
          "AddrToName");
    memset(result, 0xa5, sizeof result);
    check(!dnr(7, "cpu.fixture", result, (void*)0, (uint32_t)0) && !strcmp((char*)result + 260, "Motorola 68040") &&
              !strcmp((char*)result + 290, "Mac OS") && result[320] == 0xa5,
          "HInfo preserves two fixed 30-byte fields");
    memset(result, 0xa5, sizeof result);
    check(!dnr(8, "mail.fixture", result, (void*)0, (uint32_t)0) && get16(result + 260) == 10 &&
              !strcmp((char*)result + 262, "primary.fixture") && result[518] == 0xa5,
          "MXInfo chooses lowest preference and packed exchange");
    check(!dnr(5, enum_result, (uint32_t)0x89abcdef) && cache_count == 7 && !callback_failures,
          "EnumCache emits A/NS/CNAME/PTR records with Pascal ABI and opaque user data");
    check(dnr(5, (void*)0, (uint32_t)0) == -23043, "EnumCache requires result procedure");
    check(dnr(1, cycle) == -50 && !dnr(3, "a.fixture", result, (void*)0, (uint32_t)0) &&
              get32(result + 260) == 0x7f000002,
          "failed reconfiguration preserves initialized resolver");
    cache_count = 0;
    close_in_enum = 1;
    check(!dnr(5, enum_result, (uint32_t)0x89abcdef) && cache_count == 1,
          "closing resolver inside EnumCache stops safely without stale traversal");
    close_in_enum = 0;
    check(dnr(3, "127.0.0.1", result, (void*)0, (uint32_t)0) == -23047, "closed resolver rejects lookup");
    check(!dnr(1, cycle), "cyclic Hosts file loads as records");
    check(dnr(3, "a.fixture", result, (void*)0, (uint32_t)0) == -23041, "CNAME cycle is bounded and reported");
    check(!dnr(2), "close cyclic resolver");

    check(!dnr(1, (void*)0), "OpenResolver uses native system resolver by default");
    check(!dnr(3, "127.0.0.1", result, (void*)0, (uint32_t)0) && get32(result + 260) == 0x7f000001,
          "numeric lookup completes synchronously without callback");
    check(!dnr(3, "localhost", result, (void*)0, (uint32_t)0) && get32(result + 260) == 0x7f000001,
          "system Hosts/local-name fallback remains real");
    char query[128];
    sprintf(query, "romaw-dnr-%lu.invalid.", (unsigned long)TickCount());
    check(dnr(7, query, result, (void*)0, (uint32_t)0) == -23043,
          "uncached lookup without callback reports noResultProc");
    memset(result, 0xa5, sizeof result);
    long error = dnr(7, query, result, result_proc, (uint32_t)0x89abcdef);
    check(error == -23042 && (int32_t)get32(result) == -23042,
          "uncached query returns cacheFault while worker owns request");
    uint32_t deadline = TickCount() + 2400;
    while (!result_count && (int32_t)(deadline - TickCount()) > 0)
        SystemTask();
    check(result_count == 1 && !callback_failures && (int32_t)get32(result) < 0 && (int32_t)get32(result) != -23042 &&
              result[320] == 0xa5,
          "asynchronous DNS error result is delivered once on guest thread with bounded output");
    SystemTask();
    SystemTask();
    check(result_count == 1, "DNS result callback is not repeated");

    sprintf(query, "romaw-cancel-%lu.invalid.", (unsigned long)TickCount());
    error = dnr(7, query, cancelled, result_proc, (uint32_t)0x89abcdef);
    check(error == -23042, "start query to exercise close cancellation");
    unsigned char snapshot[320];
    memcpy(snapshot, cancelled, 320);
    check(!dnr(2), "CloseResolver abandons pending worker ownership");
    deadline = TickCount() + 30;
    while ((int32_t)(deadline - TickCount()) > 0)
        SystemTask();
    check(result_count == 1 && !memcmp(snapshot, cancelled, 320),
          "closed resolver never touches abandoned guest buffer or callback");
    check(!dnr(1, (void*)0) && !dnr(3, "127.0.0.1", result, (void*)0, (uint32_t)0),
          "resolver reopens while abandoned worker retains its host resources");
    check(!dnr(2) && !dnr(2), "CloseResolver is balanced and safe when already closed");
    check(!callback_failures, "all resolver callback arguments validated");
    HUnlock(resource);
    char line[80];
    long length = sprintf(line, "failures=%u checks=%u\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
