/*+===================================================================
  File:      speech_test.c

  Summary:   Classic trap stack and guest-memory checks with intercepted host synthesis.

  Classes:   None.

  Functions: CToPascalCall, DisposPtr, ROMlib_win_accessible_span, _NewPtr_flags, host_speech_close,
             host_speech_get_status, host_speech_open, host_speech_pause, host_speech_queue, host_speech_resume,
             host_speech_set_rate, host_speech_set_volume, host_speech_shutdown, host_speech_speak, host_speech_stop,
             host_speech_voice_count, host_speech_voice_name, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic trap stack and guest-memory checks with intercepted host synthesis. */
#define NDEBUG 1
#include "../../sound/speech.c"
#undef main
#undef NDEBUG
#include <assert.h>

CPUState cpu_state;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
uint32 ROMlib_guest_memory_size = 0x4000;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
static unsigned next_token = 0x2000;
static int live, disposed, fail_allocation, native_live;
static char spoken[256];
static unsigned callback_count, text_callback_count, queue_count;
static int text_mode;
static int callback_disposes, callback_resets;

struct host_speech_channel {
    int busy, paused, rate;
};

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

int ROMlib_win_accessible_span(const void* pointer, uint32 size, int write)
{
    uintptr_t address = (uintptr_t)pointer;
    return address >= ROMlib_offset && address <= ROMlib_offset + 0x4000 && size <= ROMlib_offset + 0x4000 - address;
}

Ptr _NewPtr_flags(Size size, boolean_t system, boolean_t clear)
{
    assert(size == 16 && clear);
    if (fail_allocation)
        return NULL;
    Ptr result = (Ptr)(ROMlib_offset + next_token);
    next_token += 16;
    ++live;
    memset(result, 0, size);
    return result;
}

void DisposPtr(Ptr pointer)
{
    assert(pointer && live);
    --live;
    ++disposed;
}

int host_speech_voice_count(void)
{
    return 2;
}

host_speech_channel* host_speech_open(unsigned index)
{
    assert(index <= 2);
    host_speech_channel* host = calloc(1, sizeof *host);
    assert(host);
    ++native_live;
    return host;
}

void host_speech_close(host_speech_channel* host)
{
    if (host) {
        --native_live;
        free(host);
    }
}

int host_speech_speak(host_speech_channel* host, const char* text, size_t length)
{
    assert(length < sizeof spoken);
    memcpy(spoken, text, length);
    spoken[length] = 0;
    host->busy = length != 0;
    return 1;
}

int host_speech_queue(host_speech_channel* host, const char* text, size_t length)
{
    ++queue_count;
    return host_speech_speak(host, text, length);
}

int host_speech_stop(host_speech_channel* host)
{
    host->busy = 0;
    return 1;
}

int host_speech_pause(host_speech_channel* host)
{
    host->paused = 1;
    return 1;
}

int host_speech_resume(host_speech_channel* host)
{
    host->paused = 0;
    return 1;
}

int host_speech_get_status(host_speech_channel* host, host_speech_status* status)
{
    *status = (host_speech_status){host->busy, host->paused, 0, 0};
    return 1;
}

int host_speech_set_rate(host_speech_channel* host, int rate)
{
    assert(rate >= -10 && rate <= 10);
    host->rate = rate;
    return 1;
}

int host_speech_voice_name(unsigned index, char* name, size_t size)
{
    assert(index >= 1 && index <= 2 && size >= 16);
    strcpy(name, "Test host voice");
    return 1;
}

int host_speech_set_volume(host_speech_channel* host, unsigned volume)
{
    assert(volume <= 100);
    return 1;
}

void host_speech_shutdown(void)
{
    assert(!native_live);
}

static uint32 call_flags(const char* name, uint32 a, uint32 b, uint32 c, uint32 flags)
{
    uint32 selector, result = 0xdeadbeef, args[] = {a, b, c, flags};
    assert(ROMlib_speech_signature(name, &selector));
    assert(ROMlib_speech_service(selector, args, &result));
    return result;
}

static uint32 call(const char* name, uint32 a, uint32 b, uint32 c)
{
    return call_flags(name, a, b, c, 0);
}

