/*+===================================================================
  File:      speech.c

  Summary:   Classic Speech Manager marshaling. SAPI objects stay outside guest RAM.

  Classes:   None.

  Functions: ROMlib_speech_dispatch, ROMlib_speech_poll, ROMlib_speech_reset, ROMlib_speech_service,
             ROMlib_speech_signature

  Origin:    Existing repository source in executor/src/sound; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Speech Manager marshaling. SAPI objects stay outside guest RAM. */
#include "rsys/common.h"
#include "rsys/speech.h"
#include "rsys/multimedia.h"
#if defined(CYGWIN32) && defined(SYN68K)
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "rsys/host_speech.h"
#include "rsys/mman.h"
#include <math.h>

extern int ROMlib_win_accessible_span(const void*, uint32, int);

typedef struct speech_channel {
    struct speech_channel* next;
    Ptr token;
    host_speech_channel* host;
    uint32 rate, volume, voice, refcon, done_callback, saved_a5;
    uint64 serial, generation;
    uint32 text_callback;
    int active, text_pending, accept_next;
    int16 first_error, last_error;
    uint16 error_count;
} speech_channel;

static speech_channel* channels;
static host_speech_channel* string_channel;
static uint64 next_serial;
static int polling;

static const struct {
    const char *name, *signature;
    uint32 selector;
} services[] = {
    {"SpeechManagerVersion", "4", 0x0000000c},
    {"CountVoices", "2p", 0x0108000c},
    {"GetIndVoice", "22p", 0x030c000c},
    {"MakeVoiceSpec", "244p", 0x0604000c},
    {"GetVoiceDescription", "2pp4", 0x0610000c},
    {"NewSpeechChannel", "2pp", 0x0418000c},
    {"DisposeSpeechChannel", "2p", 0x021c000c},
    {"SpeakString", "2p", 0x0220000c},
    {"SpeakText", "2pp4", 0x0624000c},
    {"SpeakBuffer", "2pp44", 0x0828000c},
    {"StopSpeech", "2p", 0x022c000c},
    {"PauseSpeechAt", "2p4", 0x0434000c},
    {"ContinueSpeech", "2p", 0x0238000c},
    {"SpeechBusy", "2", 0x003c000c},
    {"SpeechBusySystemWide", "2", 0x0040000c},
    {"SetSpeechRate", "2p4", 0x0444000c},
    {"GetSpeechRate", "2pp", 0x0448000c},
    {"SetSpeechInfo", "2p4p", 0x0654000c},
    {"GetSpeechInfo", "2p4p", 0x0658000c},
};

static void* speech_memory(uint32 address, uint32 length, int write)
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

static uint32 speech_read(const void* pointer)
{
    uint32 value;
    memcpy(&value, pointer, sizeof value);
    return CL(value);
}

static void speech_write(void* pointer, uint32 value)
{
    value = CL(value);
    memcpy(pointer, &value, sizeof value);
}

static speech_channel* speech_find(uint32 token)
{
    token &= syn68k_address_mask;
    for (speech_channel* channel = channels; channel; channel = channel->next)
        if (US_TO_SYN68K(channel->token) == token)
            return channel;
    return NULL;
}

const char* ROMlib_speech_signature(const char* name, uint32_t* selector)
{
    for (unsigned i = 0; i < NELEM(services); ++i)
        if (!strcmp(name, services[i].name)) {
            if (selector)
                *selector = services[i].selector;
            return services[i].signature;
        }
    return NULL;
}

