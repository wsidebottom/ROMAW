/*+===================================================================
  File:      tcp.h

  Summary:   Each parameter block and every referenced buffer remains alive until its asynchronous operation completes.
             All controls, including create/abort/release, use PBControlAsync and yield through WaitNextEvent while
             pending.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in tools/classic-remote; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT
   Original asynchronous MacTCP transport. Mac OS Open Transport supplies the
   compatible .IPP driver. ABI layouts follow the user's MacTCP interfaces;
   no Apple implementation or SDK headers are linked or redistributed.

   Each parameter block and every referenced buffer remains alive until its
   asynchronous operation completes. All controls, including create/abort/release,
   use PBControlAsync and yield through WaitNextEvent while pending. */

enum {
    TCP_CREATE = 30,
    TCP_LISTEN = 31,
    TCP_SEND = 34,
    TCP_RECEIVE = 37,
    TCP_CLOSE = 38,
    TCP_ABORT = 39,
    TCP_RELEASE = 42
};

static u16 tcp_driver;
static u32 tcp_stream;
static unsigned tcp_connected;
_Alignas(4) static u8 tcp_pb[256], tcp_cleanup_pb[256], tcp_buffer[32768], tcp_wds[12];

static void tcp_prepare(u8* pb, unsigned operation)
{
    memset(pb, 0, 256);
    put16(pb + 24, tcp_driver);
    put16(pb + 26, operation);
    put32(pb + 28, tcp_stream);
}

static s32 tcp_result(const u8* pb)
{
    /* ioResult is written by the driver, including at interrupt time. */
    u16 value = *(const volatile u16*)(pb + 16);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap16(value);
#endif
    return (s16)value;
}

static s32 tcp_start_block(u8* pb)
{
    put16(pb + 16, 1);
    s32 error = (s16)call(CONTROL_ASYNC, address(pb), 0, 0, 0);
    if (error < 0)
        put16(pb + 16, error);
    return error < 0 ? error : 0;
}

static s32 tcp_start(void)
{
    return tcp_start_block(tcp_pb);
}

static s32 tcp_local(u8* pb)
{
    s32 error = tcp_start_block(pb);
    if (error)
        return error;
    while (tcp_result(pb) > 0)
        remote_idle(1);
    return tcp_result(pb);
}

static void tcp_disconnect(unsigned graceful);

static s32 tcp_wait(u32 duration)
{
    u32 deadline = call(TICK_COUNT, 0, 0, 0, 0) + duration;
    while (tcp_result(tcp_pb) > 0) {
        u32 tick = remote_idle(1);
        if (duration && (s32)(tick - deadline) >= 0) {
            tcp_disconnect(0);
            return -1712;
        }
    }
    return tcp_result(tcp_pb);
}

static void tcp_disconnect(unsigned graceful)
{
    release_input();
    input_enabled = 0;
    if (!tcp_stream)
        return;
    if (graceful && tcp_connected && tcp_result(tcp_pb) <= 0) {
        tcp_prepare(tcp_pb, TCP_CLOSE);
        if (!tcp_start()) {
            u32 deadline = call(TICK_COUNT, 0, 0, 0, 0) + 180;
            while (tcp_result(tcp_pb) > 0 && (s32)(remote_idle(1) - deadline) < 0)
                ;
        }
    }
    tcp_prepare(tcp_cleanup_pb, TCP_ABORT);
    tcp_local(tcp_cleanup_pb);
    /* Aborting the stream completes outstanding I/O. Never reuse its parameter
     block or release buffers while the driver still owns them. Keep yielding
     if a faulty driver delays cancellation. */
    while (tcp_result(tcp_pb) > 0)
        remote_idle(1);
    // A failed release does not transfer buffer ownership back to the app.
    // Keep the stream and static buffers alive, yielding between retries rather
    // than exiting or creating another stream over driver-owned memory.
    for (;;) {
        tcp_prepare(tcp_cleanup_pb, TCP_RELEASE);
        if (!tcp_local(tcp_cleanup_pb))
            break;
        remote_idle(60);
    }
    tcp_stream = 0;
    tcp_connected = 0;
}

static s32 tcp_initialize(void)
{
    static const u8 driver_name[] = "\004.IPP";
    return (s16)call(OPEN_DRIVER, address(driver_name), address(&tcp_driver), 0, 0);
}

static s32 tcp_accept(void)
{
    tcp_prepare(tcp_pb, TCP_CREATE);
    put32(tcp_pb + 32, address(tcp_buffer));
    put32(tcp_pb + 36, sizeof tcp_buffer);
    s32 error = tcp_local(tcp_pb);
    if (error)
        return error;
    tcp_stream = get32(tcp_pb + 28);
    if (!tcp_stream)
        return -50;
    tcp_prepare(tcp_pb, TCP_LISTEN);
    /* TCPOpenPB is packed to two-byte alignment: localPort is at +14. */
    put16(tcp_pb + 46, 6804);
    error = tcp_start();
    if (!error)
        error = tcp_wait(0);
    if (!error)
        tcp_connected = 1;
    return error;
}

static s32 tcp_send(const u8* data, u32 length)
{
    while (length) {
        u32 chunk = length > 32768 ? 32768 : length;
        memset(tcp_wds, 0, sizeof tcp_wds);
        put16(tcp_wds, chunk);
        put32(tcp_wds + 2, address(data));
        tcp_prepare(tcp_pb, TCP_SEND);
        tcp_pb[35] = 1; /* Push short command responses promptly. */
        put32(tcp_pb + 38, address(tcp_wds));
        s32 error = tcp_start();
        if (!error)
            error = tcp_wait(900);
        if (error)
            return error;
        data += chunk;
        length -= chunk;
    }
    return 0;
}

static s32 tcp_receive(u8* data, u32 length)
{
    /* TCP has no message boundaries. Accumulate partial receives, with one
     deadline for the complete command rather than a fresh one per byte. */
    u32 deadline = call(TICK_COUNT, 0, 0, 0, 0) + 900;
    while (length) {
        s32 remaining = (s32)(deadline - call(TICK_COUNT, 0, 0, 0, 0));
        if (remaining <= 0)
            return -1712;
        u32 chunk = length > 32768 ? 32768 : length;
        tcp_prepare(tcp_pb, TCP_RECEIVE);
        tcp_pb[32] = 15; /* Driver receive timeout, in seconds. */
        put32(tcp_pb + 36, address(data));
        put16(tcp_pb + 40, chunk);
        s32 error = tcp_start();
        if (!error)
            error = tcp_wait(remaining);
        if (error)
            return error;
        u32 received = get16(tcp_pb + 40);
        if (!received || received > chunk)
            return -36;
        data += received;
        length -= received;
    }
    return 0;
}
