/*+===================================================================
  File:      classic_utilities.h

  Summary:   Implements CarbonLib memory, event, Gestalt, and fixed-point utilities.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 PowerPC disassembly, System 9.2.1 InterfaceLib
             overlays, and Universal Interfaces 3.4.2.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_CLASSIC_UTILITIES_H
#define ROMAW_CARBON_CLASSIC_UTILITIES_H

#include "internal.h"
#include "cf_batch.h"
#include "OSUtil.h"
#include "TextEdit.h"
#include "rsys/mman.h"
#include "rsys/osutil.h"
#include "synppc/ppc.h"

static int carbon_Delay(ROMlib_carbon_ppc_frame* call)
{
    uint32_t ticks;
    return ROMlib_carbon_call_register_vector(0x04ec, 0x133832, 0xa03b, 0, call, &ticks) &&
           (!call->words[1] || cfp_write(call->words[1], ticks));
}

static int carbon_SetTime(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_ppc_frame date = *call;
    uint32_t ignored;
    return ROMlib_carbon_call_register_vector(0x151c, 0x9832, 0, 0, call, &date.words[0]) &&
           ROMlib_carbon_call_register_vector(0x04e8, 0x33822, 0xa03a, 0, &date, &ignored);
}

static int carbon_GetSysPPtr(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = 0x1f8;
    return 1;
}

static int carbon_ScreenRes(ROMlib_carbon_ppc_frame* call)
{
    /* The classic inline writes vertical first. The outputs may alias. */
    return cfp_move(call->words[1], 0x102, 2) && cfp_move(call->words[0], 0x104, 2);
}

static int carbon_TEFromScrap(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = TEFromScrap();
    return 1;
}

static int carbon_TEToScrap(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = TEToScrap();
    return 1;
}

static int carbon_machine_location(ROMlib_carbon_ppc_frame* call, uint32_t trap_word)
{
    if (!call->words[0])
        return 1;
    if (!ROMlib_carbon_guest_memory(call->words[0], ROMLIB_MACHINE_LOCATION_SIZE, trap_word == 0xa051))
        return 0;
    ROMlib_carbon_ppc_frame args = *call;
    args.words[1] = ROMLIB_MACHINE_LOCATION_XPRAM;
    uint32_t result;
    return ROMlib_carbon_call_register_vector(0x400 + 4 * (trap_word & 0xff), 0x733802, trap_word, 0, &args, &result);
}

static int carbon_ReadLocation(ROMlib_carbon_ppc_frame* call)
{
    return carbon_machine_location(call, 0xa051);
}

static int carbon_WriteLocation(ROMlib_carbon_ppc_frame* call)
{
    return carbon_machine_location(call, 0xa052);
}

enum { UTC_UNDERFLOW = -8850, UTC_OVERFLOW = -8851 };

static int carbon_clock_status(ROMlib_carbon_ppc_frame* call, int32 status)
{
    call->words[0] = status;
    return 1;
}

static uint64 carbon_read_clock(const Byte* bytes)
{
    uint64 value = 0;
    for (unsigned index = 0; index < 8; ++index)
        value = (value << 8) | bytes[index];
    return value;
}

static void carbon_write_clock(Byte* bytes, uint64 value)
{
    for (unsigned index = 0; index < 8; ++index)
        bytes[index] = value >> (56 - 8 * index);
}

static int carbon_clock_adjust(uint64* seconds, int32 offset, uint64 maximum)
{
    int64 adjusted = (int64)*seconds + offset;
    if (adjusted < 0)
        return UTC_UNDERFLOW;
    if ((uint64)adjusted > maximum)
        return UTC_OVERFLOW;
    *seconds = adjusted;
    return noErr;
}

