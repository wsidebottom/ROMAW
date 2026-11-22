/*+===================================================================
  File:      sound_input.c

  Summary:   Implements Carbon Sound Input for Windows PCM capture.

  Classes:   None.

  Functions: ROMlib_carbon_sound_input, ROMlib_carbon_sound_input_poll,
             ROMlib_carbon_sound_input_reset.

  Origin:    Inside Macintosh: Sound, Universal Interfaces 3.4.2 Sound.h,
             and the Windows waveform input API.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "sound_input.h"
#include <windows.h>
#include <mmreg.h>
#include <mmeapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOUND_CODE(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

enum {
    noErr = 0,
    unimpErr = -4,
    paramErr = -50,
    permErr = -54,
    memFullErr = -108,
    input_no_hardware = -220,
    input_bad_index = -221,
    input_no_buffer = -222,
    input_bad_compression = -223,
    input_bad_rate = -225,
    input_bad_size = -226,
    input_busy = -227,
    input_bad_name = -228,
    input_bad_ref = -229,
    input_device_error = -230,
    input_unknown_info = -231,
    input_unknown_quality = -232,
    input_aborted = -27,
    max_recording = 64 * 1024 * 1024,
    input_idle = 0,
    input_running = 1,
    input_paused = 2,
    spb_ref = 0,
    spb_count = 4,
    spb_ms = 8,
    spb_length = 12,
    spb_buffer = 16,
    spb_completion = 20,
    spb_interrupt = 24,
    spb_error = 32,
    spb_extent = 38
};

typedef struct sound_input_device {
    uint32_t ref, rate, channels, bits;
    UINT index;
    int writable, signed_bytes, state, asynchronous, meter_enabled, last_error;
    uint16_t meter;
    uint32_t block, buffer, completion, limit, recorded;
    HWAVEIN capture;
    HANDLE event;
    WAVEHDR header;
    uint8_t* audio;
    ULONGLONG test_ready_at;
} sound_input_device;

static sound_input_device devices[32];
static uint32_t next_ref;
static int test_input, polling;

static uint32_t read_word(const uint8_t* data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
}

static void write_value(uint8_t* data, uint32_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        data[i] = value >> (8 * (size - i - 1));
}

static uint8_t* span(uint32_t address, unsigned size, int write)
{
    return address ? ROMlib_carbon_try_guest_memory(address, size, write) : NULL;
}

static UINT device_count(void)
{
    return test_input ? 1 : waveInGetNumDevs();
}

static int device_name(UINT index, uint8_t name[256])
{
    WAVEINCAPSA caps;
    const char* text = "ROMAW Test Input";
    size_t capacity = strlen(text);
    if (!test_input) {
        if (waveInGetDevCapsA(index, &caps, sizeof caps) != MMSYSERR_NOERROR)
            return 0;
        text = caps.szPname;
        capacity = sizeof caps.szPname;
    }
    size_t length = 0;
    while (length < capacity && text[length])
        ++length;
    name[0] = length;
    memcpy(name + 1, text, length);
    return 1;
}

static sound_input_device* find_device(uint32_t ref)
{
    for (unsigned i = 0; ref && i < ARRAYSIZE(devices); ++i)
        if (devices[i].ref == ref)
            return &devices[i];
    return NULL;
}

static uint32_t frame_size(const sound_input_device* device)
{
    return device->channels * device->bits / 8;
}

static uint32_t milliseconds(const sound_input_device* device, uint32_t bytes)
{
    return (uint64_t)(bytes / frame_size(device)) * 1000 / device->rate;
}

static WAVEFORMATEX input_format(uint32_t rate, unsigned channels, unsigned bits)
{
    const WORD sample_frame = channels * bits / 8;
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, channels, rate, rate * sample_frame, sample_frame, bits, 0};
    return format;
}

static int supports_format(UINT index, uint32_t rate, unsigned channels, unsigned bits)
{
    WAVEFORMATEX format = input_format(rate, channels, bits);
    return test_input || waveInOpen(NULL, index, &format, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR;
}

static const struct {
    uint32_t name, rate;
} quality_rates[] = {
    {SOUND_CODE('g', 'o', 'o', 'd'), 8000},
    {SOUND_CODE('b', 'e', 't', 'r'), 16000},
    {SOUND_CODE('b', 'e', 's', 't'), 44100},
};

static int open_device(const uint32_t* args)
{
    uint8_t* ref = span(args[2], 4, 1);
    const uint8_t* name = args[0] ? span(args[0], 1, 0) : NULL;
    if (!ref || (args[0] && !name) || (uint16_t)args[1] > 1)
        return paramErr;
    uint32_t name_size = name ? *name : 0;
    if (name_size && !(name = span(args[0], name_size + 1, 0)))
        return paramErr;
    const UINT count = device_count();
    if (!count)
        return input_no_hardware;
    UINT index = 0;
    if (name_size) {
        uint8_t candidate[256];
        while (index < count && (!device_name(index, candidate) || candidate[0] != name_size ||
                                 memcmp(candidate + 1, name + 1, name_size)))
            ++index;
        if (index == count)
            return input_bad_name;
    }
    sound_input_device* free_slot = NULL;
    for (unsigned i = 0; i < ARRAYSIZE(devices); ++i) {
        sound_input_device* device = &devices[i];
        if (!device->ref && !device->capture && !device->audio && !free_slot)
            free_slot = device;
        if (device->ref && device->index == index && device->writable && (uint16_t)args[1])
            return input_busy;
    }
    if (!free_slot || next_ref == INT32_MAX)
        return memFullErr;
    const uint32_t rates[] = {22050, 44100, 48000, 11025};
    for (unsigned i = 0; i < ARRAYSIZE(rates); ++i) {
        if (supports_format(index, rates[i], 1, 16)) {
            *free_slot = (sound_input_device){.ref = ++next_ref,
                                              .rate = rates[i],
                                              .channels = 1,
                                              .bits = 16,
                                              .index = index,
                                              .writable = (uint16_t)args[1] != 0};
            write_value(ref, free_slot->ref, 4);
            return noErr;
        }
    }
    return input_bad_rate;
}

static int indexed_device(const uint32_t* args)
{
    const uint16_t index = args[0];
    uint8_t name[256];
    if (!device_count())
        return input_no_hardware;
    if (!index || index > device_count() || !device_name(index - 1, name))
        return input_bad_index;
    uint8_t* output = span(args[1], name[0] + 1, 1);
    uint8_t* icon = span(args[2], 4, 1);
    if (!output || (args[2] && !icon))
        return paramErr;
    memcpy(output, name, name[0] + 1);
    if (icon)
        write_value(icon, 0, 4);
    return noErr;
}

static int device_info(sound_input_device* device, uint32_t selector, uint32_t value_address, int setting)
{
    uint32_t value = 0;
    unsigned size = 2;
    switch (selector) {
    case SOUND_CODE('s', 'r', 'a', 't'):
        value = device->rate << 16;
        size = 4;
        break;
    case SOUND_CODE('s', 's', 'i', 'z'):
        value = device->bits;
        break;
    case SOUND_CODE('c', 'h', 'a', 'n'):
        value = device->channels;
        break;
    case SOUND_CODE('c', 'o', 'm', 'p'):
        value = SOUND_CODE('N', 'O', 'N', 'E');
        size = 4;
        break;
    case SOUND_CODE('q', 'u', 'a', 'l'):
        value = SOUND_CODE('n', 'o', 'n', 'e');
        for (unsigned i = 0; i < ARRAYSIZE(quality_rates); ++i)
            if (device->rate == quality_rates[i].rate && device->channels == 1 && device->bits == 16)
                value = quality_rates[i].name;
        size = 4;
        break;
    case SOUND_CODE('t', 'w', 'o', 's'):
        value = device->bits == 16 || device->signed_bytes;
        break;
    case SOUND_CODE('l', 'm', 'e', 't'):
        value = device->meter_enabled;
        if (!setting)
            size = 4;
        break;
    case SOUND_CODE('a', 's', 'y', 'n'):
    case SOUND_CODE('d', 'c', 'o', 'n'):
        value = 1;
        break;
    case SOUND_CODE('c', 'o', 'n', 't'):
        break;
    case SOUND_CODE('i', 'c', 'o', 'n'):
        size = 4;
        break;
    case SOUND_CODE('n', 'a', 'm', 'e'): {
        uint8_t name[256];
        if (setting)
            return input_unknown_info;
        if (!device_name(device->index, name))
            return input_device_error;
        uint8_t* output = span(value_address, name[0] + 1, 1);
        if (!output)
            return paramErr;
        memcpy(output, name, name[0] + 1);
        return noErr;
    }
    default:
        return input_unknown_info;
    }
    uint8_t* info = span(value_address, size, !setting);
    if (!info)
        return paramErr;
    if (!setting) {
        if (selector == SOUND_CODE('l', 'm', 'e', 't')) {
            write_value(info, value, 2);
            write_value(info + 2, value ? device->meter : 0, 2);
        }
        else
            write_value(info, value, size);
        return noErr;
    }
    if (!device->writable)
        return permErr;
    if (device->state != input_idle)
        return input_busy;
    value = size == 4 ? read_word(info) : (uint32_t)info[0] << 8 | info[1];
    switch (selector) {
    case SOUND_CODE('s', 'r', 'a', 't'):
        if ((value & 0xffff) || !(value >> 16) ||
            !supports_format(device->index, value >> 16, device->channels, device->bits))
            return input_bad_rate;
        device->rate = value >> 16;
        break;
    case SOUND_CODE('s', 's', 'i', 'z'):
        if ((value != 8 && value != 16) || !supports_format(device->index, device->rate, device->channels, value))
            return input_bad_size;
        device->bits = value;
        break;
    case SOUND_CODE('c', 'h', 'a', 'n'):
        if ((value != 1 && value != 2) || !supports_format(device->index, device->rate, value, device->bits))
            return paramErr;
        device->channels = value;
        break;
    case SOUND_CODE('c', 'o', 'm', 'p'):
        return value == SOUND_CODE('N', 'O', 'N', 'E') ? noErr : input_bad_compression;
    case SOUND_CODE('q', 'u', 'a', 'l'):
        for (unsigned i = 0; i < ARRAYSIZE(quality_rates); ++i) {
            if (quality_rates[i].name == value) {
                if (!supports_format(device->index, quality_rates[i].rate, 1, 16))
                    return input_bad_rate;
                device->rate = quality_rates[i].rate;
                device->channels = 1;
                device->bits = 16;
                return noErr;
            }
        }
        return input_unknown_quality;
    case SOUND_CODE('t', 'w', 'o', 's'):
        device->signed_bytes = value != 0;
        break;
    case SOUND_CODE('l', 'm', 'e', 't'):
        device->meter_enabled = value != 0;
        break;
    default:
        return input_unknown_info;
    }
    return noErr;
}

static int release_capture(sound_input_device* device)
{
    if (device->capture) {
        if (waveInReset(device->capture) != MMSYSERR_NOERROR)
            return 0;
        if ((device->header.dwFlags & WHDR_PREPARED) &&
            waveInUnprepareHeader(device->capture, &device->header, sizeof device->header) != MMSYSERR_NOERROR)
            return 0;
        if (waveInClose(device->capture) != MMSYSERR_NOERROR)
            return 0;
        device->capture = NULL;
    }
    if (device->event) {
        CloseHandle(device->event);
        device->event = NULL;
    }
    free(device->audio);
    device->audio = NULL;
    memset(&device->header, 0, sizeof device->header);
    return 1;
}

static int finish_recording(sound_input_device* device, int status)
{
    const uint32_t sample_frame = frame_size(device);
    uint32_t count = test_input ? (status ? 0 : device->limit) : device->recorded + device->header.dwBytesRecorded;
    count = min(count, device->limit);
    count -= count % sample_frame;
    uint8_t* block = span(device->block, spb_extent, 1);
    uint8_t* output = count ? span(device->buffer, count, 1) : NULL;
    if (!block || (count && !output))
        status = paramErr;
    const int have_samples = !status || status == input_aborted;
    device->recorded = have_samples ? count : 0;
    device->meter = 0;
    if (have_samples) {
        unsigned peak = 0;
        const unsigned width = device->bits / 8;
        for (uint32_t i = 0; i < count; i += width) {
            if (width == 2) {
                const uint16_t sample = (uint16_t)device->audio[i + 1] << 8 | device->audio[i];
                output[i] = sample >> 8;
                output[i + 1] = sample;
                peak = max(peak, (unsigned)abs((int16_t)sample));
            }
            else {
                const uint8_t sample = device->audio[i];
                output[i] = sample ^ (device->signed_bytes ? 0x80 : 0);
                peak = max(peak, (unsigned)abs((int)sample - 128) << 8);
            }
        }
        device->meter = min(255, peak >> 7);
    }
    if (!release_capture(device))
        status = input_device_error;
    const uint32_t parameter = device->block, completion = device->asynchronous ? device->completion : 0;
    device->state = input_idle;
    device->last_error = status;
    if (block) {
        write_value(block + spb_count, device->recorded, 4);
        write_value(block + spb_ms, milliseconds(device, device->recorded), 4);
        write_value(block + spb_error, status, 2);
        if (completion) {
            void* procedure = ROMlib_carbon_guest_memory(completion, 2, 0);
            if (procedure)
                ROMlib_carbon_invoke_callback(procedure, &parameter, 1, NULL);
        }
    }
    return status;
}

static int queue_input(sound_input_device* device, uint32_t limit)
{
    device->header.lpData = (LPSTR)(device->audio + device->recorded);
    device->header.dwBufferLength = limit - device->recorded;
    return waveInPrepareHeader(device->capture, &device->header, sizeof device->header) == MMSYSERR_NOERROR &&
           waveInAddBuffer(device->capture, &device->header, sizeof device->header) == MMSYSERR_NOERROR;
}

static int recording_ready(const sound_input_device* device)
{
    return test_input ? !device->asynchronous || GetTickCount64() >= device->test_ready_at
                      : ((const volatile WAVEHDR*)&device->header)->dwFlags & WHDR_DONE;
}

static int record_input(const uint32_t* args)
{
    uint8_t* block = span(args[0], spb_extent, 1);
    if (!block)
        return paramErr;
    sound_input_device* device = find_device(read_word(block + spb_ref));
    if (!device)
        return input_bad_ref;
    if (!device->writable)
        return permErr;
    if (device->state != input_idle || device->capture)
        return input_busy;
    if (read_word(block + spb_interrupt))
        return unimpErr;
    const uint32_t sample_frame = frame_size(device);
    const uint32_t length = read_word(block + spb_length), buffer = read_word(block + spb_buffer);
    const uint64_t duration = ((uint64_t)read_word(block + spb_ms) * device->rate + 999) / 1000 * sample_frame;
    const uint64_t requested = max(duration, read_word(block + spb_count));
    uint32_t limit = requested && requested < length ? requested : length;
    limit -= limit % sample_frame;
    if (!buffer || !limit)
        return input_no_buffer;
    if (limit > max_recording || !span(buffer, limit, 1))
        return paramErr;
    device->audio = malloc(limit);
    if (!device->audio)
        return memFullErr;
    device->recorded = 0;
    device->last_error = noErr;
    int status = noErr;
    if (test_input) {
        for (uint32_t i = 0; i < limit; ++i)
            device->audio[i] = 0x12 + (i & 1) * 0x22;
    }
    else {
        const WAVEFORMATEX format = input_format(device->rate, device->channels, device->bits);
        device->event = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!device->event ||
            waveInOpen(&device->capture, device->index, &format, (DWORD_PTR)device->event, 0, CALLBACK_EVENT) !=
                MMSYSERR_NOERROR ||
            !queue_input(device, limit) || waveInStart(device->capture) != MMSYSERR_NOERROR)
            status = input_device_error;
    }
    if (status) {
        release_capture(device);
        write_value(block + spb_error, status, 2);
        return status;
    }
    device->block = args[0];
    device->buffer = buffer;
    device->completion = read_word(block + spb_completion);
    device->limit = limit;
    device->asynchronous = (uint8_t)args[1] != 0;
    device->test_ready_at = test_input ? GetTickCount64() + 2000 : 0;
    device->state = input_running;
    write_value(block + spb_error, 1, 2);
    if (device->asynchronous)
        return noErr;
    const ULONGLONG deadline = GetTickCount64() + milliseconds(device, limit) + 5000;
    while (!recording_ready(device)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline ||
            WaitForSingleObject(device->event, (DWORD)min(deadline - now, MAXDWORD - 1)) != WAIT_OBJECT_0)
            return finish_recording(device, input_device_error);
    }
    return finish_recording(device, noErr);
}

static int input_recording_status(sound_input_device* device, const uint32_t* args)
{
    uint32_t count = device->recorded;
    if (device->capture && device->state != input_idle) {
        MMTIME position = {.wType = TIME_BYTES};
        if (waveInGetPosition(device->capture, &position, sizeof position) == MMSYSERR_NOERROR &&
            position.wType == TIME_BYTES)
            count = max(count, min(position.u.cb, device->limit));
    }
    const uint32_t sample_frame = frame_size(device);
    const uint32_t values[] = {device->state ? device->state : device->last_error,
                               device->meter_enabled ? device->meter : 0,
                               device->limit / sample_frame,
                               count / sample_frame,
                               milliseconds(device, device->limit),
                               milliseconds(device, count)};
    uint8_t* outputs[ARRAYSIZE(values)] = {0};
    for (unsigned i = 0; i < ARRAYSIZE(values); ++i)
        if (args[i + 1] && !(outputs[i] = span(args[i + 1], i < 2 ? 2 : 4, 1)))
            return paramErr;
    for (unsigned i = 0; i < ARRAYSIZE(values); ++i)
        if (outputs[i])
            write_value(outputs[i], values[i], i < 2 ? 2 : 4);
    return noErr;
}

static int input_operation(enum sound_input_operation operation, const uint32_t* args)
{
    if (operation == input_index)
        return indexed_device(args);
    if (operation == input_open)
        return open_device(args);
    if (operation == input_record)
        return record_input(args);
    sound_input_device* device = find_device(args[0]);
    if (!device)
        return input_bad_ref;
    switch (operation) {
    case input_close:
        if (device->state != input_idle)
            return input_busy;
        if (!release_capture(device))
            return input_device_error;
        memset(device, 0, sizeof *device);
        return noErr;
    case input_get:
    case input_set:
        return device_info(device, args[1], args[2], operation == input_set);
    case input_status:
        return input_recording_status(device, args);
    case input_stop:
        if (device->state == input_idle)
            return noErr;
        if (device->capture && waveInReset(device->capture) != MMSYSERR_NOERROR)
            return input_device_error;
        return finish_recording(device, input_aborted) == input_aborted ? noErr : input_device_error;
    case input_pause:
        if (device->state != input_running)
            return noErr;
        if (device->capture) {
            if (waveInStop(device->capture) != MMSYSERR_NOERROR ||
                waveInUnprepareHeader(device->capture, &device->header, sizeof device->header) != MMSYSERR_NOERROR)
                return finish_recording(device, input_device_error);
            const uint32_t count = min(device->header.dwBytesRecorded, device->limit - device->recorded);
            device->recorded += count - count % frame_size(device);
            memset(&device->header, 0, sizeof device->header);
        }
        device->state = input_paused;
        return noErr;
    case input_resume:
        if (device->state != input_paused)
            return noErr;
        if (device->recorded == device->limit)
            return finish_recording(device, noErr);
        if (device->capture &&
            (!queue_input(device, device->limit) || waveInStart(device->capture) != MMSYSERR_NOERROR))
            return finish_recording(device, input_device_error);
        device->state = input_running;
        return noErr;
    case input_to_bytes:
    case input_to_ms: {
        uint8_t* output = span(args[1], 4, 1);
        if (!output)
            return paramErr;
        const uint32_t value = read_word(output);
        const uint64_t converted = operation == input_to_bytes
                                       ? ((uint64_t)value * device->rate / 1000) * frame_size(device)
                                       : milliseconds(device, value);
        if (converted > UINT32_MAX)
            return paramErr;
        write_value(output, converted, 4);
        return noErr;
    }
    default:
        return paramErr;
    }
}

int ROMlib_carbon_sound_input(ROMlib_carbon_ppc_frame* call, enum sound_input_operation operation)
{
    const int status = input_operation(operation, call->words);
    if (status < 0 && operation == input_record) {
        uint8_t* block = span(call->words[0], spb_extent, 1);
        if (block)
            write_value(block + spb_error, status, 2);
    }
    call->words[0] = status;
    return 1;
}

void ROMlib_carbon_sound_input_poll(void)
{
    if (polling)
        return;
    polling = 1;
    for (unsigned i = 0; i < ARRAYSIZE(devices); ++i)
        if (devices[i].state == input_running && recording_ready(&devices[i]))
            finish_recording(&devices[i], noErr);
    polling = 0;
}

void ROMlib_carbon_sound_input_reset(int testing)
{
    for (unsigned i = 0; i < ARRAYSIZE(devices); ++i) {
        sound_input_device* device = &devices[i];
        /* A driver can still own the host buffer after a failed reset. Keep
           that storage reserved and invalidate the guest reference anyway. */
        device->ref = 0;
        device->state = input_idle;
        if (release_capture(device))
            memset(device, 0, sizeof *device);
    }
    test_input = testing;
}
