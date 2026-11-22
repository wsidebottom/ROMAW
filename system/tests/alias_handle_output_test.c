/*+===================================================================
  File:      alias_handle_output_test.c

  Summary:   Alias outputs occupy four guest bytes, including beside a saved frame.

  Classes:   None.

  Functions: CToPascalCall, C_GetString, DisposHandle, GetHandleSize, MemError, PBGetCatInfo, PBHGetVInfo,
             SetHandleSize, _NewHandle_flags, _warning, main, syn68k_guest_to_host, syn68k_host_to_guest

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
static HIDDEN_Ptr master;
static unsigned char payload[2048];
static OSErr volume_error, file_error;
static boolean_t allocation_failure;
static Size allocated_size;
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
    assert(!system && !clear && size > 0 && size <= sizeof payload);
    MemErr = CW(allocation_failure ? memFullErr : noErr);
    if (allocation_failure)
        return NULL;
    allocated_size = size;
    memset(payload, 0xCC, sizeof payload);
    HIDDEN_RAW_SET(master, RM((Ptr)payload));
    return &master;
}

OSErr MemError(void)
{
    return CW(MemErr);
}

Size GetHandleSize(Handle handle)
{
    assert(handle == &master);
    return allocated_size;
}

void SetHandleSize(Handle h, Size size)
{
    assert(h == &master && size <= sizeof payload);
    allocated_size = size;
    MemErr = 0;
}

void DisposHandle(Handle h)
{
    assert(h == &master);
}

StringHandle C_GetString(INTEGER identifier)
{
    assert(identifier == -16413 || identifier == -16096);
    return NULL;
}

OSErrRET PBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    StringPtr name = PPR(pb->ioParam.ioNamePtr);
    assert(!async && pb->ioParam.ioVRefNum == CWC(-5));
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
    return file_error;
}

static OSErr create_alias(int api, void* output)
{
    FSSpec spec = {0};
    spec.vRefNum = CWC(-5);
    spec.parID = CLC(42);
    memcpy(spec.name, "\004File", 5);
    Str32 zone = {0};
    Str31 server = {0};
    if (api == 0)
        return C_NewAliasMinimal(&spec, output);
    if (api == 1)
        return C_NewAlias(NULL, &spec, output);
    return C_NewAliasMinimalFromFullPath(9, (Ptr) "Disk:File", zone, server, output);
}

int main(void)
{
    _Static_assert(sizeof(HIDDEN_Handle) == 4, "Guest handles are four bytes");
    void* low = calloc(1, 65536);
    assert(low);
    ROMlib_offset = (uintptr_t)low;
    for (int api = 0; api < 3; ++api)
        for (int alignment = 0; alignment <= 2; alignment += 2)
            for (int failure = 0; failure < 4; ++failure) {
                unsigned char caller_frame[32];
                memset(caller_frame, 0xA5, sizeof caller_frame);
                HIDDEN_Handle* output = (HIDDEN_Handle*)(caller_frame + 4 + alignment);
                volume_error = failure == 1 ? nsvErr : noErr;
                file_error = failure == 2 ? fnfErr : noErr;
                allocation_failure = failure == 3;
                OSErr result = create_alias(api, output);
                OSErr expected = allocation_failure ? memFullErr
                                 : api == 2         ? noErr
                                 : volume_error     ? volume_error
                                                    : file_error;
                assert(result == expected);
                for (unsigned i = 0; i < sizeof caller_frame; ++i)
                    if ((i < 4 + alignment || i >= 8 + alignment) && caller_frame[i] != 0xA5) {
                        fprintf(stderr, "Alias output overwrote adjacent byte %u\n", i);
                        return 1;
                    }
                if (result == noErr) {
                    assert(HIDDEN_NATIVE(*output) == &master);
                    alias_head_t* head = (alias_head_t*)payload;
                    assert(CW(head->length) == allocated_size);
                    assert(head->fileName[0] == 4);
                    assert(!memcmp(head->fileName + 1, "File", 4));
                    assert(!memcmp(payload + allocated_size - 4, "\xff\xff\0\0", 4));
                    assert(payload[allocated_size] == 0xCC);
                }
                else
                    assert(HIDDEN_RAW(*output) == NULL);
            }
    unsigned char* pages = VirtualAlloc(NULL, 8192, 0x3000, 4);
    unsigned long previous;
    assert(pages && VirtualProtect(pages + 4096, 4096, 1, &previous));
    volume_error = file_error = noErr;
    for (int api = 0; api < 3; ++api)
        for (int failure = 0; failure < 2; ++failure) {
            allocation_failure = failure;
            memset(pages, 0xA5, 4096);
            HIDDEN_Handle* output = (HIDDEN_Handle*)(pages + 4092);
            assert(create_alias(api, output) == (failure ? memFullErr : noErr));
            assert(failure ? HIDDEN_RAW(*output) == NULL : HIDDEN_NATIVE(*output) == &master);
            for (unsigned i = 0; i < 4092; ++i)
                assert(pages[i] == 0xA5);
        }
    assert(VirtualFree(pages, 0, 0x8000));
    assert(create_alias(0, NULL) == paramErr);
    assert(create_alias(1, NULL) == paramErr);
    assert(create_alias(2, NULL) == paramErr);
    free(low);
    puts("Alias creation preserves four-byte outputs and adjacent saved frames");
    return 0;
}
