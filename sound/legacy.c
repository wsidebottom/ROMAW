/*+===================================================================
  File:      legacy.c

  Summary:   Classic Sound Driver free-form and four-tone synthesizers (IM II-224..238). Rendering and completions run
             on the emulation thread, never SDL's thread.

  Classes:   None.

  Functions: ROMlib_legacy_sound_control, ROMlib_legacy_sound_mix, ROMlib_legacy_sound_reset, ROMlib_legacy_sound_write

  Origin:    Existing repository source in executor/src/sound; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Sound Driver free-form and four-tone synthesizers (IM II-224..238).
   Rendering and completions run on the emulation thread, never SDL's thread. */
#include "rsys/common.h"
#include "SoundDvr.h"
#include "rsys/legacysound.h"
#include "rsys/sounddriver.h"
#include "rsys/file.h"
#include "rsys/mman.h"
#include "rsys/blockinterrupts.h"

typedef struct legacy_request {
    struct legacy_request* next;
    ParmBlkPtr pb;
    uint32 address, length;
    uint64 phase[4];
    uint32 published_phase[4], ticks;
    BOOLEAN async;
    int mode;
} legacy_request;

static legacy_request *head, **tail = &head;
static unsigned amplitude = 255;

static uint16 word(const uint8* p)
{
    return (uint16)p[0] << 8 | p[1];
}

static uint32 longword(const uint8* p)
{
    return (uint32)word(p) << 16 | word(p + 2);
}

