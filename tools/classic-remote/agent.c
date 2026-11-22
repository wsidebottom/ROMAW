/*+===================================================================
  File:      agent.c

  Summary:   Implements agent support for ROMAW build and development support.

  Classes:   None.

  Functions: memcmp, memcpy, memset, remote_main

  Origin:    https://github.com/marciot/mac-minivnc (no implementation code copied).

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT
   Original cooperative classic Mac control agent. No Apple headers required.
   Calls use classic 32-bit, big-endian, two-byte-packed Toolbox records.
   Low-memory cursor technique: see the MiniVNC author's technical discussion,
   https://github.com/marciot/mac-minivnc (no implementation code copied).
 */
#include "pairing.h"
#include "monocypher-ed25519.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;
typedef signed int s32;

void* memset(void* destination, int value, unsigned length)
{
    volatile u8* p = destination;
    for (unsigned i = 0; i < length; ++i)
        p[i] = value;
    return destination;
}

void* memcpy(void* destination, const void* source, unsigned length)
{
    volatile u8* d = destination;
    const volatile u8* s = source;
    for (unsigned i = 0; i < length; ++i)
        d[i] = s[i];
    return destination;
}

int memcmp(const void* left, const void* right, unsigned length)
{
    const u8 *a = left, *b = right;
    for (unsigned i = 0; i < length; ++i)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* Small addresses name real Macintosh low-memory globals, not hosted C
   objects. Keep their absolute address opaque to GCC's object-size analysis. */
static void* low(u32 value)
{
    __asm__("" : "+r"(value));
    return (void*)value;
}

enum {
    GET_CURRENT_PROCESS,
    GET_PROCESS_INFORMATION,
    HSETVOL,
    FSOPEN,
    FSCLOSE,
    FSREAD,
    FSWRITE,
    SETEOF,
    WAIT_NEXT_EVENT,
    GET_MAIN_DEVICE,
    POST_EVENT,
    TICK_COUNT,
    EXIT_TO_SHELL,
    MICROSECONDS,
#if REMOTE_TCP
    OPEN_DRIVER,
    CONTROL_SYNC,
    CONTROL_ASYNC,
#endif
    IMPORT_COUNT
};
u32 remote_imports[IMPORT_COUNT];
extern u32 remote_call(u32 vector, const u32* arguments);

static u32 call(unsigned index, u32 a, u32 b, u32 c, u32 d)
{
    u32 args[8] = {a, b, c, d, 0, 0, 0, 0};
    return remote_call(remote_imports[index], args);
}

static u16 get16(const void* p)
{
    const u8* b = p;
    return ((u16)b[0] << 8) | b[1];
}

static u32 get32(const void* p)
{
    const u8* b = p;
    return ((u32)get16(b) << 16) | get16(b + 2);
}

static void put16(void* p, u16 v)
{
    u8* b = p;
    b[0] = v >> 8;
    b[1] = v;
}

static void put32(void* p, u32 v)
{
    put16(p, v >> 16);
    put16((u8*)p + 2, v);
}

static u32 address(const void* p)
{
    return (u32)p;
}

static u32 pointer(u32 p)
{
    return get32((const void*)p);
}

#if !REMOTE_TCP
static const u8 command_name[] = "\016Remote Command";
static const u8 response_name[] = "\017Remote Response";
#endif
static const u8 public_key[32] = REMOTE_PUBLIC_KEY;
static u8 command[512], response[48], pixels[1024 * 768 * 3], session[16];
static u8 remote_keys[16];
static u32 last_sequence, held_until;
static unsigned button_down;
static unsigned input_enabled;

#if !REMOTE_TCP
static s32 open_file(const u8* name, u16* ref)
{
    return (s16)call(FSOPEN, address(name), 0, address(ref), 0);
}

static s32 transfer(unsigned operation, u16 ref, void* data, u32 length)
{
    u32 amount = length;
    s32 error = (s16)call(operation, ref, address(&amount), address(data), 0);
    return error ? error : amount == length ? 0 : -36;
}
#endif
static void release_input(void)
{
    if (button_down) {
        *(volatile u8*)low(0x172) = 0x80;
        put32(low(0x16e), call(TICK_COUNT, 0, 0, 0, 0) - 3);
    }
    button_down = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (remote_keys[i])
            *(volatile u8*)low(0x174 + i) &= ~remote_keys[i];
        remote_keys[i] = 0;
    }
}