static int carbon_clock_convert(ROMlib_carbon_ppc_frame* call, int to_local, int extended)
{
    uint32 input = call->words[0], output = call->words[1];
    if (!output || (extended && !input))
        return carbon_clock_status(call, paramErr);

    uint64 value = (uint64)input << 16;
    if (extended) {
        const Byte* source = ROMlib_carbon_guest_memory(input, 8, 0);
        if (!source)
            return carbon_clock_status(call, paramErr);
        value = carbon_read_clock(source);
    }
    uint64 seconds = value >> 16;
    int32 offset = ROMlib_machine_gmt_delta();
    int status = carbon_clock_adjust(&seconds, to_local ? offset : -offset, extended ? UINT64_MAX >> 16 : UINT32_MAX);
    if (status != noErr)
        return carbon_clock_status(call, status);

    Byte* destination = ROMlib_carbon_guest_memory(output, extended ? 8 : 4, 1);
    if (!destination)
        return carbon_clock_status(call, paramErr);
    if (extended)
        carbon_write_clock(destination, (seconds << 16) | (value & 0xffff));
    else {
        uint32 encoded = CLC((uint32)seconds);
        memcpy(destination, &encoded, sizeof encoded);
    }
    return carbon_clock_status(call, noErr);
}

static int carbon_clock_access(ROMlib_carbon_ppc_frame* call, int local, int write_clock)
{
    uint32 address = call->words[0];
    if (!address || call->words[1])
        return carbon_clock_status(call, paramErr);
    Byte* date = ROMlib_carbon_guest_memory(address, 8, !write_clock);
    if (!date)
        return carbon_clock_status(call, paramErr);

    uint64 value = write_clock ? carbon_read_clock(date) : ROMlib_get_local_clock();
    uint64 seconds = value >> 16;
    if (!local) {
        int32 offset = ROMlib_machine_gmt_delta();
        int status = carbon_clock_adjust(&seconds, write_clock ? offset : -offset, UINT64_MAX >> 16);
        if (status != noErr)
            return carbon_clock_status(call, status);
        value = (seconds << 16) | (value & 0xffff);
    }
    if (write_clock) {
        OSErr status = ROMlib_set_local_clock(value);
        if (status == noErr && !cfp_write(0x20c, (uint32)seconds))
            return 0;
        return carbon_clock_status(call, status);
    }
    carbon_write_clock(date, value);
    return carbon_clock_status(call, noErr);
}

static int carbon_ConvertLocalTimeToUTC(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_convert(call, 0, 0);
}

static int carbon_ConvertUTCToLocalTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_convert(call, 1, 0);
}

static int carbon_ConvertLocalToUTCDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_convert(call, 0, 1);
}

static int carbon_ConvertUTCToLocalDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_convert(call, 1, 1);
}

static int carbon_GetLocalDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_access(call, 1, 0);
}

static int carbon_GetUTCDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_access(call, 0, 0);
}

static int carbon_SetLocalDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_access(call, 1, 1);
}

static int carbon_SetUTCDateTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_clock_access(call, 0, 1);
}

static int carbon_Gestalt(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "Gestalt", &ref))
        return 0;
    if (ref.code_offset != 0x72170 || ref.code_size != 0xb8fb4 || !ref.toc || ref.toc_offset != 0x2f78 ||
        ref.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t toc = US_TO_SYN68K(ref.toc);
    uint32_t flag_address, delegate_address;
    if (!cfp_get(toc - 0x45c, &flag_address) || !cfp_get(toc - 0x458, &delegate_address))
        return 0;
    Byte* initialized = ROMlib_carbon_guest_memory(flag_address, 1, 1);
    if (!initialized)
        return 0;
    uint32_t delegate;
    if (!*initialized) {
        uint32_t names;
        if (!cfp_get(toc - 0x10c, &names))
            return 0;
        const uint32_t lookup[] = {names, names + 0xe};
        if (!cfp_private(&ref, 0x55c5c, lookup, 2, &delegate) || !cfp_write(delegate_address, delegate))
            return 0;
        *initialized = 1;
    }
    else if (!cfp_get(delegate_address, &delegate))
        return 0;

    const uint32_t selector = call->words[0], output = call->words[1];
    uint32_t status;
    if (selector == TICK("uisf") || selector == TICK("uisv")) {
        const uint32_t args[] = {selector == TICK("uisf") ? 0 : output, selector == TICK("uisf") ? output : 0};
        if (!cfp_private(&ref, 0x2b7d4, args, 2, &status))
            return 0;
        call->words[0] = (int16_t)status;
        return 1;
    }

    const uint32_t args[] = {selector, output};
    if (!cfp_callback(delegate, args, 2, &status))
        return 0;
    if (!status && (selector == TICK("menu") || selector == TICK("fold"))) {
        uint32_t value;
        if (!cfp_get(output, &value))
            return 0;
        if (selector == TICK("menu")) {
            if (!cfp_write(output, value | 0xc))
                return 0;
        }
        else {
            value |= 0x10;
            if (!cfp_write(output, value))
                return 0;
            _Alignas(8) uint32_t filesystem = 0;
            const uint32_t fs_args[] = {TICK("fs  "), US_TO_SYN68K(&filesystem)};
            if (!cfp_callback(delegate, fs_args, 2, &status))
                return 0;
            if (!status && (CL(filesystem) & 0x1000) && !cfp_write(output, value | 0x20))
                return 0;
        }
    }
    call->words[0] = status;
    return 1;
}

