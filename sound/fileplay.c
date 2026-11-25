/*+===================================================================
  File:      fileplay.c

  Summary:   AIFF disk playback. File I/O and guest completions stay on the CPU thread. PCM is copied into host-owned
             memory before starting the output device.

  Classes:   None.

  Functions: C_SndPauseFilePlay, C_SndStartFilePlay, C_SndStopFilePlay, ROMlib_fileplay_busy, ROMlib_fileplay_cancel,
             ROMlib_fileplay_mix, ROMlib_fileplay_poll, ROMlib_fileplay_reset,
             ROMlib_parse_aiff_header, ROMlib_parse_snd_header

  Origin:    Existing repository source in executor/src/sound; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* AIFF disk playback. File I/O and guest completions stay on the CPU thread.
   PCM is copied into host-owned memory before starting the output device. */
#include "rsys/common.h"
#include "MemoryMgr.h"
#include "FileMgr.h"
#include "SoundMgr.h"
#include "ResourceMgr.h"
#include "rsys/aiff.h"
#include "rsys/sound_resource.h"
#include "rsys/pcm.h"
#include "rsys/mace.h"
#include "rsys/fileplay.h"
#include "rsys/sounddriver.h"
#include "rsys/mman.h"
#include "rsys/blockinterrupts.h"

typedef struct file_play {
    struct file_play* next;
    SndChannelPtr channel;
    uint8* samples;
    uint32 frames, rate, saved_a5;
    ProcPtr completion;
    uint64 serial;
    snd_time end;
    int rendered, ready;
} file_play;

static file_play* plays;
static uint64 next_serial;
static int polling;

static file_play* find_play(SndChannelPtr channel)
{
    for (file_play* play = plays; play; play = play->next)
        if (play->channel == channel)
            return play;
    return NULL;
}

static boolean_t valid_channel(SndChannelPtr channel)
{
    for (SndChannelPtr item = MR(HIDDEN_RAW(allchans)); item; item = PPR(item->nextChan))
        if (item == channel)
            return TRUE;
    return FALSE;
}

boolean_t ROMlib_fileplay_busy(SndChannelPtr channel)
{
    return find_play(channel) != NULL;
}

void ROMlib_fileplay_cancel(SndChannelPtr channel)
{
    for (file_play** link = &plays; *link; link = &(*link)->next)
        if ((*link)->channel == channel) {
            file_play* play = *link;
            *link = play->next;
            free(play->samples);
            free(play);
            channel->flags &= CWC(~CHAN_PAUSED_FLAG);
            return;
        }
}

void ROMlib_fileplay_reset(void)
{
    while (plays)
        ROMlib_fileplay_cancel(plays->channel);
}

void ROMlib_fileplay_mix(SndChannelPtr channel, struct hunger_info info)
{
    file_play* play = find_play(channel);
    if (!play)
        return;
    if (!play->rendered) {
        play->rendered =
            !play->frames ||
            ROMlib_sound_resample(play->samples, info.buf, play->frames + 1, info.bufsize, play->rate, SND_RATE << 16,
                                  &SND_CHAN_CURRENT_START(channel), &SND_CHAN_PREV_SAMP(channel),
                                  &SND_CHAN_TIME(channel), info.t3, SND_CHAN_FIRSTMOD(channel)->amplitude);
        play->end = SND_CHAN_TIME(channel);
        if (play->rendered)
            warning_sound_log("file rendered channel=%p frames=%u rate=%08x end=%llu", channel, play->frames,
                              play->rate, (unsigned long long)play->end);
    }
    /* The mixer renders one quantum ahead of the currently playing quantum.
     Keep silence flowing until the final samples have reached the device. */
    snd_time played = info.t2 - (info.t3 - info.t2);
    if (!play->ready && play->rendered && !((played - play->end) >> 63)) {
        play->ready = 1;
        warning_sound_log("file finished channel=%p", channel);
    }
}

