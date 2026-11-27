/*+===================================================================
  File:      mactcp.c

  Summary:   MacTCP .IPP address query and asynchronous ICMP echo with guest-owned completion delivery.

  Classes:   None.

  Functions: ROMlib_mactcp_dispatch, ROMlib_mactcp_poll, ROMlib_mactcp_reset, ROMlib_mactcp_resolver_definition,
             ROMlib_mactcp_memory, ROMlib_mactcp_complete.

  Origin:    ROMAW implementation; ABI follows the local MacTCP.h and MiscIPPB.h interface references.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "rsys/common.h"
#include "rsys/mactcp.h"
#include "rsys/host_mactcp.h"
#include "rsys/mman.h"
#include "rsys/file.h"
#include "MemoryMgr.h"
#include "ToolboxEvent.h"

extern int ROMlib_win_accessible_span(const void*, uint32, int);

typedef struct {
    host_mactcp_request* host;
    Ptr notification;
    uint32 callback, saved_world, started, user;
} echo_request;

static echo_request requests[16];
static boolean_t polling;
static unsigned resolver_users;

static uint32 read32(const void* p)
{
    uint32 value;
    memcpy(&value, p, 4);
    return CL(value);
}

static uint16 read16(const void* p)
{
    uint16 value;
    memcpy(&value, p, 2);
    return CW(value);
}

static void write32(void* p, uint32 value)
{
    value = CL(value);
    memcpy(p, &value, 4);
}

static void write16(void* p, uint16 value)
{
    value = CW(value);
    memcpy(p, &value, 2);
}

void* ROMlib_mactcp_memory(uint32 address, uint32 length, int write)
{
    address &= syn68k_address_mask;
    if (!address || (uint64)address + length > UINT64_C(0x100000000))
        return NULL;
    uint32 relative = address - syn68k_host_alias_base;
    if ((relative >> 17) < syn68k_host_alias_count) {
        if (length > 0x20000 - (relative & 0x1ffff))
            return NULL;
    }
    else if ((uint64)address + length > ROMlib_guest_memory_size)
        return NULL;
    void* pointer = SYN68K_TO_US(address);
    return ROMlib_win_accessible_span(pointer, length, write) ? pointer : NULL;
}

static void* guest_memory(uint32 address, uint32 length)
{
    return ROMlib_mactcp_memory(address, length, FALSE);
}

/* MacTCP uses a caller-cleaned C stack argument. Retain Device Manager's
   register preservation and the bridge's PPC routine-descriptor handling. */
static void call_completion(uint32 callback, ParmBlkPtr pb, OSErr error)
{
    uint32 saved_stack = EM_A7;
    EM_A7 -= 4;
    write32(SYN68K_TO_US(EM_A7), US_TO_SYN68K(pb));
    CALLCOMPLETION(pb, (ProcPtr)SYN68K_TO_US(callback), error);
    EM_A7 = saved_stack;
}

void ROMlib_mactcp_complete(ParmBlkPtr pb, BOOLEAN async, OSErr error)
{
    pb->ioParam.ioResult = CW(error);
    uint32 completion = read32((unsigned char*)pb + 12);
    if (async && completion)
        call_completion(completion, pb, error);
}

void ROMlib_mactcp_reset(void)
{
    ROMlib_mactcp_stream_reset();
    while (resolver_users) {
        host_mactcp_resolver_close();
        --resolver_users;
    }
    for (unsigned i = 0; i < NELEM(requests); ++i) {
        host_mactcp_dispose(requests[i].host);
        if (requests[i].notification)
            DisposPtr(requests[i].notification);
        memset(&requests[i], 0, sizeof requests[i]);
    }
}

void ROMlib_mactcp_poll(void)
{
    if (polling)
        return;
    polling = TRUE;
    ROMlib_mactcp_stream_poll();
    for (unsigned i = 0; i < NELEM(requests); ++i) {
        echo_request* request = &requests[i];
        if (!request->host)
            continue;
        const void* data = NULL;
        uint16 length = 0;
        uint32 milliseconds = 0;
        int error = host_mactcp_result(request->host, &data, &length, &milliseconds);
        if (error == 1)
            continue;
        Ptr notification = request->notification;
        uint32 callback = request->callback, saved_world = request->saved_world;
        write16(notification + 16, error);
        write32(notification + 50, request->started);
        write32(notification + 54, request->started + ((uint64)milliseconds * 60 + 999) / 1000);
        write16(notification + 58, error ? 0 : length + 8);
        write32(notification + 60, US_TO_SYN68K(notification + 72));
        write32(notification + 64, 0);
        write32(notification + 68, request->user);
        if (!error) {
            /* IP Helper returns payload only; MacTCP exposes an ICMP header too.
               Windows owns identifier/sequence, so normalize those fields to zero. */
            if (length)
                memcpy(notification + 80, data, length);
            unsigned checksum = 0;
            for (unsigned at = 0; at < length + 8U; at += 2) {
                checksum += (unsigned char)notification[72 + at] << 8;
                if (at + 1 < length + 8U)
                    checksum += (unsigned char)notification[73 + at];
            }
            while (checksum >> 16)
                checksum = (checksum & 0xffff) + (checksum >> 16);
            write16(notification + 74, ~checksum);
        }
        host_mactcp_dispose(request->host);
        memset(request, 0, sizeof *request);
        if (callback) {
            uint32 saved_a5 = EM_A5;
            EM_A5 = saved_world;
            call_completion(callback, (ParmBlkPtr)notification, error);
            EM_A5 = saved_a5;
        }
        DisposPtr(notification);
    }
    polling = FALSE;
}

