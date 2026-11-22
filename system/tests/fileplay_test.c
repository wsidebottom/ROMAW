/*+===================================================================
  File:      fileplay_test.c

  Summary:   File Manager and output-device fixtures around real AIFF playback paths.

  Classes:   None.

  Functions: CToPascalCall, C_GetResource, C_ResError, DisposPtr, FSRead, GetEOF, GetFPos, GetHandleSize, HGetState,
             HLock, HSetState, ROMlib_legacy_sound_mix, SetFPos, _NewPtr_flags, _gui_fatal, _sound_warning, _warning,
             do_virtual_interrupt, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* File Manager and output-device fixtures around real AIFF playback paths. */
#define NDEBUG 1
#include "../../sound/sound.c"
static void advance(void);
#undef check_virtual_interrupt
#define check_virtual_interrupt() advance()
#include "../../sound/fileplay.c"
#include "../../sound/soundIMVI.c"
#undef main
#undef NDEBUG
#include <assert.h>

CPUState cpu_state;
uint16* syn68k_callback_address_space;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
uint32 ROMlib_guest_memory_size = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
sound_driver_t sound_driver;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
int ROMlib_SND_RATE = 22050;
const char NULL_STRING[] = "";
static uint8 file_bytes[9000], output[32];
static LONGINT file_size = 62;
static LONGINT mark = 17;
static int live, fail_read, completions, dispose_in_callback;
static SndChannelPtr current;
static snd_time output_time;
virtual_int_state_t _virtual_interrupts_blocked;
static unsigned regular_completions;
static uint8 resource_bytes[4096];
static HIDDEN_Ptr resource_handle;
static Size resource_size;
static SignedByte resource_state = 0x20;

Handle C_GetResource(ResType type, INTEGER id)
{
    assert(type == TICK("snd "));
    return id == 100 ? &resource_handle : NULL;
}

INTEGER
C_ResError(void)
{
    return resNotFound;
}

SignedByte HGetState(Handle handle)
{
    assert(handle == &resource_handle);
    return resource_state;
}

void HLock(Handle handle)
{
    assert(handle == &resource_handle);
    resource_state |= 0x80;
}

void HSetState(Handle handle, SignedByte state)
{
    assert(handle == &resource_handle);
    resource_state = state;
}

