/*+===================================================================
  File:      mactcp_guest_probe.c

  Summary:   Verify MacTCP driver and DNR resource ABIs through real 68K guest calls.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW MacTCP guest regression fixture; uses public Device Manager and resolver interfaces.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <Files.h>
#include <Devices.h>
#include <Resources.h>
#include <Memory.h>
#include <OSUtils.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static short report, driver;
static int failures, checks;
static unsigned char blocks[2][54], payload[56];
static volatile unsigned accepted, completed;
static unsigned callback_failures;
static volatile unsigned cancelled;
static unsigned cancel_failures;

static void check(int passed, const char* name)
{
    char line[180];
    long length = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    failures += !passed;
}

static uint16_t get16(const void* bytes)
{
    uint16_t value;
    memcpy(&value, bytes, 2);
    return value;
}

static uint32_t get32(const void* bytes)
{
    uint32_t value;
    memcpy(&value, bytes, 4);
    return value;
}

static void put16(void* bytes, uint16_t value)
{
    memcpy(bytes, &value, 2);
}

static void put32(void* bytes, uint32_t value)
{
    memcpy(bytes, &value, 4);
}

static void request_done(unsigned char* pb)
{
    if ((pb != blocks[0] && pb != blocks[1]) || get16(pb + 16))
        ++callback_failures;
    ++accepted;
}

static void echo_done(unsigned char* pb)
{
    uint16_t length = get16(pb + 58);
    const unsigned char* data = (const unsigned char*)get32(pb + 60);
    if (get16(pb + 16) || length != sizeof payload + 8 || get32(pb + 68) > 1 || !data ||
        memcmp(data + 8, payload, sizeof payload))
        ++callback_failures;
    unsigned checksum = 0;
    if (data && length == sizeof payload + 8) {
        for (unsigned i = 0; i < length; i += 2)
            checksum += (unsigned)data[i] << 8 | data[i + 1];
        while (checksum >> 16)
            checksum = (checksum & 65535) + (checksum >> 16);
        if (checksum != 65535)
            ++callback_failures;
    }
    ++completed;
}

static void cancelled_echo(unsigned char* pb)
{
    if ((short)get16(pb + 16) != -27 || get16(pb + 58))
        ++cancel_failures;
    ++cancelled;
}

static short kill_io(unsigned char* pb)
{
    register void* a0 __asm__("a0") = pb;
    register long d0 __asm__("d0");
    __asm__ volatile(".word 0xa006" : "=d"(d0), "+a"(a0) : : "d1", "a1", "cc", "memory");
    return d0;
}

static short control(unsigned char* pb, int async)
{
    register void* a0 __asm__("a0") = pb;
    register long d0 __asm__("d0");
    if (async)
        __asm__ volatile(".word 0xa404" : "=d"(d0), "+a"(a0) : : "d1", "a1", "cc", "memory");
    else
        __asm__ volatile(".word 0xa004" : "=d"(d0), "+a"(a0) : : "d1", "a1", "cc", "memory");
    return d0;
}

/* GetDCtlEntry is an SDK library routine; inspect its documented low-memory
   unit table directly because the standalone Retro68 runtime does not ship it. */
static void* driver_entry(short reference)
{
    void** table = *(void***)0x11c;
    return table && reference < 0 ? table[-reference - 1] : 0;
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:mactcp:guest-results.txt";
    HCreate(0, 0, path, 'MTPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    check(!OpenDriver("\p.IPP", &driver), "open .IPP through Device Manager");
    check(driver_entry(driver) != 0, "native driver has a real Device Control Entry");
    unsigned char query[64] = {0};
    put16(query + 24, driver);
    put16(query + 26, 15);
    check(!control(query, 0) && get32(query + 28) && get32(query + 32), "real local address and mask");
    put16(query + 26, 30);
    check(control(query, 0) == -23006, "TCPCreate rejects a missing receive buffer");

    Handle resolver = GetIndResource('dnrp', 1);
    check(resolver && GetHandleSize(resolver) == 10, "DNR loads through Resource Manager");
    if (resolver) {
        HLock(resolver);
        long (*dnr)(long, ...) = (long (*)(long, ...)) * resolver;
        check(!dnr(1, (void*)0), "DNR opens its actual host resolver");
        unsigned char info[276] = {0};
        check(!dnr(3, "localhost", info, (void*)0, (void*)0) && !get32(info) && get32(info + 260) == 0x7f000001,
              "StrToAddr uses C stack ABI and packed hostInfo");
        char text[16];
        check(!dnr(4, (uint32_t)0x7f000001, text) && !strcmp(text, "127.0.0.1"), "AddrToStr");
        check(!CloseDriver(driver), "close transport while resolver remains open");
        check(!dnr(3, "127.0.0.1", info, (void*)0, (void*)0) && get32(info + 260) == 0x7f000001,
              "transport close preserves independent resolver ownership");
        check(!OpenDriver("\p.IPP", &driver), "reopen transport while resolver remains open");
        check(!dnr(2), "DNR close");
        HUnlock(resolver);
    }
    for (unsigned i = 0; i < sizeof payload; ++i)
        payload[i] = i * 13;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char* pb = blocks[i];
        put32(pb + 12, (uint32_t)request_done);
        put16(pb + 24, driver);
        put16(pb + 26, 17);
        put32(pb + 28, 0x7f000001);
        put16(pb + 32, sizeof payload);
        put32(pb + 34, (uint32_t)payload);
        put16(pb + 38, 1);
        put32(pb + 46, (uint32_t)echo_done);
        put32(pb + 50, i);
        check(!control(pb, 1), "asynchronous ICMP request accepted");
    }
    uint32_t deadline = TickCount() + 300;
    while (completed < 2 && (int32_t)(deadline - TickCount()) > 0)
        SystemTask();
    check(accepted == 2, "request completions receive their C stack arguments");
    check(completed == 2 && !callback_failures, "SystemTask delivers exact echo data, ICMP headers and user data");
    SystemTask();
    check(completed == 2, "echo notifications delivered exactly once");
    unsigned char cancelpb[54] = {0};
    put16(cancelpb + 24, driver);
    put16(cancelpb + 26, 17);
    put32(cancelpb + 28, 0x7f000001);
    put16(cancelpb + 38, 2);
    put32(cancelpb + 46, (uint32_t)cancelled_echo);
    check(!control(cancelpb, 0), "accept echo before cancellation");
    check(!kill_io(query), "KillIO cancels accepted echo");
    SystemTask();
    check(cancelled == 1 && !cancel_failures, "cancelled echo notifies abortErr exactly once");
    SystemTask();
    check(cancelled == 1, "cancelled host result cannot notify again");
    check(!CloseDriver(driver), "close .IPP");
    check(control(query, 0) == -28, "closed driver rejects controls");
    check(!OpenDriver("\p.IPP", &driver) && driver_entry(driver), "reopen native driver");
    put16(query + 24, driver);
    put16(query + 26, 15);
    check(!control(query, 0), "reopened driver accepts controls");
    check(!CloseDriver(driver), "close reopened driver");
    long length;
    char line[80];
    length = sprintf(line, "failures=%d checks=%d\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