OSErr ROMlib_mactcp_dispatch(ParmBlkPtr pb, BOOLEAN async, DriverRoutineType routine)
{
    OSErr error = noErr;
    unsigned char* bytes = (unsigned char*)pb;
    if (routine == Ctl && CW(pb->cntrlParam.csCode) != 15 && CW(pb->cntrlParam.csCode) != 17)
        return ROMlib_mactcp_stream_control(pb, async);
    if (routine == Close)
        ROMlib_mactcp_reset();
    else if (routine != Open) {
        error = controlErr;
        if (routine == Ctl && CW(pb->cntrlParam.csCode) == 15) {
            uint32 address, mask;
            error = host_mactcp_address(&address, &mask);
            if (!error) {
                write32(bytes + 28, address);
                write32(bytes + 32, mask);
            }
        }
        else if (routine == Ctl && CW(pb->cntrlParam.csCode) == 17) {
            uint16 length = read16(bytes + 32), option_length = read16(bytes + 44);
            uint32 data_address = read32(bytes + 34), options_address = read32(bytes + 40);
            const void* data = length ? guest_memory(data_address, length) : NULL;
            const void* options = option_length ? guest_memory(options_address, option_length) : NULL;
            error = length && !data ? -23013 : option_length > 40 || (option_length && !options) ? paramErr : noErr;
            if (!error) {
                echo_request* request = NULL;
                for (unsigned i = 0; i < NELEM(requests); ++i)
                    if (!requests[i].host) {
                        request = &requests[i];
                        break;
                    }
                error = -23009;
                if (request) {
                    Ptr notification = NewPtrSysClear(80 + length);
                    if (notification) {
                        memcpy(notification, bytes, 50);
                        memset(notification + 72, 0, 8);
                        int host_error;
                        host_mactcp_request* host =
                            host_mactcp_echo(read32(bytes + 28), data, length, MAX(1, read16(bytes + 38)) * 1000U,
                                             options, option_length, &host_error);
                        error = host_error;
                        if (host) {
                            request->host = host;
                            request->notification = notification;
                            request->callback = read32(bytes + 46);
                            request->user = read32(bytes + 50);
                            request->saved_world = EM_A5;
                            request->started = TickCount();
                        }
                        else
                            DisposPtr(notification);
                    }
                }
            }
        }
    }
    /* MacTCP completions use a C stack argument, unlike Device Manager's A0 ABI.
       The echo notification is a separate callback after request acceptance. */
    pb->ioParam.ioResult = CW(error);
    uint32 completion = read32(bytes + 12);
    if (async && completion)
        call_completion(completion, pb, error);
    return error;
}

/* DNR's public C entry takes a long selector followed by selector-specific
   arguments. Results may be returned synchronously; no resultProc is then due. */
static syn68k_addr_t resolver_callback(syn68k_addr_t pc, void* unused)
{
    (void)pc;
    (void)unused;
    unsigned char* stack = guest_memory(EM_A7, 12);
    OSErr error = paramErr;
    if (stack) {
        uint32 selector = read32(stack + 4);
        uint32 argument = read32(stack + 8);
        if (selector == 1) {
            error = host_mactcp_resolver_open();
            if (!error)
                ++resolver_users;
        }
        else if (selector == 2) {
            if (resolver_users) {
                host_mactcp_resolver_close();
                --resolver_users;
            }
            error = noErr;
        }
        else if (selector == 3 && resolver_users) {
            unsigned char* arguments = guest_memory(EM_A7, 24);
            unsigned char* info = arguments ? guest_memory(read32(arguments + 12), 276) : NULL;
            char name[256] = {0};
            unsigned length = 0;
            for (; length < 255; ++length) {
                const char* byte = guest_memory(argument + length, 1);
                if (!byte) {
                    length = 255;
                    break;
                }
                name[length] = *byte;
                if (!*byte)
                    break;
            }
            if (info && ROMlib_win_accessible_span(info, 276, TRUE) && length < 255 && !name[length]) {
                uint32 addresses[4];
                char canonical[255];
                error = length ? host_mactcp_resolve(name, addresses, canonical) : -23041;
                memset(info, 0, 276);
                write32(info, (int32)error);
                if (!error) {
                    memcpy(info + 4, canonical, 255);
                    for (unsigned i = 0; i < 4; ++i)
                        write32(info + 260 + i * 4, addresses[i]);
                }
            }
        }
        else if (selector == 4) {
            unsigned char* arguments = guest_memory(EM_A7, 16);
            char* output = arguments ? guest_memory(read32(arguments + 12), 16) : NULL;
            if (output && ROMlib_win_accessible_span(output, 16, TRUE)) {
                snprintf(output, 16, "%u.%u.%u.%u", argument >> 24, (argument >> 16) & 255, (argument >> 8) & 255,
                         argument & 255);
                error = noErr;
            }
        }
        else
            error = -23047;
    }
    EM_D0 = (int32)error;
    return POPADDR();
}

ProcPtr ROMlib_mactcp_resolver_definition(void)
{
    static syn68k_addr_t entry;
    if (!entry)
        entry = callback_install(resolver_callback, NULL);
    return (ProcPtr)SYN68K_TO_US(entry);
}
