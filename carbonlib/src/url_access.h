/*+===================================================================
  File:      url_access.h

  Summary:   Implement buffered URL Access transfers for local Macintosh files.

  Classes:   None.

  Functions: ROMlib_carbon_url_access_reset; private Carbon frame adapters.

  Origin:    Universal Interfaces 3.4.2 URLAccess.h, CarbonLib 1.4 URLAccessLib imports,
             and the existing CoreFoundation file URL and Macintosh File Manager services.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_URL_ACCESS_H
#define ROMAW_CARBON_URL_ACCESS_H

#include "cfx_internal.h"
#include "MemoryMgr.h"
#include <stdlib.h>

enum {
    url_initiating = 1,
    url_found = 4,
    url_downloading = 5,
    url_available = 21,
    url_transaction_complete = 6,
    url_error = 7,
    url_aborting = 8,
    url_completed = 9,
    url_invalid_ref = -30770,
    url_invalid_url = -30773,
    url_unsupported_scheme = -30774,
    url_property_pending = -30777,
    url_unknown_property = -30778,
    url_short_buffer = -30779,
    url_readonly_property = -30780,
    url_invalid_call = -30781,
    url_buffer_extent = 16384,
    url_string_limit = 8192,
};

typedef struct url_access_ref {
    struct url_access_ref* next;
    Ptr token, buffer;
    uint32_t notify, events, context, state, size, transferred, buffered;
    uint32_t type, creator;
    int32_t error;
    int16_t file;
    int opened, borrowed, disposed;
    char text[];
} url_access_ref;

static url_access_ref* url_references;
static int url_pumping;

static int url_result(ROMlib_carbon_ppc_frame* call, int32_t value)
{
    call->words[0] = value;
    return 1;
}

static int url_original(const char* name, ROMlib_carbon_ppc_frame* call, int32_t unavailable)
{
    ROMlib_carbon_reference saved;
    return ROMlib_carbon_load_reference("URLAccessLib", name, &saved) ? ROMlib_carbon_invoke_frame(saved.function, call)
                                                                      : url_result(call, unavailable);
}

static url_access_ref* url_reference(uint32_t address)
{
    for (url_access_ref* ref = url_references; ref; ref = ref->next)
        if (!ref->disposed && US_TO_SYN68K(ref->token) == address)
            return ref;
    return NULL;
}

static int url_writable(uint32_t address, size_t length)
{
    return address && ROMlib_carbon_try_guest_memory(address, length, 1);
}

static const char* url_guest_string(uint32_t address, unsigned maximum)
{
    if (!address)
        return NULL;
    for (unsigned length = 0; length < maximum; ++length) {
        if (address > UINT32_MAX - length)
            return NULL;
        const char* byte = ROMlib_carbon_try_guest_memory(address + length, 1, 0);
        if (!byte)
            return NULL;
        if (!*byte)
            return ROMlib_carbon_try_guest_memory(address, length + 1, 0);
    }
    return NULL;
}

static void url_close(url_access_ref* ref)
{
    if (ref->opened) {
        FSClose(ref->file);
        ref->opened = 0;
    }
}

static void url_reap(void)
{
    url_access_ref** link = &url_references;
    while (*link) {
        url_access_ref* ref = *link;
        if (!ref->disposed) {
            link = &ref->next;
            continue;
        }
        *link = ref->next;
        url_close(ref);
        if (ref->buffer)
            DisposPtr(ref->buffer);
        DisposPtr(ref->token);
        free(ref);
    }
}

void ROMlib_carbon_url_access_reset(void)
{
    for (url_access_ref* ref = url_references; ref; ref = ref->next)
        ref->disposed = 1;
    url_reap();
    url_pumping = 0;
}

static int carbon_URLNewReference(ROMlib_carbon_ppc_frame* call)
{
    const char* url = url_guest_string(call->words[0], url_string_limit);
    const uint32_t output = call->words[1];
    if (!url || !url_writable(output, 4))
        return url_result(call, paramErr);
    ROMlib_carbon_reference saved;
    if (ROMlib_carbon_load_reference("URLAccessLib", "URLNewReference", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    if (_strnicmp(url, "file:", 5))
        return url_result(call, strchr(url, ':') ? url_unsupported_scheme : url_invalid_url);
    const size_t length = strlen(url) + 1;
    url_access_ref* ref = calloc(1, sizeof *ref + length);
    if (!ref)
        return url_result(call, memFullErr);
    ref->token = NewPtr(4);
    if (!ref->token) {
        free(ref);
        return url_result(call, memFullErr);
    }
    memcpy(ref->text, url, length);
    ref->next = url_references;
    url_references = ref;
    return cfp_write(output, US_TO_SYN68K(ref->token)) && url_result(call, noErr);
}

static int carbon_URLDisposeReference(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLDisposeReference", call, url_invalid_ref);
    ref->disposed = 1;
    if (!url_pumping)
        url_reap();
    return url_result(call, noErr);
}

static OSErr url_open_file(ROMlib_carbon_ppc_frame* call, url_access_ref* ref)
{
    /* CFURL consumes guest bytes, published below the active stack's alias boundary. */
    ROMlib_carbon_reference saved;
    if (!ROMlib_carbon_load_reference("CoreFoundationLib", "CFURLCreateWithBytes", &saved))
        return fnfErr;
    const size_t length = strlen(ref->text);
    Ptr input = NewPtr(length);
    if (!input)
        return memFullErr;
    memcpy(input, ref->text, length);
    void* url = ROMlib_carbon_cf_url_create_bytes(0, (const Byte*)input, length, 0x08000100, NULL);
    DisposPtr(input);
    if (!url)
        return fnfErr;
    _Alignas(128) FSSpec spec = {0};
    ROMlib_carbon_ppc_frame convert = *call;
    convert.words[0] = 0;
    convert.words[1] = US_TO_SYN68K(url);
    convert.words[2] = US_TO_SYN68K(&spec);
    const int found = ROMlib_cfx_CFGetFSSpecFromURL(&convert) && (uint8_t)convert.words[0];
    ROMlib_carbon_cf_release(url);
    if (!found)
        return fnfErr;

    int16 encoded_file;
    OSErr error = C_FSpOpenDF(&spec, 1, &encoded_file);
    if (error)
        return error;
    ref->file = CW(encoded_file);
    ref->opened = 1;
    LONGINT eof;
    if ((error = GetEOF(ref->file, &eof)) || eof < 0) {
        url_close(ref);
        return error ? error : paramErr;
    }
    ref->size = eof;
    return noErr;
}

