/* Preview Macintosh sounds using the same disk parsers as the Sound Manager. */
#include "finder/sound_preview.h"
#include "rsys/aiff.h"
#include "rsys/mace.h"
#include "rsys/sound_resource.h"

#include <algorithm>
#include <cmath>
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
Bytes pcm_wave(const uint8_t* input, size_t input_size, uint32_t frames, uint32_t fixed_rate, unsigned channels,
               unsigned bits, unsigned compression, bool signed_bytes, bool little_endian)
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

// System 7 dcmp 2 word table, documented by python-rsrcfork:
// https://github.com/dgelessus/python-rsrcfork/blob/master/src/rsrcfork/compress/dcmp2.py
Bytes unpack_sound_resource(Bytes bytes)
{
    if (bytes.size() < 4 || resource_long(bytes.data()) != 0xa89f6572)
        return bytes;
    static constexpr uint16_t default_words[] = {
        0x0000, 0x0008, 0x4eba, 0x206e, 0x4e75, 0x000c, 0x0004, 0x7000, 0x0010, 0x0002, 0x486e, 0xfffc, 0x6000, 0x0001,
        0x48e7, 0x2f2e, 0x4e56, 0x0006, 0x4e5e, 0x2f00, 0x6100, 0xfff8, 0x2f0b, 0xffff, 0x0014, 0x000a, 0x0018, 0x205f,
        0x000e, 0x2050, 0x3f3c, 0xfff4, 0x4cee, 0x302e, 0x6700, 0x4cdf, 0x266e, 0x0012, 0x001c, 0x4267, 0xfff0, 0x303c,
        0x2f0c, 0x0003, 0x4ed0, 0x0020, 0x7001, 0x0016, 0x2d40, 0x48c0, 0x2078, 0x7200, 0x588f, 0x6600, 0x4fef, 0x42a7,
        0x6706, 0xfffa, 0x558f, 0x286e, 0x3f00, 0xfffe, 0x2f3c, 0x6704, 0x598f, 0x206b, 0x0024, 0x201f, 0x41fa, 0x81e1,
        0x6604, 0x6708, 0x001a, 0x4eb9, 0x508f, 0x202e, 0x0007, 0x4eb0, 0xfff2, 0x3d40, 0x001e, 0x2068, 0x6606, 0xfff6,
        0x4ef9, 0x0800, 0x0c40, 0x3d7c, 0xffec, 0x0005, 0x203c, 0xffe8, 0xdefc, 0x4a2e, 0x0030, 0x0028, 0x2f08, 0x200b,
        0x6002, 0x426e, 0x2d48, 0x2053, 0x2040, 0x1800, 0x6004, 0x41ee, 0x2f28, 0x2f01, 0x670a, 0x4840, 0x2007, 0x6608,
        0x0118, 0x2f07, 0x3028, 0x3f2e, 0x302b, 0x226e, 0x2f2b, 0x002c, 0x670c, 0x225f, 0x6006, 0x00ff, 0x3007, 0xffee,
        0x5340, 0x0040, 0xffe4, 0x4a40, 0x660a, 0x000f, 0x4ead, 0x70ff, 0x22d8, 0x486b, 0x0022, 0x204b, 0x670e, 0x4aae,
        0x4e90, 0xffe0, 0xffc0, 0x002a, 0x2740, 0x6702, 0x51c8, 0x02b6, 0x487a, 0x2278, 0xb06e, 0xffe6, 0x0009, 0x322e,
        0x3e00, 0x4841, 0xffea, 0x43ee, 0x4e71, 0x7400, 0x2f2c, 0x206c, 0x003c, 0x0026, 0x0050, 0x1880, 0x301f, 0x2200,
        0x660c, 0xffda, 0x0038, 0x6602, 0x302c, 0x200c, 0x2d6e, 0x4240, 0xffe2, 0xa9f0, 0xff00, 0x377c, 0xe580, 0xffdc,
        0x4868, 0x594f, 0x0034, 0x3e1f, 0x6008, 0x2f06, 0xffde, 0x600a, 0x7002, 0x0032, 0xffcc, 0x0080, 0x2251, 0x101f,
        0x317c, 0xa029, 0xffd8, 0x5240, 0x0100, 0x6710, 0xa023, 0xffce, 0xffd4, 0x2006, 0x4878, 0x002e, 0x504f, 0x43fa,
        0x6712, 0x7600, 0x41e8, 0x4a6e, 0x20d9, 0x005a, 0x7fff, 0x51ca, 0x005c, 0x2e00, 0x0240, 0x48c7, 0x6714, 0x0c80,
        0x2e9f, 0xffd6, 0x8000, 0x1000, 0x4842, 0x4a6b, 0xffd2, 0x0048, 0x4a47, 0x4ed1, 0x206f, 0x0041, 0x600c, 0x2a78,
        0x422e, 0x3200, 0x6574, 0x6716, 0x0044, 0x486d, 0x2008, 0x486c, 0x0b7c, 0x2640, 0x0400, 0x0068, 0x206d, 0x000d,
        0x2a40, 0x000b, 0x003e, 0x0220,
    };
    const auto* data = bytes.data();
    if (bytes.size() < 18 || resource_word(data + 4) != 18 || resource_word(data + 6) != 0x0901 ||
        resource_word(data + 12) != 2 || data[17] > 3)
        throw std::runtime_error("Unsupported compressed Macintosh sound resource.");
    const size_t length = resource_long(data + 8);
    const unsigned flags = data[17];
    const unsigned count = flags & 1 ? unsigned(data[16]) + 1 : 256;
    size_t input = 18 + ((flags & 1) ? 2 * count : 0);
    auto invalid = [] { throw std::runtime_error("The compressed Macintosh sound resource is damaged."); };
    if (!length || length > max_pcm_bytes || input > bytes.size() || (!(flags & 1) && data[16]))
        invalid();
    Bytes output;
    output.reserve(length);
    unsigned tag = 0, bit = 0;
    while (output.size() < length) {
        if (input >= bytes.size())
            invalid();
        if (length - output.size() == 1 && bytes.size() - input == 1) {
            output.push_back(data[input++]);
            break;
        }
        if ((flags & 2) && !bit) {
            tag = data[input++];
            bit = 128;
        }
        if (input >= bytes.size())
            invalid();
        if (!(flags & 2) || (tag & bit)) {
            unsigned index = data[input++];
            if (index >= count || length - output.size() < 2)
                invalid();
            unsigned word = flags & 1 ? resource_word(data + 18 + index * 2) : default_words[index];
            output.push_back(word >> 8);
            output.push_back(word & 255);
        }
        else {
            size_t count = std::min<size_t>(2, length - output.size());
            if (bytes.size() - input < count)
                invalid();
            output.insert(output.end(), data + input, data + input + count);
            input += count;
        }
        bit >>= 1;
    }
    if (input != bytes.size())
        invalid();
    return output;
}

