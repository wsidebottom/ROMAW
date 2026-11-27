/*+===================================================================
  File:      mactcp_stream_guest_probe.c

  Summary:   Verify TCP/UDP controls, callbacks, no-copy ownership and data ordering from a real 68K guest.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW MacTCP loopback regression using public parameter-block layouts.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <Files.h>
#include <Devices.h>
#include <Resources.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static short report, driver;
static unsigned checks, failures;
static volatile unsigned completions, udp_events, tcp_closing, tcp_terminated;
static unsigned callback_failures;
static unsigned ulp_testing, ulp_timeouts, ulp_terminations;
static uint32_t ulp_client;
static unsigned char timeout_sends[96][128], timeout_lists[96][12];
static unsigned char udp_buffers[3][8192], tcp_buffers[2][8192], payload[513];
static unsigned char udp_create[3][128], tcp_create[2][128];
static unsigned char bulk_payload[4][65535];

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

static void put16(void* p, uint16_t v)
{
    memcpy(p, &v, 2);
}

static void put32(void* p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void check(int passed, const char* name)
{
    char line[200];
    long length = sprintf(line, "%s %s\n", passed ? "PASS" : "FAIL", name);
    FSWrite(report, &length, line);
    ++checks;
    failures += !passed;
}

static short control(unsigned char* pb, int async)
{
    register void* a0 __asm__("a0") = pb;
    register long d0 __asm__("d0");
    if (async)
        __asm__ volatile(".word 0xa404" : "=d"(d0), "+a"(a0)::"d1", "a1", "cc", "memory");
    else
        __asm__ volatile(".word 0xa004" : "=d"(d0), "+a"(a0)::"d1", "a1", "cc", "memory");
    return d0;
}

static void io_done(unsigned char* pb)
{
    if (get16(pb + 16) == 1)
        ++callback_failures;
    ++completions;
}

static pascal void udp_notify(uint32_t stream, unsigned short event, Ptr user, Ptr icmp)
{
    if (!stream || event != 1 || (uint32_t)user != 0x13579bdf || icmp)
        ++callback_failures;
    ++udp_events;
}

static pascal void tcp_notify(uint32_t stream, unsigned short event, Ptr user, unsigned short reason, Ptr icmp)
{
    if (!stream || (uint32_t)user != 0x2468ace0 || icmp)
        ++callback_failures;
    if (ulp_testing) {
        if (event == 2 && stream == ulp_client)
            ++ulp_timeouts;
        if (event == 3) {
            if (stream == ulp_client && reason == 5)
                ++ulp_terminations;
            else if (reason != 2 && reason != 3 && reason != 6)
                ++callback_failures;
        }
        return;
    }
    if (event == 1)
        ++tcp_closing;
    if (event == 3) {
        if (reason != 6)
            ++callback_failures;
        ++tcp_terminated;
    }
}

static void prepare(unsigned char* pb, unsigned code, uint32_t stream)
{
    memset(pb, 0, 128);
    put16(pb + 24, driver);
    put16(pb + 26, code);
    put32(pb + 28, stream);
}

static short wait_result(unsigned char* pb)
{
    uint32_t deadline = TickCount() + 300;
    while (get16(pb + 16) == 1 && (int32_t)(deadline - TickCount()) > 0)
        SystemTask();
    return (int16_t)get16(pb + 16);
}

static short create(unsigned char* pb, void* buffer, unsigned udp)
{
    prepare(pb, udp ? 20 : 30, 0);
    put32(pb + 32, (uint32_t)buffer);
    put32(pb + 36, 8192);
    put32(pb + 40, udp ? (uint32_t)udp_notify : (uint32_t)tcp_notify);
    put32(pb + (udp ? 46 : 44), udp ? 0x13579bdf : 0x2468ace0);
    return control(pb, 0);
}

static void wds(unsigned char* list, const void* bytes, unsigned length)
{
    memset(list, 0, 12);
    put16(list, length);
    put32(list + 2, (uint32_t)bytes);
}

/* A peer which never reads creates real socket backpressure. Each send has
   a one-second ULP deadline; the two actions must preserve or abort the socket. */