void ROMlib_fileplay_poll(void)
{
    if (polling)
        return;
    polling = 1;

    struct {
        SndChannelPtr channel;
        uint64 serial;
    } ready[64];

    unsigned count = 0;
    for (file_play* play = plays; play && count < NELEM(ready); play = play->next)
        if (play->ready) {
            ready[count].channel = play->channel;
            ready[count++].serial = play->serial;
        }
    uint32 saved_a5 = EM_A5;
    for (unsigned i = 0; i < count; ++i) {
        file_play* play = find_play(ready[i].channel);
        if (!play || play->serial != ready[i].serial)
            continue;
        ProcPtr completion = play->completion;
        EM_A5 = play->saved_a5;
        ROMlib_fileplay_cancel(ready[i].channel);
        if (completion)
            CToPascalCall(completion, 5 << 3, ready[i].channel);
        EM_A5 = saved_a5;
    }
    polling = 0;
}

typedef struct {
    INTEGER refnum;
    OSErr error;
} file_source;

static int read_file(void* context, uint32_t offset, void* buffer, uint32_t length)
{
    file_source* file = context;
    LONGINT count = length;
    file->error = SetFPos(file->refnum, fsFromStart, offset);
    if (!file->error)
        file->error = FSRead(file->refnum, &count, buffer);
    if (!file->error && count != length)
        file->error = eofErr;
    return file->error == noErr;
}

static void store_sound_description(void* description, uint32 format, unsigned channels, unsigned bits,
                                    uint32 rate, uint32 frames, uint32 offset, uint32* out_frames,
                                    uint32* out_offset)
{
    /* Seven guest longwords, with the two adjacent shorts in word 2.
       There is no sample buffer attached to a parsed description. */
    const uint32 data[7] = {0, CL(format), CL((channels << 16) | bits), CL(rate), 0, 0, 0};
    memcpy(description, data, sizeof data);
    if (out_frames) {
        const uint32 count = CL(frames);
        memcpy(out_frames, &count, sizeof count);
    }
    if (out_offset) {
        const uint32 position = CL(offset);
        memcpy(out_offset, &position, sizeof position);
    }
}

OSErr ROMlib_parse_aiff_header(INTEGER refnum, void* description, uint32* frames, uint32* offset)
{
    if (!description)
        return paramErr;
    LONGINT position, size;
    OSErr error = GetFPos(refnum, &position);
    if (error)
        return error;
    error = GetEOF(refnum, &size);
    if (error)
        return error;
    if (size < 0)
        return -208;

    aiff_info info;
    file_source file = {refnum, noErr};
    error = aiff_read_header(read_file, &file, size, &info);
    if (error == ioErr && file.error)
        error = file.error;
    const OSErr restored = SetFPos(refnum, fsFromStart, position);
    if (error || restored)
        return error ? error : restored;
    const uint32 format = info.format == TICK("NONE") ? TICK("twos") : info.format;
    store_sound_description(description, format, info.channels, info.bits, info.rate, info.frames, info.offset,
                            frames, offset);
    return noErr;
}

OSErr ROMlib_parse_snd_header(Handle resource, void* description, uint32* frames, uint32* offset)
{
    if (!resource || !description)
        return paramErr;
    const Size size = GetHandleSize(resource);
    const uint8* data = (const uint8*)STARH(resource);
    resource_sound info;
    const OSErr error = size < 0 ? badFormat : resource_sound_read(data, size, &info, 0);
    if (!error)
        store_sound_description(description, info.format, info.channels, info.bits, info.rate, info.frames, info.offset,
                                frames, offset);
    return error;
}

static void* guest_input(void* pointer, uint32 size)
{
    uint32 address = US_TO_SYN68K(pointer) & syn68k_address_mask;
    if (!pointer || !address || address >= ROMlib_guest_memory_size || size > ROMlib_guest_memory_size - address)
        return NULL;
    return SYN68K_TO_US(address);
}

static OSErr select_frames(AudioSelectionPtr selection, uint32 frames, uint32 rate, uint32* first, uint32* last)
{
    *first = 0;
    *last = frames;
    if (!selection)
        return noErr;
    const AudioSelection* part = guest_input(selection, sizeof *part);
    if (!part)
        return paramErr;
    if (CL(part->unitType) == 0xffff)
        return noErr;
    int32 start = CL(part->selStart), end = CL(part->selEnd);
    if (part->unitType || start < 0 || end < start)
        return paramErr;
    uint64 begin = (uint64)start * rate >> 32;
    uint64 finish = (uint64)end * rate >> 32;
    *first = MIN(begin, frames);
    *last = MIN(finish, frames);
    return noErr;
}

