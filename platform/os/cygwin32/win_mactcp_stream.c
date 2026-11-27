/*+===================================================================
  File:      win_mactcp_stream.c

  Summary:   Implement nonblocking Windows TCP/UDP streams, port ranges and live IP Helper diagnostics.

  Classes:   None.

  Functions: host_mactcp_stream_create, host_mactcp_stream_dispose, host_mactcp_stream_open,
             host_mactcp_stream_open_result, host_mactcp_stream_send, host_mactcp_stream_receive,
             host_mactcp_stream_shutdown, host_mactcp_stream_status_get, host_mactcp_interface_get.

  Origin:    ROMAW backend using public Winsock and Windows IP Helper interfaces.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <mswsock.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/host_mactcp.h"
#include "rsys/host_mactcp_stream.h"

static LPFN_WSARECVMSG receive_message;

struct host_mactcp_stream {
    SOCKET* sockets;
    unsigned count;
    int udp, opening, passive, bound, aborted;
    uint32_t filter_address;
    uint16_t filter_port;
    uint64_t bytes_sent, bytes_received;
};

static int socket_error(int error)
{
    switch (error) {
    case WSAEWOULDBLOCK:
    case WSAEINPROGRESS:
    case WSAEALREADY:
        return 1;
    case WSAEADDRINUSE:
        return -23017;
    case WSAENOBUFS:
    case WSAEMFILE:
        return -23009;
    case WSAECONNREFUSED:
        return -23015;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAESHUTDOWN:
        return -23012;
    case WSAETIMEDOUT:
        return -23016;
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
        return -23037;
    case WSAEADDRNOTAVAIL:
        return -23004;
    case WSAEMSGSIZE:
        return -23006;
    case WSAENOTCONN:
        return -23008;
    default:
        return -50;
    }
}

static SOCKET new_socket(int udp)
{
    SOCKET socket_handle = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (socket_handle == INVALID_SOCKET)
        return socket_handle;
    u_long nonblocking = 1;
    BOOL exclusive = TRUE;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) ||
        setsockopt(socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof exclusive)) {
        int error = WSAGetLastError();
        closesocket(socket_handle);
        WSASetLastError(error);
        return INVALID_SOCKET;
    }
    if (udp) {
        setsockopt(socket_handle, SOL_SOCKET, SO_BROADCAST, (const char*)&exclusive, sizeof exclusive);
        DWORD returned;
        GUID identifier = WSAID_WSARECVMSG;
        if (setsockopt(socket_handle, IPPROTO_IP, IP_PKTINFO, (const char*)&exclusive, sizeof exclusive) ||
            WSAIoctl(socket_handle, SIO_GET_EXTENSION_FUNCTION_POINTER, &identifier, sizeof identifier,
                     &receive_message, sizeof receive_message, &returned, NULL, NULL)) {
            int error = WSAGetLastError();
            closesocket(socket_handle);
            WSASetLastError(error);
            return INVALID_SOCKET;
        }
    }
    return socket_handle;
}

static struct sockaddr_in endpoint(uint32_t address, uint16_t port)
{
    struct sockaddr_in result;
    memset(&result, 0, sizeof result);
    result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(address);
    result.sin_port = htons(port);
    return result;
}

host_mactcp_stream* host_mactcp_stream_create(int udp, uint32_t address, uint16_t first_port, uint16_t last_port,
                                              int* error)
{
    *error = host_mactcp_resolver_open();
    if (*error)
        return NULL;
    if (!last_port)
        last_port = first_port;
    if (last_port < first_port) {
        *error = -23006;
        host_mactcp_resolver_close();
        return NULL;
    }
    host_mactcp_stream* stream = calloc(1, sizeof *stream);
    if (!stream) {
        *error = -23009;
        host_mactcp_resolver_close();
        return NULL;
    }
    stream->udp = udp;
    stream->count = udp ? (unsigned)last_port - first_port + 1 : 1;
    stream->sockets = malloc(stream->count * sizeof *stream->sockets);
    if (!stream->sockets) {
        *error = -23009;
        host_mactcp_stream_dispose(stream);
        return NULL;
    }
    for (unsigned i = 0; i < stream->count; ++i)
        stream->sockets[i] = INVALID_SOCKET;
    for (unsigned i = 0; i < stream->count; ++i) {
        stream->sockets[i] = new_socket(udp);
        if (stream->sockets[i] == INVALID_SOCKET) {
            *error = socket_error(WSAGetLastError());
            host_mactcp_stream_dispose(stream);
            return NULL;
        }
        if (udp) {
            struct sockaddr_in local = endpoint(address, first_port ? first_port + i : 0);
            if (bind(stream->sockets[i], (const struct sockaddr*)&local, sizeof local)) {
                *error = socket_error(WSAGetLastError());
                host_mactcp_stream_dispose(stream);
                return NULL;
            }
        }
    }
    stream->bound = udp;
    *error = 0;
    return stream;
}

void host_mactcp_stream_dispose(host_mactcp_stream* stream)
{
    if (!stream)
        return;
    if (stream->sockets)
        for (unsigned i = 0; i < stream->count; ++i)
            if (stream->sockets[i] != INVALID_SOCKET)
                closesocket(stream->sockets[i]);
    free(stream->sockets);
    free(stream);
    host_mactcp_resolver_close();
}

int host_mactcp_stream_open(host_mactcp_stream* stream, int passive, uint32_t local_address, uint16_t local_port,
                            uint32_t remote_address, uint16_t remote_port, unsigned ttl, unsigned tos,
                            int dont_fragment, const void* options, unsigned option_length)
{
    if (stream->udp || stream->opening)
        return -23007;
    if (option_length > 40 || (option_length && !options))
        return -50;
    if (stream->aborted) {
        stream->sockets[0] = new_socket(0);
        if (stream->sockets[0] == INVALID_SOCKET)
            return socket_error(WSAGetLastError());
        stream->aborted = stream->bound = 0;
    }
    SOCKET socket_handle = stream->sockets[0];
    if (ttl && setsockopt(socket_handle, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof ttl))
        return socket_error(WSAGetLastError());
    if (tos && setsockopt(socket_handle, IPPROTO_IP, IP_TOS, (const char*)&tos, sizeof tos))
        return socket_error(WSAGetLastError());
    if (setsockopt(socket_handle, IPPROTO_IP, IP_DONTFRAGMENT, (const char*)&dont_fragment, sizeof dont_fragment) ||
        (option_length && setsockopt(socket_handle, IPPROTO_IP, IP_OPTIONS, options, option_length)))
        return socket_error(WSAGetLastError());
    if (!stream->bound) {
        struct sockaddr_in local = endpoint(local_address, local_port);
        if (bind(socket_handle, (const struct sockaddr*)&local, sizeof local))
            return socket_error(WSAGetLastError());
        stream->bound = 1;
    }
    stream->passive = passive;
    stream->filter_address = remote_address;
    stream->filter_port = remote_port;
    if (passive) {
        if (listen(socket_handle, SOMAXCONN))
            return socket_error(WSAGetLastError());
        stream->opening = 1;
        return 1;
    }
    if (!remote_address || !remote_port)
        return -23004;
    struct sockaddr_in remote = endpoint(remote_address, remote_port);
    if (!connect(socket_handle, (const struct sockaddr*)&remote, sizeof remote))
        return 0;
    int error = socket_error(WSAGetLastError());
    stream->opening = error == 1;
    return error;
}

int host_mactcp_stream_open_result(host_mactcp_stream* stream)
{
    if (!stream->opening)
        return 0;
    SOCKET socket_handle = stream->sockets[0];
    if (stream->passive) {
        struct sockaddr_in remote;
        int size = sizeof remote;
        SOCKET accepted = accept(socket_handle, (struct sockaddr*)&remote, &size);
        if (accepted == INVALID_SOCKET)
            return socket_error(WSAGetLastError());
        if ((stream->filter_address && stream->filter_address != ntohl(remote.sin_addr.s_addr)) ||
            (stream->filter_port && stream->filter_port != ntohs(remote.sin_port))) {
            closesocket(accepted);
            return 1;
        }
        u_long nonblocking = 1;
        if (ioctlsocket(accepted, FIONBIO, &nonblocking)) {
            int error = socket_error(WSAGetLastError());
            closesocket(accepted);
            return error;
        }
        closesocket(socket_handle);
        stream->sockets[0] = accepted;
        stream->opening = 0;
        return 0;
    }
    WSAPOLLFD poll_socket = {socket_handle, POLLWRNORM, 0};
    if (WSAPoll(&poll_socket, 1, 0) <= 0 || !poll_socket.revents)
        return 1;
    int error = 0, size = sizeof error;
    if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR, (char*)&error, &size))
        return socket_error(WSAGetLastError());
    stream->opening = 0;
    return error ? socket_error(error) : 0;
}

int host_mactcp_stream_send(host_mactcp_stream* stream, const void* data, uint32_t length, int push, int urgent,
                            uint32_t address, uint16_t port, uint16_t local_port, uint32_t* sent)
{
    *sent = 0;
    if (length > INT_MAX || (stream->udp && length > 65507))
        return -23006;
    SOCKET socket_handle = stream->sockets[0];
    if (stream->udp && local_port) {
        socket_handle = INVALID_SOCKET;
        for (unsigned i = 0; i < stream->count; ++i) {
            struct sockaddr_in local;
            int size = sizeof local;
            if (!getsockname(stream->sockets[i], (struct sockaddr*)&local, &size) &&
                ntohs(local.sin_port) == local_port) {
                socket_handle = stream->sockets[i];
                break;
            }
        }
        if (socket_handle == INVALID_SOCKET)
            return -23004;
    }
    int result;
    if (stream->udp) {
        struct sockaddr_in remote = endpoint(address, port);
        result = sendto(socket_handle, data, length, 0, (const struct sockaddr*)&remote, sizeof remote);
    }
    else {
        BOOL nodelay = push;
        setsockopt(socket_handle, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof nodelay);
        result = send(socket_handle, data, length, urgent ? MSG_OOB : 0);
    }
    if (result == SOCKET_ERROR)
        return socket_error(WSAGetLastError());
    *sent = result;
    stream->bytes_sent += result;
    return 0;
}

int host_mactcp_stream_receive(host_mactcp_stream* stream, void* data, uint32_t capacity, uint32_t* received,
                               uint32_t* address, uint16_t* port, uint32_t* destination, uint16_t* local_port,
                               unsigned* urgent)
{
    *received = *urgent = 0;
    if (capacity > INT_MAX)
        capacity = INT_MAX;
    for (unsigned i = 0; i < stream->count; ++i) {
        SOCKET socket_handle = stream->sockets[i];
        struct sockaddr_in remote, local;
        int size = sizeof remote, result;
        uint32_t packet_destination = 0;
        if (stream->udp) {
            unsigned char control[WSA_CMSG_SPACE(sizeof(IN_PKTINFO))];
            WSABUF buffer = {capacity, data};
            WSAMSG message;
            memset(&message, 0, sizeof message);
            message.name = (struct sockaddr*)&remote;
            message.namelen = size;
            message.lpBuffers = &buffer;
            message.dwBufferCount = 1;
            message.Control.buf = (char*)control;
            message.Control.len = sizeof control;
            DWORD bytes = 0;
            result = receive_message(socket_handle, &message, &bytes, NULL, NULL);
            if (result != SOCKET_ERROR) {
                result = bytes;
                for (WSACMSGHDR* entry = WSA_CMSG_FIRSTHDR(&message); entry; entry = WSA_CMSG_NXTHDR(&message, entry))
                    if (entry->cmsg_level == IPPROTO_IP && entry->cmsg_type == IP_PKTINFO) {
                        const IN_PKTINFO* info = (const IN_PKTINFO*)WSA_CMSG_DATA(entry);
                        packet_destination = ntohl(info->ipi_addr.s_addr);
                    }
            }
        }
        else {
            WSAPOLLFD fd = {socket_handle, POLLRDBAND, 0};
            if (WSAPoll(&fd, 1, 0) > 0 && (fd.revents & POLLRDBAND))
                *urgent = 1;
            result = recv(socket_handle, data, capacity, *urgent ? MSG_OOB : 0);
            if (result >= 0 && getpeername(socket_handle, (struct sockaddr*)&remote, &size))
                return socket_error(WSAGetLastError());
        }
        if (result == SOCKET_ERROR) {
            int error = socket_error(WSAGetLastError());
            if (error == 1)
                continue;
            return error;
        }
        if (!result && !stream->udp)
            return -23005;
        size = sizeof local;
        if (getsockname(socket_handle, (struct sockaddr*)&local, &size))
            return socket_error(WSAGetLastError());
        *received = result;
        *address = ntohl(remote.sin_addr.s_addr);
        *port = ntohs(remote.sin_port);
        *destination = stream->udp ? packet_destination : ntohl(local.sin_addr.s_addr);
        *local_port = ntohs(local.sin_port);
        stream->bytes_received += result;
        return 0;
    }
    return 1;
}

int host_mactcp_stream_shutdown(host_mactcp_stream* stream, int abort)
{
    if (stream->aborted)
        return 0;
    if (abort) {
        struct linger option = {1, 0};
        setsockopt(stream->sockets[0], SOL_SOCKET, SO_LINGER, (const char*)&option, sizeof option);
        closesocket(stream->sockets[0]);
        stream->sockets[0] = INVALID_SOCKET;
        stream->aborted = 1;
        stream->opening = 0;
        return 0;
    }
    return shutdown(stream->sockets[0], SD_SEND) ? socket_error(WSAGetLastError()) : 0;
}

/* Public TCP_INFO_v0 ABI from the Windows SDK, used when the MinGW SDK lacks
   its declaration. Windows owns these counters; no packet estimates are made. */