static void timeout_policy(unsigned action)
{
    unsigned char pb[128], listener_pb[128], client_pb[128], scratch[8192];
    check(!create(tcp_create[0], tcp_buffers[0], 0) && !create(tcp_create[1], tcp_buffers[1], 0),
          "create timeout-policy TCP pair");
    uint32_t listener = get32(tcp_create[0] + 28);
    uint32_t client = get32(tcp_create[1] + 28);
    ulp_testing = 1;
    ulp_client = client;
    ulp_timeouts = ulp_terminations = 0;
    prepare(listener_pb, 31, listener);
    put32(listener_pb + 42, 0x7f000001);
    put32(listener_pb + 94, 0x2468ace0);
    listener_pb[35] = 2;
    control(listener_pb, 1);
    prepare(pb, 40, listener);
    control(pb, 0);
    prepare(client_pb, 32, client);
    put32(client_pb + 36, 0x7f000001);
    put16(client_pb + 40, get16(pb + 48));
    put32(client_pb + 42, 0x7f000001);
    put32(client_pb + 94, 0x2468ace0);
    client_pb[35] = 2;
    check(!control(client_pb, 1) && !wait_result(client_pb) && !wait_result(listener_pb),
          "open timeout-policy loopback pair");
    for (unsigned i = 0; i < 96; ++i) {
        prepare(timeout_sends[i], 34, client);
        wds(timeout_lists[i], bulk_payload[0], 65535);
        put32(timeout_sends[i] + 38, (uint32_t)timeout_lists[i]);
        put32(timeout_sends[i] + 12, (uint32_t)io_done);
        timeout_sends[i][32] = 1;
        timeout_sends[i][33] = action;
        timeout_sends[i][34] = 0xc0;
        timeout_sends[i][35] = 1;
        control(timeout_sends[i], 1);
    }
    uint32_t deadline = TickCount() + 180;
    unsigned pending, expired;
    do {
        SystemTask();
        pending = expired = 0;
        for (unsigned i = 0; i < 96; ++i) {
            short result = get16(timeout_sends[i] + 16);
            pending += result == 1;
            expired += result == -23016;
        }
    } while (pending && (int32_t)(deadline - TickCount()) > 0);
    check(!pending && expired, "backpressure expires queued TCP sends without hanging");
    prepare(pb, 40, client);
    short status = control(pb, 0);
    if (action) {
        check(!status && pb[52] == 8 && pb[32] == 1 && pb[33] == 1 && ulp_timeouts && !ulp_terminations,
              "ULP notify action reports timeout and retains established connection");
        prepare(pb, 40, listener);
        control(pb, 0);
        if (get16(pb + 60)) {
            prepare(pb, 37, listener);
            pb[32] = 1;
            put32(pb + 36, (uint32_t)scratch);
            put16(pb + 40, sizeof scratch);
            check(!control(pb, 0) && get16(pb + 40), "peer can still read after notify-only timeout");
        }
        prepare(pb, 39, client);
        control(pb, 0);
    }
    else
        check(ulp_terminations == 1 && !ulp_timeouts && (status || pb[52] != 8),
              "ULP abort action terminates connection and notifies exactly once");
    for (unsigned i = 0; i < 2; ++i) {
        prepare(pb, 42, get32(tcp_create[i] + 28));
        control(pb, 0);
    }
    ulp_testing = 0;
}