static void store_long(uint8* p, uint32 value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static uint8* guest_span(uint32 address, uint32 length)
{
    address &= syn68k_address_mask;
    if (!address || address >= ROMlib_guest_memory_size || length > ROMlib_guest_memory_size - address)
        return NULL;
    return (uint8*)SYN68K_TO_US(address);
}

static void complete_request(legacy_request* request, OSErr error)
{
    ParmBlkPtr pb = request->pb;
    BOOLEAN async = request->async;
    uint32 length = request->length;
    free(request);
    pb->ioParam.ioResult = CW(error);
    pb->ioParam.ioActCount = CL(error ? 0 : length);
    if (async && PP_RAW(pb->ioParam.ioCompletion)) {
        ProcPtr completion = PPR(pb->ioParam.ioCompletion);
        CALLCOMPLETION(pb, completion, error);
    }
}

void ROMlib_legacy_sound_reset(boolean_t complete)
{
    legacy_request* list = head;
    head = NULL;
    tail = &head;
    SoundActive = soundactiveoff;
    if (!complete)
        amplitude = 255;
    /* Detach before callbacks: a completion may submit another sound. Launch
     cleanup discards requests without touching the previous guest heap. */
    while (list) {
        legacy_request* next = list->next;
        if (complete)
            complete_request(list, abortErr);
        else
            free(list);
        list = next;
    }
}

static int validate(uint32 address, uint32 length)
{
    uint8* buffer = guest_span(address, length);
    if (!buffer || length < 6)
        return -1;
    if (word(buffer) == ffMode)
        return length > 6 && (int32)longword(buffer + 2) > 0 ? ffMode : -1;
    if (word(buffer) == ftMode) {
        uint8* record = guest_span(longword(buffer + 2), 50);
        if (!record)
            return -1;
        for (unsigned i = 0; i < 4; ++i)
            if (longword(record + 34 + i * 4) && !guest_span(longword(record + 34 + i * 4), 256))
                return -1;
        return ftMode;
    }
    return -1;
}

OSErr ROMlib_legacy_sound_write(ParmBlkPtr pb, BOOLEAN async)
{
    uint32 address = CL((uint32)PP_RAW(pb->ioParam.ioBuffer));
    int32 length = CL(pb->ioParam.ioReqCount);
    int mode = length > 0 ? validate(address, length) : -1;
    legacy_request* request;
    OSErr error = noErr;
    pb->ioParam.ioActCount = 0;
    if (mode < 0 || SND_RATE <= 0)
        error = paramErr;
    else if (ROMlib_PretendSound == soundoff)
        error = notEnoughHardware;
    if (error || ROMlib_PretendSound == soundpretend) {
        if (!error)
            pb->ioParam.ioActCount = CL(length);
        FAKEASYNC(pb, async, error);
    }
    request = calloc(1, sizeof *request);
    if (!request) {
        error = memFullErr;
        FAKEASYNC(pb, async, error);
    }
    request->pb = pb;
    request->async = async;
    request->address = address;
    request->length = length;
    request->mode = mode;
    if (mode == ftMode) {
        uint8* buffer = guest_span(address, length);
        uint8* record = guest_span(longword(buffer + 2), 50);
        for (unsigned i = 0; i < 4; ++i) {
            request->published_phase[i] = longword(record + 6 + 8 * i);
            request->phase[i] = (uint64)request->published_phase[i] << 16;
        }
    }
    pb->ioParam.ioResult = CWC(1);
    *tail = request;
    tail = &request->next;
    SoundActive = soundactiveinplay;
    SOUND_GO();
    if (!async)
        while (CW(pb->ioParam.ioResult) == 1)
            check_virtual_interrupt();
    return async ? noErr : CW(pb->ioParam.ioResult);
}

OSErr ROMlib_legacy_sound_control(ParmBlkPtr pb, BOOLEAN async)
{
    OSErr error = noErr;
    switch (CW(pb->cntrlParam.csCode)) {
    case 1: /* KillIO/StopSound. */
        ROMlib_legacy_sound_reset(TRUE);
        break;
    case 3: /* Immediate amplitude change. */
        amplitude = (uint16)CW(pb->cntrlParam.csParam[0]);
        if (amplitude > 255)
            amplitude = 255;
        break;
    default:
        error = controlErr;
        break;
    }
    FAKEASYNC(pb, async, error);
}

/* Produce one sample. A zero result means completion; negative means an
   invalidated live buffer. Four-tone records/waveforms remain mutable. */
static int next_sample(legacy_request* request, int* sample)
{
    uint8* buffer = guest_span(request->address, request->length);
    if (!buffer)
        return -1;
    if (request->mode == ffMode) {
        uint64 position = request->phase[0] >> 16;
        uint32 step = longword(buffer + 2);
        if (position >= request->length - 6)
            return 0;
        if ((int32)step <= 0)
            return -1;
        *sample = buffer[6 + position] - 128;
        request->phase[0] += (uint64)step * 22255 / SND_RATE;
        return 1;
    }
    uint8* record = guest_span(longword(buffer + 2), 50);
    int sum = 0;
    if (!record)
        return -1;
    int16 duration = word(record);
    if (!duration)
        return 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint32 address = longword(record + 34 + i * 4);
        uint32 phase = longword(record + 6 + i * 8);
        if (phase != request->published_phase[i])
            request->phase[i] = (uint64)phase << 16;
        if (address) {
            uint8* wave = guest_span(address, 256);
            if (!wave)
                return -1;
            sum += wave[(request->phase[i] >> 16) & 255] - 128;
        }
        request->phase[i] += (uint64)longword(record + 2 + i * 8) * 22255 / SND_RATE;
        request->published_phase[i] = (request->phase[i] >> 16) & 255;
        store_long(record + 6 + i * 8, request->published_phase[i]);
    }
    request->ticks += 60;
    if (request->ticks >= (unsigned)SND_RATE) {
        request->ticks -= SND_RATE;
        if (duration > 0) {
            --duration;
            record[0] = duration >> 8;
            record[1] = duration;
        }
    }
    *sample = sum / 4;
    return 1;
}

boolean_t ROMlib_legacy_sound_mix(struct hunger_info info)
{
    boolean_t active = head != NULL;
    for (snd_time t = info.t2; head && t < info.t3;) {
        legacy_request* request = head;
        int sample = 0, result = next_sample(request, &sample);
        if (result <= 0) {
            head = request->next;
            if (!head)
                tail = &head;
            SoundActive = head ? soundactiveinplay : soundactiveoff;
            complete_request(request, result < 0 ? paramErr : noErr);
            continue;
        }
        if (info.buf && info.bufsize > 0) {
            int mixed = info.buf[t % info.bufsize] + sample * (int)amplitude * (SdVolume & 7) / (255 * 7);
            info.buf[t % info.bufsize] = mixed < 0 ? 0 : mixed > 255 ? 255 : mixed;
        }
        ++t;
    }
    return active;
}
