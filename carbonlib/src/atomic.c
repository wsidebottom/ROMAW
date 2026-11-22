/*+===================================================================
  File:      atomic.c

  Summary:   Native equivalents of Carbon's word and byte atomic helpers. A single lock also covers overlapping and
             unaligned words without typed unaligned loads. ABI/behavior reference: local CarbonLib 1.4 disassembly;
             narrow return types checked against Apple's Mac OS X 10.4u SDK DriverSynchronization.h.

  Classes:   None.

  Functions: ROMlib_carbon_add_atomic, ROMlib_carbon_add_atomic16, ROMlib_carbon_add_atomic8,
             ROMlib_carbon_bit_and_atomic, ROMlib_carbon_bit_and_atomic16, ROMlib_carbon_bit_and_atomic8,
             ROMlib_carbon_bit_or_atomic, ROMlib_carbon_bit_or_atomic16, ROMlib_carbon_bit_or_atomic8,
             ROMlib_carbon_bit_xor_atomic, ROMlib_carbon_bit_xor_atomic16, ROMlib_carbon_bit_xor_atomic8,
             ROMlib_carbon_compare_and_swap, ROMlib_carbon_decrement_atomic, ROMlib_carbon_decrement_atomic16,
             ROMlib_carbon_decrement_atomic8, ROMlib_carbon_increment_atomic, ROMlib_carbon_increment_atomic16,
             ROMlib_carbon_increment_atomic8, ROMlib_carbon_test_and_clear, ROMlib_carbon_test_and_set

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native equivalents of Carbon's word and byte atomic helpers. A single lock
   also covers overlapping and unaligned words without typed unaligned loads.
   ABI/behavior reference: local CarbonLib 1.4 disassembly; narrow return types
   checked against Apple's Mac OS X 10.4u SDK DriverSynchronization.h. */
#include "internal.h"
#include <stdatomic.h>

static atomic_flag atomic_lock = ATOMIC_FLAG_INIT;

static void enter_atomic(void)
{
    while (atomic_flag_test_and_set_explicit(&atomic_lock, memory_order_seq_cst))
        ;
}

static void leave_atomic(void)
{
    atomic_flag_clear_explicit(&atomic_lock, memory_order_seq_cst);
}

enum atomic_operation { carbon_add, carbon_and, carbon_or, carbon_xor };

static uint32 change_number(volatile void* value, unsigned size, uint32 operand, enum atomic_operation operation)
{
    if (!value)
        return 0;
    uint32 previous, next;
    enter_atomic();
    if (size == 1)
        previous = *(const volatile Byte*)value;
    else if (size == 2) {
        uint16 encoded;
        memcpy(&encoded, (const void*)value, sizeof encoded);
        previous = (uint16)CW(encoded);
    }
    else {
        uint32 encoded;
        memcpy(&encoded, (const void*)value, sizeof encoded);
        previous = CL(encoded);
    }
    switch (operation) {
    case carbon_add:
        next = previous + operand;
        break;
    case carbon_and:
        next = previous & operand;
        break;
    case carbon_or:
        next = previous | operand;
        break;
    default:
        next = previous ^ operand;
        break;
    }
    if (size == 1)
        *(volatile Byte*)value = (Byte)next;
    else if (size == 2) {
        uint16 encoded = CW((uint16)next);
        memcpy((void*)value, &encoded, sizeof encoded);
    }
    else {
        uint32 encoded = CL(next);
        memcpy((void*)value, &encoded, sizeof encoded);
    }
    leave_atomic();
    return previous;
}

LONGINT ROMlib_carbon_increment_atomic(volatile LONGINT* value)
{
    return (LONGINT)change_number(value, 4, 1, carbon_add);
}

LONGINT ROMlib_carbon_decrement_atomic(volatile LONGINT* value)
{
    return (LONGINT)change_number(value, 4, UINT32_MAX, carbon_add);
}

