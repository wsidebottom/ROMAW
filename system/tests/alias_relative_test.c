/*+===================================================================
  File:      alias_relative_test.c

  Summary:   Alias outputs occupy four guest bytes, including beside a saved frame.

  Classes:   None.

  Functions: BlockMove, CToPascalCall, C_FSMakeFSSpec, C_GetString, DisposHandle, GetHandleSize, HGetState, HLock,
             HSetState, MemError, PBGetCatInfo, PBHGetVInfo, PtrAndHand, ROMlib_RelString, SetHandleSize,
             _NewHandle_flags, _warning, main, syn68k_guest_to_host, syn68k_host_to_guest

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
static int missing_relative, missing_absolute, wrong_kind;
static OSErr (*catalog_override)(CInfoPBPtr);
static OSErr (*spec_override)(FSSpecPtr);
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

StringHandle C_GetString(INTEGER id)
{
    return NULL;
}

typedef struct {
    uint32 id, parent;
    const char* name;
} directory_t;

static const directory_t directories[] = {{2, 1, "Disk"},     {10, 2, "Bundle"},  {20, 10, "Links"},
                                          {30, 10, "Docs"},   {110, 2, "Copy"},   {120, 110, "Links"},
                                          {130, 110, "Docs"}, {90, 91, "CycleA"}, {91, 90, "CycleB"}};

static void pstring(unsigned char* p, const char* s)
{
    p[0] = strlen(s);
    memcpy(p + 1, s, p[0]);
}

OSErrRET PBHGetVInfo(HParmBlkPtr pb, BOOLEAN async)
{
    if (volume_error)
        return volume_error;
    INTEGER volume = CW(pb->volumeParam.ioVRefNum);
    if (volume == 0)
        volume = -5;
    if (PP_RAW(pb->volumeParam.ioNamePtr))
        pstring(PPR(pb->volumeParam.ioNamePtr), volume == -6 ? "Other" : "Disk");
    pb->volumeParam.ioVRefNum = CW(volume);
    pb->volumeParam.ioVSigWord = CWC(0x4244);
    return noErr;
}

OSErrRET PBGetCatInfo(CInfoPBPtr pb, BOOLEAN async)
{
    if (catalog_override)
        return catalog_override(pb);
    if (file_error)
        return file_error;
    uint32 id = CL(pb->dirInfo.ioDrDirID);
    if (pb->dirInfo.ioFDirIndex == CWC(-1)) {
        for (unsigned i = 0; i < NELEM(directories); ++i)
            if (directories[i].id == id) {
                pb->dirInfo.ioDrDirID = CL(id);
                pb->dirInfo.ioDrParID = CL(directories[i].parent);
                pb->dirInfo.ioFlAttrib = ATTRIB_ISADIR;
                pstring(PPR(pb->dirInfo.ioNamePtr), directories[i].name);
                return noErr;
            }
        return dirNFErr;
    }
    assert(id == 30 || id == 130 || id == 90);
    pb->hFileInfo.ioFlParID = CL(id);
    pb->hFileInfo.ioDirID = CL(id + 1000);
    pb->hFileInfo.ioFlCrDat = CLC(1234);
    pb->hFileInfo.ioFlAttrib = wrong_kind && id == 130 ? ATTRIB_ISADIR : 0;
    return noErr;
}

OSErr C_FSMakeFSSpec(INTEGER volume, LONGINT parent, Str255 path, FSSpecPtr out)
{
    if (spec_override)
        return spec_override(out);
    const char* text = (const char*)path + 1;
    uint32 found;
    if (path[0] == 10 && !memcmp(text, ":Docs:File", 10)) {
        if (missing_relative)
            return fnfErr;
        assert(parent == 10 || parent == 110);
        found = parent == 10 ? 30 : 130;
    }
    else if (path[0] == 5 && !memcmp(text, ":File", 5)) {
        if (missing_relative)
            return fnfErr;
        found = parent;
    }
    else {
        if (missing_absolute)
            return fnfErr;
        if (path[0] == 21 && !memcmp(text, "Disk:Bundle:Docs:File", 21))
            found = 30;
        else if (path[0] == 19 && !memcmp(text, "Disk:Copy:Docs:File", 19))
            found = 130;
        else {
            fprintf(stderr, "Unexpected path %.*s\n", path[0], text);
            abort();
        }
    }
    memset(out, 0, sizeof *out);
    out->vRefNum = CWC(-5);
    out->parID = CL(found);
    pstring(out->name, "File");
    return noErr;
}

LONGINT
ROMlib_RelString(unsigned char* a, unsigned char* b, BOOLEAN case_sensitive, BOOLEAN diacritic, LONGINT lengths)
{
    unsigned na = ((uint32)lengths >> 16), nb = lengths & 65535;
    return na != nb || memcmp(a, b, na);
}

static FSSpec spec(INTEGER volume, LONGINT parent, const char* name)
{
    FSSpec result = {0};
    result.vRefNum = CW(volume);
    result.parID = CL(parent);
    pstring(result.name, name);
    return result;
}
#ifndef ALIAS_RELATIVE_NO_MAIN
int main(void)
{
    void* low = calloc(1, 65536);
    assert(low);
    ROMlib_offset = (uintptr_t)low;
    FSSpec from = spec(-5, 20, "Alias"), target = spec(-5, 30, "File");
    FSSpec moved = spec(-5, 120, "Alias"), out, sentinel;
    HIDDEN_AliasHandle hidden;
    Boolean changed;
    assert(!C_NewAlias(&from, &target, &hidden));
    Handle alias = HIDDEN_NATIVE(hidden);
    alias_head_t* head = (alias_head_t*)STARH(alias);
    assert(CW(head->mystery_words[0]) == 2 && CW(head->mystery_words[1]) == 2);
    unsigned char saved[2048];
    Size old_size = GetHandleSize(alias);
    memcpy(saved, STARH(alias), old_size);
    assert(!ROMlib_follow_finder_alias(&moved, alias, FALSE, &out, &changed));
    assert(CL(out.parID) == 130 && changed);
    assert(GetHandleSize(alias) == old_size && !memcmp(saved, STARH(alias), old_size));
    /* Finder ignores same-folder relative candidates and retries the old path. */
    FSSpec same_folder = spec(-5, 130, "Alias");
    assert(!ROMlib_follow_finder_alias(&same_folder, alias, FALSE, &out, &changed));
    assert(CL(out.parID) == 30 && !changed);
    wrong_kind = 1;
    assert(!C_ResolveAlias(&moved, alias, &out, &changed));
    assert(CL(out.parID) == 30 && !changed);
    wrong_kind = 0;
    allocation_failure = TRUE;
    assert(!C_ResolveAlias(&moved, alias, &out, &changed));
    assert(CL(out.parID) == 130 && !changed);
    assert(!memcmp(saved, STARH(alias), old_size));
    allocation_failure = FALSE;
    next_handle = 1;
    assert(!C_ResolveAlias(&moved, alias, &out, &changed));
    assert(CL(out.parID) == 130 && changed);
    next_handle = 1;
    assert(!C_ResolveAlias(&moved, alias, &out, &changed) && !changed);
    missing_relative = 1;
    assert(!C_ResolveAlias(&moved, alias, &out, &changed));
    assert(CL(out.parID) == 130 && !changed);
    missing_absolute = 1;
    memset(&sentinel, 0xa5, sizeof sentinel);
    out = sentinel;
    assert(C_ResolveAlias(&moved, alias, &out, &changed) == fnfErr);
    assert(!changed && !memcmp(&out, &sentinel, sizeof out));
    missing_absolute = missing_relative = 0;
    next_handle = 1;
    FSSpec other = spec(-6, 20, "Alias");
    assert(!C_NewAlias(&other, &target, &hidden));
    head = (alias_head_t*)STARH(HIDDEN_NATIVE(hidden));
    assert(CW(head->mystery_words[0]) == -1 && CW(head->mystery_words[1]) == -1);
    next_handle = 1;
    target.parID = CLC(90);
    assert(C_NewAlias(&from, &target, &hidden) == dirNFErr);
    assert(!HIDDEN_RAW(hidden));
    free(low);
    puts("PASS: relative aliases, moved pairs, Finder fallback, cycles and failed refresh");
    return 0;
}
#endif
