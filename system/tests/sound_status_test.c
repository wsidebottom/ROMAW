/*+===================================================================
  File:      sound_status_test.c

  Summary:   Real channel registration and status paths without opening host audio.

  Classes:   None.

  Functions: CToPascalCall, DisposPtr, _NewPtr_flags, _gui_fatal, _sound_warning, _warning, do_virtual_interrupt, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Real channel registration and status paths without opening host audio. */
#define NDEBUG 1
#include "../../sound/sound.c"
#include "../../sound/fileplay.c"
#include "../../sound/soundIMVI.c"
#undef main
#undef NDEBUG
#include <assert.h>

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
CPUState cpu_state;
virtual_int_state_t _virtual_interrupts_blocked;

void do_virtual_interrupt(void)
{
    abort();
}

unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
sound_driver_t sound_driver;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];
static int allocations, fail_at, live;
const char NULL_STRING[] = "";

void _gui_fatal(const char* file, int line, const char* function, const char* format, ...)
{
    abort();
}

intptr_t CToPascalCall(void* procedure, unsigned long magic, ...)
{
    abort();
}

void _sound_warning(const char* file, int line, const char* function, const char* format, ...)
{
}

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

void _warning(int code, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
}

Ptr _NewPtr_flags(Size size, boolean_t system, boolean_t clear)
{
    if (++allocations == fail_at)
        return NULL;
    Ptr result = malloc(size);
    assert(result);
    memset(result, clear ? 0 : 0xa5, size);
    ++live;
    return result;
}

void DisposPtr(Ptr pointer)
{
    assert(pointer && live > 0);
    --live;
    free(pointer);
}

static void go(sound_driver_t* driver)
{
}

static void remove_channel(SndChannelPtr channel)
{
    assert(MR(HIDDEN_RAW(allchans)) == channel);
    HIDDEN_RAW_SET(allchans, PP_RAW(channel->nextChan));
    DisposPtr((Ptr)PPR(channel->firstMod));
    if (channel->flags & CWC(CHAN_ALLOC_FLAG))
        DisposPtr((Ptr)channel);
}

static void check_amplitude(SndChannelPtr channel)
{
    uint8 input[] = {0, 64, 128, 192, 255, 128};
    uint8 output[8];
    SndCommand amplitude = {CWC(ampCmd), 0, 0};
    assert(SND_CHAN_FIRSTMOD(channel)->amplitude == 255);
    /* Attenuate this channel before mixing; another channel stays untouched. */
    memset(output, 160, sizeof output);
    snd_time start = SND_PROMOTE(1), time = 0;
    uint8 previous = 64;
    assert(!ROMlib_sound_resample(input, output, sizeof input, sizeof output, 22050, 22050, &start, &previous, &time, 3,
                                  0));
    for (unsigned i = 0; i < sizeof output; ++i)
        assert(output[i] == 160);
    start = SND_PROMOTE(1);
    time = 0;
    assert(!ROMlib_sound_resample(input, output, sizeof input, sizeof output, 22050, 22050, &start, &previous, &time, 3,
                                  128));
    assert(output[0] == 128 && output[1] == 96 && output[2] == 128);
    assert(output[3] == 160);
    for (unsigned gain = 0; gain <= 255; ++gain) {
        amplitude.param1 = CW(gain);
        assert(!C_SndDoImmediate(channel, &amplitude));
        memset(output, 128, sizeof output);
        snd_time start = 0, time = 0;
        uint8 previous = 128;
        assert(ROMlib_sound_resample(input, output, sizeof input, sizeof output, 22050, 22050, &start, &previous, &time,
                                     8, SND_CHAN_FIRSTMOD(channel)->amplitude));
        assert(time == 5 && previous == 128);
        for (unsigned i = 0; i < 5; ++i)
            assert(output[i] == 128 + ((int)input[i] - 128) * (int)gain / 255);
        assert(output[5] == 128 && output[6] == 128 && output[7] == 128);
    }
    amplitude.param1 = CWC(256);
    assert(C_SndDoImmediate(channel, &amplitude) == paramErr);
    assert(SND_CHAN_FIRSTMOD(channel)->amplitude == 255);
    amplitude.param1 = CWC(-1);
    assert(C_SndDoImmediate(channel, &amplitude) == paramErr);

    /* An immediate gain change preserves the active sample buffer and clock. */
    channel->flags |= CWC(CHAN_CMDINPROG_FLAG | CHAN_DBINPROG_FLAG);
    SND_CHAN_TIME(channel) = 200;
    SND_CHAN_CURRENT_START(channel) = SND_PROMOTE(180);
    amplitude.param1 = CWC(64);
    assert(!C_SndDoImmediate(channel, &amplitude));
    assert(SND_CHAN_CMDINPROG_P(channel) && SND_CHAN_DBINPROG_P(channel));
    assert(SND_CHAN_TIME(channel) == 200 && SND_CHAN_CURRENT_START(channel) == SND_PROMOTE(180));
    SndCommand callback = {CWC(callBackCmd), 0, 0};
    enq(channel, callback);
    SndCommand flush = {CWC(flushCmd), 0, 0};
    assert(!C_SndDoImmediate(channel, &flush) && qempty_p(channel));
    assert(SND_CHAN_CMDINPROG_P(channel) && SND_CHAN_DBINPROG_P(channel));
    assert(SND_CHAN_TIME(channel) == 200 && SND_CHAN_CURRENT_START(channel) == SND_PROMOTE(180));
    SndCommand quiet = {CWC(quietCmd), 0, 0};
    enq(channel, callback);
    assert(!C_SndDoImmediate(channel, &quiet));
    assert(!SND_CHAN_CMDINPROG_P(channel) && !SND_CHAN_DBINPROG_P(channel));
    assert(!qempty_p(channel));
    assert(!C_SndDoImmediate(channel, &flush));
}