Bytes command_sound_wave(const Bytes& bytes)
{
    // These sequences use one format-1 square-wave or sampled-sound synthesizer.
    if (bytes.size() < 12 || resource_word(bytes.data()) != 1 || resource_word(bytes.data() + 2) != 1)
        return {};
    const unsigned synth = resource_word(bytes.data() + 4);
    const unsigned count = resource_word(bytes.data() + 10);
    const size_t end = 12 + 8 * size_t(count);
    if ((synth != 1 && synth != 5) || end > bytes.size())
        return {};
    constexpr unsigned rate = 22050;
    unsigned pitch = 69, gain = 255, timbre = 128;
    bool active = false;
    double phase = 0;
    const uint8_t* sample = nullptr;
    unsigned sample_length = 0, base_pitch = 60;
    double sample_rate = 0;
    unsigned loop_start = 0, loop_end = 0;
    Bytes pcm;
    for (size_t offset = 12; offset < end; offset += 8) {
        const auto* command = bytes.data() + offset;
        unsigned operation = resource_word(command);
        unsigned duration = resource_word(command + 2);
        uint32_t value = resource_long(command + 4);
        switch (operation) {
        case 0:
            continue; // nullCmd
        case 3:
            active = false;
            continue; // quietCmd
        case 42:
            pitch = value & 127;
            active = true;
            phase = 0;
            continue; // freqCmd
        case 43:
            gain = std::min(duration, 255u);
            continue; // ampCmd
        case 44:
            timbre = std::clamp(duration, 1u, 255u);
            continue;  // timbreCmd
        case 0x8050: { // soundCmd installs the sample used by subsequent notes.
            if (synth != 5 || value < end || value > bytes.size() || bytes.size() - value < 22)
                return {};
            const auto* header = bytes.data() + value;
            sample_length = resource_long(header + 4);
            if (resource_long(header) || header[20] || !sample_length || sample_length > bytes.size() - value - 22)
                return {};
            sample = header + 22;
            sample_rate = resource_long(header + 8) / 65536.0;
            base_pitch = header[21];
            loop_start = resource_long(header + 12);
            loop_end = resource_long(header + 16);
            if (!sample_rate)
                return {};
            continue;
        }
        case 40: // noteCmd
            pitch = value & 127;
            active = true;
            phase = 0;
            break;
        case 41:
            active = false;
            break; // restCmd
        case 10:
            break; // waitCmd sustains the current voice.
        default:
            return {};
        }
        if (synth == 5 && !sample)
            return {};
        const size_t frames = uint64_t(duration) * rate / 2000;
        if (frames > max_pcm_bytes - pcm.size())
            throw std::runtime_error("The sound is too large to preview.");
        const double step = synth == 1 ? 440.0 * std::exp2((int(pitch) - 69) / 12.0) / rate
                                       : sample_rate * std::exp2((int(pitch) - int(base_pitch)) / 12.0) / rate;
        for (size_t i = 0; i < frames; ++i) {
            int level = 0;
            if (active && synth == 1)
                level = phase * 256 < timbre ? 127 : -127;
            else if (active && phase < sample_length)
                level = int(sample[size_t(phase)]) - 128;
            pcm.push_back(uint8_t(128 + level * int(gain) / 255));
            if (active) {
                phase += step;
                if (synth == 1)
                    phase -= std::floor(phase);
                else if (loop_start < loop_end && loop_end <= sample_length && phase >= loop_end)
                    phase = loop_start + std::fmod(phase - loop_start, loop_end - loop_start);
            }
        }
    }
    if (pcm.empty())
        return {};
    return pcm_wave(pcm.data(), pcm.size(), uint32_t(pcm.size()), rate << 16, 1, 8, 0, false, false);
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
    bytes = unpack_sound_resource(std::move(bytes));
    resource_sound info{};
    if (resource_sound_parse(bytes.data(), bytes.size(), &info)) {
        auto wave = command_sound_wave(bytes);
        if (wave.empty())
            throw std::runtime_error("This Macintosh sound is damaged or uses an unsupported encoding.");
        return wave;
    }
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
