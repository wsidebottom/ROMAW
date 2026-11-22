/*+===================================================================
  File:      mace_test.c

  Summary:   Golden PCM hashes from FFmpeg 6.1.1 decoding deterministic AIFF-C inputs.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Golden PCM hashes from FFmpeg 6.1.1 decoding deterministic AIFF-C inputs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../include/rsys/mace.h"

static uint32_t checksum(const uint8_t* p, size_t n)
{
    uint32_t hash = 2166136261U;
    while (n--)
        hash = (hash ^ *p++) * 16777619U;
    return hash;
}

int main(int argc, char** argv)
{
    uint8_t input[2048], stereo[4096], output[12290], mono[12288];
    uint32_t seed = 0x12345678;
    unsigned i, mace3;
    assert(argc == 1 || argc == 3);
    for (i = 0; i < sizeof input; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        input[i] = seed;
    }
    for (mace3 = 0; mace3 <= 1; ++mace3) {
        size_t n = sizeof input * (mace3 ? 3 : 6);
        unsigned packet_bytes = mace3 ? 2 : 1;
        memset(output, 0x5A, sizeof output);
        assert(ROMlib_mace_decode(input, sizeof input, 1, mace3, output + 1, n));
        assert(output[0] == 0x5A && output[n + 1] == 0x5A);
        assert(checksum(output + 1, n) == (mace3 ? 0x5AF088ABU : 0x13EBA7F1U));
        memcpy(mono, output + 1, n);
        if (argc == 3) {
            FILE* reference = fopen(argv[mace3 ? 1 : 2], "wb");
            assert(reference && fwrite(mono, 1, n, reference) == n);
            assert(!fclose(reference));
        }
        for (i = 0; i < sizeof input / packet_bytes; ++i) {
            memcpy(stereo + i * packet_bytes * 2, input + i * packet_bytes, packet_bytes);
            memcpy(stereo + (i * 2 + 1) * packet_bytes, input + i * packet_bytes, packet_bytes);
        }
        assert(ROMlib_mace_decode(stereo, sizeof stereo, 2, mace3, output, n));
        assert(!memcmp(output, mono, n));
        for (unsigned channels = 1; channels <= 2; ++channels)
            for (unsigned chunk = 1; chunk <= 513; chunk = chunk * 8 + 1) {
                MACEContext context = {0};
                const uint8_t* source = channels == 1 ? input : stereo;
                size_t packets = sizeof input / packet_bytes;
                size_t packet = 0;
                memset(output, 0x5a, sizeof output);
                while (packet < packets) {
                    size_t count = packets - packet < chunk ? packets - packet : chunk;
                    assert(ROMlib_mace_decode_packets(&context, source + packet * packet_bytes * channels,
                                                      count * packet_bytes * channels, channels, mace3,
                                                      output + 1 + packet * 6, count * 6));
                    packet += count;
                }
                assert(output[0] == 0x5a && output[n + 1] == 0x5a);
                assert(!memcmp(output + 1, mono, n));
            }
        assert(!ROMlib_mace_decode(input, sizeof input, 1, mace3, output, n - 1));
        assert(!ROMlib_mace_decode(input, sizeof input, 3, mace3, output, n));
    }
    assert(!ROMlib_mace_decode(input, sizeof input - 1, 1, 1, output, sizeof output));
    puts("MACE 3:1/6:1 reference PCM, stereo downmix and bounds tests passed");
    return 0;
}