static OSErr load_resource(INTEGER resnum, AudioSelectionPtr selection, file_play* play)
{
    Handle resource = GetResource(TICK("snd "), resnum);
    if (!resource) {
        OSErr error = ResError();
        return error ? error : resNotFound;
    }
    SignedByte state = HGetState(resource);
    HLock(resource);
    Size size = GetHandleSize(resource);
    const uint8* bytes = (const uint8*)STARH(resource);
    resource_sound info;
    OSErr error = size < 0 ? badFormat : resource_sound_parse(bytes, size, &info);
    uint32 first, last;
    if (!error)
        error = select_frames(selection, info.frames, info.rate, &first, &last);
    if (!error) {
        /* MACE prediction depends on the preceding packets, even for a later
         selection. Decode from the beginning before extracting that range. */
        play->samples = malloc((size_t)info.frames + 1);
        if (!play->samples)
            error = memFullErr;
        else {
            const uint8* input = bytes + info.offset;
            if (!(info.compression ? ROMlib_mace_decode(input, info.input_size, info.channels, info.compression == 3,
                                                        play->samples, info.frames)
                                   : ROMlib_pcm_decode(input, info.input_size, info.channels, info.bits, play->samples,
                                                       info.frames)))
                error = badFormat;
            else {
                play->frames = last - first;
                play->rate = info.rate;
                memmove(play->samples, play->samples + first, play->frames);
                play->samples[play->frames] = play->frames ? play->samples[play->frames - 1] : 128;
            }
        }
    }
    HSetState(resource, state);
    return error;
}

static OSErr load_mace(file_source* file, const aiff_info* info, uint8* input, unsigned packets_per_read, uint32 first,
                       uint32 last, uint8* output)
{
    uint8* decoded = malloc(packets_per_read * 6);
    if (!decoded)
        return memFullErr;
    MACEContext context = {0};
    unsigned packet_bytes = info->channels * (info->compression == 3 ? 2 : 1);
    uint32 end_packet = last / 6 + (last % 6 != 0);
    OSErr error = noErr;
    for (uint32 packet = 0; packet < end_packet;) {
        uint32 count = MIN(end_packet - packet, packets_per_read);
        if (!read_file(file, info->offset + packet * packet_bytes, input, count * packet_bytes)) {
            error = file->error;
            break;
        }
        if (!ROMlib_mace_decode_packets(&context, input, count * packet_bytes, info->channels, info->compression == 3,
                                        decoded, count * 6)) {
            error = -208;
            break;
        }
        uint32 begin = packet * 6;
        uint32 copy_start = MAX(begin, first);
        uint32 copy_end = MIN(begin + count * 6, last);
        if (copy_end > copy_start)
            memcpy(output + copy_start - first, decoded + copy_start - begin, copy_end - copy_start);
        packet += count;
    }
    free(decoded);
    return error;
}

static OSErr load_file(INTEGER refnum, Ptr buffer, LONGINT buffer_size, AudioSelectionPtr selection, file_play* play)
{
    LONGINT size, position;
    OSErr error = GetFPos(refnum, &position);
    if (error)
        return error;
    error = GetEOF(refnum, &size);
    if (error)
        return error;
    if (size < 0)
        return -208;
    file_source file = {refnum, noErr};
    aiff_info info;
    error = aiff_parse(read_file, &file, size, &info);
    if (error == ioErr && file.error)
        error = file.error;
    uint32 first, last;
    if (!error)
        error = select_frames(selection, info.frames, info.rate, &first, &last);
    uint8 scratch[8192];
    unsigned capacity = sizeof scratch;
    uint8* input = scratch;
    if (!error && buffer) {
        input = guest_input(buffer, buffer_size);
        capacity = buffer_size;
        if (!input)
            error = paramErr;
    }
    else if (buffer_size)
        capacity = MIN((unsigned)buffer_size, sizeof scratch);
    if (!error) {
        unsigned stride = info.channels * (info.compression ? (info.compression == 3 ? 2 : 1) : info.bits / 8);
        capacity = MIN(capacity, sizeof scratch);
        uint32 frames_per_read = capacity / stride;
        if (!frames_per_read)
            error = -210; /* buffersTooSmall */
        else if (last - first == UINT32_MAX || !(play->samples = malloc((size_t)(last - first) + 1)))
            error = memFullErr;
        else {
            play->frames = last - first;
            play->rate = info.rate;
            if (info.compression)
                error = load_mace(&file, &info, input, frames_per_read, first, last, play->samples);
            else
                for (uint32 frame_index = first; frame_index < last;) {
                    uint32 count = MIN(last - frame_index, frames_per_read);
                    if (!read_file(&file, info.offset + frame_index * stride, input, count * stride)) {
                        error = file.error;
                        break;
                    }
                    aiff_decode_pcm(input, play->samples + frame_index - first, count, &info);
                    frame_index += count;
                }
            if (!error)
                play->samples[play->frames] = play->frames ? play->samples[play->frames - 1] : 128;
        }
    }
    OSErr restored = SetFPos(refnum, fsFromStart, position);
    return error ? error : restored;
}

