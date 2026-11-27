/*+===================================================================
  File:      mactcp_stream.c

  Summary:   Implement MacTCP TCP/UDP controls, queued I/O, notifications and guest buffer ownership.

  Classes:   None.

  Functions: ROMlib_mactcp_stream_control, ROMlib_mactcp_stream_poll, ROMlib_mactcp_stream_reset.

  Origin:    ROMAW implementation of public MacTCP.h parameter-block and callback contracts.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "rsys/mactcp.h"
#include "rsys/host_mactcp_stream.h"
#include "rsys/glue.h"
#include "MemoryMgr.h"
#include "OSUtil.h"
#include "ToolboxEvent.h"

enum { stream_limit = 32, operation_limit = 128 };

typedef struct {
    host_mactcp_stream* host;
    Ptr token;
    uint64 serial;
    uint32 buffer, capacity, notify, user, saved_world, borrowed_rds;
    uint32 sent, received;
    unsigned udp, borrowed, data_notified, closing_notified, terminate_notified, opened;
    unsigned tos, precedence, ulp_timeout, ulp_action;
} mac_stream;

typedef struct {
    uint64 serial, stream_serial;
    unsigned stream_index, code, async, urgent, push, ulp_action;
    uint32 pb, saved_world, deadline, length, position;
    unsigned char* data;
} mac_operation;

static mac_stream streams[stream_limit];
static mac_operation operations[operation_limit];
static uint64 next_serial;
static boolean_t polling;
static Ptr global_data, interface_data;
static uint32 opened_count, accepted_count, closed_count, aborted_count, attempt_count;
static uint64 total_sent, total_received;

static uint16 read16(const void* p)
{
    uint16 value;
    memcpy(&value, p, 2);
    return CW(value);
}

static uint32 read32(const void* p)
{
    uint32 value;
    memcpy(&value, p, 4);
    return CL(value);
}

static void write16(void* p, uint16 value)
{
    value = CW(value);
    memcpy(p, &value, 2);
}

static void write32(void* p, uint32 value)
{
    value = CL(value);
    memcpy(p, &value, 4);
}

static void* memory(uint32 address, uint32 length, int write)
{
    return ROMlib_mactcp_memory(address, length, write);
}

static mac_stream* find_stream(uint32 token)
{
    for (unsigned i = 0; i < stream_limit; ++i)
        if (streams[i].host && US_TO_SYN68K(streams[i].token) == token)
            return &streams[i];
    return NULL;
}

static void notify(mac_stream* stream, unsigned event, unsigned reason)
{
    uint32 callback = stream->notify;
    if (!callback)
        return;
    uint32 token = US_TO_SYN68K(stream->token), saved_world = EM_A5;
    uint32 user = stream->user;
    EM_A5 = stream->saved_world;
    if (stream->udp)
        CToPascalCall(SYN68K_TO_US(callback), (4 << 3) | (2 << 6) | (4 << 9) | (5 << 12), token, event, user, NULL);
    else
        CToPascalCall(SYN68K_TO_US(callback), (4 << 3) | (2 << 6) | (4 << 9) | (2 << 12) | (5 << 15), token, event,
                      user, reason, NULL);
    EM_A5 = saved_world;
}

static void cancel_operations(mac_stream* stream, int error);

static void finish(mac_operation* operation, int error)
{
    uint32 address = operation->pb, saved_world = EM_A5, callback_world = operation->saved_world;
    BOOLEAN async = operation->async;
    uint64 stream_serial = operation->stream_serial;
    mac_stream* stream = &streams[operation->stream_index];
    unsigned code = operation->code, action = operation->ulp_action;
    uint32 transferred = operation->position;
    int ulp_expired = error == -23016 && (code == 34 || code == 38);
    if (ulp_expired && !action && stream->host && stream->serial == stream_serial) {
        host_mactcp_stream_shutdown(stream->host, TRUE);
        stream->opened = FALSE;
        stream->terminate_notified = TRUE;
        ++aborted_count;
    }
    free(operation->data);
    memset(operation, 0, sizeof *operation);
    ParmBlkPtr pb = memory(address, 28, TRUE);
    if (pb) {
        if (code == 34) {
            unsigned char* send = memory(address, 48, TRUE);
            if (send)
                write16(send + 46, transferred);
        }
        EM_A5 = callback_world;
        ROMlib_mactcp_complete(pb, async, error);
        EM_A5 = saved_world;
    }
    if (ulp_expired && stream->host && stream->serial == stream_serial) {
        if (!action)
            cancel_operations(stream, abortErr);
        if (stream->host && stream->serial == stream_serial)
            notify(stream, action ? 2 : 3, action ? 0 : 5);
    }
}

static void cancel_operations(mac_stream* stream, int error)
{
    uint64 serial = stream->serial, submitted = next_serial;
    /* Detach each operation before its completion can release or reuse a stream. */
    for (unsigned i = 0; i < operation_limit; ++i)
        if (operations[i].serial && operations[i].serial <= submitted && operations[i].stream_serial == serial)
            finish(&operations[i], error);
}