int ROMlib_speech_service(uint32_t selector, const uint32_t* a, uint32_t* result)
{
    if (!a || !result)
        return 0;
    int known = 0;
    for (unsigned i = 0; i < NELEM(services); ++i)
        known |= selector == services[i].selector;
    if (!known)
        return 0;
    OSErr error = noErr;
    speech_channel* channel = speech_find(a[0]);
    if (selector == 0x0000000c) {
        *result = ROMLIB_SPEECH_VERSION;
        return 1;
    }
    if (selector == 0x0108000c) {
        INTEGER* count = speech_memory(a[0], 2, TRUE);
        if (!count)
            error = paramErr;
        else
            *count = CW(host_speech_voice_count());
    }
    else if (selector == 0x030c000c || selector == 0x0604000c) {
        uint32 index = selector == 0x030c000c ? (uint16)a[0] : a[1];
        void* voice = speech_memory(selector == 0x030c000c ? a[1] : a[2], 8, TRUE);
        if (!voice)
            error = paramErr;
        else if (selector == 0x030c000c && (!index || index > (unsigned)host_speech_voice_count()))
            error = -244; /* voiceNotFound */
        else {
            speech_write(voice, selector == 0x030c000c ? TICK("SAPI") : a[0]);
            speech_write((char*)voice + 4, index);
        }
    }
    else if (selector == 0x0610000c) {
        unsigned index = 1;
        const void* voice = a[0] ? speech_memory(a[0], 8, FALSE) : NULL;
        if (a[0] && !voice)
            error = paramErr;
        else if ((voice && (speech_read(voice) != TICK("SAPI") || !(index = speech_read((const char*)voice + 4)))) ||
                 index > (unsigned)host_speech_voice_count())
            error = -244;
        else if (a[1]) {
            uint32 length = a[2] < 362 ? a[2] : 362;
            unsigned char* output = speech_memory(a[1], length, TRUE);
            char name[256];
            if ((int32)a[2] < 4 || !output)
                error = paramErr;
            else if (!host_speech_voice_name(index, name, sizeof name))
                error = -244;
            else {
                unsigned char description[362] = {0};
                speech_write(description, length);
                speech_write(description + 4, TICK("SAPI"));
                speech_write(description + 8, index);
                size_t size = strlen(name);
                if (size > 63)
                    size = 63;
                description[16] = size;
                memcpy(description + 17, name, size);
                /* Host voices use MacRoman input. Age and gender are unspecified;
                 no Apple voice identity or version is fabricated. */
                memcpy(output, description, length);
            }
        }
    }
    else if (selector == 0x0418000c) {
        void* output = speech_memory(a[1], 4, TRUE);
        unsigned index = 1;
        void* voice = a[0] ? speech_memory(a[0], 8, FALSE) : NULL;
        if (!output || (a[0] && !voice))
            error = paramErr;
        else if (voice && (speech_read(voice) != TICK("SAPI") || !(index = speech_read((char*)voice + 4)) ||
                           index > (unsigned)host_speech_voice_count()))
            error = -244;
        else {
            speech_write(output, 0);
            unsigned count = 0;
            for (speech_channel* item = channels; item; item = item->next)
                ++count;
            speech_channel* created = count < 64 ? calloc(1, sizeof *created) : NULL;
            if (!created)
                error = memFullErr;
            else {
                created->host = host_speech_open(index);
                created->token = created->host ? NewPtrClear(16) : NULL;
                if (!created->host || !created->token) {
                    error = created->host ? memFullErr : host_speech_voice_count() ? -241 : -240;
                    host_speech_close(created->host);
                    free(created);
                }
                else {
                    created->serial = ++next_serial;
                    created->saved_a5 = EM_A5;
                    created->voice = index;
                    created->rate = 180U << 16;
                    created->volume = 1U << 16;
                    created->next = channels;
                    channels = created;
                    speech_write(output, US_TO_SYN68K(created->token));
                }
            }
        }
    }
    else if (selector == 0x0220000c) {
        unsigned char* text = speech_memory(a[0], 1, FALSE);
        if (!text || !speech_memory(a[0], text[0] + 1, FALSE))
            error = paramErr;
        else {
            if (!string_channel)
                string_channel = host_speech_open(1);
            if (!string_channel || !host_speech_speak(string_channel, (char*)text + 1, text[0]))
                error = -240; /* noSynthFound */
        }
    }
    else if (selector == 0x003c000c || selector == 0x0040000c) {
        ROMlib_speech_poll();
        unsigned busy = 0;
        host_speech_status state;
        if (string_channel && host_speech_get_status(string_channel, &state))
            busy += state.busy;
        for (speech_channel* item = channels; item; item = item->next)
            if (host_speech_get_status(item->host, &state))
                busy += state.busy;
        *result = busy;
        return 1;
    }
    else if (!channel)
        error = -3000; /* invalidComponentID */
    else if (selector == 0x021c000c) {
        speech_channel** link = &channels;
        while (*link != channel)
            link = &(*link)->next;
        *link = channel->next;
        host_speech_close(channel->host);
        DisposPtr(channel->token);
        free(channel);
    }
    else if (selector == 0x0624000c || selector == 0x0828000c) {
        const char* text = a[2] ? speech_memory(a[1], a[2], FALSE) : "";
        uint32 flags = selector == 0x0828000c ? a[3] : 0;
        int append = (flags & 2) && a[2];
        if (!text || (int32)a[2] < 0)
            error = paramErr;
        else if (flags & ~2U)
            error = -248; /* Prosody/preflight controls are not implemented. */
        else if (append && channel->text_pending)
            error = -242; /* The prior input still awaits its text-done callback. */
        else if (!(append ? host_speech_queue(channel->host, text, a[2])
                          : host_speech_speak(channel->host, text, a[2])))
            error = -242; /* synthNotReady */
        else {
            channel->active = 1;
            channel->text_pending = channel->accept_next = a[2] != 0;
            ++channel->generation;
        }
    }
    else if (selector == 0x022c000c) {
        error = host_speech_stop(channel->host) ? noErr : -242;
        if (!error) {
            channel->accept_next = 0;
            ++channel->generation;
        }
    }
    else if (selector == 0x0434000c) {
        if (a[1] != 0)
            error = paramErr; /* Boundary-sensitive pause is not implemented yet. */
        else
            error = host_speech_pause(channel->host) ? noErr : -242;
    }
    else if (selector == 0x0238000c)
        error = host_speech_resume(channel->host) ? noErr : -242;
    else if (selector == 0x0444000c) {
        if ((int32)a[1] <= 0)
            error = paramErr;
        else {
            double relative = (double)a[1] / (180.0 * 65536.0);
            int speed = (int)floor(10 * log(relative) / log(3.0) + 0.5);
            if (speed < -10)
                speed = -10;
            if (speed > 10)
                speed = 10;
            if (host_speech_set_rate(channel->host, speed))
                channel->rate = a[1];
            else
                error = -242;
        }
    }
    else if (selector == 0x0448000c) {
        void* rate = speech_memory(a[1], 4, TRUE);
        if (!rate)
            error = paramErr;
        else
            speech_write(rate, channel->rate);
    }
    else if (selector == 0x0654000c || selector == 0x0658000c) {
        int set = selector == 0x0654000c;
        uint32 info = a[1];
        uint32 length = info == TICK("erro") ? 14 : info == TICK("stat") || info == TICK("cvox") ? 8 : 4;
        void* data = speech_memory(a[2], length, !set);
        if (!data)
            error = paramErr;
        else if (info == TICK("rate")) {
            if (set) {
                uint32 args[] = {a[0], speech_read(data), 0, 0};
                return ROMlib_speech_service(0x0444000c, args, result);
            }
            speech_write(data, channel->rate);
        }
        else if (info == TICK("volm")) {
            if (set) {
                uint32 volume = speech_read(data);
                if (volume > 65536)
                    error = paramErr;
                else if (!host_speech_set_volume(channel->host, (volume * 100U + 32768) >> 16))
                    error = -242;
                else
                    channel->volume = volume;
            }
            else
                speech_write(data, channel->volume);
        }
        else if (info == TICK("sdcb") || info == TICK("tdcb") || info == TICK("myA5")) {
            uint32* field = info == TICK("sdcb")   ? &channel->done_callback
                            : info == TICK("tdcb") ? &channel->text_callback
                                                   : &channel->saved_a5;
            if (set) {
                uint32 value = speech_read(data);
                if (info != TICK("myA5") && value && !speech_memory(value, 2, FALSE))
                    error = paramErr;
                else
                    *field = value;
            }
            else
                speech_write(data, *field);
        }
        else if (info == TICK("refc")) {
            if (set)
                channel->refcon = speech_read(data);
            else
                speech_write(data, channel->refcon);
        }
        else if (info == TICK("cvox") && !set) {
            speech_write(data, TICK("SAPI"));
            speech_write((char*)data + 4, channel->voice);
        }
        else if (info == TICK("erro") && !set) {
            INTEGER count = CW(channel->error_count);
            INTEGER first_error = CW(channel->first_error);
            INTEGER last_error = CW(channel->last_error);
            memset(data, 0, 14);
            memcpy(data, &count, 2);
            memcpy((char*)data + 2, &first_error, 2);
            memcpy((char*)data + 8, &last_error, 2);
            channel->error_count = 0;
            channel->first_error = channel->last_error = 0;
        }
        else if (info == TICK("stat") && !set) {
            host_speech_status status;
            if (!host_speech_get_status(channel->host, &status))
                error = -242;
            else {
                unsigned char* bytes = data;
                memset(bytes, 0, 8);
                bytes[0] = !!status.busy || channel->text_pending;
                bytes[1] = !!status.paused;
                /* The host owns a copy, so no guest input bytes remain in use. */
            }
        }
        else
            error = -231; /* siUnknownInfoType */
    }
    else
        return 0;
    *result = (uint32)(int32)error;
    return 1;
}

