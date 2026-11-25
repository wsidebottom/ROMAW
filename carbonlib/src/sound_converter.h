/*+===================================================================
  File:      sound_converter.h

  Summary:   Implements direct and callback-driven Carbon Sound Converter streams for PCM and MACE.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    Universal Interfaces 3.4.2 Sound.h and Executor's MACE decoder.

  Copyright and Legal notices: See COPYING and the notices in rsys/mace.h.
===================================================================+*/

#ifndef ROMAW_CARBON_SOUND_CONVERTER_H
#define ROMAW_CARBON_SOUND_CONVERTER_H

#include "cf_batch.h"
#include "rsys/mace.h"
#include <stdlib.h>

enum converter_sample_format {
    sample_unsigned8, sample_signed8, sample_big16, sample_little16, sample_mace3, sample_mace6
};

typedef struct {
    uint32_t rate, channels, stride;
    enum converter_sample_format format;
} sound_converter_format;

typedef struct {
    uint32_t signature;
    uint32_t address;
    sound_converter_format input, output;
    uint32_t phase;
    uint32_t pending, pending_size, pending_offset;
    int16_t previous[2];
    int active, have_previous, filling;
    MACEContext predictor;
} sound_converter;

enum {
    sound_converter_signature = 0x53434e56,
    sound_converter_bad_ref = -205,
    sound_converter_bad_format = -206,
    sound_no_rate_conversion = 2,
    sound_converter_did_not_fill = 1,
    sound_converter_has_leftover = 2,
};

static int sound_converter_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

static int sound_converter_fallback(const char* name, ROMlib_carbon_ppc_frame* call, int32_t status)
{
    ROMlib_carbon_reference original;
    if (ROMlib_carbon_load_reference("SoundLib", name, &original))
        return ROMlib_carbon_invoke_frame(original.function, call);
    return sound_converter_result(call, status);
}

static int sound_converter_allocate(uint32_t size, uint32_t* address)
{
    ROMlib_carbon_ppc_frame args = {0};
    args.words[0] = size;
    return ROMlib_carbon_call_register_vector(0x0478, 0x33932, 0xa31e, 0, &args, address);
}

static int sound_converter_dispose(uint32_t address)
{
    ROMlib_carbon_ppc_frame args = {0};
    uint32_t ignored;
    args.words[0] = address;
    return ROMlib_carbon_call_register_vector(0x047c, 0x133802, 0xa01f, 0, &args, &ignored);
}

static int sound_converter_clear_pending(sound_converter* converter)
{
    if (converter->pending && !sound_converter_dispose(converter->pending))
        return 0;
    converter->pending = converter->pending_size = converter->pending_offset = 0;
    return 1;
}

static int sound_converter_read_format(const unsigned char* data, int input, sound_converter_format* format)
{
    uint32_t words[4];
    memcpy(words, data, sizeof words);
    const uint32_t code = CL(words[1]), dimensions = CL(words[2]);
    const uint32_t bits = dimensions & UINT16_MAX;
    format->rate = CL(words[3]);
    format->channels = dimensions >> 16;
    if (format->channels < 1 || format->channels > 2 || !format->rate || (CL(words[0]) & ~sound_no_rate_conversion))
        return 0;
    if (code == TICK("raw ") || (code == TICK("NONE") && bits == 8)) {
        if (bits != 8)
            return 0;
        format->format = sample_unsigned8;
        format->stride = format->channels;
    }
    else if (code == TICK("twos") && bits == 8) {
        format->format = sample_signed8;
        format->stride = format->channels;
    }
    else if ((code == TICK("twos") || code == TICK("sowt") || code == TICK("NONE")) && bits == 16) {
        format->format = code == TICK("sowt") ? sample_little16 : sample_big16;
        format->stride = 2 * format->channels;
    }
    else if (input && (code == TICK("MAC3") || code == TICK("MAC6"))) {
        format->format = code == TICK("MAC3") ? sample_mace3 : sample_mace6;
        format->stride = (format->format == sample_mace3 ? 2 : 1) * format->channels;
    }
    else
        return 0;
    return 1;
}