void ROMlib_mactcp_stream_reset(void)
{
    for (unsigned i = 0; i < operation_limit; ++i) {
        free(operations[i].data);
        memset(&operations[i], 0, sizeof operations[i]);
    }
    for (unsigned i = 0; i < stream_limit; ++i) {
        host_mactcp_stream_dispose(streams[i].host);
        if (streams[i].token)
            DisposPtr(streams[i].token);
        memset(&streams[i], 0, sizeof streams[i]);
    }
    if (global_data)
        DisposPtr(global_data);
    if (interface_data)
        DisposPtr(interface_data);
    global_data = interface_data = NULL;
    opened_count = accepted_count = closed_count = aborted_count = attempt_count = 0;
    total_sent = total_received = 0;
}

static int scatter_data(uint32 address, unsigned char** data, uint32* length)
{
    uint32 total = 0;
    unsigned count;
    for (count = 0; count < 1024; ++count) {
        const unsigned char* entry = memory(address + count * 6, 6, FALSE);
        if (!entry)
            return -23014;
        uint16 size = read16(entry);
        if (!size)
            break;
        if (size > 65535 - total || !memory(read32(entry + 2), size, FALSE))
            return -23014;
        total += size;
    }
    if (count == 1024)
        return -23014;
    unsigned char* bytes = malloc(total ? total : 1);
    if (!bytes)
        return -23009;
    uint32 at = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char* entry = memory(address + i * 6, 6, FALSE);
        uint16 size = read16(entry);
        memcpy(bytes + at, memory(read32(entry + 2), size, FALSE), size);
        at += size;
    }
    *data = bytes;
    *length = total;
    return 0;
}