static BOOLEAN change_bit(uint32 bit, volatile Byte* value, BOOLEAN set)
{
    if (!value)
        return FALSE;
    Byte mask = 0x80u >> (bit & 7);
    enter_atomic();
    Byte previous = *value;
    *value = set ? previous | mask : previous & ~mask;
    leave_atomic();
    return (previous & mask) != 0;
}

BOOLEAN ROMlib_carbon_test_and_set(uint32 bit, volatile Byte* value)
{
    return change_bit(bit, value, TRUE);
}

BOOLEAN ROMlib_carbon_test_and_clear(uint32 bit, volatile Byte* value)
{
    return change_bit(bit, value, FALSE);
}

LONGINT ROMlib_carbon_add_atomic(LONGINT amount, volatile LONGINT* value)
{
    return (LONGINT)change_number(value, 4, (uint32)amount, carbon_add);
}

LONGINT ROMlib_carbon_add_atomic8(LONGINT amount, volatile int8* value)
{
    return (int8)change_number(value, 1, (uint32)amount, carbon_add);
}

LONGINT ROMlib_carbon_add_atomic16(LONGINT amount, volatile INTEGER* value)
{
    return (INTEGER)change_number(value, 2, (uint32)amount, carbon_add);
}

LONGINT ROMlib_carbon_increment_atomic8(volatile int8* value)
{
    return ROMlib_carbon_add_atomic8(1, value);
}

LONGINT ROMlib_carbon_decrement_atomic8(volatile int8* value)
{
    return ROMlib_carbon_add_atomic8(-1, value);
}

LONGINT ROMlib_carbon_increment_atomic16(volatile INTEGER* value)
{
    return ROMlib_carbon_add_atomic16(1, value);
}

LONGINT ROMlib_carbon_decrement_atomic16(volatile INTEGER* value)
{
    return ROMlib_carbon_add_atomic16(-1, value);
}

uint32 ROMlib_carbon_bit_and_atomic(uint32 mask, volatile uint32* value)
{
    return change_number(value, 4, mask, carbon_and);
}

uint32 ROMlib_carbon_bit_or_atomic(uint32 mask, volatile uint32* value)
{
    return change_number(value, 4, mask, carbon_or);
}

uint32 ROMlib_carbon_bit_xor_atomic(uint32 mask, volatile uint32* value)
{
    return change_number(value, 4, mask, carbon_xor);
}

uint32 ROMlib_carbon_bit_and_atomic8(uint32 mask, volatile Byte* value)
{
    return change_number(value, 1, mask, carbon_and);
}

uint32 ROMlib_carbon_bit_or_atomic8(uint32 mask, volatile Byte* value)
{
    return change_number(value, 1, mask, carbon_or);
}

uint32 ROMlib_carbon_bit_xor_atomic8(uint32 mask, volatile Byte* value)
{
    return change_number(value, 1, mask, carbon_xor);
}

uint32 ROMlib_carbon_bit_and_atomic16(uint32 mask, volatile uint16* value)
{
    return change_number(value, 2, mask, carbon_and);
}

uint32 ROMlib_carbon_bit_or_atomic16(uint32 mask, volatile uint16* value)
{
    return change_number(value, 2, mask, carbon_or);
}

uint32 ROMlib_carbon_bit_xor_atomic16(uint32 mask, volatile uint16* value)
{
    return change_number(value, 2, mask, carbon_xor);
}

BOOLEAN ROMlib_carbon_compare_and_swap(uint32 expected, uint32 replacement, volatile uint32* value)
{
    if (!value)
        return FALSE;
    uint32 encoded;
    enter_atomic();
    memcpy(&encoded, (const void*)value, sizeof encoded);
    BOOLEAN matched = CL(encoded) == expected;
    if (matched) {
        encoded = CL(replacement);
        memcpy((void*)value, &encoded, sizeof encoded);
    }
    leave_atomic();
    return matched;
}