static void wait_for_play(SndChannelPtr channel)
{
    file_play* play;
    while ((play = find_play(channel)) && !play->ready)
        check_virtual_interrupt();
    ROMlib_fileplay_cancel(channel);
}

P8(PUBLIC, pascal trap OSErr, SndStartFilePlay, SndChannelPtr, channel, INTEGER, refnum, INTEGER, resnum, LONGINT,
   buffer_size, Ptr, buffer, AudioSelectionPtr, selection, ProcPtr, completion, BOOLEAN, async)
{
    if ((!channel && async) || (channel && !valid_channel(channel)))
        return badChannel;
    if (ROMlib_PretendSound != soundon)
        return notEnoughHardware;
    if (buffer_size < 0 || (buffer && !buffer_size) || (refnum && resnum))
        return paramErr;
    if (async && completion && !guest_input(completion, 2))
        return paramErr;
    if (channel && (ROMlib_fileplay_busy(channel) || SND_CHAN_CMDINPROG_P(channel) || SND_CHAN_DBINPROG_P(channel) ||
                    channel->qHead != channel->qTail))
        return -209; /* channelBusy */
    file_play* play = calloc(1, sizeof *play);
    if (!play)
        return memFullErr;
    OSErr error =
        refnum ? load_file(refnum, buffer, buffer_size, selection, play) : load_resource(resnum, selection, play);
    boolean_t allocated = !channel;
    if (!error && allocated) {
        HIDDEN_SndChannelPtr created = {0};
        error = SndNewChannel(&created, sampledSynth, 0, NULL);
        channel = MR(HIDDEN_RAW(created));
    }
    if (error) {
        free(play->samples);
        free(play);
        return error;
    }
    play->channel = channel;
    play->completion = async ? completion : NULL;
    play->saved_a5 = EM_A5;
    play->serial = ++next_serial;
    warning_sound_log("file start channel=%p ref=%d resource=%d frames=%u rate=%08x", channel, refnum, resnum,
                      play->frames, play->rate);
    play->next = plays;
    plays = play;
    channel->flags &= CWC(~CHAN_PAUSED_FLAG);
    SND_CHAN_CURRENT_START(channel) = SND_PROMOTE(SND_CHAN_TIME(channel));
    SOUND_GO();
    if (!async) {
        wait_for_play(channel);
        if (allocated)
            SndDisposeChannel(channel, TRUE);
    }
    return noErr;
}

P1(PUBLIC, pascal trap OSErr, SndPauseFilePlay, SndChannelPtr, channel)
{
    if (!valid_channel(channel))
        return badChannel;
    if (!find_play(channel))
        return -211; /* channelNotBusy */
    SndCommand command = {CWC(pauseCmd), 0, 0};
    if (channel->flags & CWC(CHAN_PAUSED_FLAG))
        command.cmd = CWC(resumeCmd);
    return SndDoImmediate(channel, &command);
}

P2(PUBLIC, pascal trap OSErr, SndStopFilePlay, SndChannelPtr, channel, BOOLEAN, quiet_now)
{
    if (!valid_channel(channel))
        return badChannel;
    file_play* play = find_play(channel);
    if (!play)
        return -211;
    play->completion = NULL;
    if (quiet_now)
        ROMlib_fileplay_cancel(channel);
    else
        wait_for_play(channel);
    return noErr;
}
