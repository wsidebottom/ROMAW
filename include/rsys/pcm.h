/*+===================================================================
  File:      pcm.h

  Summary:   ifndef RSYS_PCM_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#ifndef RSYS_PCM_H
#define RSYS_PCM_H

#include <stddef.h>
#include <stdint.h>

/* Extended Macintosh PCM is unsigned at 8 bits and signed big-endian at
   16 bits. Convert whole frames to the mixer's unsigned mono samples. */
static int ROMlib_pcm_decode(const uint8_t* input, size_t input_size, unsigned channels, unsigned bits, uint8_t* output,
                             size_t frames)
{
    size_t stride, frame_index;
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16))
        return 0;
    stride = channels * (bits / 8);
    if (frames > input_size / stride || (frames && (!input || !output)))
        return 0;
    for (frame_index = 0; frame_index < frames; ++frame_index) {
        unsigned channel;
        int sum = 0;
        for (channel = 0; channel < channels; ++channel) {
            if (bits == 8)
                sum += *input++ * 256;
            else {
                unsigned sample = ((unsigned)input[0] << 8) | input[1];
                sum += sample ^ 0x8000;
                input += 2;
            }
        }
        output[frame_index] = sum / (channels * 256);
    }
    return 1;
}

#endif
