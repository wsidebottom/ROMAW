/*+===================================================================
  File:      test_tcp.c

  Summary:   Implements test tcp support for ROMAW build and development support.

  Classes:   None.

  Functions: main, remote_call

  Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT
   Exercise the real agent transport with a deterministic asynchronous driver.
   No host network, low-memory globals or physical Mac are touched. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define REMOTE_TCP 1
#define REMOTE_DIAGNOSTIC 0
#include "agent.c"

static u32 ticks, yields, aborts, releases, closes, delay = 2, fragment = 7;
static u8 *pending, *cleanup_pending;
static u32 pending_delay, source_offset, source_length, sent_length;
static u32 cleanup_delay, cancellation_delay, cancelling;
static unsigned stalled, bad_length, immediate_error;
static unsigned release_errors, abort_error;
static unsigned driver_owns_buffer;
static u8 source_data[70000], sent_data[70000];

u32 remote_call(u32 vector, const u32* args)
{
    if (vector == TICK_COUNT)
        return ticks;
    if (vector == OPEN_DRIVER) {
        assert(!memcmp((void*)args[0], "\004.IPP", 5));
        *(u16*)args[1] = 0xfffa;
        return 0;
    }
    if (vector == WAIT_NEXT_EVENT) {
        ticks += args[2];
        ++yields;
        if (cleanup_pending && !--cleanup_delay) {
            unsigned operation = get16(cleanup_pending + 26);
            s32 result = 0;
            if (operation == TCP_ABORT && pending) {
                cancelling = cancellation_delay;
                if (!cancelling) {
                    put16(pending + 16, -23005);
                    pending = NULL;
                }
            }
            if (operation == TCP_RELEASE && release_errors) {
                --release_errors;
                result = -23000;
            }
            if (operation == TCP_RELEASE && !result)
                driver_owns_buffer = 0;
            put16(cleanup_pending + 16, result);
            cleanup_pending = NULL;
        }
        if (pending && cancelling) {
            if (!--cancelling) {
                put16(pending + 16, -23005);
                pending = NULL;
            }
        }
        else if (pending && (!stalled || get16(pending + 26) == TCP_CREATE) && !--pending_delay) {
            unsigned operation = get16(pending + 26);
            s32 result = 0;
            if (operation == TCP_CREATE) {
                assert(!driver_owns_buffer);
                driver_owns_buffer = 1;
                put32(pending + 28, 0x12345678);
            }
            else if (operation == TCP_RECEIVE) {
                u32 count = get16(pending + 40);
                if (count > fragment)
                    count = fragment;
                if (count > source_length - source_offset)
                    count = source_length - source_offset;
                if (count)
                    memcpy((void*)get32(pending + 36), source_data + source_offset, count);
                else
                    result = -23005;
                source_offset += count;
                put16(pending + 40, bad_length ? 32769 : count);
            }
            put16(pending + 16, result);
            pending = NULL;
        }
        return 0;
    }
    // Even local create/abort/release operations must never synchronously wait.
    assert(vector == CONTROL_ASYNC);
    u8* pb = (void*)args[0];
    assert(get16(pb + 24) == 0xfffa && !get32(pb + 12));
    unsigned operation = get16(pb + 26);
    if (operation == TCP_ABORT || operation == TCP_RELEASE) {
        assert(pb == tcp_cleanup_pb && !cleanup_pending);
        assert(tcp_stream == 0x12345678 && get32(pb + 28) == tcp_stream);
        if (operation == TCP_ABORT) {
            ++aborts;
            if (abort_error)
                return (u32)-50;
        }
        else {
            assert(!pending);
            ++releases;
        }
        cleanup_pending = pb;
        cleanup_delay = delay;
        return 0;
    }
    assert(pb == tcp_pb && !pending && !cleanup_pending);
    if (operation == TCP_CREATE) {
        assert(!tcp_stream && !driver_owns_buffer && get32(pb + 36) == 32768);
        assert(get32(pb + 32) == address(tcp_buffer));
        pending = pb;
        pending_delay = delay;
        return 0;
    }
    assert(get32(pb + 28) == 0x12345678);
    if (immediate_error)
        return (u32)-50;
    if (operation == TCP_LISTEN)
        assert(get16(pb + 46) == 6804 && !get32(pb + 36));
    else if (operation == TCP_SEND) {
        const u8* wds = (void*)get32(pb + 38);
        u32 count = get16(wds);
        assert(count && count <= 32768 && !get16(wds + 6) && pb[35]);
        assert(count <= sizeof sent_data - sent_length);
        memcpy(sent_data + sent_length, (void*)get32(wds + 2), count);
        sent_length += count;
    }
    else if (operation == TCP_RECEIVE)
        assert(get16(pb + 40) <= 32768 && pb[32] == 15);
    else {
        assert(operation == TCP_CLOSE);
        ++closes;
    }
    pending = pb;
    pending_delay = delay;
    return 0;
}

static void connect_test(void)
{
    assert(!tcp_stream && !pending && !cleanup_pending);
    assert(!tcp_accept());
    assert(tcp_connected && tcp_stream == 0x12345678);
}

int main(void)
{
    for (unsigned i = 0; i < IMPORT_COUNT; ++i)
        remote_imports[i] = i;
    for (unsigned i = 0; i < sizeof source_data; ++i)
        source_data[i] = (u8)(i * 37);
    assert(!tcp_initialize());
    connect_test();
    assert(yields >= 2);
    assert(!tcp_send(source_data, sizeof source_data));
    assert(sent_length == sizeof source_data && !memcmp(sent_data, source_data, sizeof source_data));
    u8 received[514] = {0};
    source_length = 512;
    assert(!tcp_receive(received + 1, 512));
    assert(!received[0] && !received[513]);
    assert(!memcmp(received + 1, source_data, 512));
    input_enabled = 1;
    tcp_disconnect(1);
    assert(!tcp_stream && !tcp_connected && !input_enabled);
    assert(closes == 1 && aborts == 1 && releases == 1);

    connect_test();
    stalled = 1;
    u32 before = ticks;
    assert(tcp_receive(received + 1, 512) == -1712);
    assert(ticks - before >= 900 + 2 * delay && !pending && !tcp_stream);
    stalled = 0;

    connect_test();
    source_offset = 0;
    source_length = 512;
    fragment = 1;
    delay = 3;
    before = ticks;
    assert(tcp_receive(received + 1, 512) == -1712);
    assert(ticks - before >= 900 + 2 * delay && source_offset < 512 && !pending);
    delay = 2;
    fragment = 7;

    connect_test();
    source_offset = 0;
    bad_length = 1;
    assert(tcp_receive(received + 1, 512) == -36);
    tcp_disconnect(0);
    bad_length = 0;

    connect_test();
    immediate_error = 1;
    assert(tcp_receive(received + 1, 512) == -50);
    assert(tcp_result(tcp_pb) == -50 && !pending);
    tcp_disconnect(0);
    immediate_error = 0;

    connect_test();
    source_offset = 0;
    source_length = 100;
    assert(tcp_receive(received + 1, 512) == -23005);
    tcp_disconnect(0);

    connect_test();
    stalled = 1;
    cancellation_delay = 9;
    release_errors = 2;
    before = ticks;
    u32 previous_releases = releases;
    assert(tcp_receive(received + 1, 512) == -1712);
    assert(ticks - before >= 900 + 9 + 120);
    assert(releases == previous_releases + 3);
    assert(!pending && !cleanup_pending && !tcp_stream);
    stalled = 0;
    cancellation_delay = 0;

    // A failed abort must still drain the original operation before release.
    connect_test();
    abort_error = 1;
    tcp_prepare(tcp_pb, TCP_SEND);
    memset(tcp_wds, 0, sizeof tcp_wds);
    put16(tcp_wds, 1);
    put32(tcp_wds + 2, address(source_data));
    put32(tcp_pb + 38, address(tcp_wds));
    tcp_pb[35] = 1;
    sent_length = 0;
    assert(!tcp_start());
    before = yields;
    tcp_disconnect(0);
    assert(yields - before >= 2 * delay);
    assert(!pending && !cleanup_pending && !tcp_stream);
    abort_error = 0;
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        delay = 1 + cycle % 5;
        cancellation_delay = cycle % 7;
        release_errors = cycle % 3;
        connect_test();
        if (cycle & 1) {
            tcp_prepare(tcp_pb, TCP_RECEIVE);
            tcp_pb[32] = 15;
            put32(tcp_pb + 36, address(received + 1));
            put16(tcp_pb + 40, 1);
            stalled = 1;
            assert(!tcp_start());
            tcp_disconnect(0);
            stalled = 0;
        }
        else
            tcp_disconnect(1);
        assert(!driver_owns_buffer && !pending && !cleanup_pending && !tcp_stream);
    }
    puts("TCP transport: partial I/O, yields, deadlines and cleanup passed");
    return 0;
}