static int operation_progress(mac_operation* operation)
{
    if (!operation->serial)
        return 0;
    mac_stream* stream = &streams[operation->stream_index];
    if (!stream->host || stream->serial != operation->stream_serial)
        return -23010;
    unsigned char* pb = memory(operation->pb, stream->udp ? 58 : 102, TRUE);
    if (!pb)
        return -23013;
    if (operation->deadline && (int32)(TickCount() - operation->deadline) >= 0) {
        if (operation->code == 31 || operation->code == 32)
            host_mactcp_stream_shutdown(stream->host, 1);
        return -23016;
    }
    /* TCP is a byte stream. A later send must not overtake a partial send;
       read requests likewise retain submission order across no-copy leases. */
    unsigned category =
        operation->code == 34 || operation->code == 38 || operation->code == 23 || operation->code == 28 ? 1 : 2;
    if (operation->code != 31 && operation->code != 32)
        for (unsigned i = 0; i < operation_limit; ++i) {
            const mac_operation* older = &operations[i];
            unsigned older_category =
                older->code == 34 || older->code == 38 || older->code == 23 || older->code == 28 ? 1 : 2;
            if (older->serial && older->serial < operation->serial &&
                older->stream_serial == operation->stream_serial &&
                (category == older_category || older->code == 31 || older->code == 32))
                return 1;
        }
    int error;
    if (operation->code == 31 || operation->code == 32) {
        error = host_mactcp_stream_open_result(stream->host);
        if (!error) {
            host_mactcp_stream_status status;
            error = host_mactcp_stream_status_get(stream->host, &status);
            if (!error) {
                write32(pb + 36, status.remote_address);
                write16(pb + 40, status.remote_port);
                write32(pb + 42, status.local_address);
                write16(pb + 46, status.local_port);
                stream->opened = TRUE;
                stream->closing_notified = stream->terminate_notified = FALSE;
                if (operation->code == 31)
                    ++accepted_count;
                else
                    ++opened_count;
            }
        }
        return error;
    }
    if (operation->code == 38) {
        error = host_mactcp_stream_shutdown(stream->host, FALSE);
        if (!error)
            ++closed_count;
        return error;
    }
    if (operation->code == 34 || operation->code == 23 || operation->code == 28) {
        uint32 remaining = operation->length - operation->position, transferred = 0;
        int urgent = operation->urgent && remaining == 1;
        if (operation->urgent && remaining > 1)
            --remaining;
        if (stream->udp) {
            error = host_mactcp_stream_udp_checksum(stream->host, pb[44] != 0);
            if (error)
                return error;
        }
        error = host_mactcp_stream_send(stream->host, operation->data + operation->position, remaining, operation->push,
                                        urgent, stream->udp ? read32(pb + 34) : 0, stream->udp ? read16(pb + 38) : 0,
                                        operation->code == 28 ? read16(pb + 52) : 0, &transferred);
        if (error)
            return error;
        operation->position += transferred;
        stream->sent += transferred;
        if (!stream->udp)
            total_sent += transferred;
        if (operation->position < operation->length)
            return 1;
        write16(pb + 46, operation->length);
        if (!stream->udp) {
            host_mactcp_stream_status status;
            if (!host_mactcp_stream_status_get(stream->host, &status))
                write32(pb + 42, status.send_window);
        }
        return 0;
    }

    if (stream->borrowed)
        return 1;
    uint32 capacity, target;
    int no_copy = stream->udp || operation->code == 35;
    if (no_copy) {
        target = stream->buffer;
        capacity = MIN(stream->capacity, 65535);
    }
    else {
        target = read32(pb + 36);
        capacity = read16(pb + 40);
    }
    if (!capacity)
        return -23006;
    void* destination_buffer = memory(target, capacity, TRUE);
    if (!destination_buffer)
        return -23013;
    uint32 received = 0, address = 0, destination = 0;
    uint16 port = 0, local_port = 0;
    unsigned urgent = 0;
    error = host_mactcp_stream_receive(stream->host, destination_buffer, capacity, &received, &address, &port,
                                       &destination, &local_port, &urgent);
    if (error)
        return error;
    stream->received += received;
    if (!stream->udp)
        total_received += received;
    stream->data_notified = FALSE;
    if (stream->udp) {
        write32(pb + 34, address);
        write16(pb + 38, port);
        write32(pb + 40, target);
        write16(pb + 44, received);
        write16(pb + 46, TickCount() / 60);
        write32(pb + 52, destination);
        write16(pb + 56, local_port);
    }
    else {
        pb[33] = 0;
        pb[34] = urgent;
        write16(pb + 40, received);
        write16(pb + 48, TickCount() / 60);
        if (no_copy) {
            unsigned char* rds = memory(read32(pb + 42), 6, TRUE);
            if (!rds || !read16(pb + 46))
                return -23014;
            write16(rds, received);
            write32(rds + 2, target);
            if (read16(pb + 46) > 1) {
                unsigned char* sentinel = memory(read32(pb + 42) + 6, 6, TRUE);
                if (sentinel)
                    memset(sentinel, 0, 6);
            }
            write16(pb + 46, 1);
            stream->borrowed_rds = read32(pb + 42);
        }
    }
    if (no_copy)
        stream->borrowed = TRUE;
    return 0;
}