static u32 service_input(void)
{
    u32 tick = call(TICK_COUNT, 0, 0, 0, 0);
    unsigned inputs_held = button_down;
    for (unsigned i = 0; i < 16; ++i)
        inputs_held |= remote_keys[i];
    if (inputs_held) {
        if ((s32)(tick - held_until) >= 0)
            release_input();
        else if (button_down)
            put32(low(0x16e), tick + 60);
    }
    return tick;
}

static u32 remote_idle(unsigned sleep)
{
    u8 ev[16];
    service_input();
    call(WAIT_NEXT_EVENT, 0, address(ev), sleep, 0);
    return service_input();
}
#if REMOTE_TCP
#include "tcp.h"
#endif
static void mouse(u32 x, u32 y)
{
    u32 point = (y << 16) | (x & 65535);
    put32(low(0x828), point);
    put32(low(0x82c), point);
    /* Mouse is the last drawn position. Leave it to the cursor task: changing
     it here makes the old and new positions equal before the redraw. */
    *(volatile u8*)low(0x8ce) = *(volatile u8*)low(0x8cf);
}

static s32 event(unsigned what, u32 message, u16 modifiers)
{
    s32 result = (s16)call(POST_EVENT, what, message, 0, 0);
    /* Key-up events are normally masked out. PostEvent still releases the key
     state; a disabled key-up notification is not an input failure. */
    if (result == 1 && what == 4)
        return 0;
    /* Patch the freshly appended event's modifiers and global location while
     this cooperative process owns the CPU. EvQEl is a packed 22-byte record. */
    u32 tail = pointer((u32)low(0x150));
    if (!result && tail && get16((void*)(tail + 6)) == what && get32((void*)(tail + 8)) == message) {
        put32((void*)(tail + 16), get32(low(0x82c)));
        put16((void*)(tail + 20), modifiers | (button_down ? 0 : 0x80));
    }
    return result;
}

static s32 capture(u32* width, u32* height)
{
    u32 handle = call(GET_MAIN_DEVICE, 0, 0, 0, 0);
    if (!handle)
        return -50;
    u32 device = pointer(handle);
    u32 pixmap = pointer(pointer(device + 22));
    if (!pixmap)
        return -50;
    u8* map = (u8*)pixmap;
    u32 stride = get16(map + 4) & 0x3fff;
    s32 top = (s16)get16(map + 6), left = (s16)get16(map + 8);
    s32 bottom = (s16)get16(map + 10), right = (s16)get16(map + 12);
    unsigned depth = get16(map + 32);
    if (right <= left || bottom <= top || right - left > 1024 || bottom - top > 768 ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32) ||
        stride < ((u32)(right - left) * depth + 7) / 8)
        return -50;
    *width = right - left;
    *height = bottom - top;
    u8 palette[256][3] = {{0}};
    if (depth <= 8) {
        u32 table_handle = get32(map + 42);
        if (!table_handle)
            return -50;
        const u8* table = (const u8*)pointer(table_handle);
        unsigned count = get16(table + 6) + 1;
        if (count > 256)
            return -50;
        for (unsigned i = 0; i < count; ++i) {
            const u8* color = table + 8 + i * 8;
            unsigned index = get16(color);
            /* ctFlags bit 15 means sequential device-table indices. */
            if (get16(table + 4) & 0x8000)
                index = i;
            if (index < 256)
                for (unsigned component = 0; component < 3; ++component)
                    palette[index][component] = color[2 + component * 2];
        }
    }
    const u8* base = (const u8*)get32(map);
    if (!base)
        return -50;
    for (unsigned y = 0; y < *height; ++y)
        for (unsigned x = 0; x < *width; ++x) {
            const u8* row = base + y * stride;
            u8* out = pixels + (y * *width + x) * 3;
            if (depth <= 8) {
                unsigned bit = x * depth;
                unsigned index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                for (unsigned c = 0; c < 3; ++c)
                    out[c] = palette[index][c];
            }
            else if (depth == 16) {
                u16 rgb = get16(row + x * 2);
                out[0] = ((rgb >> 10) & 31) * 255 / 31;
                out[1] = ((rgb >> 5) & 31) * 255 / 31;
                out[2] = (rgb & 31) * 255 / 31;
            }
            else
                for (unsigned c = 0; c < 3; ++c)
                    out[c] = row[x * 4 + c + 1];
        }
    return 0;
}

