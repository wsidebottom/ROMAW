/*+===================================================================
  File:      resource_map_eof_test.c

  Summary:   Rewriting a resource map must discard stale bytes past its new end.

  Classes:   None.

  Functions: FSWriteAll, SetEOF, SetFPos, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Rewriting a resource map must discard stale bytes past its new end. */
#define NDEBUG 1
#include "../../resources/resMod.c"
#undef NDEBUG
#undef main
#include <assert.h>
CPUState cpu_state;
uint32 ROMlib_offset, syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static unsigned char file_data[1024];
static LONGINT position, file_size;
static int seeks, writes, truncates, fail_seek, fail_write, fail_truncate;

void* syn68k_guest_to_host(uint32 p)
{
    return (void*)p;
}

uint32 syn68k_host_to_guest(ptr_sized_uint p)
{
    return p;
}

OSErr SetFPos(INTEGER ref, INTEGER mode, LONGINT offset)
{
    assert(ref == 7 && mode == fsFromStart);
    if (++seeks == fail_seek)
        return ioErr;
    position = offset;
    return noErr;
}

OSErr FSWriteAll(INTEGER ref, LONGINT* count, Ptr bytes)
{
    assert(ref == 7 && position >= 0 && position + *count <= sizeof file_data);
    if (++writes == fail_write)
        return ioErr;
    memcpy(file_data + position, bytes, *count);
    position += *count;
    return noErr;
}

OSErr SetEOF(INTEGER ref, LONGINT length)
{
    assert(ref == 7);
    ++truncates;
    if (fail_truncate)
        return ioErr;
    file_size = length;
    return noErr;
}

static void reset(resmap* record)
{
    memset(record, 0, sizeof *record);
    record->resfn = CWC(7);
    record->resfatr = CWC(mapChanged);
    record->rh.rmapoff = CLC(100);
    record->rh.maplen = CLC(sizeof *record);
    seeks = writes = truncates = fail_seek = fail_write = fail_truncate = 0;
    file_size = 1024;
}

int main(void)
{
    resmap record;
    HIDDEN_resmapptr master = {RM(&record)};
    resmaphand map = &master;
    reset(&record);
    assert(writemap(map) == noErr);
    assert(file_size == 100 + sizeof record && truncates == 1);
    assert(writes == 2 && !(CW(record.resfatr) & mapChanged));
    for (int i = 1; i <= 2; ++i) {
        reset(&record);
        fail_seek = i;
        assert(writemap(map) == ioErr && !truncates && file_size == 1024);
        assert(CW(record.resfatr) & mapChanged);
        reset(&record);
        fail_write = i;
        assert(writemap(map) == ioErr && !truncates && file_size == 1024);
        assert(CW(record.resfatr) & mapChanged);
    }
    reset(&record);
    fail_truncate = 1;
    assert(writemap(map) == ioErr && file_size == 1024);
    assert(CW(record.resfatr) & mapChanged);
    reset(&record);
    record.rh.rmapoff = CLC(INT32_MAX - 1);
    assert(writemap(map) == paramErr && !writes && !truncates);
    reset(&record);
    record.rh.maplen = CLC(-1);
    assert(writemap(map) == paramErr && !writes && !truncates);
    puts("Resource map EOF: shrinking forks, write/seek/truncate failures and overflow passed");
    return 0;
}