static void poll_operations(void)
{
    struct {
        unsigned index;
        uint64 serial;
    } snapshot[operation_limit];

    unsigned count = 0;
    for (unsigned i = 0; i < operation_limit; ++i)
        if (operations[i].serial) {
            snapshot[count].index = i;
            snapshot[count++].serial = operations[i].serial;
        }
    for (unsigned i = 0; i < count; ++i) {
        mac_operation* operation = &operations[snapshot[i].index];
        if (operation->serial != snapshot[i].serial)
            continue;
        int error = operation_progress(operation);
        if (error != 1)
            finish(operation, error);
    }
}

void ROMlib_mactcp_stream_poll(void)
{
    if (polling)
        return;
    polling = TRUE;

    poll_operations();

    struct {
        unsigned index;
        uint64 serial;
    } active[stream_limit];

    unsigned count = 0;
    for (unsigned i = 0; i < stream_limit; ++i)
        if (streams[i].host) {
            active[count].index = i;
            active[count++].serial = streams[i].serial;
        }
    for (unsigned i = 0; i < count; ++i) {
        mac_stream* stream = &streams[active[i].index];
        if (!stream->host || stream->serial != active[i].serial)
            continue;
        host_mactcp_stream_status status;
        int error = host_mactcp_stream_status_get(stream->host, &status);
        if (error < 0 && stream->opened && !stream->terminate_notified) {
            stream->terminate_notified = TRUE;
            notify(stream, 3, error == -23012 ? 2 : 3);
        }
        else if ((status.available || status.urgent || status.readable) && !stream->borrowed &&
                 !stream->data_notified) {
            stream->data_notified = TRUE;
            notify(stream, stream->udp ? 1 : status.urgent ? 5 : 4, 0);
        }
        else if (!stream->udp && status.state == 14 && !stream->closing_notified) {
            stream->closing_notified = TRUE;
            notify(stream, 1, 0);
        }
    }
    polling = FALSE;
}

static int stream_status(mac_stream* stream, unsigned char* pb)
{
    host_mactcp_stream_status status;
    int error = host_mactcp_stream_status_get(stream->host, &status);
    if (error)
        return error;
    if (stream->udp) {
        write32(pb + 32, stream->buffer);
        write32(pb + 36, stream->capacity);
        write32(pb + 40, stream->notify);
        write16(pb + 44, status.local_port);
        write32(pb + 46, stream->user);
        return 0;
    }
    pb[32] = stream->ulp_timeout;
    pb[33] = stream->ulp_action;
    write32(pb + 38, status.remote_address);
    write16(pb + 42, status.remote_port);
    write32(pb + 44, status.local_address);
    write16(pb + 48, status.local_port);
    pb[50] = stream->tos;
    pb[51] = stream->precedence;
    pb[52] = status.state;
    write16(pb + 54, MIN(status.send_window, 65535));
    write16(pb + 56, MIN(status.receive_window, 65535));
    write16(pb + 58, MIN(status.in_flight, 65535));
    write16(pb + 60, MIN(status.available, 65535));
    write32(pb + 62, 0);
    write32(pb + 66, status.send_unacked);
    write32(pb + 70, status.send_next);
    write32(pb + 74, status.congestion_window);
    write32(pb + 78, status.receive_next);
    write32(pb + 82, ((uint64)status.round_trip_us * 60 + 999999) / 1000000);
    write32(pb + 86, ((uint64)status.last_round_trip_ms * 60 + 999) / 1000);
    write32(pb + 90, status.maximum_segment);
    uint32 statistics_address = read32(pb + 94);
    if (statistics_address) {
        unsigned char* statistics = memory(statistics_address, 86, TRUE);
        if (!statistics)
            return -23013;
        memset(statistics, 0, 86);
        write32(statistics + 12, status.bytes_received);
        write32(statistics + 24, status.bytes_sent);
        write32(statistics + 0, status.segments_received);
        write32(statistics + 4, status.segments_sent);
        write32(statistics + 8, status.retransmitted_segments);
        write32(statistics + 28, status.retransmitted);
        write16(statistics + 76, MIN(((uint64)status.last_round_trip_ms * 60 + 999) / 1000, 65535));
        write16(statistics + 80, MIN(((uint64)status.variance_ms * 60 + 999) / 1000, 65535));
        write16(statistics + 82, MIN(((uint64)status.retransmit_ms * 60 + 999) / 1000, 65535));
        statistics[84] = MIN(status.retry_count, 255);
        statistics[85] = MIN(status.quenches, 255);
        write16(statistics + 78, MIN(((uint64)status.round_trip_us * 60 + 999999) / 1000000, 65535));
    }
    return 0;
}

