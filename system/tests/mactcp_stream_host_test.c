/*+===================================================================
  File:      mactcp_stream_host_test.c

  Summary:   Exercise real Windows nonblocking TCP, UDP ranges, half-close and stream lifetime.

  Classes:   None.

  Functions: main.

  Origin:    ROMAW transport regression suite using loopback sockets through the production backend.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rsys/host_mactcp_stream.h"

static void connect_pair(host_mactcp_stream** server, host_mactcp_stream** client)
{
    int error;
    *server = host_mactcp_stream_create(0, 0, 0, 0, &error);
    assert(*server && !error);
    assert(host_mactcp_stream_open(*server, 1, 0x7f000001, 0, 0, 0, 64, 0, 0, NULL, 0) == 1);
    host_mactcp_stream_status status;
    assert(!host_mactcp_stream_status_get(*server, &status) && status.local_port);
    *client = host_mactcp_stream_create(0, 0, 0, 0, &error);
    assert(*client && !error);
    error = host_mactcp_stream_open(*client, 0, 0x7f000001, 0, 0x7f000001, status.local_port, 64, 0, 0, NULL, 0);
    assert(error == 0 || error == 1);
    DWORD deadline = GetTickCount() + 5000;
    int accepted, connected;
    do {
        accepted = host_mactcp_stream_open_result(*server);
        connected = host_mactcp_stream_open_result(*client);
        assert(accepted >= 0 && connected >= 0);
        if (accepted || connected)
            Sleep(1);
    } while ((accepted || connected) && (LONG)(deadline - GetTickCount()) > 0);
    assert(!accepted && !connected);
}

static int receive(host_mactcp_stream* stream, void* bytes, uint32_t size, uint32_t* received, uint32_t* source,
                   uint16_t* source_port, uint16_t* local_port)
{
    DWORD deadline = GetTickCount() + 5000;
    uint32_t destination;
    unsigned urgent;
    int error;
    do {
        error = host_mactcp_stream_receive(stream, bytes, size, received, source, source_port, &destination, local_port,
                                           &urgent);
        if (error == 1)
            Sleep(1);
    } while (error == 1 && (LONG)(deadline - GetTickCount()) > 0);
    return error;
}

int main(void)
{
    host_mactcp_stream *server, *client;
    connect_pair(&server, &client);
    char received[8192], payload[8192];
    for (unsigned i = 0; i < sizeof payload; ++i)
        payload[i] = (i * 19) & 255;
    uint32_t sent, count, source;
    uint16_t source_port, local_port;
    assert(!host_mactcp_stream_send(client, payload, sizeof payload, 1, 0, 0, 0, 0, &sent) && sent > 0);
    unsigned total = 0;
    while (total < sent) {
        assert(!receive(server, received + total, sizeof received - total, &count, &source, &source_port, &local_port));
        assert(count && source == 0x7f000001);
        total += count;
    }
    assert(!memcmp(received, payload, sent));
    host_mactcp_stream_status status;
    assert(!host_mactcp_stream_status_get(client, &status));
    assert(status.local_port && status.remote_port && status.bytes_sent >= sent && status.maximum_segment);
    assert(!host_mactcp_stream_shutdown(client, 0));
    assert(receive(server, received, sizeof received, &count, &source, &source_port, &local_port) == -23005);
    assert(!host_mactcp_stream_shutdown(server, 1));
    host_mactcp_stream_dispose(client);
    host_mactcp_stream_dispose(server);

    int error;
    host_mactcp_stream* udp = host_mactcp_stream_create(1, 0x7f000001, 0, 0, &error);
    assert(udp && !error);
    assert(!host_mactcp_stream_status_get(udp, &status));
    uint16_t port = status.local_port;
    host_mactcp_stream* duplicate = host_mactcp_stream_create(1, 0x7f000001, port, port, &error);
    assert(!duplicate && error == -23017);
    host_mactcp_stream* sender = host_mactcp_stream_create(1, 0x7f000001, 0, 0, &error);
    assert(sender && !error);
    assert(!host_mactcp_stream_send(sender, payload, 513, 0, 0, 0x7f000001, port, 0, &sent) && sent == 513);
    assert(!receive(udp, received, sizeof received, &count, &source, &source_port, &local_port));
    assert(count == 513 && source == 0x7f000001 && local_port == port && !memcmp(received, payload, 513));
    assert(!host_mactcp_stream_send(sender, payload, 0, 0, 0, 0x7f000001, port, 0, &sent));
    assert(!receive(udp, received, sizeof received, &count, &source, &source_port, &local_port) && !count);
    host_mactcp_stream_dispose(udp);

    host_mactcp_stream* range = NULL;
    for (unsigned i = 0; i < 100 && !range; ++i) {
        if (port < 1024 || port > 65000)
            port = 40000;
        range = host_mactcp_stream_create(1, 0x7f000001, port, port + 1, &error);
        if (!range)
            port += 2;
    }
    assert(range);
    assert(!host_mactcp_stream_udp_checksum(sender, 0));
    assert(!host_mactcp_stream_udp_checksum(range, 1));
    assert(!host_mactcp_stream_send(sender, payload, 8, 0, 0, 0x7f000001, port + 1, 0, &sent));
    assert(!receive(range, received, sizeof received, &count, &source, &source_port, &local_port));
    assert(count == 8 && local_port == port + 1);
    assert(!host_mactcp_stream_send(range, payload, 3, 0, 0, 0x7f000001, source_port, port + 1, &sent));
    assert(!receive(sender, received, sizeof received, &count, &source, &source_port, &local_port));
    assert(count == 3 && source_port == port + 1);
    host_mactcp_stream_dispose(sender);
    host_mactcp_stream_dispose(range);

    uint32_t algorithm, minimum_ms, maximum_ms;
    assert(!host_mactcp_tcp_parameters(&algorithm, &minimum_ms, &maximum_ms));
    assert(algorithm && maximum_ms >= minimum_ms && maximum_ms);
    uint16_t maximum;
    assert(!host_mactcp_udp_mtu(0x7f000001, &maximum) && maximum >= 512 && maximum <= 65507);
    host_mactcp_interface_info network_info;
    assert(!host_mactcp_interface_get(0x7f000001, &network_info) && network_info.mtu);
    puts(
        "PASS MacTCP streams: active/passive TCP, partial I/O, live TCP metrics, half-close/abort, UDP datagrams/zero payload, exclusive binds, port-range receive/send and real interface data");
    return 0;
}
