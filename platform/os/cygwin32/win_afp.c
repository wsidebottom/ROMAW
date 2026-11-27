/*+===================================================================
  File:      win_afp.c

  Summary:   Native Windows AFP 3.4 guest sessions over DSI.

  Classes:   None.

  Functions: romaw_afp_connect, romaw_afp_close, romaw_afp_command,
             romaw_afp_open_volume, romaw_afp_close_volume, romaw_afp_list_volumes.

  Origin:    ROMAW implementation of AFP and the Data Stream Interface.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "rsys/host_afp.h"
#include "rsys/host_mactcp.h"

struct romaw_afp_session {
    SOCKET socket;
    uint16_t next_id;
};

static void put16(unsigned char* p, uint16_t v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static uint16_t get16(const unsigned char* p)
{
    return (uint16_t)p[0] << 8 | p[1];
}

static uint32_t get32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int transfer(SOCKET socket, void* buffer, unsigned size, int writing, ULONGLONG deadline)
{
    char* bytes = buffer;
    while (size) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            return 0;
        WSAPOLLFD fd = {socket, writing ? POLLWRNORM : POLLRDNORM, 0};
        if (WSAPoll(&fd, 1, (int)(deadline - now)) <= 0)
            return 0;
        int count = writing ? send(socket, bytes, size, 0) : recv(socket, bytes, size, 0);
        if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        if (count <= 0)
            return 0;
        bytes += count;
        size -= count;
    }
    return 1;
}

static int send_frame(romaw_afp_session* session, unsigned flags, unsigned command, uint16_t id, const void* data,
                      unsigned size, ULONGLONG deadline)
{
    unsigned char head[16] = {flags, command};
    put16(head + 2, id);
    put16(head + 8, size >> 16);
    put16(head + 10, size);
    return transfer(session->socket, head, sizeof head, 1, deadline) &&
           transfer(session->socket, (void*)data, size, 1, deadline);
}

static int32_t exchange(romaw_afp_session* session, unsigned command, const void* request, unsigned request_size,
                        void* reply, unsigned capacity, unsigned* received)
{
    *received = 0;
    if (session->socket == INVALID_SOCKET)
        return ROMAW_AFP_TRANSPORT_ERROR;
    ULONGLONG deadline = GetTickCount64() + 5000;
    uint16_t id = ++session->next_id;
    if (!send_frame(session, 0, command, id, request, request_size, deadline))
        goto failed;
    for (unsigned i = 0; i < 64; ++i) {
        unsigned char head[16], attention[2];
        if (!transfer(session->socket, head, sizeof head, 0, deadline) || get32(head + 12))
            goto failed;
        uint32_t size = get32(head + 8);
        uint16_t peer_id = get16(head + 2);
        if (!head[0] && head[1] == 5 && !size) {
            if (!send_frame(session, 1, 5, peer_id, NULL, 0, deadline))
                goto failed;
            continue;
        }
        if (!head[0] && head[1] == 8 && size == sizeof attention) {
            if (!transfer(session->socket, attention, size, 0, deadline))
                goto failed;
            continue;
        }
        if (head[0] != 1 || head[1] != command || peer_id != id || size > capacity ||
            !transfer(session->socket, reply, size, 0, deadline))
            goto failed;
        *received = size;
        return (int32_t)get32(head + 4);
    }

failed:
    closesocket(session->socket);
    session->socket = INVALID_SOCKET;
    return ROMAW_AFP_TRANSPORT_ERROR;
}

int32_t romaw_afp_command(romaw_afp_session* session, const void* request, unsigned size, void* reply,
                          unsigned capacity, unsigned* received)
{
    if (!request || !size || size > 1048576 || capacity > 1048576 || (capacity && !reply))
        return ROMAW_AFP_TRANSPORT_ERROR;
    return exchange(session, 2, request, size, reply, capacity, received);
}

romaw_afp_session* romaw_afp_connect(uint32_t address, uint16_t port, int32_t* error)
{
    *error = ROMAW_AFP_TRANSPORT_ERROR;
    if (host_mactcp_resolver_open())
        return NULL;
    romaw_afp_session* session = calloc(1, sizeof *session);
    if (!session) {
        host_mactcp_resolver_close();
        return NULL;
    }
    session->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(address);
    peer.sin_port = htons(port);
    u_long nonblocking = 1;
    if (session->socket == INVALID_SOCKET || ioctlsocket(session->socket, FIONBIO, &nonblocking))
        goto failed;
    if (connect(session->socket, (const struct sockaddr*)&peer, sizeof peer)) {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
            goto failed;
        WSAPOLLFD fd = {session->socket, POLLWRNORM, 0};
        int result, size = sizeof result;
        if (WSAPoll(&fd, 1, 5000) <= 0 || getsockopt(session->socket, SOL_SOCKET, SO_ERROR, (char*)&result, &size) ||
            result)
            goto failed;
    }
    const unsigned char options[] = {1, 4, 0, 0, 0, 2};
    unsigned char reply[256];
    unsigned received;
    *error = exchange(session, 4, options, sizeof options, reply, sizeof reply, &received);
    if (*error)
        goto failed;
    unsigned quantum = 0;
    for (unsigned offset = 0; offset < received;) {
        if (received - offset < 2)
            goto malformed;
        unsigned kind = reply[offset++], length = reply[offset++];
        if (length > received - offset)
            goto malformed;
        if (!kind) {
            if (length != 4 || quantum)
                goto malformed;
            quantum = get32(reply + offset);
        }
        offset += length;
    }
    if (!quantum)
        goto malformed;
    const unsigned char login[] = "\x12\x06"
                                  "AFP3.4\x0e"
                                  "No User Authent";
    *error = romaw_afp_command(session, login, sizeof login - 1, reply, sizeof reply, &received);
    if (!*error)
        return session;
    goto failed;

malformed:
    *error = ROMAW_AFP_TRANSPORT_ERROR;
failed:
    romaw_afp_close(session);
    return NULL;
}

void romaw_afp_close(romaw_afp_session* session)
{
    if (!session)
        return;
    if (session->socket != INVALID_SOCKET) {
        uint16_t id = ++session->next_id;
        send_frame(session, 0, 1, id, NULL, 0, GetTickCount64() + 100);
        closesocket(session->socket);
    }
    free(session);
    host_mactcp_resolver_close();
}

int32_t romaw_afp_open_volume(romaw_afp_session* session, const char* name, uint16_t* volume)
{
    size_t length = strlen(name);
    if (!length || length > 255)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned char request[260] = {24, 0, 0, 32, length};
    unsigned char reply[4];
    unsigned received;
    memcpy(request + 5, name, length);
    int32_t result = romaw_afp_command(session, request, length + 5, reply, sizeof reply, &received);
    if (result)
        return result;
    if (received != 4 || get16(reply) != 32 || !get16(reply + 2))
        return ROMAW_AFP_TRANSPORT_ERROR;
    *volume = get16(reply + 2);
    return 0;
}

int32_t romaw_afp_close_volume(romaw_afp_session* session, uint16_t volume)
{
    unsigned char request[4] = {2};
    unsigned received;
    put16(request + 2, volume);
    return romaw_afp_command(session, request, sizeof request, NULL, 0, &received);
}

int32_t romaw_afp_list_volumes(romaw_afp_session* session, romaw_afp_volume_list* volumes)
{
    const unsigned char request[] = {16, 0};
    unsigned char reply[8192];
    unsigned received;
    volumes->count = 0;
    int32_t result = romaw_afp_command(session, request, sizeof request, reply, sizeof reply, &received);
    if (result)
        return result;
    if (received < 5)
        return ROMAW_AFP_TRANSPORT_ERROR;
    unsigned count = reply[4], offset = 5;
    for (unsigned i = 0; i < count; ++i) {
        if (received - offset < 2)
            return ROMAW_AFP_TRANSPORT_ERROR;
        unsigned flags = reply[offset++], length = reply[offset++];
        if (!length || length > received - offset || memchr(reply + offset, 0, length))
            return ROMAW_AFP_TRANSPORT_ERROR;
        volumes->items[i].flags = flags;
        memcpy(volumes->items[i].name, reply + offset, length);
        volumes->items[i].name[length] = 0;
        offset += length;
    }
    volumes->count = count;
    return 0;
}