static int carbon_read_global(ROMlib_carbon_ppc_frame* call, uint32_t address, size_t extent)
{
    const void* memory = ROMlib_carbon_guest_memory(address, extent, 0);
    if (!memory)
        return 0;
    if (extent == 2) {
        uint16_t guest;
        memcpy(&guest, memory, sizeof guest);
        call->words[0] = (int16_t)CW(guest);
    }
    else {
        uint32_t guest;
        memcpy(&guest, memory, sizeof guest);
        call->words[0] = CL(guest);
    }
    return 1;
}

static int carbon_TickCount(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0x16a, 4);
}

static int carbon_GetDblTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0x2f0, 4);
}

static int carbon_GetCaretTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0x2f4, 4);
}

static int carbon_GetGrayRgn(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0x9ee, 4);
}

static int carbon_MemError(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0x220, 2);
}

static int carbon_GetGrowZone(ROMlib_carbon_ppc_frame* call)
{
    /* The callback remains a guest procedure descriptor or 68K address. */
    if (!carbon_read_global(call, 0x118, 4))
        return 0;
    if (!call->words[0])
        return 1;
    uint32_t zone = call->words[0];
    if (!ROMlib_carbon_guest_memory(zone, offsetof(Zone, gzProc) + 4, 0))
        return 0;
    return carbon_read_global(call, zone + offsetof(Zone, gzProc), 4);
}

static int carbon_SetEventMask(ROMlib_carbon_ppc_frame* call)
{
    void* mask = ROMlib_carbon_guest_memory(0x144, 2, 1);
    if (!mask)
        return 0;
    const uint16_t guest = CW(call->words[0]);
    memcpy(mask, &guest, sizeof guest);
    return 1;
}

static int carbon_GetKeys(ROMlib_carbon_ppc_frame* call)
{
    const unsigned char* keymap = ROMlib_carbon_guest_memory(0x174, 16, 0);
    unsigned char* output = ROMlib_carbon_guest_memory(call->words[0], 16, 1);
    if (!keymap || !output)
        return 0;
    /* CarbonLib loads and stores four consecutive guest words in order. */
    for (unsigned offset = 0; offset < 16; offset += 4) {
        uint32_t word;
        memcpy(&word, keymap + offset, sizeof word);
        memcpy(output + offset, &word, sizeof word);
    }
    return 1;
}

static int carbon_GetGlobalMouse(ROMlib_carbon_ppc_frame* call)
{
    const void* mouse = ROMlib_carbon_guest_memory(0x830, 4, 0);
    void* output = ROMlib_carbon_guest_memory(call->words[0], 4, 1);
    if (!mouse || !output)
        return 0;
    memmove(output, mouse, 4);
    return 1;
}

static int carbon_AEInitializeDesc(ROMlib_carbon_ppc_frame* call)
{
    uint32_t descriptor[2] = {CLC(TICK("null")), 0};
    void* output = ROMlib_carbon_guest_memory(call->words[0], sizeof descriptor, 1);
    if (!output)
        return 0;
    memcpy(output, descriptor, sizeof descriptor);
    return 1;
}

static int carbon_write_longword(uint32_t address, uint32_t value)
{
    void* destination = ROMlib_carbon_guest_memory(address, 4, 1);
    if (!destination)
        return 0;
    value = CL(value);
    memcpy(destination, &value, 4);
    return 1;
}

