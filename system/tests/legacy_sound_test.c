/*+===================================================================
  File:      legacy_sound_test.c

  Summary:   Real legacy synthesizers and I/O lifecycle, without using host audio.

  Classes:   None.

  Functions: ROMlib_ppc_completion, _warning, hash_lookup_code_and_create_if_needed, interpret_code, main,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Real legacy synthesizers and I/O lifecycle, without using host audio. */
#define NDEBUG 1
#include "rsys/common.h"
#include "rsys/blockinterrupts.h"
static void service_sound(void);
#undef check_virtual_interrupt
#define check_virtual_interrupt() service_sound()
#include "../../sound/legacy.c"
#undef NDEBUG
#undef main
#include <assert.h>
CPUState cpu_state;
uint16* syn68k_callback_address_space;
#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 ROMlib_guest_memory_size = 65536;
uint32 syn68k_address_mask = 0xffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;
int ROMlib_SND_RATE = 22255;
int ROMlib_PretendSound = soundon;
sound_driver_t sound_driver;
static int starts, completions, waits;

void* syn68k_guest_to_host(uint32 address)
{
    return fixture_guest_to_host(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return fixture_host_to_guest(address);
}

void _warning(int code, const char* kind, const char* file, int line, const char* function, const char* format, ...)
{
}

int ROMlib_ppc_completion(void* code, void* parameter)
{
    assert((uintptr_t)code == ROMlib_offset + 0x5000);
    assert(CW(((ParmBlkPtr)parameter)->ioParam.ioResult) == EM_D0);
    assert(EM_A0 == fixture_host_to_guest((uintptr_t)parameter));
    ++completions;
    return 1;
}

/* The PPC completion interception above always handles the callback. */
const uint16* hash_lookup_code_and_create_if_needed(syn68k_addr_t address)
{
    abort();
}

void interpret_code(const uint16* code)
{
    abort();
}

static void go(sound_driver_t* driver)
{
    ++starts;
}

static void service_sound(void)
{
    ++waits;
    struct hunger_info info = {.t2 = 0, .t3 = 1024};
    ROMlib_legacy_sound_mix(info);
}

static void prepare(ParmBlkPtr pb, uint32 address, int32 length)
{
    memset(pb, 0, sizeof *pb);
    PP_RAW(pb->ioParam.ioBuffer) = CL(address);
    pb->ioParam.ioReqCount = CL(length);
    PP_RAW(pb->ioParam.ioCompletion) = CLC(0x5000);
}

int main(void)
{
    uint8 *memory = calloc(1, 65536), output[512];
    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    SdVolume = 7;
    sound_driver.sound_go = go;
    ParmBlkPtr pb = (ParmBlkPtr)(memory + 0x3000);
    ParmBlkPtr other = (ParmBlkPtr)(memory + 0x3100);
    uint8* sound = memory + 0x1000;
    store_long(sound + 2, 0x10000);
    sound[6] = 0;
    sound[7] = 255;
    sound[8] = 128;
    sound[9] = 64;
    prepare(pb, 0x80001000, 10);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    assert(CW(pb->ioParam.ioResult) == 1 && SoundActive == soundactiveinplay);
    assert(starts == 1 && !completions);
    memset(output, 128, sizeof output);
    struct hunger_info info = {.t2 = 0, .t3 = 8, .buf = output, .bufsize = sizeof output};
    assert(ROMlib_legacy_sound_mix(info));
    assert(output[0] == 0 && output[1] == 255 && output[2] == 128 && output[3] == 64);
    assert(output[4] == 128 && !SoundActive && completions == 1);
    assert(!pb->ioParam.ioResult && CL(pb->ioParam.ioActCount) == 10);
    assert(!ROMlib_legacy_sound_mix(info) && completions == 1);
    store_long(sound + 2, 0x8000);
    prepare(pb, 0x1000, 10);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    memset(output, 128, sizeof output);
    info.t3 = 16;
    ROMlib_legacy_sound_mix(info);
    assert(output[0] == 0 && output[1] == 0 && output[2] == 255 && output[3] == 255);
    prepare(pb, 0x1000, 10);
    assert(!ROMlib_legacy_sound_write(pb, FALSE) && waits == 1);
    assert(!pb->ioParam.ioResult);
    prepare(pb, 0x1000, 10);
    prepare(other, 0x1000, 10);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    assert(!ROMlib_legacy_sound_write(other, TRUE));
    int before = completions;
    ROMlib_legacy_sound_reset(TRUE);
    assert(completions == before + 2 && CW(pb->ioParam.ioResult) == abortErr);
    assert(CW(other->ioParam.ioResult) == abortErr && !SoundActive);
    /* Four-tone: 256-byte table, live data, exact finite duration, pointer guards. */
    sound[1] = 1;
    store_long(sound + 2, 0x2000);
    uint8* record = memory + 0x2000;
    record[1] = 1;
    store_long(record + 2, 0x10000);
    store_long(record + 34, 0x4000);
    for (unsigned i = 0; i < 256; ++i)
        memory[0x4000 + i] = i & 1 ? 255 : 0;
    prepare(pb, 0x1000, 6);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    info.t3 = 400;
    memset(output, 128, sizeof output);
    ROMlib_legacy_sound_mix(info);
    assert(output[0] == 96 && output[1] == 159);
    assert(!word(record) && !SoundActive && !pb->ioParam.ioResult);
    record[0] = record[1] = 255;
    store_long(record + 2, 0);
    store_long(record + 6, 0);
    prepare(pb, 0x1000, 6);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    info.t3 = 1;
    memset(output, 128, sizeof output);
    ROMlib_legacy_sound_mix(info);
    assert(output[0] == 96 && SoundActive);
    memory[0x4000] = 255;
    memset(output, 128, sizeof output);
    ROMlib_legacy_sound_mix(info);
    assert(output[0] == 159);
    record[0] = record[1] = 0;
    ROMlib_legacy_sound_mix(info);
    assert(!head && !SoundActive);
    store_long(record + 34, 0xffff);
    prepare(pb, 0x1000, 6);
    assert(ROMlib_legacy_sound_write(pb, TRUE) == paramErr && !head);
    prepare(pb, 0x1000, -1);
    assert(ROMlib_legacy_sound_write(pb, TRUE) == paramErr);
    prepare(pb, 0xfffe, 10);
    assert(ROMlib_legacy_sound_write(pb, TRUE) == paramErr);
    sound[1] = 0;
    store_long(sound + 2, 0);
    prepare(pb, 0x1000, 10);
    assert(ROMlib_legacy_sound_write(pb, TRUE) == paramErr);
    store_long(sound + 2, 0x10000);
    prepare(pb, 0x1000, 10);
    assert(!ROMlib_legacy_sound_write(pb, TRUE));
    head->pb = (ParmBlkPtr)1;
    before = completions;
    ROMlib_legacy_sound_reset(FALSE);
    assert(!head && !SoundActive && completions == before);
    free(memory);
    puts(
        "Legacy Sound: free-form rates, four-tone live waveforms/duration, 24-bit pointers, async/sync completion, KillIO, guards and launch cleanup passed");
}