static int global_info(unsigned char* pb)
{
    if (!global_data)
        global_data = NewPtrSysClear(24 + 52 + stream_limit * 4);
    if (!global_data)
        return -23009;
    memset(global_data, 0, 24 + 52 + stream_limit * 4);
    uint32 algorithm, minimum_ms, maximum_ms;
    if (!host_mactcp_tcp_parameters(&algorithm, &minimum_ms, &maximum_ms)) {
        write32(global_data, algorithm);
        write32(global_data + 4, ((uint64)minimum_ms * 60 + 999) / 1000);
        write32(global_data + 8, ((uint64)maximum_ms * 60 + 999) / 1000);
    }
    write32(global_data + 16, stream_limit);
    uint64 sent = total_sent, received = total_received;
    unsigned used = 0;
    uint32 max_segment = 0, max_window = 0;
    for (unsigned i = 0; i < stream_limit; ++i) {
        if (!streams[i].host || streams[i].udp)
            continue;
        host_mactcp_stream_status status;
        if (!host_mactcp_stream_status_get(streams[i].host, &status)) {
            max_segment = MAX(max_segment, status.maximum_segment);
            max_window = MAX(max_window, status.receive_window);
        }
        write32(global_data + 76 + used++ * 4, US_TO_SYN68K(streams[i].token));
    }
    write32(global_data + 12, max_segment);
    write32(global_data + 20, max_window);
    write32(global_data + 24, attempt_count);
    write32(global_data + 28, opened_count);
    write32(global_data + 32, accepted_count);
    write32(global_data + 36, closed_count);
    write32(global_data + 40, aborted_count);
    write32(global_data + 44, received);
    write32(global_data + 48, sent);
    write32(pb + 32, US_TO_SYN68K(global_data));
    write32(pb + 36, US_TO_SYN68K(global_data + 24));
    write32(pb + 40, US_TO_SYN68K(global_data + 76));
    write16(pb + 48, stream_limit);
    return 0;
}

static int lap_statistics(unsigned char* pb)
{
    unsigned char* output = memory(read32(pb + 28), 24, TRUE);
    if (!output)
        return -23013;
    host_mactcp_interface_info info;
    int error = host_mactcp_interface_get(0, &info);
    if (error)
        return error;
    if (!interface_data)
        interface_data = NewPtrSysClear(256 + 32 + 20 * 14);
    if (!interface_data)
        return -23009;
    memcpy(interface_data, info.description, 256);
    memcpy(interface_data + 256, info.physical, 32);
    memset(interface_data + 288, 0, 20 * 14);
    for (unsigned i = 0; i < 20; ++i) {
        if (!info.arp[i].address)
            continue;
        unsigned char* entry = (unsigned char*)interface_data + 288 + i * 14;
        write16(entry + 2, 0x0800);
        write32(entry + 4, info.arp[i].address);
        memcpy(entry + 8, info.arp[i].physical, 6);
    }
    write16(output, info.type);
    write32(output + 2, US_TO_SYN68K(interface_data));
    write16(output + 6, info.mtu);
    write32(output + 8, info.speed);
    write16(output + 12, info.physical_length);
    write32(output + 14, US_TO_SYN68K(interface_data + 256));
    write32(output + 18, US_TO_SYN68K(interface_data + 288));
    write16(output + 22, 0);
    return 0;
}