#define CARBON_TEXTEDIT_GLOBAL(getter, setter, address)              \
    static int carbon_##getter(ROMlib_carbon_ppc_frame* call_frame)  \
    {                                                                \
        return carbon_read_global(call_frame, address, 4);           \
    }                                                                \
    static int carbon_##setter(ROMlib_carbon_ppc_frame* call_frame)  \
    {                                                                \
        return carbon_write_longword(address, call_frame->words[0]); \
    }

CARBON_TEXTEDIT_GLOBAL(TEGetDoTextHook, TESetDoTextHook, 0xa70)
CARBON_TEXTEDIT_GLOBAL(TEGetFindWordHook, TESetFindWordHook, 0x7f8)
CARBON_TEXTEDIT_GLOBAL(TEGetRecalcHook, TESetRecalcHook, 0xa74)
CARBON_TEXTEDIT_GLOBAL(TEGetScrapHandle, TESetScrapHandle, 0xab4)

#undef CARBON_TEXTEDIT_GLOBAL

static int carbon_TEScrapHandle(ROMlib_carbon_ppc_frame* call)
{
    return carbon_TEGetScrapHandle(call);
}

static int carbon_TEGetScrapLength(ROMlib_carbon_ppc_frame* call)
{
    return carbon_read_global(call, 0xab0, 2);
}

static int carbon_TESetScrapLength(ROMlib_carbon_ppc_frame* call)
{
    void* destination = ROMlib_carbon_guest_memory(0xab0, 2, 1);
    if (!destination)
        return 0;
    const uint16_t value = CW(call->words[0]);
    memcpy(destination, &value, 2);
    return 1;
}

static int carbon_PtrToHand(ROMlib_carbon_ppc_frame* call)
{
    const int32_t length = call->words[2];
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 4, 1) : NULL;
    const void* source = length > 0 ? ROMlib_carbon_try_guest_memory(call->words[0], length, 0) : NULL;
    if (!output || length < 0 || (length > 0 && !source)) {
        call->words[0] = paramErr;
        return 1;
    }
    Handle handle = NULL;
    const OSErr error = PtrToHand((Ptr)source, &handle, length);
    if (!error) {
        const uint32_t word = CL(US_TO_SYN68K_CHECK0(handle));
        memcpy(output, &word, 4);
    }
    call->words[0] = error;
    return 1;
}

static int carbon_HandToHand(ROMlib_carbon_ppc_frame* call)
{
    void* slot = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 4, 1) : NULL;
    if (!slot) {
        call->words[0] = paramErr;
        return 1;
    }
    uint32_t word;
    memcpy(&word, slot, 4);
    const uint32_t address = CL(word);
    Handle handle = address ? ROMlib_carbon_try_guest_memory(address, 4, 0) : NULL;
    const OSErr error = address && !handle ? paramErr : HandToHand(&handle);
    if (!error) {
        word = CL(US_TO_SYN68K_CHECK0(handle));
        memcpy(slot, &word, 4);
    }
    call->words[0] = error;
    return 1;
}

static int carbon_PurgeSpace(ROMlib_carbon_ppc_frame* call)
{
    Size total, contiguous;
    _PurgeSpace_flags(&total, &contiguous, FALSE);
    return (!call->words[0] || carbon_write_longword(call->words[0], total)) &&
           (!call->words[1] || carbon_write_longword(call->words[1], contiguous));
}

static int carbon_store_clock_value(uint32_t address, uint64_t value)
{
    if (!address)
        return 0;
    void* result = ROMlib_carbon_guest_memory(address, 8, 1);
    if (!result)
        return 0;
    const uint32_t words[2] = {CL((uint32_t)(value >> 32)), CL((uint32_t)value)};
    memcpy(result, words, sizeof words);
    return 1;
}

static int carbon_Microseconds(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0], ROMlib_ppc_clock_ticks() / 25);
}

static int carbon_UpTime(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0], ROMlib_ppc_clock_ticks());
}

/* The emulated PowerPC time base advances once every 40 nanoseconds.
   The System 9 'fovr' 51 library uses a signed-wide maximum for forever. */
enum { nanoseconds_per_clock_tick = 40, nanoseconds_per_microsecond = 1000, nanoseconds_per_millisecond = 1000000 };

static uint64_t carbon_time_words(const uint32_t* words)
{
    return (uint64_t)words[0] << 32 | words[1];
}

