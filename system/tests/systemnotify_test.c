/*+===================================================================
  File:      systemnotify_test.c

  Summary:   Private lifecycle notifications: callbacks, A5, recursive changes and failures.

  Classes:   None.

  Functions: CToPascalCall, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Private lifecycle notifications: callbacks, A5, recursive changes and failures. */
#define NDEBUG 1
#include "rsys/common.h"
#include "rsys/everything.h"
static int fail_alloc;
static unsigned live_allocs;

static void* checked_malloc(size_t size)
{
    void* p = fail_alloc ? NULL : malloc(size);
    if (p)
        ++live_allocs;
    return p;
}

static void* checked_calloc(size_t n, size_t size)
{
    void* p = fail_alloc ? NULL : calloc(n, size);
    if (p)
        ++live_allocs;
    return p;
}

static void checked_free(void* p)
{
    if (p)
        --live_allocs;
    free(p);
}

#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
#include "../../services/systemnotify.c"
#undef malloc
#undef calloc
#undef free
#undef NDEBUG
#undef main
#include <assert.h>
CPUState cpu_state;
static unsigned calls;
static int mutate;
static int payload;

intptr_t CToPascalCall(void* proc, unsigned long magic, ...)
{
    va_list ap;
    uint32 kind, context;
    const void* data;
    assert((uint32)proc == 0x1000 || (uint32)proc == 0x2000);
    assert(magic == ((4 << 3) | (5 << 6) | (4 << 9)));
    va_start(ap, magic);
    kind = va_arg(ap, uint32);
    data = va_arg(ap, const void*);
    context = va_arg(ap, uint32);
    va_end(ap);
    assert(kind == T('a', 'i', 'n', 'i') && data == &payload);
    assert(EM_A5 == ((uint32)proc == 0x1000 ? 17 : 29));
    assert(context == ((uint32)proc == 0x1000 ? 7 : 11));
    ++calls;
    if (mutate) {
        mutate = 0;
        ROMlib_system_notify_reset();
        EM_A5 = 29;
        assert(!ROMlib_system_notify_register((ProcPtr)0x2000, kind, 11));
    }
    EM_A5 = 99;
    return 0;
}

int main(void)
{
    uint32 kind = T('a', 'i', 'n', 'i');
    EM_A5 = 17;
    assert(ROMlib_system_notify_register(NULL, kind, 0) == paramErr);
    fail_alloc = 1;
    assert(ROMlib_system_notify_register((ProcPtr)0x1000, kind, 7) == memFullErr);
    fail_alloc = 0;
    assert(!ROMlib_system_notify_register((ProcPtr)0x1000, kind, 7));
    assert(!ROMlib_system_notify_register((ProcPtr)0x1000, kind, 7));
    assert(live_allocs == 1);
    EM_A5 = 41;
    assert(!ROMlib_system_notify(T('a', 't', 'e', 'r'), NULL) && calls == 0);
    fail_alloc = 1;
    assert(ROMlib_system_notify(kind, &payload) == memFullErr && calls == 0);
    fail_alloc = 0;
    assert(!ROMlib_system_notify(kind, &payload) && calls == 1 && EM_A5 == 41);
    mutate = 1;
    assert(!ROMlib_system_notify(kind, &payload) && calls == 2 && EM_A5 == 41);
    assert(!ROMlib_system_notify(kind, &payload) && calls == 3 && EM_A5 == 41);
    ROMlib_system_notify_reset();
    assert(!live_allocs);
    puts("System notification callbacks, A5 restoration, mutation and failures passed");
    return 0;
}
