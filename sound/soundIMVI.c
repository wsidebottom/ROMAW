/*+===================================================================
  File:      soundIMVI.c

  Summary:   Implements sound imvi support for Macintosh sound playback and conversion.

  Classes:   None.

  Functions: C_Comp3to1, C_Comp6to1, C_DirectorUnknown3, C_DirectorUnknown4, C_Exp1to3, C_Exp1to6, C_FinaleUnknown1,
             C_FinaleUnknown2, C_GetCompressionInfo, C_GetDefaultOutputVolume, C_GetSoundHeaderOffset,
             C_GetSoundPreference, C_GetSysBeepVolume, C_MACEVersion, C_SPBBytesToMilliseconds, C_SPBCloseDevice,
             C_SPBGetDeviceInfo, C_SPBGetIndexedDevice, C_SPBGetRecordingStatus, C_SPBMillisecondsToBytes,
             C_SPBOpenDevice, C_SPBPauseRecording, C_SPBRecord, C_SPBRecordToFile, C_SPBResumeRecording,
             C_SPBSetDeviceInfo, C_SPBSignInDevice, C_SPBSignOutDevice, C_SPBStopRecording, C_SPBVersion,
             C_SetDefaultOutputVolume, C_SetSoundPreference, C_SetSysBeepVolume, C_SetupAIFFHeader, C_SetupSndHeader,
             C_SndGetInfo, C_SndGetSysBeepState, C_SndManagerStatus, C_SndPlayDoubleBuffer, C_SndRecord,
             C_SndRecordToFile, C_SndSetInfo, C_SndSetSysBeepState, C_SndSoundManagerVersion, C_UnsignedFixedMulDiv,
             C_sound_timer_handler, ROMlib_get_sound_compression_name, ROMlib_sound_finish_mix,
             ROMlib_sound_play_alert, clear_pending_sounds

  Origin:    Existing repository source in executor/src/sound; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Copyright 1990, 1992, 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SoundMgr.h (DO NOT DELETE THIS LINE) */

#include "rsys/common.h"
#include "QuickDraw.h"
#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "SoundDvr.h"
#include "SoundMgr.h"
#include "TimeMgr.h"
#include "FileMgr.h"
#include "AliasMgr.h"
#include "rsys/file.h"
#include "rsys/sounddriver.h"
#include "rsys/legacysound.h"
#include "rsys/blockinterrupts.h"
#include "rsys/prefs.h"
#include "rsys/speech.h"
#include "rsys/fileplay.h"
#include "rsys/component.h"
#include "rsys/multimedia.h"

INTEGER ROMlib_sysbeep_state = 1;

enum { sound_full_gain = 0x100, sound_full_stereo = 0x01000100 };
static uint32 alert_volume = sound_full_stereo;
static uint32 output_volume = sound_full_stereo;
static unsigned alert_frames, alert_phase;

P1(PUBLIC, pascal trap void, SndGetSysBeepState, INTEGER*, statep)
{
    if (statep)
        *statep = CW(ROMlib_sysbeep_state);
}

P1(PUBLIC, pascal trap OSErr, SndSetSysBeepState, INTEGER, state)
{
    if (state & ~3)
        return paramErr;
    ROMlib_sysbeep_state = state;
    return noErr;
}

P2(PUBLIC, pascal trap OSErr, SndManagerStatus, INTEGER, length, SMStatusPtr, statusp)
{
    if (!statusp || length < (int)sizeof *statusp)
        return paramErr;
    if (ROMlib_PretendSound == soundoff)
        return notEnoughHardware;
    unsigned count = 0;
    for (SndChannelPtr channel = MR(HIDDEN_RAW(allchans)); channel; channel = PPR(channel->nextChan))
        if (++count == 32767)
            break;
    SMStatus status = {0, CW(count), 0};
    memcpy(statusp, &status, sizeof status);
    return noErr;
}