int main(void)
{
    Str255 path = "\pD:Source:ROMAW:build:mactcp:stream-guest-results.txt";
    HCreate(0, 0, path, 'MTPr', 'TEXT');
    if (HOpen(0, 0, path, fsRdWrPerm, &report))
        return 1;
    SetEOF(report, 0);
    check(!OpenDriver("\p.IPP", &driver), "open .IPP");
    for (unsigned i = 0; i < sizeof payload; ++i)
        payload[i] = (i * 37) & 255;
    unsigned char pb[128], read_pb[128], send_pb[128], list[12], stats[86], lap[24];
    prepare(pb, 19, 0);
    put32(pb + 28, (uint32_t)lap);
    check(!control(pb, 0) && get16(lap + 6) && get32(lap + 2), "LAPStats exposes real interface metadata");

    check(!create(udp_create[0], udp_buffers[0], 1), "UDPCreate receiver");
    check(!create(udp_create[1], udp_buffers[1], 1), "UDPCreate sender");
    uint32_t receiver = get32(udp_create[0] + 28), sender = get32(udp_create[1] + 28);
    uint16_t receiver_port = get16(udp_create[0] + 44), sender_port = get16(udp_create[1] + 44);
    prepare(read_pb, 21, receiver);
    put16(read_pb + 32, 2);
    put32(read_pb + 12, (uint32_t)io_done);
    check(!control(read_pb, 1) && get16(read_pb + 16) == 1, "UDPRead stays pending before data");
    prepare(send_pb, 23, sender);
    put32(send_pb + 34, 0x7f000001);
    put16(send_pb + 38, receiver_port);
    wds(list, payload, 513);
    put32(send_pb + 40, (uint32_t)list);
    send_pb[44] = 1;
    check(!control(send_pb, 0) && get16(send_pb + 46) == 513, "UDPWrite gathers WDS and sends real packet");
    check(!wait_result(read_pb) && get16(read_pb + 44) == 513 && get32(read_pb + 40) == (uint32_t)udp_buffers[0] &&
              !memcmp(udp_buffers[0], payload, 513),
          "UDPRead returns the receive-buffer lease and exact data");
    check(get32(read_pb + 34) == 0x7f000001 && get16(read_pb + 38) == sender_port &&
              get32(read_pb + 52) == 0x7f000001 && get16(read_pb + 56) == receiver_port,
          "UDPRead reports actual source and destination endpoints");
    prepare(pb, 22, receiver);
    put32(pb + 40, 1);
    check(control(pb, 0) == -23014, "UDPBfrReturn rejects foreign buffer");
    put32(pb + 40, (uint32_t)udp_buffers[0]);
    check(!control(pb, 0), "UDPBfrReturn releases the owned receive buffer");
    prepare(pb, 26, receiver);
    check(!control(pb, 0) && get16(pb + 44) == receiver_port && get32(pb + 32) == (uint32_t)udp_buffers[0],
          "UDPStatus");

    wds(list, payload, 0);
    check(!control(send_pb, 0), "UDPWrite accepts zero-byte datagram");
    uint32_t deadline = TickCount() + 180;
    while (!udp_events && (int32_t)(deadline - TickCount()) > 0)
        SystemTask();
    check(udp_events && !callback_failures, "UDP arrival notification uses Pascal ABI and preserves opaque user data");
    prepare(read_pb, 21, receiver);
    put16(read_pb + 32, 2);
    check(!control(read_pb, 0) && !get16(read_pb + 44), "zero-byte UDP datagram received");
    prepare(pb, 22, receiver);
    put32(pb + 40, (uint32_t)udp_buffers[0]);
    control(pb, 0);

    short multi_error = -1;
    uint16_t first = 40000 + (TickCount() % 1000);
    for (unsigned i = 0; i < 100 && multi_error; ++i, first += 2) {
        prepare(udp_create[2], 27, 0);
        put32(udp_create[2] + 32, (uint32_t)udp_buffers[2]);
        put32(udp_create[2] + 36, 8192);
        put16(udp_create[2] + 44, first);
        put16(udp_create[2] + 50, first + 1);
        multi_error = control(udp_create[2], 0);
        if (!multi_error)
            break;
    }
    check(!multi_error, "UDPMultiCreate binds an actual port range");
    if (!multi_error) {
        uint32_t multi = get32(udp_create[2] + 28);
        wds(list, payload, 31);
        put16(send_pb + 38, first + 1);
        check(!control(send_pb, 0), "send to second range port");
        prepare(read_pb, 29, multi);
        put16(read_pb + 32, 2);
        check(!control(read_pb, 0) && get16(read_pb + 56) == first + 1 && get16(read_pb + 44) == 31,
              "UDPMultiRead identifies destination port");
        prepare(pb, 22, multi);
        put32(pb + 40, (uint32_t)udp_buffers[2]);
        control(pb, 0);
        prepare(pb, 24, multi);
        check(!control(pb, 0), "UDPRelease range");
    }

    for (unsigned i = 0; i < 2; ++i)
        check(!create(tcp_create[i], tcp_buffers[i], 0), "TCPCreate");
    uint32_t listener = get32(tcp_create[0] + 28), client = get32(tcp_create[1] + 28);
    unsigned char listen_pb[128], open_pb[128], second_send[128], second_list[12];
    prepare(listen_pb, 31, listener);
    put32(listen_pb + 42, 0x7f000001);
    listen_pb[35] = 2;
    put32(listen_pb + 94, 0x2468ace0);
    put32(listen_pb + 12, (uint32_t)io_done);
    check(!control(listen_pb, 1) && get16(listen_pb + 16) == 1, "TCPPassiveOpen queues and listens");
    prepare(pb, 40, listener);
    check(!control(pb, 0) && pb[52] == 2 && get16(pb + 48), "TCPStatus reports listening endpoint");
    uint16_t tcp_port = get16(pb + 48);
    prepare(open_pb, 32, client);
    put32(open_pb + 36, 0x7f000001);
    put16(open_pb + 40, tcp_port);
    put32(open_pb + 42, 0x7f000001);
    open_pb[35] = 2;
    put32(open_pb + 94, 0x2468ace0);
    put32(open_pb + 12, (uint32_t)io_done);
    check(!control(open_pb, 1), "TCPActiveOpen accepted");
    check(!wait_result(listen_pb) && !wait_result(open_pb), "active and passive opens complete on guest thread");
    prepare(pb, 41, client);
    put32(pb + 94, (uint32_t)stats);
    check(!control(pb, 0) && pb[52] == 8 && get32(pb + 90), "TCPExtendedStat reports established connection and MSS");
    prepare(send_pb, 34, client);
    wds(list, payload, 257);
    put32(send_pb + 38, (uint32_t)list);
    send_pb[35] = 1;
    put32(send_pb + 12, (uint32_t)io_done);
    prepare(second_send, 34, client);
    wds(second_list, payload + 257, 256);
    put32(second_send + 38, (uint32_t)second_list);
    second_send[35] = 1;
    put32(second_send + 12, (uint32_t)io_done);
    check(!control(send_pb, 1) && !control(second_send, 1), "queue two TCP sends in order");
    unsigned char copied[513];
    unsigned used = 0;
    while (used < 513) {
        prepare(read_pb, 37, listener);
        read_pb[32] = 2;
        put32(read_pb + 36, (uint32_t)(copied + used));
        put16(read_pb + 40, 513 - used);
        short error = control(read_pb, 0);
        if (error || !get16(read_pb + 40))
            break;
        used += get16(read_pb + 40);
    }
    check(used == 513 && !memcmp(copied, payload, 513), "TCPRcv preserves ordered stream bytes across requests");
    check(!wait_result(send_pb) && !wait_result(second_send), "both TCP send completions finish");

    unsigned char bulk_pb[4][128], bulk_wds[4][12];
    Ptr bulk_copy = NewPtr(4L * 65535);
    check(bulk_copy != NULL, "allocate partial-send stress buffer");
    if (bulk_copy) {
        int accepted_all = 1;
        for (unsigned packet = 0; packet < 4; ++packet) {
            for (unsigned at = 0; at < 65535; ++at)
                bulk_payload[packet][at] = packet * 53 + at * 37;
            prepare(bulk_pb[packet], 34, client);
            wds(bulk_wds[packet], bulk_payload[packet], 65535);
            put32(bulk_pb[packet] + 38, (uint32_t)bulk_wds[packet]);
            bulk_pb[packet][35] = 1;
            put32(bulk_pb[packet] + 12, (uint32_t)io_done);
            if (control(bulk_pb[packet], 1))
                accepted_all = 0;
        }
        check(accepted_all, "queue four maximum-length sends before receiving");
        uint32_t copied_count = 0;
        while (copied_count < 4UL * 65535) {
            prepare(read_pb, 37, listener);
            read_pb[32] = 2;
            put32(read_pb + 36, (uint32_t)(bulk_copy + copied_count));
            uint32_t remaining = 4UL * 65535 - copied_count;
            put16(read_pb + 40, remaining > 65535 ? 65535 : remaining);
            if (control(read_pb, 0) || !get16(read_pb + 40))
                break;
            copied_count += get16(read_pb + 40);
        }
        int ordered = copied_count == 4UL * 65535;
        for (unsigned packet = 0; packet < 4 && ordered; ++packet)
            if (memcmp(bulk_copy + packet * 65535UL, bulk_payload[packet], 65535))
                ordered = 0;
        check(ordered, "partial TCP sends retain ordering while synchronous receives progress other I/O");
        int finished_all = 1;
        for (unsigned packet = 0; packet < 4; ++packet)
            if (wait_result(bulk_pb[packet]))
                finished_all = 0;
        check(finished_all, "all maximum-length send completions finish");
        DisposePtr(bulk_copy);
    }

    wds(list, payload, 31);
    check(!control(send_pb, 0), "send for TCP no-copy receive");
    unsigned char rds[12];
    prepare(read_pb, 35, listener);
    read_pb[32] = 2;
    put32(read_pb + 42, (uint32_t)rds);
    put16(read_pb + 46, 2);
    check(!control(read_pb, 0) && get16(rds) == 31 && get32(rds + 2) == (uint32_t)tcp_buffers[0] &&
              !memcmp(tcp_buffers[0], payload, 31),
          "TCPNoCopyRcv leases caller-provided stream buffer");
    prepare(pb, 36, listener);
    put32(pb + 42, (uint32_t)rds);
    check(!control(pb, 0), "TCPRcvBfrReturn");

    prepare(pb, 41, client);
    put32(pb + 94, (uint32_t)stats);
    check(!control(pb, 0) && get32(stats + 24) >= 513 && !get32(stats + 20),
          "TCPConnectionStats packs byte counters separately from unavailable measurements");
    prepare(pb, 43, 0);
    check(!control(pb, 0) && get32(pb + 32) && get32(pb + 36) && get32(pb + 40) && get16(pb + 48) >= 2,
          "TCPGlobalInfo returns live parameter/statistics/connection tables");
    prepare(pb, 38, client);
    check(!control(pb, 0), "TCPClose performs graceful send shutdown");
    prepare(read_pb, 37, listener);
    read_pb[32] = 2;
    put32(read_pb + 36, (uint32_t)copied);
    put16(read_pb + 40, 513);
    check(control(read_pb, 0) == -23005, "TCPRcv reports peer orderly close");
    SystemTask();
    check(tcp_closing == 1, "TCPClosing delivered exactly once");
    prepare(pb, 39, listener);
    check(!control(pb, 0) && tcp_terminated == 1, "TCPAbort cancels and notifies termination");
    for (unsigned i = 0; i < 2; ++i) {
        prepare(pb, 42, get32(tcp_create[i] + 28));
        check(!control(pb, 0) && get32(pb + 32) == (uint32_t)tcp_buffers[i], "TCPRelease returns original buffer");
    }
    for (unsigned i = 0; i < 2; ++i) {
        prepare(pb, 24, get32(udp_create[i] + 28));
        check(!control(pb, 0) && get32(pb + 32) == (uint32_t)udp_buffers[i], "UDPRelease returns original buffer");
    }
    timeout_policy(1);
    timeout_policy(0);
    check(!callback_failures, "callback arguments and opaque user data remain valid");
    check(!CloseDriver(driver), "close driver after all streams released");
    char line[80];
    long length = sprintf(line, "failures=%u checks=%u\n", failures, checks);
    FSWrite(report, &length, line);
    FSClose(report);
    return failures != 0;
}