static int carbon_SoundConverterOpen(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output_address = call->words[2];
    if (!call->words[0] || !call->words[1] || !output_address)
        return sound_converter_result(call, paramErr);
    const unsigned char* input = ROMlib_carbon_guest_memory(call->words[0], 28, 0);
    const unsigned char* output = ROMlib_carbon_guest_memory(call->words[1], 28, 0);
    if (!input || !output || !ROMlib_carbon_guest_memory(output_address, 4, 1))
        return 0;
    sound_converter initial = {0};
    if (!sound_converter_read_format(input, 1, &initial.input) ||
        !sound_converter_read_format(output, 0, &initial.output))
        return sound_converter_fallback("SoundConverterOpen", call, sound_converter_bad_format);
    uint32_t flags;
    memcpy(&flags, input, sizeof flags);
    uint32_t output_flags;
    memcpy(&output_flags, output, sizeof output_flags);
    if ((CL(flags | output_flags) & sound_no_rate_conversion) && initial.input.rate != initial.output.rate)
        return sound_converter_fallback("SoundConverterOpen", call, sound_converter_bad_format);

    if (!sound_converter_allocate(sizeof initial, &initial.address))
        return 0;
    if (!initial.address)
        return sound_converter_result(call, memFullErr);
    sound_converter* converter = ROMlib_carbon_guest_memory(initial.address, sizeof *converter, 1);
    if (!converter)
        return 0;
    initial.signature = sound_converter_signature;
    *converter = initial;
    return cfp_write(output_address, initial.address) && sound_converter_result(call, noErr);
}

static sound_converter* sound_converter_record(uint32_t address)
{
    if (address % _Alignof(sound_converter))
        return NULL;
    sound_converter* converter = address ? ROMlib_carbon_try_guest_memory(address, sizeof *converter, 1) : NULL;
    if (!converter || converter->signature != sound_converter_signature || converter->address != address)
        return NULL;
    const sound_converter_format* input = &converter->input;
    const sound_converter_format* output = &converter->output;
    if (!input->rate || !output->rate || converter->phase >= input->rate || input->channels < 1 ||
        input->channels > 2 || output->channels < 1 || output->channels > 2 || (unsigned)input->format > sample_mace6 ||
        (unsigned)output->format > sample_little16 ||
        input->stride !=
            input->channels * (input->format <= sample_signed8 || input->format == sample_mace6 ? 1 : 2) ||
        output->stride != output->channels * (output->format <= sample_signed8 ? 1 : 2))
        return NULL;
    if (converter->pending_offset > converter->pending_size || converter->pending_size % output->stride ||
        converter->pending_offset % output->stride || (!converter->pending != !converter->pending_size))
        return NULL;
    return converter;
}

static int carbon_SoundConverterClose(ROMlib_carbon_ppc_frame* call)
{
    sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterClose", call, sound_converter_bad_ref);
    if (converter->filling)
        return sound_converter_result(call, paramErr);
    if (!sound_converter_clear_pending(converter))
        return 0;
    converter->signature = 0;
    return sound_converter_dispose(call->words[0]) && sound_converter_result(call, noErr);
}

static int carbon_SoundConverterBeginConversion(ROMlib_carbon_ppc_frame* call)
{
    sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterBeginConversion", call, sound_converter_bad_ref);
    if (converter->filling)
        return sound_converter_result(call, paramErr);
    if (!sound_converter_clear_pending(converter))
        return 0;
    memset(&converter->predictor, 0, sizeof converter->predictor);
    converter->phase = 0;
    converter->have_previous = 0;
    converter->active = 1;
    return sound_converter_result(call, noErr);
}

static unsigned sound_converter_samples(const sound_converter* converter)
{
    return converter->input.format >= sample_mace3 ? 6 : 1;
}

static int carbon_SoundConverterGetBufferSizes(ROMlib_carbon_ppc_frame* call)
{
    const sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterGetBufferSizes", call, sound_converter_bad_ref);
    if (!call->words[2] || !call->words[3] || !call->words[4])
        return sound_converter_result(call, paramErr);
    const uint32_t frames = call->words[1] / converter->input.stride;
    const uint64_t samples = (uint64_t)frames * sound_converter_samples(converter);
    if (samples > UINT32_MAX)
        return sound_converter_result(call, paramErr);
    const uint64_t maximum = (samples * converter->output.rate + converter->input.rate - 1) / converter->input.rate;
    if (maximum > UINT32_MAX / converter->output.stride)
        return sound_converter_result(call, paramErr);
    return cfp_write(call->words[2], frames) && cfp_write(call->words[3], frames * converter->input.stride) &&
           cfp_write(call->words[4], maximum * converter->output.stride) && sound_converter_result(call, noErr);
}