int ROMlib_speech_dispatch(uint32_t selector, uint32_t* return_address)
{
    const char* signature = NULL;
    for (unsigned i = 0; i < NELEM(services); ++i)
        if (services[i].selector == selector)
            signature = services[i].signature;
    if (!signature)
        return 0;
    unsigned count = strlen(signature) - 1;
    uint32 arguments[4] = {0}, result;
    *return_address = POPADDR();
    for (unsigned i = count; i; --i)
        arguments[i - 1] = signature[i] == '2' ? (uint32)(int32)POPSW() : POPUL();
    ROMlib_speech_service(selector, arguments, &result);
    if (signature[0] == '4')
        WRITEUL(EM_A7, result);
    else
        WRITEUW(EM_A7, result);
    return 1;
}

/* Deliver on the CPU thread, never on SAPI's worker thread. A snapshot plus
   allocation serials permits callbacks to dispose/create channels or reset the
   subsystem without traversing freed records or delivering to reused tokens. */
void ROMlib_speech_poll(void)
{
    if (polling)
        return;
    polling = 1;

    struct {
        uint32 token;
        uint64 serial;
    } snapshot[64];

    unsigned count = 0;
    for (speech_channel* item = channels; item && count < NELEM(snapshot); item = item->next) {
        snapshot[count].token = US_TO_SYN68K(item->token);
        snapshot[count++].serial = item->serial;
    }
    uint32 saved_a5 = EM_A5;
    for (unsigned i = 0; i < count; ++i) {
        speech_channel* channel = speech_find(snapshot[i].token);
        host_speech_status status;
        if (!channel || channel->serial != snapshot[i].serial || !channel->active)
            continue;
        if (channel->text_pending) {
            channel->text_pending = 0;
            uint64 generation = channel->generation;
            uint32 callback = channel->text_callback;
            void* procedure = callback ? speech_memory(callback, 2, FALSE) : NULL;
            if (procedure) {
                char* scratch = (char*)channel->token + 4;
                memset(scratch, 0, 12);
                EM_A5 = channel->saved_a5;
                CToPascalCall(procedure, (5 << 3) | (4 << 6) | (5 << 9) | (5 << 12) | (5 << 15), channel->token,
                              channel->refcon, scratch, scratch + 4, scratch + 8);
                EM_A5 = saved_a5;
                channel = speech_find(snapshot[i].token);
                if (!channel || channel->serial != snapshot[i].serial)
                    continue;
                if (channel->generation != generation)
                    continue;
                uint32 address = speech_read(scratch);
                uint32 length = speech_read(scratch + 4);
                uint32 flags = speech_read(scratch + 8);
                if (length && channel->accept_next) {
                    const char* text = speech_memory(address, length, FALSE);
                    int error = !text ? paramErr : flags & ~2U ? -248 : noErr;
                    if (!error && !host_speech_queue(channel->host, text, length))
                        error = -242;
                    if (error) {
                        if (!channel->error_count)
                            channel->first_error = error;
                        if (channel->error_count < 32767)
                            ++channel->error_count;
                        channel->last_error = error;
                    }
                    else {
                        channel->text_pending = 1;
                    }
                }
            }
        }
        if (!host_speech_get_status(channel->host, &status) || status.busy || channel->text_pending)
            continue;
        channel->active = 0;
        uint32 callback = channel->done_callback;
        uint32 refcon = channel->refcon;
        Ptr token = channel->token;
        EM_A5 = channel->saved_a5;
        void* procedure = callback ? speech_memory(callback, 2, FALSE) : NULL;
        if (procedure)
            CToPascalCall(procedure, (5 << 3) | (4 << 6), token, refcon);
        EM_A5 = saved_a5;
    }
    polling = 0;
}

void ROMlib_speech_reset(void)
{
    while (channels) {
        speech_channel* channel = channels;
        channels = channel->next;
        host_speech_close(channel->host);
        DisposPtr(channel->token);
        free(channel);
    }
    host_speech_close(string_channel);
    string_channel = NULL;
    host_speech_shutdown();
}
#else
const char* ROMlib_speech_signature(const char* name, uint32_t* selector)
{
    return NULL;
}

int ROMlib_speech_service(uint32_t selector, const uint32_t* arguments, uint32_t* result)
{
    return 0;
}

int ROMlib_speech_dispatch(uint32_t selector, uint32_t* return_address)
{
    return 0;
}

void ROMlib_speech_poll(void)
{
}

void ROMlib_speech_reset(void)
{
}
#endif