static uint64_t carbon_ticks_to_nanoseconds(uint64_t ticks)
{
    return ticks > INT64_MAX / nanoseconds_per_clock_tick ? INT64_MAX : ticks * nanoseconds_per_clock_tick;
}

static uint64_t carbon_duration_nanoseconds(int32_t duration)
{
    if (duration == INT32_MAX)
        return INT64_MAX;
    if (duration < 0)
        return -(int64_t)duration * nanoseconds_per_microsecond;
    return (int64_t)duration * nanoseconds_per_millisecond;
}

static int32_t carbon_nanoseconds_duration(uint64_t nanoseconds)
{
    if (nanoseconds > (uint64_t)INT32_MAX * nanoseconds_per_millisecond)
        return INT32_MAX;
    if (nanoseconds > (uint64_t)INT32_MAX * nanoseconds_per_microsecond)
        return nanoseconds / nanoseconds_per_millisecond;
    return -(int32_t)(nanoseconds / nanoseconds_per_microsecond);
}

static uint64_t carbon_absolute_difference(uint64_t left, uint64_t right)
{
    return left > right ? left - right : 0;
}

static int carbon_AbsoluteToNanoseconds(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0], carbon_ticks_to_nanoseconds(carbon_time_words(call->words + 1)));
}

static int carbon_NanosecondsToAbsolute(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0], carbon_time_words(call->words + 1) / nanoseconds_per_clock_tick);
}

static int carbon_AbsoluteToDuration(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = carbon_nanoseconds_duration(carbon_ticks_to_nanoseconds(carbon_time_words(call->words)));
    return 1;
}

static int carbon_NanosecondsToDuration(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = carbon_nanoseconds_duration(carbon_time_words(call->words));
    return 1;
}

static int carbon_DurationToAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const int32_t duration = call->words[1];
    const uint64_t ticks =
        duration == INT32_MAX ? INT64_MAX : carbon_duration_nanoseconds(duration) / nanoseconds_per_clock_tick;
    return carbon_store_clock_value(call->words[0], ticks);
}

static int carbon_DurationToNanoseconds(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0], carbon_duration_nanoseconds(call->words[1]));
}

static int carbon_AddAbsoluteToAbsolute(ROMlib_carbon_ppc_frame* call)
{
    return carbon_store_clock_value(call->words[0],
                                    carbon_time_words(call->words + 1) + carbon_time_words(call->words + 3));
}

static int carbon_SubAbsoluteFromAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const uint64_t delta =
        carbon_absolute_difference(carbon_time_words(call->words + 1), carbon_time_words(call->words + 3));
    return carbon_store_clock_value(call->words[0], delta);
}

static int carbon_AbsoluteDeltaToDuration(ROMlib_carbon_ppc_frame* call)
{
    const uint64_t delta =
        carbon_absolute_difference(carbon_time_words(call->words), carbon_time_words(call->words + 2));
    call->words[0] = carbon_nanoseconds_duration(carbon_ticks_to_nanoseconds(delta));
    return 1;
}

static int carbon_AbsoluteDeltaToNanoseconds(ROMlib_carbon_ppc_frame* call)
{
    const uint64_t delta =
        carbon_absolute_difference(carbon_time_words(call->words + 1), carbon_time_words(call->words + 3));
    return carbon_store_clock_value(call->words[0], carbon_ticks_to_nanoseconds(delta));
}

static int carbon_AddDurationToAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const int32_t duration = call->words[1];
    const uint64_t absolute = carbon_time_words(call->words + 2);
    const uint64_t result = duration == INT32_MAX
                                ? INT64_MAX
                                : absolute + carbon_duration_nanoseconds(duration) / nanoseconds_per_clock_tick;
    return carbon_store_clock_value(call->words[0], result);
}

static int carbon_SubDurationFromAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const int32_t duration = call->words[1];
    const uint64_t absolute = carbon_time_words(call->words + 2);
    const uint64_t interval = carbon_duration_nanoseconds(duration) / nanoseconds_per_clock_tick;
    /* The reference passes the converted duration as the left argument. */
    return carbon_store_clock_value(call->words[0],
                                    duration == INT32_MAX ? INT64_MAX : carbon_absolute_difference(interval, absolute));
}

