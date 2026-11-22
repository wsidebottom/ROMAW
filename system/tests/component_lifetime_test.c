/*+===================================================================
  File:      component_lifetime_test.c

  Summary:   Real component registry paths with intercepted memory/callback operations.

  Classes:   None.

  Functions: CToPascalCall, C_Count1Resources, C_CurResFile, C_FSMakeFSSpec, C_Get1IndResource, C_Get1Resource,
             C_HomeResFile, C_UseResFile, DisposHandle, GetHandleSize, HLock, HUnlock, MemError, PBGetFCBInfo,
             ROMlib_ppc_component_entry, SetHandleSize, _NewHandle_flags, main, syn68k_guest_to_host,
             syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Real component registry paths with intercepted memory/callback operations. */
#define NDEBUG 1
#include "rsys/common.h"
#include "rsys/everything.h"
static int allocation, fail_at;
static unsigned live_allocs;

static void* component_calloc(size_t n, size_t size)
{
    void* p;
    if (++allocation == fail_at)
        return NULL;
    p = calloc(n, size);
    if (p)
        ++live_allocs;
    return p;
}

static void component_free(void* p)
{
    if (p)
        --live_allocs;
    free(p);
}

#define calloc component_calloc
#define free component_free
#include "../../services/components.c"
#undef calloc
#undef free
#undef main
#undef NDEBUG
#include <assert.h>
CPUState cpu_state;
void* tooltraptable[0x400];
toolstuff_t toolstuff[0x400];

Handle C_Get1Resource(ResType type, INTEGER id)
{
    abort();
}

INTEGER
C_CurResFile(void)
{
    abort();
}

INTEGER
C_HomeResFile(Handle h)
{
    abort();
}

void C_UseResFile(INTEGER ref)
{
    abort();
}

OSErrRET PBGetFCBInfo(FCBPBPtr pb, BOOLEAN async)
{
    abort();
}

OSErr C_FSMakeFSSpec(INTEGER ref, LONGINT dir, StringPtr name, FSSpecPtr spec)
{
    abort();
}

INTEGER
C_Count1Resources(ResType type)
{
    abort();
}

Handle C_Get1IndResource(ResType type, INTEGER index)
{
    abort();
}

void SetHandleSize(Handle h, Size size)
{
    abort();
}

OSErr MemError(void)
{
    return noErr;
}

#include "guest_test_memory.h"

uint64 ROMlib_offset;
uint32 syn68k_address_mask = 0xffffffff;
unsigned syn68k_host_alias_count = 1;
uint32 syn68k_host_alias_base = 0xe0000000, syn68k_host_alias_size = 0x10000000;

void* syn68k_guest_to_host(uint32 a)
{
    return fixture_guest_to_host(a);
}

uint32 syn68k_host_to_guest(ptr_sized_uint a)
{
    return fixture_host_to_guest(a);
}

struct mock_handle {
    HIDDEN_Ptr h;
    Size size;
    Ptr data;
};

Handle _NewHandle_flags(Size size, boolean_t sys, boolean_t clear)
{
    struct mock_handle* h;
    assert(sys);
    h = component_calloc(1, sizeof *h + size);
    if (!h)
        return NULL;
    h->size = size;
    h->data = (Ptr)(h + 1);
    HIDDEN_RAW_SET(h->h, RM(h->data));
    return (Handle)h;
}

Size GetHandleSize(Handle h)
{
    return ((struct mock_handle*)h)->size;
}

void HLock(Handle h)
{
    assert(h);
}

void HUnlock(Handle h)
{
    assert(h);
}

void DisposHandle(Handle h)
{
    component_free(h);
}

int ROMlib_ppc_component_entry(Handle h, const FSSpec* spec, ProcPtr* entry)
{
    return 0;
}

static int action;
static component_entry* nested_component;
static uint32 nested_instance;

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    va_list ap;
    const unsigned char* parameters;
    component_instance* self;
    int16 selector;
    assert(proc == (void*)0x1000 && magic == (4 | (5 << 3) | (5 << 6)));
    va_start(ap, magic);
    parameters = va_arg(ap, const void*);
    va_end(ap);
    memcpy(&selector, parameters + 2, 2);
    selector = CW(selector);
    if (action == 1 && selector == -1) {
        action = 0;
        nested_instance = component_open(nested_component);
        assert(nested_instance);
        return -42;
    }
    if (action == 2) {
        uint32 id;
        memcpy(&id, parameters + 4, 4);
        id = CL(id);
        self = instance_find(id);
        assert(self);
        assert(component_close(id) == -3000);
        assert(component_unregister(self->component->id) == -3000);
    }
    if (action == 3 && selector == -2)
        assert(!component_open(nested_component));
    return 0;
}

int main(void)
{
    component_description description = {0};
    Handle source;
    uint32 id, instance, other;
    int i;
    description.type = CLC(T('T', 'E', 'S', 'T'));
    source = _NewHandle_flags(8, 1, 1);
    assert(source);
    memcpy(STARH(source), "original", 8);
    for (i = 1; i <= 5; ++i) {
        allocation = 0;
        fail_at = i;
        id = component_register(&description, (ProcPtr)0x1000, source, source, source, source, NULL, 0, 1, 0, 0);
        assert(!id && !components && !instances && live_allocs == 1);
    }
    fail_at = 0;
    id = component_register(&description, (ProcPtr)0x1000, source, source, source, source, NULL, 0, 1, 0, 0);
    assert(id && !memcmp(STARH(components->code), "original", 8));
    memset(STARH(source), 0, 8);
    assert(!memcmp(STARH(components->code), "original", 8));
    other = component_register(&description, (ProcPtr)0x1000, NULL, NULL, NULL, NULL, NULL, 0, 1, 0, 0);
    nested_component = component_find(other);
    action = 1;
    assert(!component_open(component_find(id)));
    assert(instance_find(nested_instance) && nested_component->instances == 1);
    assert(!component_close(nested_instance));
    allocation = 0;
    fail_at = 1;
    assert(!component_open(component_find(id)) && !instances);
    fail_at = 0;
    action = 2;
    instance = component_open(component_find(id));
    assert(instance);
    assert(!component_close(instance) && !instances);
    action = 0;
    instance = component_open(nested_component);
    assert(instance);
    action = 3;
    ROMlib_component_reset();
    assert(!components && !instances && live_allocs == 1);
    DisposHandle(source);
    assert(!live_allocs);
    puts("Component allocation rollback, cloned resources, reentrant lifetime and shutdown passed");
    return 0;
}
