/*+===================================================================
  File:      volume_geometry.h

  Summary:   Bounded allocation geometry for the legacy GetVolInfo interface. ifndef RSYS_VOLUME_GEOMETRY_H

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/include/rsys; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Bounded allocation geometry for the legacy GetVolInfo interface. */
#ifndef RSYS_VOLUME_GEOMETRY_H
#define RSYS_VOLUME_GEOMETRY_H

#include <stdint.h>

typedef struct {
    uint32_t block_size;
    uint16_t blocks;
    uint16_t free_blocks;
} ROMlib_volume_geometry;

static inline ROMlib_volume_geometry ROMlib_legacy_volume_geometry(uint64_t total_bytes, uint64_t free_bytes)
{
    /* The classic fields contain an unsigned 16-bit block count and a
     positive signed 32-bit block size. Their product must remain wide;
     clipping the byte count to 2 GB makes large host volumes appear empty. */
    const uint64_t maximum_block = UINT64_C(0x7ffffe00);
    const uint64_t maximum = UINT16_MAX * maximum_block;
    if (total_bytes > maximum)
        total_bytes = maximum;
    if (free_bytes > total_bytes)
        free_bytes = total_bytes;
    ROMlib_volume_geometry geometry;
    uint64_t block_size = (total_bytes + UINT16_MAX - 1) / UINT16_MAX;
    block_size = (block_size + 511) & ~UINT64_C(511);
    geometry.block_size = block_size < 512 ? 512 : block_size;
    geometry.blocks = total_bytes / geometry.block_size;
    geometry.free_blocks = free_bytes / geometry.block_size;
    return geometry;
}

#endif