static int16_t sound_read_pcm(const unsigned char* data, enum converter_sample_format format)
{
    if (format == sample_unsigned8)
        return ((int)data[0] - 128) * 256;
    if (format == sample_signed8)
        return ((int)(data[0] ^ 0x80) - 128) * 256;
    if (format == sample_little16)
        return (int16_t)((uint16_t)data[1] << 8 | data[0]);
    return (int16_t)((uint16_t)data[0] << 8 | data[1]);
}

static unsigned char* sound_write_pcm(unsigned char* data, enum converter_sample_format format, int16_t sample)
{
    if (format == sample_unsigned8) {
        *data++ = (uint16_t)((int32_t)sample + 32768) >> 8;
        return data;
    }
    if (format == sample_signed8) {
        *data++ = (uint16_t)sample >> 8;
        return data;
    }
    if (format == sample_little16) {
        *data++ = sample;
        *data++ = (uint16_t)sample >> 8;
    }
    else {
        *data++ = (uint16_t)sample >> 8;
        *data++ = sample;
    }
    return data;
}

static unsigned char* sound_converter_emit(sound_converter* converter, int16_t first, int16_t second,
                                           unsigned char* output)
{
    if (converter->output.channels == 1 && converter->input.channels == 2)
        first = ((int32_t)first + second) / 2;
    if (converter->input.channels == 1)
        second = first;
    const int16_t current[] = {first, second};
    if (!converter->have_previous)
        memcpy(converter->previous, current, sizeof current);
    uint64_t phase = (uint64_t)converter->phase + converter->output.rate;
    while (phase >= converter->input.rate) {
        phase -= converter->input.rate;
        /* Phase carries fractional timing across ConvertBuffer calls. */
        for (unsigned channel = 0; channel < converter->output.channels; ++channel) {
            const int32_t difference = (int32_t)current[channel] - converter->previous[channel];
            const int16_t sample = current[channel] - (int64_t)phase * difference / converter->output.rate;
            output = sound_write_pcm(output, converter->output.format, sample);
        }
    }
    converter->phase = phase;
    memcpy(converter->previous, current, sizeof current);
    converter->have_previous = 1;
    return output;
}

static int sound_converter_overlap(uint32_t first, uint32_t first_size, uint32_t second, uint32_t second_size)
{
    return first_size && second_size && (uint64_t)first + first_size > second && (uint64_t)second + second_size > first;
}

static int sound_converter_extent(const sound_converter* converter, uint32_t packets, uint32_t* input_bytes,
                                  uint32_t* output_bytes)
{
    const unsigned samples_per_packet = sound_converter_samples(converter);
    if (packets > UINT32_MAX / converter->input.stride || packets > UINT32_MAX / samples_per_packet)
        return 0;
    const uint64_t samples = (uint64_t)packets * samples_per_packet;
    const uint64_t frames = (samples * converter->output.rate + converter->phase) / converter->input.rate;
    if (frames > UINT32_MAX / converter->output.stride)
        return 0;
    *input_bytes = packets * converter->input.stride;
    *output_bytes = frames * converter->output.stride;
    return 1;
}

static int sound_converter_run(sound_converter* converter, const unsigned char* input, uint32_t packets,
                               unsigned char* output, int overlap)
{
    unsigned char* copied = NULL;
    if (overlap) {
        const uint32_t bytes = packets * converter->input.stride;
        copied = malloc(bytes);
        if (!copied)
            return memFullErr;
        memcpy(copied, input, bytes);
        input = copied;
    }
    for (uint32_t packet = 0; packet < packets; ++packet, input += converter->input.stride) {
        if (converter->input.format >= sample_mace3) {
            int16_t decoded[2][6] = {{0}};
            ROMlib_mace_decode_frame(&converter->predictor, input, converter->input.channels,
                                     converter->input.format == sample_mace3, decoded);
            for (unsigned sample = 0; sample < 6; ++sample)
                output = sound_converter_emit(converter, decoded[0][sample], decoded[1][sample], output);
        }
        else {
            const int16_t first = sound_read_pcm(input, converter->input.format);
            const int16_t second = converter->input.channels == 2
                                       ? sound_read_pcm(input + converter->input.stride / 2, converter->input.format)
                                       : first;
            output = sound_converter_emit(converter, first, second, output);
        }
    }
    free(copied);
    return noErr;
}