intptr_t CToPascalCall(void* procedure, unsigned long magic, ...)
{
    va_list args;
    va_start(args, magic);
    Ptr token = va_arg(args, Ptr);
    uint32 refcon = va_arg(args, uint32);
    if (US_TO_SYN68K(procedure) == 0x302) {
        char* next = va_arg(args, char*);
        char* length = va_arg(args, char*);
        char* flags = va_arg(args, char*);
        va_end(args);
        assert(magic == ((5 << 3) | (4 << 6) | (5 << 9) | (5 << 12) | (5 << 15)));
        assert(refcon == 0x12345678 && EM_A5 == 0xabc);
        assert(next == (char*)token + 4 && length == next + 4 && flags == next + 8);
        assert(!speech_read(next) && !speech_read(length) && !speech_read(flags));
        ++text_callback_count;
        if (text_mode == 2)
            assert(!call("DisposeSpeechChannel", US_TO_SYN68K(token), 0, 0));
        else if (text_mode == 1 || text_callback_count == 1) {
            speech_write(next, text_mode == 1 ? 0x3fff : 0x220);
            speech_write(length, 3);
            speech_write(flags, 2);
        }
        EM_A5 = 0xdeadbeef;
        return 0;
    }
    va_end(args);
    assert(US_TO_SYN68K(procedure) == 0x300);
    assert(magic == ((5 << 3) | (4 << 6)));
    assert(refcon == 0x12345678 && EM_A5 == 0xabc);
    ++callback_count;
    ROMlib_speech_poll(); /* Recursive delivery must not reenter. */
    if (callback_resets)
        ROMlib_speech_reset();
    else if (callback_disposes)
        assert(!call("DisposeSpeechChannel", US_TO_SYN68K(token), 0, 0));
    EM_A5 = 0xdeadbeef;
    return 0;
}