static int carbon_AddNanosecondsToAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const uint64_t interval = carbon_time_words(call->words + 1) / nanoseconds_per_clock_tick;
    return carbon_store_clock_value(call->words[0], interval + carbon_time_words(call->words + 3));
}

static int carbon_SubNanosecondsFromAbsolute(ROMlib_carbon_ppc_frame* call)
{
    const uint64_t interval = carbon_time_words(call->words + 1) / nanoseconds_per_clock_tick;
    return carbon_store_clock_value(call->words[0],
                                    carbon_absolute_difference(interval, carbon_time_words(call->words + 3)));
}

static int carbon_DateToSeconds(ROMlib_carbon_ppc_frame* call)
{
    uint32_t seconds;
    return ROMlib_carbon_call_register_vector(0x151c, 0x9832, 0, 0, call, &seconds) &&
           carbon_write_longword(call->words[1], seconds);
}

static int carbon_SecondsToDate(ROMlib_carbon_ppc_frame* call)
{
    uint32_t ignored;
    return ROMlib_carbon_call_register_vector(0x1518, 0x131802, 0, 0, call, &ignored);
}

static int carbon_GetTime(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(16) uint32_t seconds;
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = US_TO_SYN68K(&seconds);
    uint32_t ignored;
    if (!ROMlib_carbon_call_register_vector(0x04e4, 0x133802, 0xa039, 0, &arguments, &ignored))
        return 0;
    arguments.words[0] = CL(seconds);
    arguments.words[1] = call->words[0];
    return carbon_SecondsToDate(&arguments);
}

static int carbon_GetAlertStage(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = (int32_t)GetAlrtStage();
    return 1;
}

static int carbon_ResetAlertStage(ROMlib_carbon_ppc_frame* call)
{
    (void)call;
    ResetAlrtStage();
    return 1;
}

static int carbon_SetDialogFont(ROMlib_carbon_ppc_frame* call)
{
    SetDAFont((int16_t)call->words[0]);
    return 1;
}

static int carbon_BlockZero(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t length = call->words[1];
    if (!length)
        return 1;
    void* output = ROMlib_carbon_guest_memory(call->words[0], length, 1);
    if (!output)
        return 0;
    memset(output, 0, length);
    return 1;
}

static int carbon_copy_uncached(ROMlib_carbon_ppc_frame* call, BOOLEAN flush_code)
{
    const int32_t length = call->words[2];
    if (length <= 0)
        return 1;
    const void* source = ROMlib_carbon_guest_memory(call->words[0], length, 0);
    void* destination = ROMlib_carbon_guest_memory(call->words[1], length, 1);
    if (!source || !destination)
        return 0;
    if (flush_code)
        BlockMove_the_trap((Ptr)source, destination, length, TRUE);
    else
        memmove(destination, source, length);
    return 1;
}

static int carbon_BlockMoveUncached(ROMlib_carbon_ppc_frame* call)
{
    return carbon_copy_uncached(call, TRUE);
}

static int carbon_BlockMoveDataUncached(ROMlib_carbon_ppc_frame* call)
{
    return carbon_copy_uncached(call, FALSE);
}

static int carbon_memory_hold(ROMlib_carbon_ppc_frame* call, unsigned selector)
{
    ROMlib_carbon_ppc_frame arguments = {0};
    arguments.words[0] = selector;
    arguments.words[1] = call->words[0];
    arguments.words[2] = call->words[1];
    return ROMlib_carbon_call_register_vector(0x0570, 0x5e671822, 0xa05c, 1, &arguments, call->words);
}

static int carbon_HoldMemory(ROMlib_carbon_ppc_frame* call)
{
    return carbon_memory_hold(call, 0);
}

static int carbon_UnholdMemory(ROMlib_carbon_ppc_frame* call)
{
    return carbon_memory_hold(call, 1);
}

static int carbon_NumToString(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t value = call->words[0];
    const bool negative = (value >> 31) != 0;
    uint32_t magnitude = negative ? -value : value;
    unsigned char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = '0' + magnitude % 10;
        magnitude /= 10;
    } while (magnitude);

    uint32_t address = call->words[1];
    unsigned char* text = address ? ROMlib_carbon_guest_memory(address, 1 + negative + count, 1) : NULL;
    if (!text)
        return 0;
    *text++ = count + negative;
    if (negative)
        *text++ = '-';
    while (count)
        *text++ = digits[--count];
    return 1;
}

