/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYN68K_CACHE040_H
#define SYN68K_CACHE040_H

#include <stdint.h>

enum Syn68kCache040Traffic {
    CACHE040_NORMAL,
    CACHE040_EXCEPTION,
    CACHE040_DESCRIPTOR,
    CACHE040_LOCKED,
    CACHE040_LOCKED_CONTINUE, /* A write chunk that does not end the RMW pair. */
    CACHE040_MOVE16           /* TT=1 for operand beats; a push still has TT/TM=0. */
};

enum Syn68kCache040CycleKind { CACHE040_OPERAND, CACHE040_PUSH };

/* Optional live interrupt inputs for interruptible cache maintenance. Level
   seven follows the CPU's pending-edge latch; other levels obey the SR mask. */
typedef struct {
    const volatile uint8_t* pending;
    const volatile uint16_t* sr;
} Syn68kCache040Interrupt;

typedef struct {
    uint32_t attributes;
    unsigned fc, burst, beat, way;
    enum Syn68kCache040CycleKind kind;
    enum Syn68kCache040Traffic traffic;
    unsigned locked, lock_end;
} Syn68kCache040Cycle;

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
    unsigned active;                    /* A physical transaction owns this bank pair. */
    enum Syn68kCache040Traffic traffic; /* Active invocation, cleared on return. */
    unsigned bypass_data_miss;          /* Disabled-cache locked-hit rule, UM 4.3.3. */
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
    /* Preferred callbacks preserve traffic, push and lock intent. A legacy
       callback is used only when the corresponding cycle callback is NULL. */
    int (*read_cycle)(void*, uint32_t, unsigned char*, unsigned, const Syn68kCache040Cycle*, unsigned*);
    int (*write_cycle)(void*, uint32_t, const unsigned char*, unsigned, const Syn68kCache040Cycle*);
} Syn68kCache040Bus;

enum Syn68kCache040Fault {
    CACHE040_OK,
    CACHE040_BAD_REQUEST,
    CACHE040_OPERAND_FAULT,
    CACHE040_FILL_FAULT,
    CACHE040_PUSH_FAULT,
    CACHE040_DESCRIPTOR_LOCKUP
};

typedef struct {
    enum Syn68kCache040Fault fault;
    uint32_t bus_address;
    /* Width-limited write operand, or the successfully completed read prefix
       in low bits and memory order. Incomplete internal fill data is excluded. */
    uint32_t value;
    /* Successful operand bytes, including a CB store committed to its new
       line before a victim-push failure. WT counts completed external beats;
       its cache update can already be visible when the external write fails. */
    unsigned completed;
    /* Push failures retain the complete memory-addressed line for format-7
       delivery. A single dirty word uses size 4; otherwise size 16. */
    uint32_t push_address;
    unsigned char push_data[16];
    unsigned push_size, push_word;
    Syn68kCache040Cycle cycle; /* Valid for operand/fill/push bus faults. */
    unsigned interrupt_level;  /* Maintenance stopped before instruction retirement. */
} Syn68kCache040Access;

void syn68k_cache040_init(Syn68kCache040* cache);
/* One-to-four-byte physical chunks, including three-byte MMU page tails.
   CM is taken from attributes bits 6:5; CACR bit
   31 enables data and bit 15 instruction caching. no_allocate retains normal
   hit behavior but prevents allocation on a miss (exception/table traffic).
   Snoop transactions are not yet bound. MOVE16 has a separate line API. */
Syn68kCache040Access syn68k_cache040_access(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t address, unsigned fc,
                                            unsigned size, int writing, uint32_t value, uint32_t attributes,
                                            uint32_t cacr, int no_allocate);
/* Exception traffic never allocates on a miss. Descriptors use WT/no-allocate
   and must be aligned longwords. A cached descriptor with DE clear reports the
   documented lockup without issuing a bus cycle. Locked operands force CI/
   serialized transfers and push/invalidate a matching line even with DE clear.
   LOCKED_CONTINUE suppresses LOCKE for a nonfinal physical write chunk.
   Caller/bus ownership still supplies atomicity across a complete RMW pair. */
Syn68kCache040Access syn68k_cache040_transfer(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t address, unsigned fc,
                                              unsigned size, int writing, uint32_t value, uint32_t attributes,
                                              uint32_t cacr, int no_allocate, enum Syn68kCache040Traffic traffic);
/* One nonallocating MOVE16 half. Low EA bits 1:0 are ignored; bits 3:2
   select the first longword of the wrapped burst. bytes are in memory order.
   Reads publish only after the entire line succeeds; writes latch all input
   before callbacks. A write hit invalidates without pushing. Failed writes
   retain the whole line in push_data for PD recovery, independently of their
   OPERAND fault kind. completed counts line bytes; cycle retains the last bus
   beat even on success. A cached read has no bus cycle (burst remains zero).
   A NULL cache explicitly selects an uncached physical line transaction. */
Syn68kCache040Access syn68k_cache040_move16(Syn68kCache040*, const Syn68kCache040Bus*, uint32_t address, unsigned fc,
                                            int writing, unsigned char bytes[16], uint32_t attributes, uint32_t cacr);
/* Physical CINV/CPUSH selection: caches 0=neither, 1=data, 2=instruction, 3=both;
   scope 1=line, 2=page, 3=all. page_shift is 12 or 13. Operations also affect
   disabled caches. A failed push retains captured data in the result; earlier
   successful pushes/invalidations are not rolled back. */
Syn68kCache040Access syn68k_cache040_maintain(Syn68kCache040*, const Syn68kCache040Bus*, unsigned caches,
                                              unsigned scope, uint32_t address, unsigned page_shift, int push);
Syn68kCache040Access syn68k_cache040_maintain_interruptible(Syn68kCache040*, const Syn68kCache040Bus*, unsigned caches,
                                                            unsigned scope, uint32_t address, unsigned page_shift,
                                                            int push, const Syn68kCache040Interrupt*);
/* LOCKED maintenance is preparation for an atomic descriptor update: data
   cache, line scope and push only. Its push cycles carry LOCK without LOCKE;
   the caller performs and completes the following atomic descriptor update. */
Syn68kCache040Access syn68k_cache040_maintain_traffic(Syn68kCache040*, const Syn68kCache040Bus*, unsigned caches,
                                                      unsigned scope, uint32_t address, unsigned page_shift, int push,
                                                      uint32_t cacr, enum Syn68kCache040Traffic traffic);

#endif