P0(PUBLIC, pascal trap NumVersion, SndSoundManagerVersion)
{
    NumVersion ret;

    warning_sound_log(NULL_STRING);
    switch (ROMlib_PretendSound) {
    case soundoff:
        ret = 0;
        break;
    case soundpretend:
    case soundon:
        ret = ROMLIB_SOUND_VERSION;
        break;
    default:
        gui_abort();
        ret = 0;
    }

    return ret;
}

P0(PUBLIC, pascal trap NumVersion, MACEVersion)
{
    /* #warning MACEVersion not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? CLC(0x2000000) : 0;
}

P0(PUBLIC, pascal trap NumVersion, SPBVersion)
{
    /* #warning SPBVersion not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? CLC(0x1000000) : 0;
}

PRIVATE struct {
    SndDoubleBufferHeaderPtr headp;
    SndChannelPtr chanp;
    int current_buffer;
    TMTask task;
    BOOLEAN busy;
} call_back_info;

/*
 * clear_pending_sounds may be called before we ever initialize sound, so
 * we have to check to make sure the sound_clear_pending function has
 * been initialized.
 */

void clear_pending_sounds(void)
{
    ROMlib_speech_reset();
    ROMlib_fileplay_reset();
    ROMlib_legacy_sound_reset(FALSE);
    alert_frames = alert_phase = 0;
    call_back_info.headp = NULL;
    call_back_info.busy = FALSE;
    if (sound_driver.sound_clear_pending)
        SOUND_CLEAR_PENDING();
    HIDDEN_RAW_SET(allchans, CLC(0));
}

/*
 * NOTE: we're not really playing anything here, although we could.
 */

PRIVATE OSErr start_playing(SndChannelPtr chanp, SndDoubleBufferHeaderPtr paramp, int which_buf)
{
    ProcPtr pp;
    static boolean_t task_inserted = FALSE;

    pp = PPR(paramp->dbhDoubleBack);
    if (pp) {
        SndDoubleBufferPtr dbp;

        dbp = HIDDEN_NATIVE(paramp->dbhBufferPtr[which_buf]);
        if (!(dbp->dbFlags & CLC(dbLastBuffer))) {
            LONGINT duration_in_mills;

            if (call_back_info.busy)
                warning_unexpected("busy");
            call_back_info.headp = paramp;
            call_back_info.chanp = chanp;
            call_back_info.current_buffer = which_buf;
            call_back_info.busy = TRUE;
            if (!task_inserted) {
                FIELD_ASSIGN_RAW(call_back_info.task.tmAddr, RM((ProcPtr)P_sound_timer_handler));
                InsTime((QElemPtr)&call_back_info.task);
                task_inserted = TRUE;
            }
            duration_in_mills = (((long long)1000 * (1 << 16) * CL(dbp->dbNumFrames)) / CL(paramp->dbhSampleRate));
            PrimeTime((QElemPtr)&call_back_info.task, duration_in_mills);
        }
        else {
            RmvTime((QElemPtr)&call_back_info.task);
            task_inserted = FALSE;
        }
    }
    return noErr;
}

A0(PUBLIC, void, C_sound_timer_handler)
{
    SndDoubleBufferPtr dbp;
    ProcPtr pp;
    int current_buffer;

    if (call_back_info.headp) {
        current_buffer = call_back_info.current_buffer;
        pp = PPR(call_back_info.headp->dbhDoubleBack);
        dbp = HIDDEN_NATIVE(call_back_info.headp->dbhBufferPtr[current_buffer]);
        call_back_info.busy = FALSE;
        start_playing(call_back_info.chanp, call_back_info.headp, current_buffer ^ 1);
        dbp->dbFlags &= CLC(~dbBufferReady);
        CToPascalCall(pp, CTOP_SetCTitle, call_back_info.chanp, dbp);
    }
}

P2(PUBLIC, pascal trap OSErr, SndPlayDoubleBuffer, SndChannelPtr, chanp, SndDoubleBufferHeaderPtr, paramp)
{
    OSErr retval;
    /* #warning SndPlayDoubleBuffer not implemented */
    switch (ROMlib_PretendSound) {
    case soundoff:
        retval = notEnoughHardware;
        break;

    case soundpretend:
        retval = start_playing(chanp, paramp, 0); /* always start with buffer 0 */
        break;

    case soundon:
        if (!paramp)
            warning_sound_log("paramp = NULL");
        else
            warning_sound_log("nc %d sz %d c %d p %d", CW(paramp->dbhNumChannels), CW(paramp->dbhSampleSize),
                              CW(paramp->dbhCompressionID), CW(paramp->dbhPacketSize));
        SND_CHAN_DBHP(chanp) = paramp;
        SND_CHAN_CURRENT_DB(chanp) = 0;
        /*
      SND_CHAN_CURRENT_START (chanp) = SND_PROMOTE (SND_CHAN_TIME (chanp));
      */
        SND_CHAN_TIME(chanp) = 0;
        SND_CHAN_CURRENT_START(chanp) = 0;
        SND_CHAN_FLAGS_X(chanp) |= CWC(CHAN_DBINPROG_FLAG);
        SOUND_GO();
        retval = noErr;
        break;

    default:
        gui_abort();
        retval = noErr; /* quiet gcc if necessary */
        break;
    }
    return retval;
}

P7(PUBLIC, pascal trap void, Comp3to1, Ptr, inp, Ptr, outp, LONGINT, cnt, Ptr, instatep, Ptr, outstatep, LONGINT,
   numchannels, LONGINT, whichchannel)
{
    /* #warning Comp3to1 not implemented */
    warning_sound_log(NULL_STRING);
}

P7(PUBLIC, pascal trap void, Comp6to1, Ptr, inp, Ptr, outp, LONGINT, cnt, Ptr, instatep, Ptr, outstatep, LONGINT,
   numchannels, LONGINT, whichchannel)
{
    /* #warning Comp6to1 not implemented */
    warning_sound_log(NULL_STRING);
}

P7(PUBLIC, pascal trap void, Exp1to3, Ptr, inp, Ptr, outp, LONGINT, cnt, Ptr, instatep, Ptr, outstatep, LONGINT,
   numchannels, LONGINT, whichchannel)
{
    /* #warning Exp1to3 not implemented */
    warning_sound_log(NULL_STRING);
}

P7(PUBLIC, pascal trap void, Exp1to6, Ptr, inp, Ptr, outp, LONGINT, cnt, Ptr, instatep, Ptr, outstatep, LONGINT,
   numchannels, LONGINT, whichchannel)
{
    /* #warning Exp1to6 not implemented */
    warning_sound_log(NULL_STRING);
}

P4(PUBLIC, pascal trap OSErr, SndRecord, ProcPtr, filterp, Point, corner, OSType, quality, Handle*, sndhandlep)
{
    /* #warning SPBRecord not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P4(PUBLIC, pascal trap OSErr, SndRecordToFile, ProcPtr, filterp, Point, corner, OSType, quality, INTEGER, refnum)
{
    /* #warning SPBRecordToFile not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P3(PUBLIC, pascal trap OSErr, SPBOpenDevice, Str255, name, INTEGER, permission, LONGINT*, inrefnump)
{
    /* #warning SPBOpenDevice not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P1(PUBLIC, pascal trap OSErr, SPBCloseDevice, LONGINT, inrefnum)
{
    /* #warning SPBCloseDevice not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P2(PUBLIC, pascal trap OSErr, SPBRecord, SPBPtr, inparamp, BOOLEAN, async)
{
    /* #warning SPBRecord not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P3(PUBLIC, pascal trap OSErr, SPBRecordToFile, INTEGER, refnum, SPBPtr, inparamp, BOOLEAN, async)
{
    /* #warning SPBRecordToFile not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P1(PUBLIC, pascal trap OSErr, SPBPauseRecording, LONGINT, refnum)
{
    /* #warning SPBPauseRecording not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P1(PUBLIC, pascal trap OSErr, SPBResumeRecording, LONGINT, refnum)
{
    /* #warning SPBResumeRecording not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P1(PUBLIC, pascal trap OSErr, SPBStopRecording, LONGINT, refnum)
{
    /* #warning PPBStopRecording not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P7(PUBLIC, pascal trap OSErr, SPBGetRecordingStatus, LONGINT, refnum, INTEGER*, recordingstatus, INTEGER*, meterlevel,
   LONGINT*, totalsampstorecord, LONGINT*, numsampsrecorded, LONGINT*, totalmsecstorecord, LONGINT*,
   numbermsecsrecorded)
{
    /* #warning SPBGetRecordingStatus not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P3(PUBLIC, pascal trap OSErr, SPBGetDeviceInfo, LONGINT, refnum, OSType, info, Ptr, infop)
{
    /* #warning SPBGetDeviceInfo not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P3(PUBLIC, pascal trap OSErr, SPBSetDeviceInfo, LONGINT, refnum, OSType, info, Ptr, infop)
{
    /* #warning SPBSetDeviceInfo not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

enum { sound_mace3 = 3, sound_mace6 = 4 };

typedef struct {
    uint32 format;
    int16 compression;
    uint16 samples_per_packet;
    uint16 bytes_per_packet;
    uint16 bytes_per_sample;
} sound_encoding;

static OSErr sound_get_encoding(OSType format, INTEGER bits, sound_encoding* encoding)
{
    if ((format == TICK("NONE") || format == TICK("raw ") || format == TICK("twos")) && (bits == 8 || bits == 16)) {
        if ((format == TICK("raw ") && bits != 8) || (format == TICK("twos") && bits != 16))
            return badFormat;
        *encoding = (sound_encoding){format, 0, 1, bits / 8, bits / 8};
        return noErr;
    }
    if (bits == 8 && (format == TICK("MAC3") || format == TICK("MAC6"))) {
        const boolean_t three_to_one = format == TICK("MAC3");
        *encoding = (sound_encoding){format, three_to_one ? sound_mace3 : sound_mace6, 6, three_to_one ? 2 : 1, 1};
        return noErr;
    }
    return badFormat;
}

static void sound_store_word(Byte* out, uint16 value)
{
    const uint16 encoded = CW(value);
    memcpy(out, &encoded, sizeof encoded);
}

static void sound_store_long(Byte* out, uint32 value)
{
    const uint32 encoded = CL(value);
    memcpy(out, &encoded, sizeof encoded);
}

static void sound_store_extended_rate(Byte* out, uint32 rate)
{
    const unsigned top_bit = 31 - __builtin_clz(rate);
    const uint64 mantissa = (uint64)rate << (63 - top_bit);
    sound_store_word(out, 16383 + top_bit - 16);
    sound_store_long(out + 2, mantissa >> 32);
    sound_store_long(out + 6, mantissa);
}

OSErr ROMlib_get_sound_compression_name(OSType format, Str255 name)
{
    static const struct {
        OSType code;
        const char* name;
    } builtins[] = {
        {T('N', 'O', 'N', 'E'), "not compressed"},
        {T('A', 'C', 'E', '2'), "ACE 2-to-1"},
        {T('A', 'C', 'E', '8'), "ACE 8-to-3"},
        {T('M', 'A', 'C', '3'), "MACE 3-to-1"},
        {T('M', 'A', 'C', '6'), "MACE 6-to-1"},
    };
    if (!name)
        return paramErr;
    for (unsigned i = 0; i < sizeof builtins / sizeof builtins[0]; ++i) {
        if (builtins[i].code == format) {
            const size_t length = strlen(builtins[i].name);
            name[0] = length;
            memcpy(name + 1, builtins[i].name, length);
            return noErr;
        }
    }
    return ROMlib_component_copy_name(TICK("sdec"), format, name);
}

P8(PUBLIC, pascal trap OSErr, SetupSndHeader, Handle, sndhandle, INTEGER, numchannels, Fixed, rate, INTEGER, size,
   OSType, compression, INTEGER, basefreq, LONGINT, numbytes, INTEGER*, headerlenp)
{
    if (!sndhandle || !headerlenp || !rate || (numchannels != 1 && numchannels != 2) ||
        (unsigned)basefreq > 127 || numbytes < 0)
        return paramErr;
    sound_encoding encoding;
    OSErr error = sound_get_encoding(compression, size, &encoding);
    if (error)
        return error;
    const unsigned frame_bytes = encoding.bytes_per_packet * numchannels;
    if ((unsigned)numbytes % frame_bytes)
        return paramErr;

    enum { resource_prefix = 20, extended_header = 64 };
    const boolean_t standard = !encoding.compression && numchannels == 1 && size == 8;
    const uint16 length = resource_prefix + (standard ? sizeof(SoundHeader) : extended_header);
    const Size handle_size = GetHandleSize(sndhandle);
    error = MemError();
    if (error)
        return error;
    if (!STARH(sndhandle) || handle_size < length)
        return paramErr;

    Byte resource[resource_prefix + extended_header] = {0};
    sound_store_word(resource, 1);
    sound_store_word(resource + 2, 1);
    sound_store_word(resource + 4, sampledSynth);
    sound_store_long(resource + 6, numchannels == 1 ? 0x80 : 0xc0);
    sound_store_word(resource + 10, 1);
    sound_store_word(resource + 12, 0x8000 | bufferCmd);
    sound_store_long(resource + 16, resource_prefix);

    Byte* header = resource + resource_prefix;
    sound_store_long(header + 4, standard ? numbytes : numchannels);
    sound_store_long(header + 8, rate);
    header[20] = standard ? stdSH : encoding.compression ? cmpSH : extSH;
    header[21] = basefreq;
    if (!standard) {
        sound_store_long(header + 22, (uint32)numbytes / frame_bytes);
        sound_store_extended_rate(header + 26, rate);
        if (encoding.compression) {
            sound_store_long(header + 40, encoding.format);
            sound_store_word(header + 56, encoding.compression);
            sound_store_word(header + 58, encoding.bytes_per_packet * 8);
            sound_store_word(header + 62, size);
        }
        else
            sound_store_word(header + 48, size);
    }
    memcpy(STARH(sndhandle), resource, length);
    sound_store_word((Byte*)headerlenp, length);
    return noErr;
}

P7(PUBLIC, pascal trap OSErr, SetupAIFFHeader, INTEGER, refnum, INTEGER, numchannels, Fixed, samplerate, INTEGER,
   samplesize, OSType, compression, LONGINT, numbytes, LONGINT, numframes)
{
    if (!samplerate || (numchannels != 1 && numchannels != 2) || numbytes < 0 || numframes < 0)
        return paramErr;

    const boolean_t compressed = compression != TICK("NONE");
    Str255 name;
    if (compressed && ROMlib_get_sound_compression_name(compression, name))
        return -223; /* siInvalidCompression */

    sound_encoding encoding;
    if (compression == TICK("NONE") || compression == TICK("MAC3") || compression == TICK("MAC6")) {
        if (sound_get_encoding(compression, samplesize, &encoding))
            return -223;
        const unsigned packet_bytes = encoding.bytes_per_packet * numchannels;
        const uint32 packets = (uint32)numbytes / packet_bytes;
        if ((unsigned)numbytes % packet_bytes || (numframes && (uint32)numframes > packets))
            return paramErr;
        if (!numframes)
            numframes = packets;
    }
    else if (samplesize < 1 || samplesize > 32 || (numbytes && !numframes))
        return paramErr;

    const unsigned common_size = compressed ? (24U + name[0]) & ~1U : 18;
    const unsigned header_size = 12 + (compressed ? 12 : 0) + 8 + common_size + 16;
    Byte header[12 + 12 + 8 + 22 + sizeof name + 16] = {0};
    memcpy(header, "FORM", 4);
    sound_store_long(header + 4, header_size - 8 + (uint32)numbytes + ((uint32)numbytes & 1));
    memcpy(header + 8, compressed ? "AIFC" : "AIFF", 4);
    Byte* chunk = header + 12;
    if (compressed) {
        memcpy(chunk, "FVER", 4);
        sound_store_long(chunk + 4, 4);
        sound_store_long(chunk + 8, 0xa2805140);
        chunk += 12;
    }

    memcpy(chunk, "COMM", 4);
    sound_store_long(chunk + 4, common_size);
    sound_store_word(chunk + 8, numchannels);
    sound_store_long(chunk + 10, numframes);
    sound_store_word(chunk + 14, samplesize);
    sound_store_extended_rate(chunk + 16, samplerate);
    if (compressed) {
        sound_store_long(chunk + 26, compression);
        memcpy(chunk + 30, name, 1 + name[0]);
    }
    chunk += 8 + common_size;
    memcpy(chunk, "SSND", 4);
    sound_store_long(chunk + 4, (uint32)numbytes + 8);

    Ptr buffer = NewPtr(header_size);
    if (!buffer)
        return MemError();
    memcpy(buffer, header, header_size);
    LONGINT count = header_size;
    const OSErr error = FSWrite(refnum, &count, buffer);
    DisposPtr(buffer);
    if (error)
        return error;
    return count == (LONGINT)header_size ? noErr : ioErr;
}

P2(PUBLIC, pascal trap OSErr, SPBSignInDevice, INTEGER, refnum, Str255, name)
{
    /* #warning SPBSignInDevice not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P1(PUBLIC, pascal trap OSErr, SPBSignOutDevice, INTEGER, refnum)
{
    /* #warning SPBSignOutDevice not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P3(PUBLIC, pascal trap OSErr, SPBGetIndexedDevice, INTEGER, count, Str255, name, Handle*, deviceiconhandlep)
{
    /* #warning SPBGetIndexedDevice not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P2(PUBLIC, pascal trap OSErr, SPBMillisecondsToBytes, LONGINT, refnum, LONGINT*, millip)
{
    /* #warning SPBMillisecondsToBytes not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P2(PUBLIC, pascal trap OSErr, SPBBytesToMilliseconds, LONGINT, refnum, LONGINT*, bytecountp)
{
    /* #warning SPBBytesToMilliseconds not implemented */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

P0(PUBLIC, pascal trap void, FinaleUnknown1)
{
    /* Finale calls this */
    warning_sound_log(NULL_STRING);
}

P4(PUBLIC, pascal trap OSErr, FinaleUnknown2, ResType, arg1, LONGINT, arg2, Ptr, arg3, Ptr, arg4)
{
    /* Finale calls this */
    warning_sound_log(NULL_STRING);
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

/* various self-running demos (made by Macromedia Director, I think)
   seem to call this */

P0(PUBLIC, pascal trap long, DirectorUnknown3)
{
    warning_sound_log(NULL_STRING);
    return 0;
}

P4(PUBLIC, pascal trap INTEGER, DirectorUnknown4, ResType, arg1, INTEGER, arg2, Ptr, arg3, Ptr, arg4)
{
    warning_sound_log(NULL_STRING);
    return paramErr;
}

/* The Sound Manager packs right and left 8.8 gains in a long word.
   ROMAW's output stream is mono, so it uses the mean of both gains. */
static unsigned sound_mono_gain(uint32 level)
{
    return ((level >> 16) + (uint16)level) / 2;
}

static uint32 sound_output_level(uint32 level)
{
    return ((uint32)MIN(level >> 16, sound_full_gain) << 16) | MIN((uint16)level, sound_full_gain);
}

boolean_t ROMlib_sound_play_alert(void)
{
    if (!alert_volume || !output_volume)
        return TRUE;
    if ((alert_volume == sound_full_stereo && output_volume == sound_full_stereo) ||
        !sound_driver.sound_works_p || !SOUND_WORKS_P())
        return FALSE;
    alert_frames = SND_RATE / 8;
    alert_phase = 0;
    SOUND_GO();
    return TRUE;
}

boolean_t ROMlib_sound_finish_mix(struct hunger_info info)
{
    const boolean_t played_alert = alert_frames != 0;
    if (!played_alert && output_volume == sound_full_stereo)
        return FALSE;

    const unsigned alert_gain = sound_mono_gain(alert_volume);
    const unsigned output_gain = sound_mono_gain(output_volume);
    for (snd_time sample_index = info.t2; sample_index < info.t3; ++sample_index) {
        int alert = 0;
        if (alert_frames) {
            const int phase = alert_phase < (unsigned)SND_RATE / 2 ? 64 : -64;
            alert = phase * (int)alert_gain / sound_full_gain;
            alert_phase = (alert_phase + 880) % SND_RATE;
            --alert_frames;
        }
        if (info.buf && info.bufsize > 0) {
            uint8* sample = &info.buf[sample_index % info.bufsize];
            const int scaled = ((int)*sample - 128 + alert) * (int)output_gain / sound_full_gain;
            *sample = MAX(-128, MIN(scaled, 127)) + 128;
        }
    }
    return played_alert;
}

P1(PUBLIC, pascal trap OSErr, GetSysBeepVolume, LONGINT*, levelp)
{
    if (!levelp)
        return paramErr;
    *levelp = CL(alert_volume);
    return noErr;
}

P1(PUBLIC, pascal trap OSErr, SetSysBeepVolume, LONGINT, level)
{
    alert_volume = sound_output_level(level);
    return noErr;
}

P1(PUBLIC, pascal trap OSErr, GetDefaultOutputVolume, LONGINT*, levelp)
{
    if (!levelp)
        return paramErr;
    *levelp = CL(output_volume);
    return noErr;
}

P1(PUBLIC, pascal trap OSErr, SetDefaultOutputVolume, LONGINT, level)
{
    output_volume = sound_output_level(level);
    return noErr;
}

P2(PUBLIC, pascal trap OSErr, GetSoundHeaderOffset, Handle, sndHandle, LONGINT*, offset)
{
    OSErr retval;
    int num_commands;
    SndCommand* cmds;
    int i;

    warning_sound_log(NULL_STRING);

    num_commands = ROMlib_get_snd_cmds(sndHandle, &cmds);

    retval = badFormat;
    for (i = 0; i < num_commands; ++i) {
        if (cmds[i].cmd == (INTEGER)CWC(bufferCmd | 0x8000) || cmds[i].cmd == (INTEGER)CWC(soundCmd | 0x8000)) {
            *offset = cmds[i].param2;
            retval = noErr;
            break;
        }
    }

    return retval;
}

P3(PUBLIC, pascal trap UnsignedFixed, UnsignedFixedMulDiv, UnsignedFixed, value, UnsignedFixed, multiplier,
   UnsignedFixed, divisor)
{
    if (!divisor)
        return UINT32_MAX;
    const uint64_t quotient = (uint64_t)value * multiplier / divisor;
    return MIN(quotient, UINT32_MAX);
}

P5(PUBLIC, pascal trap OSErr, GetCompressionInfo, INTEGER, compressionID, OSType, format, INTEGER, numChannels, INTEGER,
   sampleSize, CompressionInfoPtr, cp)
{
    if (!cp || (numChannels != 1 && numChannels != 2))
        return paramErr;
    if (compressionID == sound_mace3)
        format = TICK("MAC3");
    else if (compressionID == sound_mace6)
        format = TICK("MAC6");
    else if (compressionID != 0 && compressionID != -1)
        return badFormat;
    else if (!format)
        format = TICK("NONE");

    sound_encoding encoding;
    OSErr error = sound_get_encoding(format, sampleSize, &encoding);
    if (error)
        return error;
    Byte result[20] = {0};
    sound_store_long(result, sizeof result);
    sound_store_long(result + 4, encoding.format);
    sound_store_word(result + 8, encoding.compression);
    sound_store_word(result + 10, encoding.samples_per_packet);
    sound_store_word(result + 12, encoding.bytes_per_packet);
    sound_store_word(result + 14, encoding.bytes_per_packet * numChannels);
    sound_store_word(result + 16, encoding.bytes_per_sample);
    memcpy(cp, result, sizeof result);
    return noErr;
}

static OSErr copy_sound_preference(Handle destination, Handle source)
{
    if (destination == source)
        return noErr;
    const Size size = GetHandleSize(source);
    const OSErr size_error = MemError();
    if (size_error)
        return size_error;
    const SignedByte state = HGetState(source);
    HLock(source);
    SetHandleSize(destination, size);
    const OSErr error = MemError();
    if (!error && size)
        memcpy(STARH(destination), STARH(source), size);
    HSetState(source, state);
    return error;
}

static OSErr write_sound_preference(OSType type, Str255 name, Handle source, Handle stored)
{
    if (stored) {
        const INTEGER attributes = GetResAttrs(stored);
        OSErr error = ResError();
        if (!error && (attributes & resProtected))
            error = resAttrErr;
        if (error)
            return error;
        error = copy_sound_preference(stored, source);
        if (error)
            return error;
        ChangedResource(stored);
        return ResError();
    }

    Handle copy = NewHandle(0);
    if (!copy)
        return MemError();
    OSErr error = copy_sound_preference(copy, source);
    if (!error) {
        AddResource(copy, type, Unique1ID(type), name);
        error = ResError();
    }
    if (error)
        DisposHandle(copy);
    return error;
}

static OSErr sound_preference(OSType type, Str255 name, Handle settings, Boolean writing)
{
    if (!name || !name[0] || !settings)
        return paramErr;

    INTEGER volume;
    LONGINT directory;
    OSErr error = FindFolder(kOnSystemDisk, kPreferencesFolderType, writing, &volume, &directory);
    if (error)
        return error;
    Str255 file_name = "\021Sound Preferences";
    const INTEGER previous = CurResFile();
    const Handle previous_top = TopMapHndl;
    const SignedByte permission = writing ? fsRdWrPerm : fsRdPerm;
    INTEGER file = HOpenResFile(CW(volume), CL(directory), file_name, permission);
    if (file < 0 && writing && ResError() == fnfErr) {
        HCreateResFile(CW(volume), CL(directory), file_name);
        error = ResError();
        if (error)
            return error;
        file = HOpenResFile(CW(volume), CL(directory), file_name, permission);
    }
    if (file < 0) {
        error = ResError();
        return error ? error : resFNotFound;
    }

    const int owned = previous_top != TopMapHndl;
    if (writing) {
        const fcbrec* fcb = PRNTOFPERR(file, &error);
        if (!error && !(fcb->fcflags & fcwriteperm))
            error = permErr;
    }
    if (!error) {
        Handle stored = Get1NamedResource(type, name);
        if (stored) {
            LoadResource(stored);
            error = ResError();
        }
        else {
            error = ResError();
            if (writing && error == resNotFound)
                error = noErr;
        }
        if (!error) {
            if (writing) {
                error = write_sound_preference(type, name, settings, stored);
                if (!error) {
                    UpdateResFile(file);
                    error = ResError();
                }
            }
            else
                error = stored ? copy_sound_preference(settings, stored) : resNotFound;
        }
    }
    if (owned) {
        CloseResFile(file);
        if (!error)
            error = ResError();
    }
    if (CurResFile() != previous)
        UseResFile(previous);
    return error;
}

P3(PUBLIC, pascal trap OSErr, SetSoundPreference, OSType, theType, Str255, name, Handle, settings)
{
    if (!settings)
        return paramErr;
    const SignedByte state = HGetState(settings);
    HLock(settings);
    const OSErr error = sound_preference(theType, name, settings, TRUE);
    HSetState(settings, state);
    return error;
}

P3(PUBLIC, pascal trap OSErr, GetSoundPreference, OSType, theType, Str255, name, Handle, settings)
{
    return sound_preference(theType, name, settings, FALSE);
}

/* Sound Manager 3.1 */

P3(PUBLIC, pascal trap OSErr, SndGetInfo, SndChannelPtr, chan, OSType, selector, void*, infoPtr)
{
    OSErr retval;

    warning_sound_log(NULL_STRING);
    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}

P3(PUBLIC, pascal trap OSErr, SndSetInfo, SndChannelPtr, chan, OSType, selector, void*, infoPtr)
{
    OSErr retval;

    warning_sound_log(NULL_STRING);
    warning_unimplemented(NULL_STRING);
    retval = paramErr;
    return retval;
}
