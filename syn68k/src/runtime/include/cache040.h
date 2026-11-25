/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_CACHE040_H
#define SYN68K_CACHE040_H

#include <stdint.h>

/* Functional physical L1 storage, MC68040UM chapter 4. Banks are data (0)
   and instruction (1). Initialization is a board operation, not CPU reset:
   reset disables caches without changing their contents. */
typedef struct {
    uint32_t address;
    unsigned char bytes[16];
    unsigned valid, dirty; /* One dirty bit per memory-addressed longword. */
} Syn68kCache040Line;

typedef struct {
    Syn68kCache040Line lines[2][64][4];
    unsigned replacement[2];
    unsigned active; /* A physical transaction owns this bank pair. */
} Syn68kCache040;

/* Whole-beat callbacks. burst is zero for scalar transfers, 16 for a line;
   beat is its transfer index and way identifies the selected data cache line.
   A first-read-beat inhibit response prevents allocation without truncating
   the burst. Later inhibit responses are ignored. Pushes use FC/TM zero.
   Callbacks must not recursively execute this same cache object. */
typedef struct {
    void* opaque;
    int (*read)(void*, uint32_t, unsigned char*, unsigned, uint32_t, unsigned, unsigned, unsigned, unsigned, unsigned*);
    int (*write)(void*, uint32_t, const unsigned char*, unsigned, uint32_t, unsigned, unsigned, unsigned, unsigned);
} Syn68kCache040Bus;

enum Syn68kCache040Fault {
    CACHE040_OK,
    CACHE040_BAD_REQUEST,
    CACHE040_OPERAND_FAULT,
    CACHE040_FILL_FAULT,
    CACHE040_PUSH_FAULT
};

typedef struct {
    enum Syn68kCache040Fault fault;
    uint32_t bus_address, value;
    /* Successful operand bytes, including a CB store committed to its new
       line before a victim-push failure. WT counts completed external beats;
       its cache update can already be visible when the external write fails. */
    unsigned completed;
    /* Push failures retain the complete memory-addressed line for format-7
       delivery. A single dirty word uses size 4; otherwise size 16. */
    uint32_t push_address;
    unsigned char push_data[16];
    unsigned push_size, push_word;
} Syn68kCache040Access;

void syn68k_cache040_init(Syn68kCache040* cache);
/* Scalar physical accesses. CM is taken from attributes bits 6:5; CACR bit
   31 enables data and bit 15 instruction caching. no_allocate retains normal
   hit behavior but prevents allocation on a miss (exception/table traffic).
   This component does not yet bind locked/MOVE16/snoop bus transactions. */
Syn68kCache040Access syn68k_cache040_access(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t address, unsigned fc,
                                            unsigned size, int writing, uint32_t value, uint32_t attributes,
                                            uint32_t cacr, int no_allocate);
/* Physical CINV/CPUSH selection: caches 0=neither, 1=data, 2=instruction, 3=both;
   scope 1=line, 2=page, 3=all. page_shift is 12 or 13. Operations also affect
   disabled caches. A failed push retains captured data in the result; earlier
   successful pushes/invalidations are not rolled back. */
Syn68kCache040Access syn68k_cache040_maintain(Syn68kCache040*, const Syn68kCache040Bus*, unsigned caches,
                                              unsigned scope, uint32_t address, unsigned page_shift, int push);

#endif