static int carbon_URLOpen(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLOpen", call, url_invalid_ref);
    if (call->words[1] || call->words[2] || ref->borrowed || (ref->state && ref->state != url_completed))
        return url_result(call, url_invalid_call);
    if (!ref->buffer && !(ref->buffer = NewPtr(url_buffer_extent)))
        return url_result(call, memFullErr);
    ref->notify = call->words[3];
    ref->events = call->words[4];
    ref->context = call->words[5];
    ref->transferred = ref->buffered = ref->size = 0;
    ref->error = url_open_file(call, ref);
    ref->state = url_initiating;
    return url_result(call, noErr);
}

static int url_notify(url_access_ref* ref)
{
    if (!ref->notify || !(ref->events & (UINT32_C(1) << (ref->state - 1))))
        return 1;
    _Alignas(32) uint32_t info[5] = {0, CL(US_TO_SYN68K(ref->token)), 0, CL(ref->transferred), 0};
    const uint32_t arguments[] = {ref->context, ref->state, US_TO_SYN68K(info)};
    uint32_t result;
    if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(ref->notify), arguments, 3, &result))
        return 0;
    if (result && ref->state != url_completed && ref->state != url_aborting) {
        ref->error = result;
        ref->state = url_aborting;
    }
    return 1;
}

static int url_advance(url_access_ref* ref)
{
    switch (ref->state) {
    case url_initiating:
        if (!url_notify(ref) || ref->disposed)
            return ref->disposed;
        if (ref->state == url_aborting)
            return 1;
        ref->state = ref->error ? url_error : url_found;
        break;
    case url_found:
        ref->state = url_downloading;
        break;
    case url_available:
        if (ref->borrowed || ref->buffered)
            return 1;
        /* Fall through to refill the released buffer. */
    case url_downloading: {
        if (ref->transferred == ref->size) {
            url_close(ref);
            ref->state = url_transaction_complete;
            break;
        }
        LONGINT amount = MIN(ref->size - ref->transferred, url_buffer_extent);
        ref->error = FSRead(ref->file, &amount, ref->buffer);
        if (ref->error || amount <= 0) {
            if (!ref->error)
                ref->error = eofErr;
            url_close(ref);
            ref->state = url_error;
            break;
        }
        ref->transferred += amount;
        ref->buffered = amount;
        ref->state = url_available;
        break;
    }
    case url_aborting:
        url_close(ref);
        if (!ref->borrowed)
            ref->buffered = 0;
        if (!url_notify(ref) || ref->disposed)
            return ref->disposed;
        /* Fall through to completion. */
    case url_error:
    case url_transaction_complete:
        ref->state = url_completed;
        break;
    default:
        return 1;
    }
    return url_notify(ref);
}

static int carbon_URLIdle(ROMlib_carbon_ppc_frame* call)
{
    if (url_pumping)
        return url_result(call, noErr);
    url_pumping = 1;
    int ok = 1;
    for (url_access_ref* ref = url_references; ok && ref; ref = ref->next)
        if (!ref->disposed)
            ok = url_advance(ref);
    url_pumping = 0;
    url_reap();
    return ok && url_original("URLIdle", call, noErr);
}