int main(void)
{
    HIDDEN_SndChannelPtr channel = {0};
    ROMlib_PretendSound = soundon;
    sound_driver.sound_go = go;
    SndCommand quiet = {CWC(quietCmd), 0, 0};
    assert(C_SndDoImmediate(NULL, &quiet) == badChannel);
    assert(C_SndDoImmediate((SndChannelPtr)1, &quiet) == badChannel);
    assert(C_SndDoCommand(NULL, &quiet, FALSE) == badChannel);
    assert(C_SndDoCommand((SndChannelPtr)1, &quiet, FALSE) == badChannel);
    assert(C_SndDoImmediate(NULL, NULL) == paramErr);
    assert(C_SndDoCommand(NULL, NULL, FALSE) == paramErr);
    assert(C_SndSoundManagerVersion() == 0x03218000);
    for (fail_at = 1; fail_at <= 2; ++fail_at) {
        allocations = 0;
        assert(C_SndNewChannel(&channel, sampledSynth, 0, NULL) == memFullErr);
        assert(!HIDDEN_RAW(channel) && !HIDDEN_RAW(allchans) && !live);
    }
    fail_at = 0;
    assert(C_SndNewChannel(NULL, sampledSynth, 0, NULL) == paramErr);
    assert(C_SndNewChannel(&channel, sampledSynth, 0, NULL) == noErr);
    SndChannelPtr first = MR(HIDDEN_RAW(channel));
    assert(live == 2 && MR(HIDDEN_RAW(allchans)) == first);
    assert(C_SndDoImmediate(first, NULL) == paramErr);
    assert(C_SndDoCommand(first, NULL, FALSE) == paramErr);
    assert(!SND_CHAN_TIME(first) && !SND_CHAN_CURRENT_START(first));
    assert(C_SndNewChannel(&channel, sampledSynth, 0, NULL) == badChannel);

    unsigned char output[sizeof(SCStatus) + 4];
    memset(output, 0xa5, sizeof output);
    SCStatusPtr status = (SCStatusPtr)(output + 2);
    assert(C_SndChannelStatus(first, sizeof *status - 1, status) == paramErr);
    assert(C_SndChannelStatus(first, -1, status) == paramErr);
    assert(C_SndChannelStatus(NULL, sizeof *status, status) == badChannel);
    assert(C_SndChannelStatus((SndChannelPtr)1, sizeof *status, status) == badChannel);
    assert(C_SndChannelStatus(first, sizeof *status, NULL) == paramErr);
    for (unsigned i = 0; i < sizeof output; ++i)
        assert(output[i] == 0xa5);
    assert(!C_SndChannelStatus(first, sizeof *status, status));
    assert(!status->scChannelBusy && !status->scChannelPaused && !status->scChannelDisposed && !status->scUnused);
    assert(CL(status->scChannelAttributes) == 0x80);
    assert(output[0] == 0xa5 && output[1] == 0xa5 && output[sizeof output - 1] == 0xa5);
    first->flags |= CWC(CHAN_DBINPROG_FLAG);
    assert(!C_SndChannelStatus(first, sizeof *status, status) && status->scChannelBusy);
    first->flags &= CWC(~CHAN_DBINPROG_FLAG);
    first->qTail = CWC(1);
    assert(!C_SndChannelStatus(first, sizeof *status, status) && status->scChannelBusy);
    first->qTail = 0;

    SndCommand pause = {CWC(pauseCmd), 0, 0};
    SndCommand resume = {CWC(resumeCmd), 0, 0};
    assert(!C_SndDoImmediate(first, &pause));
    assert(!C_SndChannelStatus(first, sizeof *status, status));
    assert(status->scChannelPaused && status->scChannelBusy);
    SND_CHAN_TIME(first) = 100;
    SND_CHAN_CURRENT_START(first) = SND_PROMOTE(80);
    assert(pause_channel_until(first, 200));
    assert(SND_CHAN_TIME(first) == 200);
    assert(SND_CHAN_CURRENT_START(first) == SND_PROMOTE(180));
    assert(pause_channel_until(first, 200));
    assert(SND_CHAN_CURRENT_START(first) == SND_PROMOTE(180));
    assert(!C_SndDoImmediate(first, &pause));
    assert(!C_SndDoImmediate(first, &resume));
    assert(!pause_channel_until(first, 300));
    assert(SND_CHAN_TIME(first) == 200);
    assert(!C_SndChannelStatus(first, sizeof *status, status));
    assert(!status->scChannelPaused && !status->scChannelBusy);
    check_amplitude(first);

    SndChannel provided, before;
    memset(&provided, 0x5a, sizeof provided);
    before = provided;
    HIDDEN_SndChannelPtr second = {RM(&provided)};
    fail_at = allocations + 1;
    assert(C_SndNewChannel(&second, sampledSynth, 0xc0, NULL) == memFullErr);
    assert(!memcmp(&provided, &before, sizeof provided) && live == 2);
    fail_at = 0;
    assert(!C_SndNewChannel(&second, sampledSynth, 0xc0, NULL));
    assert(!C_SndChannelStatus(&provided, sizeof *status, status) && CL(status->scChannelAttributes) == 0xc0);

    unsigned char manager_bytes[10];
    memset(manager_bytes, 0x5a, sizeof manager_bytes);
    SMStatusPtr manager = (SMStatusPtr)(manager_bytes + 2);
    assert(sizeof *manager == 6);
    assert(C_SndManagerStatus(5, manager) == paramErr);
    assert(C_SndManagerStatus(-1, manager) == paramErr);
    assert(C_SndManagerStatus(6, NULL) == paramErr);
    assert(!C_SndManagerStatus(6, manager));
    assert(CW(manager->smNumChannels) == 2);
    assert(manager_bytes[0] == 0x5a && manager_bytes[1] == 0x5a && manager_bytes[8] == 0x5a &&
           manager_bytes[9] == 0x5a);
    INTEGER beep = CWC(-1);
    C_SndGetSysBeepState(&beep);
    assert(CW(beep) == 1);
    for (int value = 0; value < 4; ++value) {
        assert(!C_SndSetSysBeepState(value));
        C_SndGetSysBeepState(&beep);
        assert(CW(beep) == value);
    }
    assert(C_SndSetSysBeepState(4) == paramErr);
    C_SndGetSysBeepState(&beep);
    assert(CW(beep) == 3);
    C_SndGetSysBeepState(NULL);
    remove_channel(&provided);
    remove_channel(first);
    assert(!HIDDEN_RAW(allchans) && !live);
    ROMlib_PretendSound = soundpretend;
    HIDDEN_RAW_SET(channel, 0);
    assert(!C_SndNewChannel(&channel, sampledSynth, 0, NULL));
    assert(!C_SndChannelStatus(MR(HIDDEN_RAW(channel)), sizeof *status, status));
    assert(!C_SndDisposeChannel(MR(HIDDEN_RAW(channel)), TRUE));
    ROMlib_PretendSound = soundoff;
    HIDDEN_RAW_SET(channel, 0);
    assert(C_SndNewChannel(&channel, sampledSynth, 0, NULL) == notEnoughHardware);
    assert(!HIDDEN_RAW(channel) && !live);
    assert(C_SndManagerStatus(6, manager) == notEnoughHardware);
    puts("Sound: channel allocation rollback, real status and beep state passed");
    return 0;
}