static int carbon_StringToNum(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t address = call->words[0];
    uint32_t value = 0;
    if (address) {
        const unsigned char* text = ROMlib_carbon_guest_memory(address, 1, 0);
        if (!text)
            return 0;
        const unsigned count = text[0];
        if (count) {
            text = ROMlib_carbon_guest_memory(address, count + 1, 0);
            if (!text)
                return 0;
            /* Carbon uses only each byte's low nibble and wraps in 32 bits. */
            const bool negative = text[1] == '-';
            for (unsigned i = negative || text[1] == '+' ? 2 : 1; i <= count; ++i)
                value = value * 10 + (text[i] & 15);
            if (negative)
                value = -value;
        }
    }
    return cfp_write(call->words[1], value);
}

static int32_t carbon_fixed_clamp(int64_t scaled)
{
    return scaled > INT32_MAX ? INT32_MAX : scaled < INT32_MIN ? INT32_MIN : (int32_t)scaled;
}

static int32_t carbon_fixed_product(int32_t left, int32_t right, unsigned bits)
{
    const int64_t unit = INT64_C(1) << bits;
    const int64_t biased = (int64_t)left * right + unit / 2;
    /* Convert C's truncation to floor after adding the half-unit bias. */
    return carbon_fixed_clamp(biased / unit - (biased % unit < 0));
}

static int32_t carbon_fixed_quotient(int32_t left, int32_t right, unsigned bits)
{
    if (!right)
        return left < 0 ? INT32_MIN : INT32_MAX;
    int64_t scaled = (int64_t)left * (INT64_C(1) << bits);
    int64_t divisor = right;
    if (divisor < 0) {
        divisor = -divisor;
        scaled = -scaled;
    }
    const int64_t fraction = 2 * (scaled % divisor);
    /* The saved routines round to the nearest unit, with ties toward +∞. */
    return carbon_fixed_clamp(scaled / divisor + (fraction >= divisor) - (fraction < -divisor));
}

static int32_t carbon_FixMul(int32_t left, int32_t right)
{
    return carbon_fixed_product(left, right, 16);
}

static int32_t carbon_FracMul(int32_t left, int32_t right)
{
    return carbon_fixed_product(left, right, 30);
}

static int32_t carbon_FixDiv(int32_t left, int32_t right)
{
    return carbon_fixed_quotient(left, right, 16);
}

static int32_t carbon_FracDiv(int32_t left, int32_t right)
{
    return carbon_fixed_quotient(left, right, 30);
}

static int32_t carbon_FixRatio(int16_t left, int16_t right)
{
    return carbon_fixed_quotient(left, right, 16);
}

static int32_t carbon_fixed_shift(int32_t value, unsigned shift)
{
    const int64_t scaled = (int64_t)value * (UINT32_C(1) << shift);
    return carbon_fixed_clamp(scaled);
}

static int32_t carbon_fixed_round(int32_t value, unsigned shift)
{
    /* The original rounds halfway cases toward positive infinity. Biasing
       the input turns signed floor division into an unsigned right shift. */
    const uint64_t biased = (uint32_t)value ^ UINT32_C(0x80000000);
    return (int32_t)((biased + (UINT32_C(1) << (shift - 1))) >> shift) - (int32_t)(UINT32_C(0x80000000) >> shift);
}

static int32_t carbon_Long2Fix(int32_t value)
{
    return carbon_fixed_shift(value, 16);
}

static int32_t carbon_Fix2Frac(int32_t value)
{
    return carbon_fixed_shift(value, 14);
}

static int32_t carbon_Frac2Fix(int32_t value)
{
    return carbon_fixed_round(value, 14);
}

static int32_t carbon_Fix2Long(int32_t value)
{
    return carbon_fixed_round(value, 16);
}

static int16_t carbon_FixRound(int32_t value)
{
    const int32_t rounded = carbon_fixed_round(value, 16);
    return rounded > INT16_MAX ? INT16_MAX : (int16_t)rounded;
}

#endif