static int carbon_URLAbort(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLAbort", call, url_invalid_ref);
    if (!ref->state || ref->state == url_completed)
        return url_result(call, url_invalid_call);
    ref->state = url_aborting;
    ref->error = -128; /* userCanceledErr */
    return url_result(call, noErr);
}

static int carbon_URLGetCurrentState(ROMlib_carbon_ppc_frame* call)
{
    const url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLGetCurrentState", call, url_invalid_ref);
    if (!url_writable(call->words[1], 4))
        return url_result(call, paramErr);
    return cfp_write(call->words[1], ref->state) && url_result(call, noErr);
}

static int carbon_URLGetError(ROMlib_carbon_ppc_frame* call)
{
    const url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLGetError", call, url_invalid_ref);
    if (!url_writable(call->words[1], 4))
        return url_result(call, paramErr);
    return cfp_write(call->words[1], ref->error) && url_result(call, noErr);
}

static int carbon_URLGetDataAvailable(ROMlib_carbon_ppc_frame* call)
{
    const url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLGetDataAvailable", call, url_invalid_ref);
    if (!url_writable(call->words[1], 4))
        return url_result(call, paramErr);
    return cfp_write(call->words[1], ref->state == url_available && !ref->borrowed ? ref->buffered : 0) &&
           url_result(call, noErr);
}

static int carbon_URLGetBuffer(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLGetBuffer", call, url_invalid_ref);
    if (!url_writable(call->words[1], 4) || !url_writable(call->words[2], 4))
        return url_result(call, paramErr);
    uint32_t address = 0, size = 0;
    if (ref->state == url_available && !ref->borrowed && ref->buffered) {
        address = US_TO_SYN68K(ref->buffer);
        size = ref->buffered;
        ref->borrowed = 1;
    }
    return cfp_write(call->words[1], address) && cfp_write(call->words[2], size) && url_result(call, noErr);
}

static int carbon_URLReleaseBuffer(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLReleaseBuffer", call, url_invalid_ref);
    if (!ref->borrowed || call->words[1] != US_TO_SYN68K(ref->buffer))
        return url_result(call, url_invalid_call);
    ref->borrowed = 0;
    ref->buffered = 0;
    return url_result(call, noErr);
}

static int url_property(ROMlib_carbon_ppc_frame* call, int size_only)
{
    const url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original(size_only ? "URLGetPropertySize" : "URLGetProperty", call, url_invalid_ref);
    const char* property = url_guest_string(call->words[1], 64);
    if (!property)
        return url_result(call, paramErr);
    const void* bytes;
    uint32_t word, length;
    if (!strcmp(property, "URLString")) {
        bytes = ref->text;
        length = strlen(ref->text) + 1;
    }
    else {
        if (!strcmp(property, "URLResourceSize")) {
            if (ref->state < url_found || ref->error)
                return url_result(call, url_property_pending);
            word = CL(ref->size);
        }
        else if (!strcmp(property, "URLFileType"))
            word = CL(ref->type);
        else if (!strcmp(property, "URLFileCreator"))
            word = CL(ref->creator);
        else
            return url_result(call, url_unknown_property);
        bytes = &word;
        length = 4;
    }
    if (size_only) {
        if (!url_writable(call->words[2], 4))
            return url_result(call, paramErr);
        return cfp_write(call->words[2], length) && url_result(call, noErr);
    }
    if ((int32_t)call->words[3] < 0)
        return url_result(call, paramErr);
    if (call->words[3] < length)
        return url_result(call, url_short_buffer);
    void* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], length, 1) : NULL;
    if (!output)
        return url_result(call, paramErr);
    memcpy(output, bytes, length);
    return url_result(call, noErr);
}

static int carbon_URLGetProperty(ROMlib_carbon_ppc_frame* call)
{
    return url_property(call, 0);
}

static int carbon_URLGetPropertySize(ROMlib_carbon_ppc_frame* call)
{
    return url_property(call, 1);
}

static int carbon_URLSetProperty(ROMlib_carbon_ppc_frame* call)
{
    url_access_ref* ref = url_reference(call->words[0]);
    if (!ref)
        return url_original("URLSetProperty", call, url_invalid_ref);
    const char* property = url_guest_string(call->words[1], 64);
    if (!property)
        return url_result(call, paramErr);
    uint32_t* word;
    if (!strcmp(property, "URLFileType"))
        word = &ref->type;
    else if (!strcmp(property, "URLFileCreator"))
        word = &ref->creator;
    else if (!strcmp(property, "URLString") || !strcmp(property, "URLResourceSize"))
        return url_result(call, url_readonly_property);
    else
        return url_result(call, url_unknown_property);
    if (call->words[3] != 4 || !call->words[2] || !ROMlib_carbon_try_guest_memory(call->words[2], 4, 0))
        return url_result(call, paramErr);
    return cfp_get(call->words[2], word) && url_result(call, noErr);
}

#endif