static s32 reply(u32 sequence, s32 error, u32 width, u32 height, u32 length)
{
    put32(response, 0x434d5231);
    put32(response + 4, sequence);
    put32(response + 8, error);
    put32(response + 12, width);
    put32(response + 16, height);
    put32(response + 20, length);
    put32(response + 24, call(TICK_COUNT, 0, 0, 0, 0));
    put32(response + 28, input_enabled);
    memcpy(response + 32, session, sizeof session);
#if REMOTE_TCP
    s32 result = tcp_send(response, sizeof response);
    if (!result && length)
        result = tcp_send(pixels, length);
    return result;
#else
    u16 ref;
    s32 result = open_file(response_name, &ref);
    if (result)
        return result;
    result = (s16)call(SETEOF, ref, 0, 0, 0);
    if (!result)
        result = transfer(FSWRITE, ref, response, sizeof response);
    for (u32 offset = 0; !result && offset < length;) {
        u32 chunk = length - offset;
        if (chunk > 8192)
            chunk = 8192;
        result = transfer(FSWRITE, ref, pixels + offset, chunk);
        offset += chunk;
    }
    call(FSCLOSE, ref, 0, 0, 0);
    return result;
#endif
}

void remote_main(void)
{
    u8 process[8], info[60] = {0}, spec[70];
#if REMOTE_DIAGNOSTIC || !REMOTE_TCP
    u8 ev[16];
#endif
    if (call(GET_CURRENT_PROCESS, address(process), 0, 0, 0))
        goto done;
    put32(info, sizeof info);
    put32(info + 56, address(spec));
    if (call(GET_PROCESS_INFORMATION, address(process), address(info), 0, 0) ||
        call(HSETVOL, 0, (s16)get16(spec), get32(spec + 2), 0))
        goto done;
    call(MICROSECONDS, address(session), 0, 0, 0);
    put32(session + 8, call(TICK_COUNT, 0, 0, 0, 0));
    put32(session + 12, address(process));
#if REMOTE_TCP
    if (tcp_initialize())
        goto done;
#else
    /* A newly deployed AFP mailbox may not yet be writable. Do not enter the
     command loop without publishing the session needed to authenticate it.
     Yield between bounded retries so permissions can be repaired remotely. */
    for (unsigned attempt = 0; reply(0, 0, 0, 0, 0); ++attempt) {
        if (attempt == 59)
            goto done;
        call(WAIT_NEXT_EVENT, 0, address(ev), 60, 0);
    }
#endif
#if REMOTE_DIAGNOSTIC == 2
    /* One-shot native validation: publish progress around the first event yield
     and screen read, then exit without processing any remote input. */
    reply(1, 0, 0, 0, 0);
    call(WAIT_NEXT_EVENT, 0, address(ev), 1, 0);
    reply(2, 0, 0, 0, 0);
    u32 width = 0, height = 0;
    s32 error = capture(&width, &height);
    reply(3, error, width, height, error ? 0 : width * height * 3);
#endif
#if REMOTE_DIAGNOSTIC
    goto done;
#endif
    for (;;) {
        u32 tick = remote_idle(REMOTE_TCP ? 1 : 3);
#if REMOTE_TCP
        if (!tcp_connected) {
            if (tcp_accept() || reply(last_sequence, 0, 0, 0, 0)) {
                tcp_disconnect(0);
                remote_idle(60);
                continue;
            }
        }
        s32 error = tcp_receive(command, sizeof command);
        tick = service_input();
#else
        u16 ref;
        if (open_file(command_name, &ref))
            continue;
        s32 error = transfer(FSREAD, ref, command, sizeof command);
        call(FSCLOSE, ref, 0, 0, 0);
#endif
        if (error || get32(command) != 0x434d5231) {
#if REMOTE_TCP
            tcp_disconnect(0);
#endif
            continue;
        }
        u32 sequence = get32(command + 4);
        if (memcmp(command + 8, session, sizeof session) || !sequence || sequence <= last_sequence ||
            crypto_ed25519_check(command + 256, public_key, command, 256)) {
#if REMOTE_TCP
            tcp_disconnect(0);
#endif
            continue;
        }
        last_sequence = sequence;
        u32 op = get32(command + 40), x = get32(command + 48);
        u32 y = get32(command + 52), width = 0, height = 0, length = 0;
        error = 0;
        if (op == 1) {
            error = capture(&width, &height);
            if (!error)
                length = width * height * 3;
        }
        else if (((op >= 2 && op <= 5) || op == 11) && !input_enabled)
            error = -54;
        else if ((op >= 2 && op <= 4) || op == 11) {
            if (x >= 1024 || y >= 768)
                error = -50;
            else {
                mouse(x, y);
                if (button_down)
                    held_until = tick + 120;
                if (op != 2) {
                    button_down = op == 3 || op == 11;
                    *(volatile u8*)low(0x172) = button_down ? 0 : 0x80;
                    held_until = tick + 120;
                    if (button_down)
                        put32(low(0x16e), tick + 60);
                    else
                        put32(low(0x16e), tick - 3);
                    u16 modifiers = get16(command + 58) & 0x1f00;
                    error = event(button_down ? 1 : 2, 0, modifiers);
                    if (op == 11) {
                        /* A simple click must not depend on the foreground
                         app yielding while tracking the button. Queue both
                         halves before yielding or writing the AFP reply. */
                        button_down = 0;
                        *(volatile u8*)low(0x172) = 0x80;
                        put32(low(0x16e), tick - 3);
                        s32 up_error = event(2, 0, modifiers);
                        if (!error)
                            error = up_error;
                    }
                }
            }
        }
        else if (op == 5) {
            if (x > 127 || y > 255)
                error = -50;
            else {
                unsigned down = command[56] != 0;
                held_until = tick + 120;
                u8* key = low(0x174 + x / 8);
                if (down) {
                    *key |= 1u << (x % 8);
                    remote_keys[x / 8] |= 1u << (x % 8);
                }
                else {
                    *key &= ~(1u << (x % 8));
                    remote_keys[x / 8] &= ~(1u << (x % 8));
                }
                error = event(down ? 3 : 4, (x << 8) | y, get16(command + 58) & 0x1f00);
            }
        }
        else if (op == 8 || op == 9) {
            input_enabled = op == 8;
            if (!input_enabled)
                release_input();
        }
        else if (op == 10) {
            /* Fixed, read-only input diagnostics; never expose arbitrary memory
             reads through the remote protocol. Points use Macintosh y,x order. */
            memcpy(pixels, low(0x828), 12);
            pixels[12] = *(volatile u8*)low(0x8ce);
            pixels[13] = *(volatile u8*)low(0x8cf);
            pixels[14] = *(volatile u8*)low(0x172);
            pixels[15] = input_enabled;
            memcpy(pixels + 16, low(0x174), 16);
            length = 32;
        }
        else if (op != 6 && op != 7)
            error = -50;
        s32 reply_error = reply(sequence, error, width, height, length);
#if REMOTE_TCP
        if (reply_error)
            tcp_disconnect(0);
#else
        (void)reply_error;
#endif
        if (op == 6)
            break;
    }
done:
#if REMOTE_TCP
    tcp_disconnect(1);
#endif
#if !REMOTE_DIAGNOSTIC
    release_input();
#endif
    call(EXIT_TO_SHELL, 0, 0, 0, 0);
}
