/*+===================================================================
  File:      host_mactcp_stream.h

  Summary:   Nonblocking host TCP/UDP transport, interface information and TCP diagnostics.

  Classes:   None.

  Functions: Host stream lifecycle, open, I/O, status and interface declarations.

  Origin:    ROMAW Windows implementation of the public MacTCP transport contracts.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#ifndef RSYS_HOST_MACTCP_STREAM_H
#define RSYS_HOST_MACTCP_STREAM_H
#include <stdint.h>

typedef struct host_mactcp_stream host_mactcp_stream;

typedef struct {
    uint32_t local_address, remote_address, available, send_window, receive_window;
    uint32_t maximum_segment, round_trip_us, minimum_round_trip_us, in_flight, congestion_window;
    uint64_t bytes_sent, bytes_received;
    uint32_t retransmitted, duplicate_acks, timeout_episodes;
    uint32_t send_unacked, send_next, receive_next, segments_sent, segments_received;
    uint32_t retransmitted_segments, retry_count, last_round_trip_ms, variance_ms, retransmit_ms, quenches;
    uint16_t local_port, remote_port;
    unsigned state, urgent, readable;
} host_mactcp_stream_status;

typedef struct {
    uint16_t type, mtu, physical_length;
    uint32_t speed;
    char description[256];
    unsigned char physical[32];

    struct {
        uint32_t address;
        unsigned char physical[6];
    } arp[20];
} host_mactcp_interface_info;

host_mactcp_stream* host_mactcp_stream_create(int udp, uint32_t address, uint16_t first_port, uint16_t last_port,
                                              int* error);
void host_mactcp_stream_dispose(host_mactcp_stream* stream);
int host_mactcp_stream_open(host_mactcp_stream* stream, int passive, uint32_t local_address, uint16_t local_port,
                            uint32_t remote_address, uint16_t remote_port, unsigned ttl, unsigned tos,
                            int dont_fragment, const void* options, unsigned option_length);
int host_mactcp_stream_open_result(host_mactcp_stream* stream);
int host_mactcp_stream_send(host_mactcp_stream* stream, const void* data, uint32_t length, int push, int urgent,
                            uint32_t address, uint16_t port, uint16_t local_port, uint32_t* sent);
int host_mactcp_stream_receive(host_mactcp_stream* stream, void* data, uint32_t capacity, uint32_t* received,
                               uint32_t* address, uint16_t* port, uint32_t* destination, uint16_t* local_port,
                               unsigned* urgent);
int host_mactcp_stream_shutdown(host_mactcp_stream* stream, int abort);
int host_mactcp_stream_status_get(host_mactcp_stream* stream, host_mactcp_stream_status* status);
int host_mactcp_stream_udp_checksum(host_mactcp_stream* stream, int enabled);
int host_mactcp_tcp_parameters(uint32_t* algorithm, uint32_t* minimum_ms, uint32_t* maximum_ms);
int host_mactcp_udp_mtu(uint32_t destination, uint16_t* maximum);
int host_mactcp_interface_get(uint32_t destination, host_mactcp_interface_info* info);
#endif