OSErr ROMlib_mactcp_stream_control(ParmBlkPtr parameter_block, BOOLEAN async)
{
    unsigned char* pb = (unsigned char*)parameter_block;
    unsigned code = read16(pb + 26);
    int error = noErr;
    if (code == 1) {
        uint64 submitted = next_serial;
        for (unsigned i = 0; i < operation_limit; ++i)
            if (operations[i].serial && operations[i].serial <= submitted) {
                if (operations[i].code == 31 || operations[i].code == 32)
                    host_mactcp_stream_shutdown(streams[operations[i].stream_index].host, TRUE);
                finish(&operations[i], abortErr);
            }
        ROMlib_mactcp_complete(parameter_block, async, noErr);
        return noErr;
    }
    if (code == 19 || code == 25 || code == 43) {
        if (code == 19)
            error = lap_statistics(pb);
        else if (code == 43)
            error = global_info(pb);
        else {
            uint16 maximum;
            error = host_mactcp_udp_mtu(read32(pb + 34), &maximum);
            if (!error)
                write16(pb + 32, maximum);
        }
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    if (code == 20 || code == 27 || code == 30) {
        mac_stream* stream = NULL;
        for (unsigned i = 0; i < stream_limit; ++i)
            if (!streams[i].host) {
                stream = &streams[i];
                break;
            }
        uint32 buffer = read32(pb + 32), capacity = read32(pb + 36);
        error = !capacity ? -23006 : !memory(buffer, capacity, TRUE) ? -23013 : !stream ? -23009 : 0;
        if (!error) {
            Ptr token = NewPtrSysClear(4);
            if (!token)
                error = -23009;
            else {
                int udp = code != 30;
                host_mactcp_stream* host = host_mactcp_stream_create(udp, 0, udp ? read16(pb + 44) : 0,
                                                                     code == 27 ? read16(pb + 50) : 0, &error);
                if (!host)
                    DisposPtr(token);
                else {
                    memset(stream, 0, sizeof *stream);
                    stream->host = host;
                    stream->token = token;
                    stream->serial = ++next_serial;
                    stream->udp = udp;
                    stream->buffer = buffer;
                    stream->capacity = capacity;
                    stream->ulp_timeout = 60;
                    stream->notify = read32(pb + 40);
                    stream->user = read32(pb + (udp ? 46 : 44));
                    stream->saved_world = EM_A5;
                    write32(pb + 28, US_TO_SYN68K(token));
                    if (udp) {
                        host_mactcp_stream_status status;
                        if (!host_mactcp_stream_status_get(host, &status))
                            write16(pb + 44, status.local_port);
                    }
                }
            }
        }
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    mac_stream* stream = find_stream(read32(pb + 28));
    if (!stream || (stream->udp ? code < 20 || code > 29 : code < 31 || code > 42)) {
        error = stream ? controlErr : -23010;
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    if (code == 22 || code == 36) {
        error = !stream->borrowed ||
                        (code == 22 ? read32(pb + 40) != stream->buffer : read32(pb + 42) != stream->borrowed_rds)
                    ? -23014
                    : 0;
        if (!error) {
            stream->borrowed = FALSE;
            stream->data_notified = FALSE;
            stream->borrowed_rds = 0;
        }
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    if (code == 24 || code == 42) {
        uint64 serial = stream->serial;
        cancel_operations(stream, -23012);
        if (stream->serial == serial && stream->host) {
            write32(pb + 32, stream->buffer);
            write32(pb + 36, stream->capacity);
            write32(pb + 40, stream->notify);
            if (stream->udp)
                write32(pb + 46, stream->user);
            else
                write32(pb + 44, stream->user);
            host_mactcp_stream_dispose(stream->host);
            DisposPtr(stream->token);
            memset(stream, 0, sizeof *stream);
        }
        ROMlib_mactcp_complete(parameter_block, async, noErr);
        return noErr;
    }
    if (code == 26 || code == 40 || code == 41) {
        error = stream_status(stream, pb);
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    if (code == 39) {
        uint64 serial = stream->serial;
        error = host_mactcp_stream_shutdown(stream->host, code == 39);
        if (!error) {
            if (code == 39) {
                ++aborted_count;
                cancel_operations(stream, -23012);
                if (stream->serial == serial && stream->host) {
                    stream->opened = FALSE;
                    stream->borrowed = FALSE;
                    notify(stream, 3, 6);
                }
            }
            else
                ++closed_count;
        }
        ROMlib_mactcp_complete(parameter_block, async, error);
        return error;
    }
    if (code != 31 && code != 32 && code != 34 && code != 35 && code != 37 && code != 38 && code != 21 && code != 23 &&
        code != 28 && code != 29) {
        ROMlib_mactcp_complete(parameter_block, async, controlErr);
        return controlErr;
    }
    for (unsigned i = 0; i < operation_limit; ++i)
        if (operations[i].serial && operations[i].pb == US_TO_SYN68K(pb)) {
            /* The caller still owns the pending PB; do not overwrite its result. */
            return -23007;
        }
    mac_operation* operation = NULL;
    for (unsigned i = 0; i < operation_limit; ++i)
        if (!operations[i].serial) {
            operation = &operations[i];
            break;
        }
    if (!operation) {
        ROMlib_mactcp_complete(parameter_block, async, -23009);
        return -23009;
    }
    memset(operation, 0, sizeof *operation);
    operation->serial = ++next_serial;
    operation->stream_serial = stream->serial;
    operation->stream_index = stream - streams;
    operation->code = code;
    operation->async = async;
    operation->pb = US_TO_SYN68K(pb);
    operation->saved_world = EM_A5;
    unsigned timeout = 0;
    if (!stream->udp && (code == 31 || code == 32 || code == 34 || code == 38)) {
        if ((pb[34] & 0x40) && pb[33] > 1) {
            finish(operation, paramErr);
            return paramErr;
        }
        if (pb[34] & 0x80)
            stream->ulp_timeout = pb[32];
        if (pb[34] & 0x40)
            stream->ulp_action = pb[33];
        operation->ulp_action = stream->ulp_action;
    }
    if (code == 31 || code == 32) {
        ++attempt_count;
        timeout = pb[35];
        if (stream->opened)
            error = -23011;
        else if (pb[52] || pb[53] > 40)
            error = paramErr;
        else {
            stream->tos = (pb[34] & 0x20) ? pb[48] : 0;
            stream->precedence = (pb[34] & 0x10) ? pb[49] : 0;
            unsigned tos = ((stream->tos & 1) ? 16 : 0) | ((stream->tos & 2) ? 8 : 0) | ((stream->tos & 4) ? 4 : 0) |
                           ((stream->precedence & 7) << 5);
            error = host_mactcp_stream_open(stream->host, code == 31, read32(pb + 42), read16(pb + 46), read32(pb + 36),
                                            read16(pb + 40), pb[51], tos, pb[50], pb + 54, pb[53]);
            if (!error || error == 1) {
                stream->user = read32(pb + 94);
                stream->saved_world = EM_A5;
            }
        }
    }
    else if (code == 38) {
        timeout = stream->ulp_timeout;
    }
    else if (code == 34 || code == 23 || code == 28) {
        operation->push = !stream->udp && pb[35];
        operation->urgent = !stream->udp && pb[36];
        timeout = stream->udp ? 60 : stream->ulp_timeout;
        error = scatter_data(read32(pb + (stream->udp ? 40 : 38)), &operation->data, &operation->length);
        if (!error && stream->udp && operation->length > 65507)
            error = -23006;
    }
    else {
        timeout = stream->udp ? read16(pb + 32) : pb[32];
        if (stream->borrowed)
            error = -23007;
        if (code == 35 && (!read16(pb + 46) || !memory(read32(pb + 42), 6, TRUE)))
            error = -23014;
    }
    operation->deadline = timeout ? TickCount() + timeout * 60 : 0;
    pb[16] = 0;
    pb[17] = 1;
    uint64 serial = operation->serial;
    if (error < 0)
        finish(operation, error);
    else {
        error = operation_progress(operation);
        if (error != 1)
            finish(operation, error);
    }
    if (async)
        return read16(pb + 16) == 1 ? noErr : (int16)read16(pb + 16);
    while (operation->serial == serial) {
        error = operation_progress(operation);
        if (error != 1)
            finish(operation, error);
        else {
            poll_operations();
            if (operation->serial == serial) {
                ULONGINT final_ticks;
                Delay(1, &final_ticks);
            }
        }
    }
    return (int16)read16(pb + 16);
}
