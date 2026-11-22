/*+===================================================================
  File:      alias_update_test.c

  Summary:   Alias outputs occupy four guest bytes, including beside a saved frame.

  Classes:   None.

  Functions: BlockMove, CToPascalCall, C_FSMakeFSSpec, C_GetString, DisposHandle, GetHandleSize, HGetState, HLock,
             HSetState, MemError, PBGetCatInfo, PBHGetVInfo, PtrAndHand, SetHandleSize, _NewHandle_flags, _warning,
             main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Alias outputs occupy four guest bytes, including beside a saved frame. */
#define NDEBUG 1
#ifndef ALIAS_IMPLEMENTATION
#define ALIAS_IMPLEMENTATION "../../files/alias.c"
#endif
#include ALIAS_IMPLEMENTATION
#undef main
#undef NDEBUG
#include <assert.h>

extern __declspec(dllimport) void* __stdcall VirtualAlloc(void*, size_t, unsigned long, unsigned long);
extern __declspec(dllimport) int __stdcall VirtualProtect(void*, size_t, unsigned long, unsigned long*);
extern __declspec(dllimport) int __stdcall VirtualFree(void*, size_t, unsigned long);

#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base, syn68k_host_alias_size = 0xffffffff;
static ptr_sized_uint pointers[64];
static unsigned pointer_count;
static HIDDEN_Ptr masters[4];
static unsigned next_handle;
static unsigned char payloads[4][2048];
static OSErr volume_error, file_error;
static boolean_t allocation_failure;
static Size sizes[4];
static SignedByte states[4];
static boolean_t resize_failure;
static int directory_target;
static OSErr resolution_error;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

intptr_t CToPascalCall(void* procedure, unsigned long convention, ...)
{
    (void)procedure;
    (void)convention;
    assert(!"Unexpected guest callback in native alias fixture");
    return 0;
}

void _warning(int error, const char* type, const char* file, int line, const char* function, const char* format, ...)
{
    (void)error;
    (void)type;
    (void)file;
    (void)line;
    (void)function;
    (void)format;
}

void* syn68k_guest_to_host(uint32 address)
{
    assert(address > 0 && address <= pointer_count);
    return (void*)pointers[address - 1];
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    for (unsigned i = 0; i < pointer_count; ++i)
        if (pointers[i] == address)
            return i + 1;
    assert(pointer_count < NELEM(pointers));
    pointers[pointer_count++] = address;
    return pointer_count;
}

Handle _NewHandle_flags(Size size, boolean_t system, boolean_t clear)
{
    MemErr = CW(allocation_failure ? memFullErr : noErr);
    if (allocation_failure)
        return NULL;
    assert(next_handle < 4 && size <= 2048);
    unsigned i = next_handle++;
    sizes[i] = size;
    HIDDEN_RAW_SET(masters[i], RM((Ptr)payloads[i]));
    return &masters[i];
}

OSErr MemError(void)
{
    return CW(MemErr);
}

Size GetHandleSize(Handle h)
{
    return sizes[h - masters];
}

SignedByte HGetState(Handle h)
{
    return states[h - masters];
}

void HSetState(Handle h, SignedByte state)
{
    states[h - masters] = state;
}

void HLock(Handle h)
{
    states[h - masters] |= 0x80;
}

void SetHandleSize(Handle h, Size size)
{
    assert(size <= 2048);
    MemErr = CW(resize_failure ? memFullErr : noErr);
    if (!resize_failure)
        sizes[h - masters] = size;
}

void DisposHandle(Handle h)
{
    assert(h >= masters && h < masters + 4);
}

void BlockMove(Ptr a, Ptr b, Size n)
{
    memmove(b, a, n);
}

OSErrRET PtrAndHand(Ptr p, Handle h, LONGINT n)
{
    Size old = GetHandleSize(h);
    SetHandleSize(h, old + n);
    if (!MemError())
        memcpy(STARH(h) + old, p, n);
    return MemError();
}

StringHandle C_GetString(INTEGER identifier)
{
    assert(identifier == -16413 || identifier == -16096);
    return NULL;
}

OSErrRET PBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->ioParam.ioNamePtr);
    assert(!async && (pb->ioParam.ioVRefNum == CWC(-5) || pb->ioParam.ioVRefNum == 0));
    if (name)
        memcpy(name, "\004Disk", 5);
    pb->volumeParam.ioVCrDate = CLC(0x12345678);
    pb->volumeParam.ioVSigWord = CWC(0x4244);
    pb->volumeParam.ioVRefNum = CWC(-5);
    return volume_error;
}

OSErrRET PBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->hFileInfo.ioNamePtr);
    assert(!async && pb->hFileInfo.ioVRefNum == CWC(-5));
    if (pb->hFileInfo.ioFDirIndex == CWC(-1)) {
        assert(pb->dirInfo.ioDrDirID == CLC(42));
        memcpy(name, "\006Parent", 7);
        pb->dirInfo.ioFlAttrib = ATTRIB_ISADIR;
        pb->dirInfo.ioDrParID = CLC(2);
        return file_error;
    }
    assert(pb->hFileInfo.ioDirID == CLC(42));
    assert(name[0] == 4 && !memcmp(name + 1, "File", 4));
    pb->hFileInfo.ioFlCrDat = CLC(0x12345679);
    pb->hFileInfo.ioFlFndrInfo.fdType = CLC(TICK("TEXT"));
    pb->hFileInfo.ioFlFndrInfo.fdCreator = CLC(TICK("ttxt"));
    pb->hFileInfo.ioDirID = CLC(77);
    pb->hFileInfo.ioFlParID = CLC(42);
    pb->hFileInfo.ioFlAttrib = directory_target ? ATTRIB_ISADIR : 0;
    return file_error;
}