int main(void)
{
    unsigned char* memory = calloc(1, 0x4000);
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    assert(call("SpeechManagerVersion", 0, 0, 0) == 0x01508000);
    uint32 version_return;
    EM_A7 = 0x380;
    PUSHUL(0x12345678);
    assert(ROMlib_speech_dispatch(0x0000000c, &version_return));
    assert(version_return == 0x12345678 && EM_A7 == 0x380);
    assert(READUL(EM_A7) == 0x01508000);
    assert(!call("CountVoices", 0x100, 0, 0));
    assert(READUW(0x100) == 2);
    assert((int32)call("CountVoices", 0, 0, 0) == paramErr);
    assert((int32)call("CountVoices", 0x3fff, 0, 0) == paramErr);
    assert(!call("GetIndVoice", 1, 0x110, 0));
    assert(READUL(0x110) == TICK("SAPI") && READUL(0x114) == 1);
    assert((int32)call("GetIndVoice", 3, 0x110, 0) == -244);
    assert(!call("GetVoiceDescription", 0x110, 0x500, 362));
    assert(READUL(0x500) == 362 && memory[0x510] == 15);
    assert(!memcmp(memory + 0x511, "Test host voice", 15));
    memory[0x508] = 0x5a;
    assert(!call("GetVoiceDescription", 0x110, 0x500, 8));
    assert(READUL(0x500) == 8 && memory[0x508] == 0x5a);
    assert(!call("GetVoiceDescription", 0x110, 0, 0));
    assert((int32)call("GetVoiceDescription", 0x110, 0x3ffe, 362) == paramErr);
    WRITEUL(0x120, 0xdeadbeef);
    fail_allocation = 1;
    assert((int32)call("NewSpeechChannel", 0x110, 0x120, 0) == memFullErr);
    assert(!READUL(0x120) && !live && !native_live);
    fail_allocation = 0;
    assert(!call("NewSpeechChannel", 0x110, 0x120, 0));
    uint32 channel = READUL(0x120);
    assert(channel && live == 1 && native_live == 1);
    assert(!call("GetSpeechRate", channel, 0x130, 0));
    assert(READUL(0x130) == (180U << 16));
    assert(!call("SetSpeechRate", channel, 240U << 16, 0));
    assert(!call("GetSpeechRate", channel, 0x130, 0));
    assert(READUL(0x130) == (240U << 16));
    assert((int32)call("SetSpeechRate", channel, 0, 0) == paramErr);
    WRITEUL(0x160, 32768);
    assert(!call("SetSpeechInfo", channel, TICK("volm"), 0x160));
    assert(!call("GetSpeechInfo", channel, TICK("volm"), 0x164));
    assert(READUL(0x164) == 32768);
    WRITEUL(0x160, 65537);
    assert((int32)call("SetSpeechInfo", channel, TICK("volm"), 0x160) == paramErr);
    assert(!call("GetSpeechInfo", channel, TICK("cvox"), 0x170));
    assert(READUL(0x170) == TICK("SAPI") && READUL(0x174) == 1);
    assert((int32)call("GetSpeechInfo", channel, TICK("nope"), 0x160) == -231);
    assert((int32)call("GetSpeechInfo", channel, TICK("stat"), 0x3ffc) == paramErr);
    memcpy(memory + 0x200, "hello", 5);
    assert(!call_flags("SpeakBuffer", channel, 0x200, 5, 0));
    assert((int32)call_flags("SpeakBuffer", channel, 0x200, 4, 2) == -242);
    assert(!strcmp(spoken, "hello") && !queue_count);
    ROMlib_speech_poll(); /* The copied input is released, audio remains busy. */
    assert(!call_flags("SpeakBuffer", channel, 0x200, 4, 2));
    assert(queue_count == 1 && !strcmp(spoken, "hell"));
    assert((int32)call_flags("SpeakBuffer", channel, 0x200, 3, 1) == -248);
    assert((int32)call_flags("SpeakBuffer", channel, 0x200, 3, 4) == -248);
    assert(!strcmp(spoken, "hell") && queue_count == 1);
    assert((int32)call_flags("SpeakBuffer", channel, 0x3ffe, 4, 0) == paramErr);
    assert((int32)call_flags("SpeakBuffer", channel, 0x200, 0xffffffff, 0) == paramErr);
    /* A zero-length request stops, including non-interrupting requests. */
    assert(!call_flags("SpeakBuffer", channel, 0, 0, 2));
    assert(!spoken[0] && !call("SpeechBusy", 0, 0, 0));
    queue_count = 0;
    assert(!call("SpeakText", channel, 0x200, 5));
    assert(!strcmp(spoken, "hello") && call("SpeechBusy", 0, 0, 0) == 1);
    assert((int32)call("SpeakText", channel, 0x3ffe, 4) == paramErr);
    assert((int32)call("SpeakText", channel, 0xfffffffe, 4) == paramErr);
    assert(!call("GetSpeechInfo", channel, TICK("stat"), 0x180));
    assert(memory[0x180] == 1 && READUL(0x182) == 0);
    assert(!call("PauseSpeechAt", channel, 0, 0));
    assert(!call("GetSpeechInfo", channel, TICK("stat"), 0x180));
    assert(memory[0x181] == 1);
    assert(!call("ContinueSpeech", channel, 0, 0));
    WRITEUL(0x190, 0x12345678);
    assert(!call("SetSpeechInfo", channel, TICK("refc"), 0x190));
    WRITEUL(0x190, 0xabc);
    assert(!call("SetSpeechInfo", channel, TICK("myA5"), 0x190));
    WRITEUL(0x190, 0x300);
    assert(!call("SetSpeechInfo", channel, TICK("sdcb"), 0x190));
    EM_A5 = 0xdef;
    ROMlib_speech_poll();
    assert(!callback_count && EM_A5 == 0xdef);
    assert(!call("StopSpeech", channel, 0, 0));
    assert(!call("SpeechBusy", 0, 0, 0));
    assert(callback_count == 1 && EM_A5 == 0xdef);
    ROMlib_speech_poll();
    assert(callback_count == 1);

    /* Pascal arguments are popped in reverse order; return data remains in
     the caller's reserved result slot, with adjacent bytes preserved. */
    EM_A7 = 0x1000;
    WRITEUL(0x1000, 0x12345678);
    WRITEUL(0x1004, 0x140);
    WRITEUW(0x1008, 0xa5a5);
    WRITEUW(0x100a, 0x5a5a);
    uint32 returned;
    assert(ROMlib_speech_dispatch(0x0108000c, &returned));
    assert(returned == 0x12345678 && EM_A7 == 0x1008);
    assert(READUW(0x140) == 2 && !READUW(0x1008) && READUW(0x100a) == 0x5a5a);
    EM_A7 = 0x1000;
    WRITEUL(0x1000, 0x87654321);
    WRITEUL(0x1004, 0x150);
    WRITEUW(0x1008, 2);
    WRITEUW(0x100a, 0xa5a5);
    assert(ROMlib_speech_dispatch(0x030c000c, &returned));
    assert(returned == 0x87654321 && EM_A7 == 0x100a);
    assert(READUL(0x154) == 2 && !READUW(0x100a));
    assert(!ROMlib_speech_dispatch(0xdeadbeef, &returned));
    EM_A7 = 0x1000;
    WRITEUL(0x1000, 0x87654321);
    WRITEUL(0x1004, 0); /* controlFlags */
    WRITEUL(0x1008, 5); /* byte length */
    WRITEUL(0x100c, 0x200);
    WRITEUL(0x1010, channel);
    WRITEUW(0x1014, 0xa5a5);
    WRITEUW(0x1016, 0x5a5a);
    assert(ROMlib_speech_dispatch(0x0828000c, &returned));
    assert(returned == 0x87654321 && EM_A7 == 0x1014);
    assert(!READUW(0x1014) && READUW(0x1016) == 0x5a5a);
    assert(!strcmp(spoken, "hello"));
    assert(!call("DisposeSpeechChannel", channel, 0, 0));
    assert(!live && !native_live && disposed == 1);
    assert((int32)call("StopSpeech", channel, 0, 0) == -3000);
    assert(!call("NewSpeechChannel", 0, 0x120, 0));
    memory[0x210] = 2;
    memcpy(memory + 0x211, "hi", 2);
    assert(!call("SpeakString", 0x210, 0, 0));
    assert(call("SpeechBusy", 0, 0, 0) == 1);
    channel = READUL(0x120);
    WRITEUL(0x190, 0x12345678);
    assert(!call("SetSpeechInfo", channel, TICK("refc"), 0x190));
    WRITEUL(0x190, 0xabc);
    assert(!call("SetSpeechInfo", channel, TICK("myA5"), 0x190));
    WRITEUL(0x190, 0x300);
    assert(!call("SetSpeechInfo", channel, TICK("sdcb"), 0x190));
    assert(!call("SpeakText", channel, 0x200, 5));
    callback_disposes = 1;
    assert(!call("StopSpeech", channel, 0, 0));
    ROMlib_speech_poll();
    assert(callback_count == 2 && !live && EM_A5 == 0xdef);
    callback_disposes = 0;
    callback_resets = 1;
    for (unsigned i = 0; i < 2; ++i) {
        assert(!call("NewSpeechChannel", 0, 0x120, 0));
        channel = READUL(0x120);
        WRITEUL(0x190, 0x12345678);
        assert(!call("SetSpeechInfo", channel, TICK("refc"), 0x190));
        WRITEUL(0x190, 0xabc);
        assert(!call("SetSpeechInfo", channel, TICK("myA5"), 0x190));
        WRITEUL(0x190, 0x300);
        assert(!call("SetSpeechInfo", channel, TICK("sdcb"), 0x190));
        assert(!call("SpeakText", channel, 0x200, 5));
        assert(!call("StopSpeech", channel, 0, 0));
    }
    ROMlib_speech_poll();
    assert(callback_count == 3 && !live && !native_live && EM_A5 == 0xdef);
    ROMlib_speech_reset();
    assert(!live && !native_live && !channels && !string_channel);
    callback_resets = 0;
    assert(!call("NewSpeechChannel", 0, 0x120, 0));
    channel = READUL(0x120);
    WRITEUL(0x190, 0x12345678);
    assert(!call("SetSpeechInfo", channel, TICK("refc"), 0x190));
    WRITEUL(0x190, 0xabc);
    assert(!call("SetSpeechInfo", channel, TICK("myA5"), 0x190));
    WRITEUL(0x190, 0x302);
    assert(!call("SetSpeechInfo", channel, TICK("tdcb"), 0x190));
    memcpy(memory + 0x220, "bye", 3);
    assert(!call("SpeakText", channel, 0x200, 5));
    ROMlib_speech_poll();
    assert(text_callback_count == 1 && queue_count == 1 && !strcmp(spoken, "bye"));
    assert(EM_A5 == 0xdef);
    ROMlib_speech_poll();
    assert(text_callback_count == 2 && queue_count == 1);
    assert(!call("StopSpeech", channel, 0, 0));
    ROMlib_speech_poll();
    text_mode = 1;
    assert(!call("SpeakText", channel, 0x200, 5));
    ROMlib_speech_poll();
    assert(text_callback_count == 3 && queue_count == 1);
    assert(!call("GetSpeechInfo", channel, TICK("erro"), 0x190));
    assert(READUW(0x190) == 1 && READSW(0x192) == paramErr);
    assert(!call("GetSpeechInfo", channel, TICK("erro"), 0x190));
    assert(!READUW(0x190));
    text_mode = 2;
    assert(!call("SpeakText", channel, 0x200, 5));
    ROMlib_speech_poll();
    assert(text_callback_count == 4 && !live && !native_live && EM_A5 == 0xdef);
    ROMlib_speech_reset();
    free(memory);
    puts("Speech: guest spans, channels, controls, Pascal ABI and reset passed");
    return 0;
}