static int sound_converter_writable(const sound_converter* converter, uint32_t address, uint32_t size)
{
    return (!size || address) && !sound_converter_overlap(converter->address, sizeof *converter, address, size) &&
           !sound_converter_overlap(converter->pending, converter->pending_size, address, size);
}

static int carbon_SoundConverterConvertBuffer(ROMlib_carbon_ppc_frame* call)
{
    sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterConvertBuffer", call, sound_converter_bad_ref);
    const uint32_t packets = call->words[2];
    uint32_t input_bytes, output_bytes;
    if (!converter->active || converter->filling || converter->pending ||
        !sound_converter_extent(converter, packets, &input_bytes, &output_bytes) || (input_bytes && !call->words[1]) ||
        !sound_converter_writable(converter, call->words[3], output_bytes) ||
        !sound_converter_writable(converter, call->words[4], 4) ||
        !sound_converter_writable(converter, call->words[5], 4))
        return sound_converter_result(call, paramErr);
    const unsigned char* input = input_bytes ? ROMlib_carbon_guest_memory(call->words[1], input_bytes, 0) : NULL;
    unsigned char* output = output_bytes ? ROMlib_carbon_guest_memory(call->words[3], output_bytes, 1) : NULL;
    if ((input_bytes && !input) || (output_bytes && !output) || !ROMlib_carbon_guest_memory(call->words[4], 4, 1) ||
        !ROMlib_carbon_guest_memory(call->words[5], 4, 1))
        return 0;
    const int status =
        sound_converter_run(converter, input, packets, output,
                            sound_converter_overlap(call->words[1], input_bytes, call->words[3], output_bytes));
    if (status != noErr)
        return sound_converter_result(call, status);
    return cfp_write(call->words[4], output_bytes / converter->output.stride) &&
           cfp_write(call->words[5], output_bytes) && sound_converter_result(call, noErr);
}

static int sound_converter_drain(sound_converter* converter, unsigned char* output, uint32_t size, uint32_t* written)
{
    const uint32_t remaining = converter->pending_size - converter->pending_offset;
    *written = size < remaining ? size : remaining;
    if (!*written)
        return 1;
    const unsigned char* buffer = ROMlib_carbon_guest_memory(converter->pending, converter->pending_size, 0);
    if (!buffer)
        return 0;
    memcpy(output, buffer + converter->pending_offset, *written);
    converter->pending_offset += *written;
    return converter->pending_offset != converter->pending_size || sound_converter_clear_pending(converter);
}