OSErr C_FSMakeFSSpec(INTEGER volume, LONGINT parent, Str255 name, FSSpecPtr output)
{
    if (volume == 0)
        assert(!parent && (name[0] == 9 && !memcmp(name + 1, "Disk:File", 9) ||
                           name[0] == 16 && !memcmp(name + 1, "Disk:Parent:File", 16)));
    else
        assert(volume == -5 && parent == 42 && name[0] == 4 && !memcmp(name + 1, "File", 4));
    if (resolution_error)
        return resolution_error;
    output->vRefNum = CWC(-5);
    output->parID = CLC(42);
    memcpy(output->name, "\004File", 5);
    return noErr;
}

int main(void)
{
    void* low = calloc(1, 65536);
    ROMlib_offset = (uintptr_t)low;
    FSSpec target = {0};
    target.vRefNum = CWC(-5);
    target.parID = CLC(42);
    memcpy(target.name, "\004File", 5);
    for (int test = 0; test < 8; ++test) {
        int failure = test % 4;
        directory_target = test / 4;
        next_handle = 0;
        allocation_failure = resize_failure = FALSE;
        volume_error = file_error = noErr;
        HIDDEN_AliasHandle output;
        assert(!C_NewAliasMinimal(&target, &output));
        Handle h = HIDDEN_NATIVE(output);
        PtrAndHand((Ptr) "USER", h, 4);
        alias_head_t* head = (alias_head_t*)STARH(h);
        assert(head->zero_or_neg_one == CLC(42) && head->ioDirID == CLC(77));
        assert(CW(head->usually_0) == directory_target);
        head->type = CLC(0x12345678);
        head->fileName[1] = 'X';
        states[0] = 0x40;
        Size old_size = GetHandleSize(h);
        unsigned char saved[2048];
        memcpy(saved, STARH(h), old_size);
        allocation_failure = failure == 1;
        resize_failure = failure == 2;
        file_error = failure == 3 ? fnfErr : noErr;
        Boolean changed = TRUE;
        OSErr err = C_UpdateAlias(NULL, &target, h, &changed);
        if (failure) {
            assert(err == (failure == 3 ? fnfErr : memFullErr));
            assert(!changed && GetHandleSize(h) == old_size);
            assert(!memcmp(saved, STARH(h), old_size));
        }
        else {
            assert(!err && changed);
            assert(!memcmp(STARH(h) + GetHandleSize(h) - 4, "USER", 4));
            assert(((alias_head_t*)STARH(h))->type == CLC(0x12345678));
            next_handle = 1;
            assert(!C_UpdateAlias(NULL, &target, h, &changed) && !changed);
            FSSpec resolved;
            memset(&resolved, 0xa5, sizeof resolved);
            assert(!C_ResolveAlias(NULL, h, &resolved, &changed) && !changed);
            assert(resolved.vRefNum == target.vRefNum && resolved.parID == target.parID &&
                   !memcmp(resolved.name, target.name, 5));
            FSSpec saved = resolved;
            resolution_error = fnfErr;
            assert(C_ResolveAlias(NULL, h, &resolved, &changed) == fnfErr);
            assert(!changed && !memcmp(&saved, &resolved, sizeof saved));
            resolution_error = noErr;
            head = (alias_head_t*)STARH(h);
            head->zero_or_neg_one = 0;
            head->ioDirID = CLC(42);
            assert(!C_ResolveAlias(NULL, h, &resolved, &changed));
        }
        assert(states[0] == 0x40);
    }
    next_handle = 0;
    directory_target = 0;
    file_error = volume_error = noErr;
    HIDDEN_AliasHandle full;
    Str32 zone = {0};
    Str31 server = {0};
    assert(!C_NewAliasMinimalFromFullPath(9, (Ptr) "Disk:File", zone, server, &full));
    FSSpec resolved;
    Boolean changed = TRUE;
    assert(!C_ResolveAlias(NULL, HIDDEN_NATIVE(full), &resolved, &changed));
    assert(!changed && resolved.parID == CLC(42));
    assert(C_NewAliasMinimalFromFullPath(-1, (Ptr) "Disk:File", zone, server, &full) == paramErr);
    assert(!HIDDEN_RAW(full));
    assert(C_NewAliasMinimal(NULL, &full) == paramErr && !HIDDEN_RAW(full));
    next_handle = 0;
    assert(!C_NewAliasMinimal(&target, &full));
    Handle large = HIDDEN_NATIVE(full);
    Size actual_size = sizes[0];
    unsigned char saved_large[2048];
    memcpy(saved_large, STARH(large), actual_size);
    sizes[0] = INT32_MAX; /* No giant allocation or out-of-extent copy is needed. */
    assert(C_UpdateAlias(NULL, &target, large, &changed) == memFullErr);
    assert(!changed && sizes[0] == INT32_MAX);
    assert(!memcmp(saved_large, STARH(large), actual_size));
    free(low);
    puts("PASS: alias update, unchanged records, user data and failure preservation");
    return 0;
}