Size GetHandleSize(Handle handle)
{
    assert(handle == &resource_handle && (resource_state & 0x80));
    return resource_size;
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

void _gui_fatal(const char* file, int line, const char* function, const char* format, ...)
{
    abort();
}

void _sound_warning(const char* file, int line, const char* function, const char* format, ...)
{
}

void _warning(int code, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
}

Ptr _NewPtr_flags(Size size, boolean_t system, boolean_t clear)
{
    Ptr result = malloc(size);
    assert(result);
    memset(result, clear ? 0 : 0xa5, size);
    ++live;
    return result;
}

void DisposPtr(Ptr pointer)
{
    assert(pointer && live);
    --live;
    free(pointer);
}

OSErr GetFPos(INTEGER refnum, LONGINT* position)
{
    if (refnum != 7)
        return -51;
    *position = mark;
    return noErr;
}

OSErr GetEOF(INTEGER refnum, LONGINT* size)
{
    assert(refnum == 7);
    *size = file_size;
    return noErr;
}

OSErr SetFPos(INTEGER refnum, INTEGER mode, LONGINT position)
{
    assert(refnum == 7 && mode == fsFromStart);
    assert(position >= 0 && position <= file_size);
    mark = position;
    return noErr;
}

OSErr FSRead(INTEGER refnum, LONGINT* count, Ptr buffer)
{
    assert(refnum == 7 && *count >= 0 && *count <= file_size - mark);
    if (fail_read)
        return ioErr;
    memcpy(buffer, file_bytes + mark, *count);
    mark += *count;
    return noErr;
}

static void go(sound_driver_t* driver)
{
}

boolean_t ROMlib_legacy_sound_mix(struct hunger_info info)
{
    return FALSE;
}

static struct hunger_info get_hunger(sound_driver_t* driver)
{
    struct hunger_info info = {output_time, output_time + sizeof output, output_time + sizeof output, output,
                               sizeof output};
    return info;
}

static void finish_hunger(sound_driver_t* driver)
{
    output_time += sizeof output;
}

static void advance(void)
{
    SndChannelPtr mixing = plays ? plays->channel : current;
    struct hunger_info info = {output_time, output_time + sizeof output, output_time + sizeof output, output,
                               sizeof output};
    memset(output, 128, sizeof output);
    if (!pause_channel_until(mixing, info.t3)) {
        if (ROMlib_fileplay_busy(mixing))
            ROMlib_fileplay_mix(mixing, info);
        else
            while (earlier_p(SND_CHAN_TIME(mixing), info.t3)) {
                if (SND_CHAN_CMDINPROG_P(mixing))
                    do_current_command(mixing, info);
                else if (!qempty_p(mixing)) {
                    mixing->cmdInProg = deq(mixing);
                    mixing->flags |= CWC(CHAN_CMDINPROG_FLAG);
                }
                else
                    break;
            }
    }
    output_time = info.t3;
}

void do_virtual_interrupt(void)
{
    advance();
}

intptr_t CToPascalCall(void* procedure, unsigned long magic, ...)
{
    va_list arguments;
    va_start(arguments, magic);
    SndChannelPtr channel = va_arg(arguments, SndChannelPtr);
    va_end(arguments);
    if (procedure == (void*)0x102) {
        assert(magic == CTOP_SetCTitle && channel == current);
        assert(C_SndDisposeChannel(channel, TRUE) == badChannel);
        ++regular_completions;
        return 0;
    }
    assert(procedure == (void*)0x100 && magic == (5 << 3));
    assert(channel == current && EM_A5 == 0xabc);
    assert(!ROMlib_fileplay_busy(channel));
    ++completions;
    ROMlib_fileplay_poll();
    if (dispose_in_callback)
        assert(!C_SndDisposeChannel(channel, TRUE));
    EM_A5 = 0xdead;
    return 0;
}

static void put_long(uint8* p, uint32 value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void start(AudioSelectionPtr selection)
{
    output_time = 0;
    SND_CHAN_TIME(current) = SND_CHAN_CURRENT_START(current) = 0;
    EM_A5 = 0xabc;
    assert(!C_SndStartFilePlay(current, 7, 0, 0, NULL, selection, (ProcPtr)0x100, TRUE));
    assert(mark == 17);
    EM_A5 = 0xdef;
}

static void check_mace_files(void)
{
    uint8 saved[62], input[2048], baseline[12288];
    memcpy(saved, file_bytes, sizeof saved);
    uint32 seed = 0x12345678;
    for (unsigned i = 0; i < sizeof input; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        input[i] = seed;
    }
    for (unsigned mace3 = 0; mace3 <= 1; ++mace3)
        for (unsigned channels = 1; channels <= 2; ++channels) {
            unsigned packet_bytes = mace3 ? 2 : 1;
            unsigned packets = sizeof input / packet_bytes;
            memset(file_bytes, 0, sizeof file_bytes);
            file_size = 72 + sizeof input * channels;
            memcpy(file_bytes, "FORM", 4);
            put_long(file_bytes + 4, file_size - 8);
            memcpy(file_bytes + 8, "AIFCFVER", 8);
            put_long(file_bytes + 16, 4);
            put_long(file_bytes + 20, 0xa2805140);
            memcpy(file_bytes + 24, "COMM", 4);
            put_long(file_bytes + 28, 24);
            file_bytes[33] = channels;
            put_long(file_bytes + 34, packets);
            file_bytes[39] = 8;
            memcpy(file_bytes + 40, saved + 28, 10);
            memcpy(file_bytes + 50, mace3 ? "MAC3" : "MAC6", 4);
            memcpy(file_bytes + 56, "SSND", 4);
            put_long(file_bytes + 60, sizeof input * channels + 8);
            for (unsigned packet = 0; packet < packets; ++packet)
                for (unsigned channel_index = 0; channel_index < channels; ++channel_index)
                    memcpy(file_bytes + 72 + (packet * channels + channel_index) * packet_bytes,
                           input + packet * packet_bytes, packet_bytes);
            uint8 scratch[6] = {0xa5, 0, 0, 0, 0, 0x5a};
            assert(!C_SndStartFilePlay(current, 7, 0, channels * packet_bytes, scratch + 1, NULL, NULL, TRUE));
            assert(mark == 17 && scratch[0] == 0xa5 && scratch[5] == 0x5a);
            assert(plays->frames == packets * 6);
            uint32 hash = 2166136261U;
            for (unsigned i = 0; i < plays->frames; ++i)
                hash = (hash ^ plays->samples[i]) * 16777619U;
            assert(hash == (mace3 ? 0x5af088abU : 0x13eba7f1U));
            memcpy(baseline, plays->samples, plays->frames);
            assert(!C_SndStopFilePlay(current, TRUE));
            AudioSelection selection = {0, CLC(21), CLC(105)};
            assert(!C_SndStartFilePlay(current, 7, 0, channels * packet_bytes, scratch + 1, &selection, NULL, TRUE));
            assert(plays->frames == 28 && !memcmp(plays->samples, baseline + 7, 28));
            assert(mark == 17);
            assert(!C_SndStopFilePlay(current, TRUE));
            --file_size;
            assert(C_SndStartFilePlay(current, 7, 0, 0, NULL, NULL, NULL, TRUE) == -208);
            assert(mark == 17 && !plays);
        }
    memcpy(file_bytes, saved, sizeof saved);
    file_size = sizeof saved;
}

int main(void)
{
    memcpy(file_bytes, "FORM", 4);
    put_long(file_bytes + 4, file_size - 8);
    memcpy(file_bytes + 8, "AIFFCOMM", 8);
    put_long(file_bytes + 16, 18);
    file_bytes[21] = 1;
    put_long(file_bytes + 22, 8);
    file_bytes[27] = 8;
    const uint8 rate[] = {0x40, 0x0d, 0xac, 0x44, 0, 0, 0, 0, 0, 0};
    memcpy(file_bytes + 28, rate, sizeof rate);
    memcpy(file_bytes + 38, "SSND", 4);
    put_long(file_bytes + 42, 16);
    memset(file_bytes + 54, 0x7f, 8);
    ROMlib_PretendSound = soundon;
    sound_driver.sound_go = go;
    HIDDEN_SndChannelPtr channel = {0};
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, NULL));
    current = MR(HIDDEN_RAW(channel));
    assert(C_SndStartFilePlay(NULL, 7, 0, 0, NULL, NULL, NULL, TRUE) == badChannel);
    assert(C_SndStartFilePlay((SndChannelPtr)1, 7, 0, 0, NULL, NULL, NULL, TRUE) == badChannel);
    assert(C_SndPauseFilePlay(current) == -211);
    assert(C_SndStopFilePlay(current, TRUE) == -211);
    assert(C_SndStartFilePlay(current, 8, 0, 0, NULL, NULL, NULL, TRUE) == -51);
    fail_read = 1;
    assert(C_SndStartFilePlay(current, 7, 0, 0, NULL, NULL, NULL, TRUE) == ioErr);
    assert(mark == 17 && !plays);
    fail_read = 0;
    file_bytes[0] = 'X';
    assert(C_SndStartFilePlay(current, 7, 0, 0, NULL, NULL, NULL, TRUE) == -208);
    assert(mark == 17 && !plays);
    file_bytes[0] = 'F';
    start(NULL);
    assert(C_SndStartFilePlay(current, 7, 0, 0, NULL, NULL, NULL, TRUE) == -209);
    SCStatus status;
    assert(!C_SndChannelStatus(current, sizeof status, &status) && status.scChannelBusy);
    advance();
    for (unsigned i = 0; i < 8; ++i)
        assert(output[i] == 255); /* Includes the final sample. */
    assert(output[8] == 128);
    ROMlib_fileplay_poll();
    assert(!completions);
    advance();
    ROMlib_fileplay_poll();
    assert(!completions);
    advance();
    ROMlib_fileplay_poll();
    assert(completions == 1 && !plays && EM_A5 == 0xdef);
    ROMlib_fileplay_poll();
    assert(completions == 1);
    assert(!C_SndChannelStatus(current, sizeof status, &status) && !status.scChannelBusy);

    start(NULL);
    assert(!C_SndPauseFilePlay(current));
    advance();
    assert(!C_SndChannelStatus(current, sizeof status, &status) && status.scChannelPaused && status.scChannelBusy);
    for (unsigned i = 0; i < sizeof output; ++i)
        assert(output[i] == 128);
    assert(!C_SndPauseFilePlay(current));
    advance();
    assert(output[0] == 255 && completions == 1);
    assert(!C_SndStopFilePlay(current, TRUE) && !plays);
    ROMlib_fileplay_poll();
    assert(completions == 1);

    start(NULL);
    assert(!C_SndStopFilePlay(current, FALSE) && !plays);
    assert(completions == 1); /* Stop/wait suppresses completion. */
    AudioSelection selection = {0, CLC(6), CLC(12)};
    start(&selection);
    advance();
    assert(plays->frames == 2 && output[0] == 255 && output[1] == 255 && output[2] == 128);
    assert(!C_SndStopFilePlay(current, TRUE));
    selection.selStart = CLC(13);
    assert(C_SndStartFilePlay(current, 7, 0, 0, NULL, &selection, NULL, TRUE) == paramErr);
    assert(mark == 17 && !plays);
    selection.unitType = CLC(0xffff);
    start(&selection); /* No-selection ignores the reversed endpoints. */
    assert(plays->frames == 8);
    assert(!C_SndStopFilePlay(current, TRUE));
    uint8 scratch[] = {0xa5, 0, 0x5a};
    assert(!C_SndStartFilePlay(current, 7, 0, 1, scratch + 1, NULL, NULL, TRUE));
    assert(scratch[0] == 0xa5 && scratch[1] == 0x7f && scratch[2] == 0x5a);
    assert(!C_SndStopFilePlay(current, TRUE));
    assert(C_SndStartFilePlay(current, 7, 0, 0, scratch + 1, NULL, NULL, TRUE) == paramErr);
    output_time = 0;
    assert(!C_SndStartFilePlay(NULL, 7, 0, 0, NULL, NULL, (ProcPtr)0x100, FALSE));
    assert(live == 2 && !plays && completions == 1 && mark == 17);
    start(NULL);
    ROMlib_fileplay_reset();
    assert(!plays);

    HIDDEN_RAW_SET(resource_handle, RM((Ptr)resource_bytes));
    resource_size = 50;
    resource_bytes[1] = resource_bytes[3] = resource_bytes[11] = 1;
    resource_bytes[5] = 5;
    resource_bytes[12] = 0x80;
    resource_bytes[13] = 81;
    put_long(resource_bytes + 16, 20);
    put_long(resource_bytes + 24, 8);
    put_long(resource_bytes + 28, 22050U << 16);
    memset(resource_bytes + 42, 255, 8);
    output_time = 0;
    SND_CHAN_TIME(current) = SND_CHAN_CURRENT_START(current) = 0;
    assert(!C_SndStartFilePlay(current, 0, 100, 0, NULL, NULL, NULL, TRUE));
    assert(resource_state == 0x20 && mark == 17);
    memset(resource_bytes + 42, 0, 8); /* Playback owns a separate copy. */
    advance();
    assert(output[0] == 255 && output[7] == 255 && output[8] == 128);
    assert(!C_SndStopFilePlay(current, TRUE));
    assert(C_SndStartFilePlay(current, 0, 101, 0, NULL, NULL, NULL, TRUE) == resNotFound);
    resource_bytes[16] = 0xff;
    assert(C_SndStartFilePlay(current, 0, 100, 0, NULL, NULL, NULL, TRUE) == badFormat);
    assert(resource_state == 0x20 && !plays);
    put_long(resource_bytes + 16, 20);
    resource_size = 20 + 64 + 2048;
    resource_bytes[40] = 0xfe;
    resource_bytes[83] = 8;
    for (unsigned mace3 = 0; mace3 <= 1; ++mace3) {
        put_long(resource_bytes + 24, 1);
        put_long(resource_bytes + 42, mace3 ? 1024 : 2048);
        resource_bytes[77] = mace3 ? 3 : 4;
        uint32 seed = 0x12345678;
        for (unsigned i = 0; i < 2048; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            resource_bytes[84 + i] = seed;
        }
        assert(!C_SndStartFilePlay(current, 0, 100, 0, NULL, NULL, NULL, TRUE));
        uint32 hash = 2166136261U;
        for (unsigned i = 0; i < plays->frames; ++i)
            hash = (hash ^ plays->samples[i]) * 16777619U;
        assert(hash == (mace3 ? 0x5af088abU : 0x13eba7f1U));
        assert(!C_SndStopFilePlay(current, TRUE));
    }
    check_mace_files();
    start(NULL);
    dispose_in_callback = 1;
    advance();
    advance();
    advance();
    ROMlib_fileplay_poll();
    assert(completions == 2 && !live && !HIDDEN_RAW(allchans) && !plays);
    assert(C_SndDisposeChannel(NULL, TRUE) == badChannel);
    assert(C_SndDisposeChannel((SndChannelPtr)1, TRUE) == badChannel);
    HIDDEN_RAW_SET(channel, 0);
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, (ProcPtr)0x102));
    current = MR(HIDDEN_RAW(channel));
    output_time = 0;
    cpu_state.interrupt_status_changed = INTERRUPT_STATUS_CHANGED;
    SndCommand callback = {CWC(callBackCmd), 0, 0};
    uint8 samples[30] = {0};
    put_long(samples + 4, 8);
    put_long(samples + 8, 22050U << 16);
    memset(samples + 22, 255, 8);
    SndCommand buffer = {CWC(bufferCmd), 0, RM((Ptr)samples)};
    assert(!C_SndDoCommand(current, &buffer, FALSE));
    assert(!C_SndDoCommand(current, &callback, FALSE));
    assert(!C_SndDisposeChannel(current, FALSE));
    assert(regular_completions == 1 && !live && !HIDDEN_RAW(allchans) && output[0] == 255);
    HIDDEN_RAW_SET(channel, 0);
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, (ProcPtr)0x102));
    current = MR(HIDDEN_RAW(channel));
    assert(!C_SndDoCommand(current, &callback, FALSE));
    assert(!C_SndDisposeChannel(current, TRUE));
    assert(regular_completions == 1 && !live && !HIDDEN_RAW(allchans));
    HIDDEN_RAW_SET(channel, 0);
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, NULL));
    current = MR(HIDDEN_RAW(channel));
    dispose_in_callback = 0;
    start(NULL);
    assert(!C_SndDisposeChannel(current, FALSE));
    assert(completions == 3 && !live && !HIDDEN_RAW(allchans) && !plays);
    HIDDEN_RAW_SET(channel, 0);
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, NULL));
    current = MR(HIDDEN_RAW(channel));
    start(NULL);
    sound_driver.sound_hunger_start = go;
    sound_driver.sound_get_hunger_info = get_hunger;
    sound_driver.sound_hunger_finish = finish_hunger;
    sound_driver.sound_stop = go;
    EM_A7 = 0x10000;
    /* No event/status polling: only real CPU-side sound interrupts. */
    for (unsigned i = 0; i < 32 && plays; ++i)
        sound_callback(0, NULL);
    assert(completions == 4 && !plays);
    assert(!C_SndDisposeChannel(current, TRUE));
    puts("File playback: PCM, pause/resume, completion timing, stop and cleanup passed");
    return 0;
}