typedef struct {
    ULONG state, mss;
    ULONG64 connection_ms;
    BOOLEAN timestamps;
    ULONG rtt_us, minimum_rtt_us, in_flight, congestion_window, send_window, receive_window, receive_buffer;
    ULONG64 bytes_sent, bytes_received;
    ULONG reordered, retransmitted, fast_retransmits, duplicate_acks, timeouts;
    UCHAR syn_retransmits;
} tcp_info_v0;

int host_mactcp_stream_status_get(host_mactcp_stream* stream, host_mactcp_stream_status* status)
{
    memset(status, 0, sizeof *status);
    status->bytes_sent = stream->bytes_sent;
    status->bytes_received = stream->bytes_received;
    if (stream->aborted)
        return 0;
    SOCKET socket_handle = stream->sockets[0];
    struct sockaddr_in address;
    int size = sizeof address;
    if (!getsockname(socket_handle, (struct sockaddr*)&address, &size)) {
        status->local_address = ntohl(address.sin_addr.s_addr);
        status->local_port = ntohs(address.sin_port);
    }
    size = sizeof address;
    if (!getpeername(socket_handle, (struct sockaddr*)&address, &size)) {
        status->remote_address = ntohl(address.sin_addr.s_addr);
        status->remote_port = ntohs(address.sin_port);
        status->state = 8;
    }
    if (stream->opening)
        status->state = stream->passive ? 2 : 4;
    for (unsigned i = 0; i < stream->count; ++i) {
        if (stream->udp) {
            WSAPOLLFD fd = {stream->sockets[i], POLLRDNORM, 0};
            if (WSAPoll(&fd, 1, 0) > 0 && (fd.revents & POLLRDNORM))
                status->readable = 1;
        }
        u_long available = 0;
        if (!ioctlsocket(stream->sockets[i], FIONREAD, &available))
            status->available += available;
    }
    size = sizeof(int);
    int value = 0;
    if (!getsockopt(socket_handle, SOL_SOCKET, SO_SNDBUF, (char*)&value, &size))
        status->send_window = value;
    if (!getsockopt(socket_handle, SOL_SOCKET, SO_RCVBUF, (char*)&value, &size))
        status->receive_window = value;
    if (!stream->udp) {
        ULONG version = 0;
        DWORD returned = 0;
        tcp_info_v0 info;
        if (!WSAIoctl(socket_handle, 0xd8000027, &version, sizeof version, &info, sizeof info, &returned, NULL, NULL)) {
            status->state = info.state * 2;
            status->maximum_segment = info.mss;
            status->round_trip_us = info.rtt_us;
            status->minimum_round_trip_us = info.minimum_rtt_us;
            status->in_flight = info.in_flight;
            status->congestion_window = info.congestion_window;
            status->send_window = info.send_window;
            status->receive_window = info.receive_window;
            status->bytes_sent = info.bytes_sent;
            status->bytes_received = info.bytes_received;
            status->retransmitted = info.retransmitted;
            status->duplicate_acks = info.duplicate_acks;
            status->timeout_episodes = info.timeouts;
        }
        WSAPOLLFD fd = {socket_handle, POLLRDBAND | POLLRDNORM, 0};
        if (WSAPoll(&fd, 1, 0) > 0) {
            status->urgent = !!(fd.revents & POLLRDBAND);
            if (!status->available && (fd.revents & (POLLRDNORM | POLLHUP)) && !stream->opening) {
                char byte;
                int count = recv(socket_handle, &byte, 1, MSG_PEEK);
                if (!count)
                    status->state = 14;
                else if (count == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
                    return socket_error(WSAGetLastError());
            }
        }
    }
    return 0;
}

int host_mactcp_interface_get(uint32_t destination, host_mactcp_interface_info* info)
{
    DWORD index = 0;
    if (GetBestInterface(htonl(destination ? destination : 0x08080808), &index))
        return -23037;
    MIB_IFROW row;
    memset(&row, 0, sizeof row);
    row.dwIndex = index;
    if (GetIfEntry(&row))
        return -23004;
    memset(info, 0, sizeof *info);
    info->type = row.dwType;
    info->mtu = row.dwMtu > 65535 ? 65535 : row.dwMtu;
    info->speed = row.dwSpeed;
    info->physical_length = row.dwPhysAddrLen > sizeof info->physical ? sizeof info->physical : row.dwPhysAddrLen;
    memcpy(info->physical, row.bPhysAddr, info->physical_length);
    size_t count = row.dwDescrLen < sizeof info->description - 1 ? row.dwDescrLen : sizeof info->description - 1;
    memcpy(info->description, row.bDescr, count);
    ULONG size = 0;
    if (GetIpNetTable(NULL, &size, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        MIB_IPNETTABLE* table = malloc(size);
        if (table) {
            if (!GetIpNetTable(table, &size, FALSE)) {
                unsigned used = 0;
                for (DWORD i = 0; i < table->dwNumEntries && used < 20; ++i) {
                    const MIB_IPNETROW* item = &table->table[i];
                    if (item->dwIndex != index || item->dwPhysAddrLen != 6 || item->dwType == 2)
                        continue;
                    info->arp[used].address = ntohl(item->dwAddr);
                    memcpy(info->arp[used++].physical, item->bPhysAddr, 6);
                }
            }
            free(table);
        }
    }
    return 0;
}
