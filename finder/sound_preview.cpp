/* Preview Macintosh sounds using the same disk parsers as the Sound Manager. */
#include "finder/sound_preview.h"
#include "rsys/aiff.h"
#include "rsys/mace.h"
#include "rsys/sound_resource.h"

#include <cstring>
#include <mmsystem.h>
#include <stdexcept>
#include <utility>

namespace finder {
namespace {
// WinMM continues to read the memory buffer after PlaySound returns.
Bytes playing;
constexpr size_t max_pcm_bytes = 64u * 1024 * 1024;

uint32_t little_endian_word(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}

void write_word(Bytes& bytes, size_t offset, uint32_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = value >> (8 * i);
}

int read_audio_bytes(void* context, uint32_t offset, void* output, uint32_t length)
{
    const auto& bytes = *static_cast<const Bytes*>(context);
    if (offset > bytes.size() || length > bytes.size() - offset)
        return 0;
    std::memcpy(output, bytes.data() + offset, length);
    return 1;
}

// Keep stereo PCM intact. The existing MACE decoder supplies mono 8-bit PCM.
Bytes pcm_wave(const uint8_t* input, size_t input_size, uint32_t frames, uint32_t fixed_rate,
               unsigned channels, unsigned bits, unsigned compression, bool signed_bytes, bool little_endian)
{
    uint64_t samples = compression ? uint64_t(frames) : uint64_t(frames) * channels * (bits / 8);
    uint32_t rate = uint32_t((uint64_t(fixed_rate) + 0x8000) >> 16);
    if (!rate || !samples || samples > max_pcm_bytes)
        throw std::runtime_error("The sound has an invalid sample rate, is empty, or is too large to preview.");
    if (!compression && samples > input_size)
        throw std::runtime_error("The sound's sample data is truncated.");
    unsigned wave_channels = compression ? 1 : channels;
    unsigned wave_bits = compression ? 8 : bits;
    unsigned alignment = wave_channels * (wave_bits / 8);
    constexpr size_t header = 44;
    Bytes wave(header + size_t(samples) + (samples & 1));
    std::memcpy(wave.data(), "RIFF", 4);
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    std::memcpy(wave.data() + 36, "data", 4);
    write_word(wave, 4, uint32_t(wave.size() - 8), 4);
    write_word(wave, 16, 16, 4);
    write_word(wave, 20, WAVE_FORMAT_PCM, 2);
    write_word(wave, 22, wave_channels, 2);
    write_word(wave, 24, rate, 4);
    write_word(wave, 28, rate * alignment, 4);
    write_word(wave, 32, alignment, 2);
    write_word(wave, 34, wave_bits, 2);
    write_word(wave, 40, uint32_t(samples), 4);
    auto* output = wave.data() + header;
    if (compression) {
        if (!ROMlib_mace_decode(input, input_size, channels, compression == 3, output, size_t(samples)))
            throw std::runtime_error("The MACE sound data is invalid.");
    }
    else if (bits == 16 && !little_endian) {
        for (size_t i = 0; i < samples; i += 2) {
            output[i] = input[i + 1];
            output[i + 1] = input[i];
        }
    }
    else if (bits == 8 && signed_bytes) {
        for (size_t i = 0; i < samples; ++i)
            output[i] = input[i] ^ 0x80;
    }
    else
        std::memcpy(output, input, size_t(samples));
    return wave;
}

Bytes sound_wave(Bytes bytes)
{
    if (bytes.size() >= 12 && !std::memcmp(bytes.data(), "RIFF", 4) && !std::memcmp(bytes.data() + 8, "WAVE", 4)) {
        size_t end = size_t(little_endian_word(bytes.data() + 4)) + 8;
        if (end < 12 || end > bytes.size())
            throw std::runtime_error("The WAVE file is truncated.");
        for (size_t offset = 12; offset < end;) {
            if (end - offset < 8)
                throw std::runtime_error("The WAVE file has an incomplete chunk.");
            size_t length = little_endian_word(bytes.data() + offset + 4);
            offset += 8;
            if (length > end - offset)
                throw std::runtime_error("The WAVE file has an incomplete chunk.");
            offset += length;
            if ((length & 1) && offset < end)
                ++offset;
        }
        bytes.resize(end);
        return bytes;
    }
    if (bytes.size() >= 12 && !std::memcmp(bytes.data(), "FORM", 4)) {
        aiff_info info{};
        if (aiff_parse(read_audio_bytes, &bytes, uint32_t(bytes.size()), &info))
            throw std::runtime_error("This AIFF sound is damaged or uses an unsupported encoding.");
        return pcm_wave(bytes.data() + info.offset, info.input_size, info.frames, info.rate, info.channels, info.bits,
                        info.compression, true, info.little_endian);
    }
    resource_sound info{};
    if (resource_sound_parse(bytes.data(), bytes.size(), &info))
        throw std::runtime_error("This Macintosh sound is damaged or uses an unsupported encoding.");
    return pcm_wave(bytes.data() + info.offset, info.input_size, info.frames, info.rate, info.channels, info.bits,
                    info.compression, false, false);
}
}

void play_sound_preview(Bytes bytes)
{
    auto wave = sound_wave(std::move(bytes));
    if (!PlaySoundW(reinterpret_cast<LPCWSTR>(wave.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT))
        throw std::runtime_error("Windows could not play this sound.");
    playing.swap(wave);
}

void stop_sound_preview()
{
    if (!playing.empty() && PlaySoundW(nullptr, nullptr, 0))
        Bytes{}.swap(playing);
}

}