static int sound_converter_fill(sound_converter* converter, ROMlib_carbon_ppc_frame* call, unsigned char* output,
                                uint32_t capacity, uint32_t* written, int* status)
{
    if (!sound_converter_drain(converter, output, capacity, written))
        return 0;
    while (*written < capacity) {
        _Alignas(8) uint32_t source_address = 0;
        ROMlib_carbon_ppc_frame callback = {0};
        callback.words[0] = US_TO_SYN68K(&source_address);
        callback.words[1] = call->words[2];
        uint32_t available;
        if (!ROMlib_carbon_invoke_pascal(call->words[1], 0x3d0, &callback, &available))
            return 0;
        if (!(uint8_t)available)
            break;
        if (!source_address) {
            *status = paramErr;
            break;
        }
        uint32_t record[7];
        const void* descriptor = ROMlib_carbon_guest_memory(CL(source_address), sizeof record, 0);
        if (!descriptor)
            return 0;
        memcpy(record, descriptor, sizeof record);
        sound_converter_format input_format;
        const uint32_t packets = CL(record[4]), input_address = CL(record[5]);
        uint32_t input_bytes, output_bytes;
        if (!sound_converter_read_format((const unsigned char*)record, 1, &input_format) ||
            input_format.format != converter->input.format || input_format.channels != converter->input.channels ||
            input_format.rate != converter->input.rate) {
            *status = sound_converter_bad_format;
            break;
        }
        if (!packets)
            break;
        if (!input_address || packets > INT32_MAX ||
            !sound_converter_extent(converter, packets, &input_bytes, &output_bytes)) {
            *status = paramErr;
            break;
        }
        const unsigned char* input = ROMlib_carbon_guest_memory(input_address, input_bytes, 0);
        if (!input)
            return 0;
        const uint32_t space = capacity - *written;
        if (output_bytes <= space) {
            const int overlap =
                sound_converter_overlap(input_address, input_bytes, call->words[3] + *written, output_bytes);
            *status = sound_converter_run(converter, input, packets, output + *written, overlap);
            if (*status != noErr)
                break;
            *written += output_bytes;
            continue;
        }

        /* The callback owns its buffer. Save converted bytes before calling it again. */
        uint32_t address;
        if (output_bytes > INT32_MAX) {
            *status = paramErr;
            break;
        }
        if (!sound_converter_allocate(output_bytes, &address))
            return 0;
        if (!address) {
            *status = memFullErr;
            break;
        }
        unsigned char* pending = ROMlib_carbon_guest_memory(address, output_bytes, 1);
        if (!pending) {
            sound_converter_dispose(address);
            return 0;
        }
        sound_converter_run(converter, input, packets, pending, 0);
        memcpy(output + *written, pending, space);
        converter->pending = address;
        converter->pending_size = output_bytes;
        converter->pending_offset = space;
        *written += space;
    }
    return 1;
}

static int carbon_SoundConverterFillBuffer(ROMlib_carbon_ppc_frame* call)
{
    sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterFillBuffer", call, sound_converter_bad_ref);
    if (!converter->active || converter->filling || !call->words[1] ||
        !sound_converter_writable(converter, call->words[3], call->words[4]))
        return sound_converter_result(call, paramErr);
    for (unsigned i = 5; i < 8; ++i) {
        if (!sound_converter_writable(converter, call->words[i], 4))
            return sound_converter_result(call, paramErr);
        if (!ROMlib_carbon_guest_memory(call->words[i], 4, 1))
            return 0;
    }
    const uint32_t capacity = call->words[4] - call->words[4] % converter->output.stride;
    unsigned char* output = capacity ? ROMlib_carbon_guest_memory(call->words[3], capacity, 1) : NULL;
    if (capacity && !output)
        return 0;
    uint32_t written = 0;
    int status = noErr;
    converter->filling = 1;
    const int ok = sound_converter_fill(converter, call, output, capacity, &written, &status);
    converter->filling = 0;
    const uint32_t flags = (written < call->words[4] ? sound_converter_did_not_fill : 0) |
                           (written == capacity && converter->pending ? sound_converter_has_leftover : 0);
    return ok && cfp_write(call->words[5], written) && cfp_write(call->words[6], written / converter->output.stride) &&
           cfp_write(call->words[7], flags) && sound_converter_result(call, status);
}

static int carbon_SoundConverterEndConversion(ROMlib_carbon_ppc_frame* call)
{
    sound_converter* converter = sound_converter_record(call->words[0]);
    if (!converter)
        return sound_converter_fallback("SoundConverterEndConversion", call, sound_converter_bad_ref);
    const uint32_t size = converter->pending_size - converter->pending_offset;
    if (!converter->active || converter->filling || !sound_converter_writable(converter, call->words[1], size) ||
        !sound_converter_writable(converter, call->words[2], 4) ||
        !sound_converter_writable(converter, call->words[3], 4))
        return sound_converter_result(call, paramErr);
    unsigned char* output = size ? ROMlib_carbon_guest_memory(call->words[1], size, 1) : NULL;
    uint32_t written;
    if ((size && !output) || !ROMlib_carbon_guest_memory(call->words[2], 4, 1) ||
        !ROMlib_carbon_guest_memory(call->words[3], 4, 1) ||
        !sound_converter_drain(converter, output, size, &written) ||
        !cfp_write(call->words[2], written / converter->output.stride) || !cfp_write(call->words[3], written))
        return 0;
    converter->active = 0;
    return sound_converter_result(call, noErr);
}

#endif
