/*+===================================================================
  File:      corefoundation.c

  Summary:   Access real CarbonLib 1.4/CoreFoundationLib guest objects without replacing their allocator, class
             registration, ownership or storage. Layout reference: CFArrayGetCount 0x1bd6c, CFData accessors
             0x1e590-0x1e5e8 and CFGetTypeID 0x53394.

  Classes:   None.

  Functions: ROMlib_carbon_cf_absolute_time, ROMlib_carbon_cf_allocator_allocate, ROMlib_carbon_cf_allocator_context,
             ROMlib_carbon_cf_allocator_create, ROMlib_carbon_cf_allocator_deallocate,
             ROMlib_carbon_cf_system_allocator_workers, ROMlib_carbon_cf_allocator_default, ROMlib_carbon_cf_allocator_preferred,
             ROMlib_carbon_cf_allocator_reallocate, ROMlib_carbon_cf_allocator_type_id, ROMlib_carbon_cf_array_apply,
             ROMlib_carbon_cf_array_count, ROMlib_carbon_cf_array_create, ROMlib_carbon_cf_array_search,
             ROMlib_carbon_cf_array_type_id, ROMlib_carbon_cf_array_value, ROMlib_carbon_cf_array_values,
             ROMlib_carbon_cf_bundle_data, ROMlib_carbon_cf_bundle_function, ROMlib_carbon_cf_data_bytes,
             ROMlib_carbon_cf_data_get_bytes, ROMlib_carbon_cf_data_length, ROMlib_carbon_cf_date_compare,
             ROMlib_carbon_cf_date_interval, ROMlib_carbon_cf_date_time, ROMlib_carbon_cf_encode_stream,
             ROMlib_carbon_cf_get_allocator, ROMlib_carbon_cf_release, ROMlib_carbon_cf_retain,
             ROMlib_carbon_cf_retain_count, ROMlib_carbon_cf_string_bytes, ROMlib_carbon_cf_string_c,
             ROMlib_carbon_cf_string_c_pointer, ROMlib_carbon_cf_string_character, ROMlib_carbon_cf_string_characters,
             ROMlib_carbon_cf_string_fastest, ROMlib_carbon_cf_string_get_characters, ROMlib_carbon_cf_string_length,
             ROMlib_carbon_cf_string_pascal, ROMlib_carbon_cf_string_pascal_pointer, ROMlib_carbon_cf_type_id,
             ROMlib_carbon_cf_unichar_member

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Access real CarbonLib 1.4/CoreFoundationLib guest objects without replacing
   their allocator, class registration, ownership or storage. Layout reference:
   CFArrayGetCount 0x1bd6c, CFData accessors 0x1e590-0x1e5e8 and
   CFGetTypeID 0x53394. */
#include "internal.h"
#include "OSUtil.h"

static uint32 cf_word(const void* object, size_t offset)
{
    uint32 encoded;
    memcpy(&encoded, (const Byte*)object + offset, sizeof encoded);
    return CL(encoded);
}

static const Byte* cf_pointer(const void* object, size_t offset)
{
    return (const Byte*)SYN68K_TO_US_CHECK0(cf_word(object, offset));
}

static void cf_store_word(void* object, size_t offset, uint32 value)
{
    uint32 encoded = CL(value);
    memcpy((Byte*)object + offset, &encoded, sizeof encoded);
}

static int cf_reference_is_14(const ROMlib_carbon_reference* reference, uint32 offset)
{
    return reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
           reference->toc && reference->toc_size == 0x72a8;
}

enum {
    cf_heap_get_ptr_size = 0x24,
    cf_heap_get_zone = 0x28,
    cf_heap_temp_new_handle = 0x3c,
    cf_heap_new_ptr = 0x5c,
    cf_heap_lock_handle = 0xa4,
    cf_heap_set_zone = 0xb8,
    cf_heap_unlock_handle = 0xe4,
    cf_heap_dispose_ptr = 0xec,
    cf_heap_recover_handle = 0xf4,
    cf_heap_get_handle_size = 0xf8,
    cf_heap_dispose_handle = 0x10,
    cf_heap_ptr_tag = 0xaaaacfcfU,
    cf_heap_handle_tag = 0xbbbbcfcfU,
};

static int cf_system_callback(const void* allocator, uint32 procedure, unsigned descriptor, uint32 worker,
                              ROMlib_carbon_reference* reference)
{
    if (!procedure || !ROMlib_carbon_probe_reference("CoreFoundationLib", "CFAllocatorGetDefault", reference) ||
        !cf_reference_is_14(reference, 0x17344) || allocator != cf_pointer(reference->toc, 0x3164) ||
        procedure != US_TO_SYN68K(reference->toc) + descriptor)
        return 0;
    const uint32 entry = cf_word(reference->toc, descriptor);
    return cf_word(reference->toc, descriptor + 4) == US_TO_SYN68K(reference->toc) &&
           (entry == cf_word(reference->function, 0) - reference->code_offset + worker ||
            ROMlib_carbon_is_cf_system_allocator_worker((descriptor - 0x125c) / 8, entry));
}

static int cf_heap_call(const ROMlib_carbon_reference* reference, unsigned slot, const uint32* arguments,
                        unsigned count, uint32* result)
{
    return ROMlib_carbon_invoke_callback((void*)cf_pointer(reference->toc, slot), arguments, count, result);
}

/* System allocator workers in 1.4 share this tagged layout with pointers
   handed to saved PPC functions. The Memory Manager still owns both heaps. */
static void* cf_system_allocate(const ROMlib_carbon_reference* reference, LONGINT size)
{
    if (size <= 0 || size > INT32_MAX - 4)
        return NULL;
    const uint32 extent = size + 4;
    uint32 base = 0, tag = cf_heap_ptr_tag;
    if (extent < 32768) {
        const uint32 zone = cf_word(reference->toc, 0x8a0);
        uint32 previous;
        if (!cf_heap_call(reference, cf_heap_get_zone, NULL, 0, &previous) ||
            !cf_heap_call(reference, cf_heap_set_zone, &zone, 1, NULL))
            return NULL;
        const int allocated = cf_heap_call(reference, cf_heap_new_ptr, &extent, 1, &base);
        if (!cf_heap_call(reference, cf_heap_set_zone, &previous, 1, NULL) || !allocated)
            return NULL;
    }
    if (!base) {
        _Alignas(8) OSErr error = noErr;
        const uint32 arguments[] = {extent, US_TO_SYN68K(&error)};
        uint32 handle;
        if (!cf_heap_call(reference, cf_heap_temp_new_handle, arguments, 2, &handle) || !handle ||
            !cf_heap_call(reference, cf_heap_lock_handle, &handle, 1, NULL))
            return NULL;
        const void* master = ROMlib_carbon_guest_memory(handle, 4, 0);
        if (!master)
            return NULL;
        base = cf_word(master, 0);
        tag = cf_heap_handle_tag;
    }
    void* block = base ? ROMlib_carbon_guest_memory(base, extent, 1) : NULL;
    if (!block)
        return NULL;
    cf_store_word(block, 0, tag);
    return (Byte*)block + 4;
}

static int cf_system_block(const void* pointer, uint32* base, uint32* tag)
{
    const uint32 address = US_TO_SYN68K(pointer);
    const void* header = address > 4 ? ROMlib_carbon_guest_memory(address - 4, 4, 0) : NULL;
    if (!header)
        return 0;
    *base = address - 4;
    *tag = cf_word(header, 0);
    return 1;
}

static int cf_system_dispose(const ROMlib_carbon_reference* reference, uint32 base, uint32 tag)
{
    if (tag == cf_heap_ptr_tag)
        return cf_heap_call(reference, cf_heap_dispose_ptr, &base, 1, NULL);
    if (tag == cf_heap_handle_tag) {
        uint32 handle;
        return cf_heap_call(reference, cf_heap_recover_handle, &base, 1, &handle) && handle &&
               cf_heap_call(reference, cf_heap_unlock_handle, &handle, 1, NULL) &&
               cf_heap_call(reference, cf_heap_dispose_handle, &handle, 1, NULL);
    }
    return 1;
}

static void* cf_system_reallocate(const ROMlib_carbon_reference* reference, void* pointer, LONGINT size)
{
    uint32 base, tag, extent;
    if (size <= 0 || size > INT32_MAX - 4 || !cf_system_block(pointer, &base, &tag))
        return NULL;
    if (tag == cf_heap_ptr_tag) {
        if (!cf_heap_call(reference, cf_heap_get_ptr_size, &base, 1, &extent))
            return NULL;
    }
    else if (tag == cf_heap_handle_tag) {
        uint32 handle;
        if (!cf_heap_call(reference, cf_heap_recover_handle, &base, 1, &handle) || !handle ||
            !cf_heap_call(reference, cf_heap_get_handle_size, &handle, 1, &extent))
            return NULL;
    }
    else
        return NULL;
    if ((int32)extent < 4)
        return NULL;

    void* moved = cf_system_allocate(reference, size);
    if (moved) {
        const size_t copy = MIN((uint32)size, extent - 4);
        const void* source = copy ? ROMlib_carbon_guest_memory(base + 4, copy, 0) : NULL;
        if (copy && !source)
            return NULL;
        if (copy)
            memmove(moved, source, copy);
    }
    /* The 1.4 system callback consumes the original block even when the
       destination could not be allocated. */
    return cf_system_dispose(reference, base, tag) ? moved : NULL;
}

static int cf_system_worker_reference(ROMlib_carbon_reference* reference)
{
    return ROMlib_carbon_reference_function("CoreFoundationLib", "CFAllocatorGetDefault", reference) &&
           cf_reference_is_14(reference, 0x17344);
}

static void* cf_allocate_worker(LONGINT size, uint32 hints, uint32 context)
{
    (void)hints;
    (void)context;
    ROMlib_carbon_reference reference;
    return cf_system_worker_reference(&reference) ? cf_system_allocate(&reference, size) : NULL;
}

static void* cf_reallocate_worker(void* pointer, LONGINT size, uint32 hints, uint32 context)
{
    (void)hints;
    (void)context;
    ROMlib_carbon_reference reference;
    return cf_system_worker_reference(&reference) ? cf_system_reallocate(&reference, pointer, size) : NULL;
}

static void cf_dispose_worker(void* pointer, uint32 context)
{
    (void)context;
    ROMlib_carbon_reference reference;
    uint32 base, tag;
    if (cf_system_worker_reference(&reference) && cf_system_block(pointer, &base, &tag))
        cf_system_dispose(&reference, base, tag);
}

static LONGINT cf_preferred_worker(LONGINT size, uint32 hints, uint32 context)
{
    (void)hints;
    (void)context;
    return size;
}

const ROMlib_carbon_export* ROMlib_carbon_cf_system_allocator_workers(void)
{
    static const ROMlib_carbon_export workers[ROMLIB_CARBON_CF_SYSTEM_WORKER_COUNT] = {
        {.name = "allocate",
         .function = cf_allocate_worker,
         .signature = 5 | (UINT64_C(4) << 3) | (UINT64_C(4) << 6) | (UINT64_C(4) << 9),
         .reference_offset = 0x16ee4},
        {.name = "reallocate",
         .function = cf_reallocate_worker,
         .signature = 5 | (UINT64_C(4) << 3) | (UINT64_C(4) << 6) | (UINT64_C(4) << 9) | (UINT64_C(5) << 12),
         .reference_offset = 0x17048},
        {.name = "deallocate",
         .function = cf_dispose_worker,
         .signature = (UINT64_C(4) << 3) | (UINT64_C(5) << 6),
         .reference_offset = 0x16fcc},
        {.name = "preferredSize",
         .function = cf_preferred_worker,
         .signature = 4 | (UINT64_C(4) << 3) | (UINT64_C(4) << 6) | (UINT64_C(4) << 9),
         .reference_offset = 0x17130},
    };
    return workers;
}

static int cf_callback_is_builtin(const ROMlib_carbon_reference* reference, uint32 procedure, uint32 descriptor,
                                  uint32 code)
{
    const void* callback = SYN68K_TO_US_CHECK0(procedure);
    return callback == (const Byte*)reference->toc + descriptor &&
           cf_word(callback, 0) == cf_word(reference->function, 0) - reference->code_offset + code &&
           cf_word(callback, 4) == US_TO_SYN68K(reference->toc);
}

/* CoreFoundationLib 1.4's helper at 0x17a2c returns TOC+0x6f24.
   GetDefault 0x17344 reads its allocator word at +4, otherwise the live
   kCFAllocatorSystemDefault word at TOC+0x3164. Keep fallback setters shared. */
static void* cf_allocator_default_value(const void* toc)
{
    uint32 allocator = cf_word(toc, 0x6f28);
    if (!allocator)
        allocator = cf_word(toc, 0x3164);
    return SYN68K_TO_US_CHECK0(allocator);
}

void* ROMlib_carbon_cf_allocator_default(void)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFAllocatorGetDefault", &reference))
        return NULL;
    if (cf_reference_is_14(&reference, 0x17344))
        return cf_allocator_default_value(reference.toc);
    uint32 result;
    if (!ROMlib_carbon_invoke_callback(reference.function, NULL, 0, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

uint32 ROMlib_carbon_cf_allocator_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFAllocatorGetTypeID", &reference))
        return 0;
    if (cf_reference_is_14(&reference, 0x1733c))
        return cf_word(reference.toc, 0x89c);
    return ROMlib_carbon_invoke_callback(reference.function, NULL, 0, &result) ? result : 0;
}

/* 0x173f4 retains context info before allocation, and leaves it retained
   when allocation fails. 0x221 is the guest kCFAllocatorUseContext sentinel.
   Class initialization mirrors 0x53360 using the real registry's bounds. */
void* ROMlib_carbon_cf_allocator_create(uint32 allocator, const void* context)
{
    ROMlib_carbon_reference reference;
    uint32 result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFAllocatorCreate", &reference))
        return NULL;
    if (!cf_reference_is_14(&reference, 0x173f4)) {
        uint32 arguments[] = {allocator, context ? US_TO_SYN68K(context) : 0};
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 2, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                        : NULL;
    }
    if (!context)
        return NULL;
    uint32 info = cf_word(context, 4), retain = cf_word(context, 8);
    if (retain && !ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(retain), &info, 1, &info))
        return NULL;
    void* object;
    if (allocator == 0x221) {
        uint32 arguments[] = {48, 0, info};
        if (!ROMlib_carbon_invoke_callback((void*)cf_pointer(context, 20), arguments, 3, &result))
            return NULL;
        object = SYN68K_TO_US_CHECK0(result);
    }
    else {
        const void* parent = SYN68K_TO_US_CHECK0(allocator);
        if (!parent) {
            parent = ROMlib_carbon_cf_allocator_default();
            if (!parent)
                return NULL;
            allocator = US_TO_SYN68K(parent);
        }
        object = ROMlib_carbon_cf_allocator_allocate(parent, 48, 0);
    }
    if (!object)
        return NULL;
    cf_store_word(object, 0, 0);
    cf_store_word(object, 4, 0);
    uint32 type = cf_word(reference.toc, 0x89c);
    if (type > cf_word(reference.toc, 0xa6c) && type < cf_word(reference.toc, 0xa68))
        cf_store_word(object, 4, (type & 255) << 8);
    cf_store_word(object, 8, allocator);
    cf_store_word(object, 12, cf_word(context, 0));
    cf_store_word(object, 16, info);
    for (unsigned i = 0; i < 7; ++i)
        cf_store_word(object, 20 + i * 4, cf_word(context, 8 + i * 4));
    return object;
}

/* CFAllocatorGetContext 0x1778c exposes the eight live context words
   beginning at object+16, with a zero version word in caller storage. */
void ROMlib_carbon_cf_allocator_context(const void* allocator, void* context)
{
    if (!context)
        return;
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator)
        return;
    memset(context, 0, 4);
    for (unsigned i = 0; i < 8; ++i) {
        uint32 word;
        memcpy(&word, (const Byte*)allocator + 16 + i * 4, 4);
        memcpy((Byte*)context + 4 + i * 4, &word, 4);
    }
}

/* 0x176f8 calls context.preferredSize only for positive requests, then
   returns max(0, requested size, signed callback result). */
LONGINT ROMlib_carbon_cf_allocator_preferred(const void* allocator, LONGINT size, uint32 hints)
{
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator)
        return 0;
    uint32 procedure = cf_word(allocator, 44), result = 0;
    if (size > 0 && procedure) {
        uint32 arguments[] = {(uint32)size, hints, cf_word(allocator, 16)};
        if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, 3, &result))
            return 0;
    }
    LONGINT preferred = (LONGINT)result;
    return preferred < size ? size : preferred;
}

/* 0x1752c forwards every nonzero signed size, including negative values,
   to the allocator's required allocation callback. */
void* ROMlib_carbon_cf_allocator_allocate(const void* allocator, LONGINT size, uint32 hints)
{
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator || !size)
        return NULL;
    const uint32 procedure = cf_word(allocator, 32);
    ROMlib_carbon_reference reference;
    if (cf_system_callback(allocator, procedure, 0x125c, 0x16ee4, &reference))
        return cf_system_allocate(&reference, size);
    uint32 arguments[] = {(uint32)size, hints, cf_word(allocator, 16)}, result;
    if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, 3, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

/* 0x17694 permits an absent deallocation callback and ignores null blocks. */
static int cf_deallocate(const void* allocator, void* pointer)
{
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator)
        return 0;
    if (!pointer)
        return 1;
    uint32 procedure = cf_word(allocator, 40);
    if (procedure) {
        ROMlib_carbon_reference reference;
        if (cf_system_callback(allocator, procedure, 0x126c, 0x16fcc, &reference)) {
            uint32 base, tag;
            return cf_system_block(pointer, &base, &tag) && cf_system_dispose(&reference, base, tag);
        }
        uint32 arguments[] = {US_TO_SYN68K(pointer), cf_word(allocator, 16)};
        return ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, 2, NULL);
    }
    return 1;
}

void ROMlib_carbon_cf_allocator_deallocate(const void* allocator, void* pointer)
{
    cf_deallocate(allocator, pointer);
}

/* 0x175a0 routes null/positive requests to allocation and nonnull/zero
   requests to deallocation. Other requests use the optional realloc callback. */
void* ROMlib_carbon_cf_allocator_reallocate(const void* allocator, void* pointer, LONGINT size, uint32 hints)
{
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator)
        return NULL;
    if (!pointer && size > 0)
        return ROMlib_carbon_cf_allocator_allocate(allocator, size, hints);
    if (pointer && !size) {
        ROMlib_carbon_cf_allocator_deallocate(allocator, pointer);
        return NULL;
    }
    uint32 procedure = cf_word(allocator, 36);
    if ((!pointer && !size) || !procedure)
        return NULL;
    ROMlib_carbon_reference reference;
    if (pointer && cf_system_callback(allocator, procedure, 0x1264, 0x17048, &reference))
        return cf_system_reallocate(&reference, pointer, size);
    uint32 arguments[] = {pointer ? US_TO_SYN68K(pointer) : 0, (uint32)size, hints, cf_word(allocator, 16)}, result;
    if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, 4, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int cf_original_call(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

static int cf_overflow(const void* toc, const void* object, const char* operation, uint32 value, uint32* result)
{
    uint32 arguments[] = {cf_word(toc, 0xa64), US_TO_SYN68K(object), value};
    return cf_original_call(operation, arguments, !strcmp(operation, "CFDictionarySetValue") ? 3 : 2, result);
}

/* The 1.4 retain/release lock helpers at 0x1a98c/0x1a990 are no-ops.
   Header counts are the low 15 bits of the upper halfword, plus one;
   bit 31 selects the real dictionary's additional groups of 32768. */
void* ROMlib_carbon_cf_retain(void* object)
{
    if (!object)
        return NULL;
    ROMlib_carbon_reference reference;
    uint32 result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFRetain", &reference))
        return NULL;
    if (!cf_reference_is_14(&reference, 0x533e4)) {
        uint32 argument = US_TO_SYN68K(object);
        return ROMlib_carbon_invoke_callback(reference.function, &argument, 1, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                        : NULL;
    }
    uint32 count = (cf_word(object, 4) >> 16) + 1;
    if (!(count & 0x7fff)) {
        if (!cf_overflow(reference.toc, object, "CFDictionaryGetValue", 0, &result) ||
            !cf_overflow(reference.toc, object, "CFDictionarySetValue", result + 1, NULL))
            return NULL;
        count = 0x8000;
    }
    cf_store_word(object, 4, (count << 16) | (cf_word(object, 4) & 0xffff));
    return object;
}

LONGINT ROMlib_carbon_cf_retain_count(const void* object)
{
    if (!object)
        return 0;
    ROMlib_carbon_reference reference;
    uint32 high = 0;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFGetRetainCount", &reference))
        return 0;
    if (!cf_reference_is_14(&reference, 0x535f0)) {
        uint32 argument = US_TO_SYN68K(object);
        return ROMlib_carbon_invoke_callback(reference.function, &argument, 1, &high) ? (LONGINT)high : 0;
    }
    uint32 count = cf_word(object, 4) >> 16;
    if ((count & 0x8000) && !cf_overflow(reference.toc, object, "CFDictionaryGetValue", 0, &high))
        return 0;
    uint32 total = (high << 15) + (count & 0x7fff) + 1;
    return total & 0x80000000 ? INT32_MAX : (LONGINT)total;
}

static void* cf_object_allocator(const void* object, const void* toc)
{
    uint32 info = cf_word(object, 4);
    if (((info >> 8) & 255) == 2) {
        uint32 parent = cf_word(object, 8);
        return parent == 0x221 ? (void*)object : SYN68K_TO_US_CHECK0(parent);
    }
    if (info & 0x80)
        return (void*)cf_pointer(toc, 0x3164);
    return (void*)cf_pointer((const Byte*)object - 4, 0);
}

void* ROMlib_carbon_cf_get_allocator(const void* object)
{
    if (!object)
        return NULL;
    ROMlib_carbon_reference reference;
    uint32 result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFGetAllocator", &reference))
        return NULL;
    if (cf_reference_is_14(&reference, 0x53844))
        return cf_object_allocator(object, reference.toc);
    uint32 argument = US_TO_SYN68K(object);
    return ROMlib_carbon_invoke_callback(reference.function, &argument, 1, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                    : NULL;
}

static int cf_allocator_dispose(void* object)
{
    uint32 parent = cf_word(object, 8), release = cf_word(object, 24), info = cf_word(object, 16);
    if (parent == 0x221) {
        uint32 deallocate = cf_word(object, 40);
        uint32 arguments[] = {US_TO_SYN68K(object), info};
        if (deallocate && !ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(deallocate), arguments, 2, NULL))
            return 0;
    }
    if (release && !ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(release), &info, 1, NULL))
        return 0;
    return parent == 0x221 || cf_deallocate(SYN68K_TO_US_CHECK0(parent), object);
}

void ROMlib_carbon_cf_release(void* object)
{
    if (!object)
        return;
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFRelease", &reference))
        return;
    uint32 argument = US_TO_SYN68K(object);
    if (!cf_reference_is_14(&reference, 0x53488)) {
        ROMlib_carbon_invoke_callback(reference.function, &argument, 1, NULL);
        return;
    }
    uint32 count = cf_word(object, 4) >> 16;
    if (count) {
        if (count == 0x8000) {
            uint32 high;
            if (!cf_overflow(reference.toc, object, "CFDictionaryGetValue", 0, &high))
                return;
            if (high == 1) {
                if (!cf_overflow(reference.toc, object, "CFDictionaryRemoveValue", 0, NULL))
                    return;
                count = 0x7fff;
            }
            else {
                if (!cf_overflow(reference.toc, object, "CFDictionarySetValue", high - 1, NULL))
                    return;
                count = 0xffff;
            }
        }
        else
            --count;
        cf_store_word(object, 4, (count << 16) | (cf_word(object, 4) & 0xffff));
        return;
    }
    uint32 info = cf_word(object, 4), type = (info >> 8) & 255;
    if (type == 2) {
        cf_allocator_dispose(object);
        return;
    }
    const void* klass = cf_pointer(reference.toc, 0x65c4 + type * 4);
    if (!klass) {
        ROMlib_carbon_invoke_callback(NULL, &argument, 1, NULL);
        return;
    }
    uint32 finalize = cf_word(klass, 16);
    if (finalize && !ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(finalize), &argument, 1, NULL))
        return;
    if (cf_word(object, 4) & 0x80)
        cf_deallocate(cf_pointer(reference.toc, 0x3164), object);
    else {
        void* allocator = cf_object_allocator(object, reference.toc);
        if (cf_deallocate(allocator, (Byte*)object - 4))
            ROMlib_carbon_cf_release(allocator);
    }
}

/* Runtime instance allocation at 0x53204: a custom parent owns the four-byte
   prefix; system allocations carry the no-prefix flag. Class init runs before
   the caller initializes its own fields. */
static void* cf_instance_allocate(const void* allocator, uint32 type, uint32 size, const void* toc)
{
    if (size < 8 || type <= cf_word(toc, 0xa6c) || type >= cf_word(toc, 0xa68))
        return NULL;
    if (!allocator)
        allocator = ROMlib_carbon_cf_allocator_default();
    if (!allocator)
        return NULL;
    int system = allocator == (const void*)cf_pointer(toc, 0x3164);
    uint64 total = (((uint64)size - 8 + 3) & ~UINT64_C(3)) + 8 + (system ? 0 : 4);
    if (total > INT32_MAX)
        return NULL;
    Byte* allocation = ROMlib_carbon_cf_allocator_allocate(allocator, (LONGINT)total, 0);
    if (!allocation)
        return NULL;
    Byte* object = allocation;
    if (!system) {
        void* parent = ROMlib_carbon_cf_retain((void*)allocator);
        if (!parent)
            return NULL;
        cf_store_word(allocation, 0, US_TO_SYN68K(parent));
        object += 4;
    }
    cf_store_word(object, 0, 0);
    cf_store_word(object, 4, ((type & 255) << 8) | (system ? 0x80 : 0));
    const void* klass = cf_pointer(toc, 0x65c4 + type * 4);
    uint32 argument = US_TO_SYN68K(object);
    if (!klass) {
        ROMlib_carbon_invoke_callback(NULL, &argument, 1, NULL);
        return NULL;
    }
    uint32 initialize = cf_word(klass, 8);
    if (initialize && !ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(initialize), &argument, 1, NULL))
        return NULL;
    return object;
}

uint32 ROMlib_carbon_cf_array_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFArrayGetTypeID", &reference))
        return 0;
    if (cf_reference_is_14(&reference, 0x1b5c4))
        return cf_word(reference.toc, 0x8c0);
    return ROMlib_carbon_invoke_callback(reference.function, NULL, 0, &result) ? result : 0;
}

static int cf_array_callbacks_equal(const void* first, const void* second)
{
    for (unsigned i = 1; i < 5; ++i)
        if (cf_word(first, i * 4) != cf_word(second, i * 4))
            return 0;
    return 1;
}

void* ROMlib_carbon_cf_array_create(const void* allocator, const void* values, LONGINT count, const void* callbacks)
{
    ROMlib_carbon_reference reference;
    uint32 original_allocator = allocator ? US_TO_SYN68K(allocator) : 0, result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFArrayCreate", &reference))
        return NULL;
    if (!cf_reference_is_14(&reference, 0x1b8b4)) {
        uint32 arguments[] = {original_allocator, values ? US_TO_SYN68K(values) : 0, (uint32)count,
                              callbacks ? US_TO_SYN68K(callbacks) : 0};
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 4, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                        : NULL;
    }
    if (count < 0 || (count && !values))
        return NULL;
    if (callbacks) {
        const uint32 imported = US_TO_SYN68K(callbacks);
        const uint32 original = ROMlib_carbon_canonical_cf_callbacks(imported, reference.toc);
        if (original != imported)
            callbacks = SYN68K_TO_US(original);
    }
    const Byte* null_callbacks = (const Byte*)reference.toc + 0x3248;
    const Byte* type_callbacks = (const Byte*)reference.toc + 0x3234;
    unsigned kind = !callbacks || cf_array_callbacks_equal(callbacks, null_callbacks) ? 0
                    : cf_array_callbacks_equal(callbacks, type_callbacks)             ? 1
                                                                                      : 3;
    unsigned header = kind == 3 ? 32 : 12;
    uint64 size = header + (uint64)(uint32)count * 4;
    if (size > INT32_MAX)
        return NULL;
    Byte* array = cf_instance_allocate(allocator, cf_word(reference.toc, 0x8c0), (uint32)size, reference.toc);
    if (!array)
        return NULL;
    cf_store_word(array, 4, (cf_word(array, 4) & ~UINT32_C(0x7f)) | (kind << 2));
    cf_store_word(array, 8, 0);
    const void* selected = kind == 0 ? null_callbacks : type_callbacks;
    if (kind == 3) {
        memmove(array + 12, callbacks, 20);
        selected = array + 12;
    }
    for (LONGINT i = 0; i < count; ++i) {
        uint32 value = cf_word(values, (size_t)(uint32)i * 4), retain = cf_word(selected, 4);
        if (retain) {
            uint32 arguments[] = {original_allocator, value};
            if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(retain), arguments, 2, &value))
                return NULL;
        }
        cf_store_word(array, header + (size_t)(uint32)i * 4, value);
    }
    cf_store_word(array, 8, (uint32)count);
    return array;
}

/* CFAbsoluteTimeGetCurrent 0x232d8 uses the unsigned classic GetDateTime
   second count and subtracts the 1904-to-2001 epoch interval. CFDate readers
   0x23474-0x234b4 operate on a big-endian IEEE double at object offset 8. */
double ROMlib_carbon_cf_absolute_time(void)
{
    LONGINT encoded;
    GetDateTime(&encoded);
    return (double)(uint32)CL(encoded) - 3061152000.0;
}

double ROMlib_carbon_cf_date_time(const void* date)
{
    if (!date)
        return 0;
    uint64 bits = ((uint64)cf_word(date, 8) << 32) | cf_word(date, 12);
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

double ROMlib_carbon_cf_date_interval(const void* date, const void* other)
{
    return ROMlib_carbon_cf_date_time(date) - ROMlib_carbon_cf_date_time(other);
}

LONGINT ROMlib_carbon_cf_date_compare(const void* date, const void* other, uint32 context)
{
    (void)context;
    double first = ROMlib_carbon_cf_date_time(date), second = ROMlib_carbon_cf_date_time(other);
    return first < second ? -1 : first > second ? 1 : 0;
}

/* CFStringGetLength 0x38858 and CFStringGetCharactersPtr 0x39228 distinguish
   inline storage from external buffers and optional Pascal length bytes. */
static const Byte* cf_string_contents(const void* string, uint32 info)
{
    return info & 0x60 ? cf_pointer(string, 8) : (const Byte*)string + ((info & 5) == 4 ? 8 : 12);
}

static LONGINT cf_string_length_value(const void* string, int cache)
{
    if (!string)
        return 0;
    uint32 info = cf_word(string, 4);
    if ((info & 5) == 4) {
        const Byte* contents = cf_string_contents(string, info);
        return contents ? contents[0] : 0;
    }
    if (!(info & 0x60))
        return cf_word(string, 8);
    uint32 length = cf_word(string, 12);
    if (length == 0x00ffffff) {
        const Byte* contents = cf_string_contents(string, info);
        if (!contents)
            return 0;
        length = strlen((const char*)contents);
        if (cache) {
            uint32 encoded = CL(length);
            memcpy((Byte*)string + 12, &encoded, sizeof encoded);
        }
    }
    return length;
}

LONGINT ROMlib_carbon_cf_string_length(void* string)
{
    return cf_string_length_value(string, 1);
}

const void* ROMlib_carbon_cf_string_characters(const void* string)
{
    if (!string)
        return NULL;
    uint32 info = cf_word(string, 4);
    return info & 0x10 ? cf_string_contents(string, info) : NULL;
}

/* Built-in single-byte decoders keep their live tables and refusal rules.
   The character readers replace a rejected byte with 0xfffe. MacRoman accepts
   every table word; Windows/NeXT reject the replacement value 0xfffd. */
static int cf_string_decode_builtin_byte(const ROMlib_carbon_reference* reference, uint32 procedure, uint16 value,
                                         Byte* output)
{
    uint32 table = 0;
    int reject_replacement = 0;
    if (cf_callback_is_builtin(reference, procedure, 0x1fb4, 0x4ce50))
        table = 0x52f8;
    else if (cf_callback_is_builtin(reference, procedure, 0x1fe4, 0x4ccb0))
        value = 0xfffe; /* ASCII refuses every high byte. */
    else if (cf_callback_is_builtin(reference, procedure, 0x1fcc, 0x4cd08)) {
        /* ISO Latin-1 is an identity mapping. */
    }
    else if (cf_callback_is_builtin(reference, procedure, 0x1f9c, 0x4cfc8)) {
        if (value <= 0x9f)
            table = 0x548c;
        reject_replacement = 1;
    }
    else if (cf_callback_is_builtin(reference, procedure, 0x1f84, 0x4d164)) {
        table = 0x56f4;
        reject_replacement = 1;
    }
    else
        return 0;
    if (table) {
        uint16 encoded;
        memcpy(&encoded, (const Byte*)reference->toc + table + (value - 128) * 2, 2);
        value = CW(encoded);
        if (reject_replacement && value == 0xfffd)
            value = 0xfffe;
    }
    uint16 encoded = CW(value);
    memcpy(output, &encoded, 2);
    return 1;
}

/* CFStringGetCharacterAtIndex 0x3891c and GetCharacters 0x38a0c share the
   current byte-to-Unicode converter at TOC+0x6f5c. A failed conversion emits
   0xfffe; with no converter installed each byte widens unchanged. */
static int cf_string_decode_bytes(const ROMlib_carbon_reference* reference, const Byte* input, Byte* output,
                                  uint32 count)
{
    const void* toc = reference->toc;
    int converting = cf_word(toc, 0x6f5c) != 0;
    for (uint32 i = 0; i < count; ++i) {
        uint16 value = input[i];
        if (converting && value >= 128) {
            uint32 procedure = cf_word(toc, 0x6f5c);
            if (cf_string_decode_builtin_byte(reference, procedure, value, output + (size_t)i * 2))
                continue;
            uint32 arguments[] = {0, value, US_TO_SYN68K(output + (size_t)i * 2)}, result;
            if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), arguments, 3, &result))
                return 0;
            if (result & 255)
                continue;
            value = 0xfffe;
        }
        uint16 encoded = CW(value);
        memcpy(output + (size_t)i * 2, &encoded, sizeof encoded);
    }
    return 1;
}

uint32 ROMlib_carbon_cf_string_character(const void* string, LONGINT index)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {string ? US_TO_SYN68K(string) : 0, (uint32)index}, result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFStringGetCharacterAtIndex", &reference))
        return 0;
    if (!cf_reference_is_14(&reference, 0x3891c))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 2, &result) ? result & 65535 : 0;
    if (!string || index < 0 || index >= cf_string_length_value(string, 0))
        return 0;
    uint32 info = cf_word(string, 4);
    const Byte* contents = cf_string_contents(string, info);
    uint16 encoded = 0;
    if (!contents)
        return 0;
    if (info & 0x10)
        memcpy(&encoded, contents + (size_t)(uint32)index * 2, sizeof encoded);
    else if (!cf_string_decode_bytes(&reference, contents + !!(info & 4) + index, (Byte*)&encoded, 1))
        return 0;
    return CW(encoded);
}

void ROMlib_carbon_cf_string_get_characters(const void* string, LONGINT location, LONGINT length, void* output)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {string ? US_TO_SYN68K(string) : 0, (uint32)location, (uint32)length,
                          output ? US_TO_SYN68K(output) : 0};
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFStringGetCharacters", &reference))
        return;
    if (!cf_reference_is_14(&reference, 0x38a0c)) {
        ROMlib_carbon_invoke_callback(reference.function, arguments, 4, NULL);
        return;
    }
    if (!string || !output || location < 0 || length <= 0 ||
        (int64)location + length > cf_string_length_value(string, 0))
        return;
    uint32 info = cf_word(string, 4);
    const Byte* contents = cf_string_contents(string, info);
    if (!contents)
        return;
    if (info & 0x10)
        memmove(output, contents + (size_t)(uint32)location * 2, (size_t)(uint32)length * 2);
    else
        cf_string_decode_bytes(&reference, contents + !!(info & 4) + location, output, (uint32)length);
}

static int cf_call_reference(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

static Ptr cf_scratch_allocate(Size size)
{
    int16 saved_mem_error = MemErr;
    Ptr scratch = NewPtrSys(size);
    MemErr = saved_mem_error;
    return scratch;
}

static void cf_scratch_dispose(Ptr scratch)
{
    int16 saved_mem_error = MemErr;
    DisposPtr(scratch);
    MemErr = saved_mem_error;
}

static int cf_string_read_units(const ROMlib_carbon_reference* reference, const void* string, uint32 location,
                                uint32 length, Byte* output)
{
    if (!length)
        return 1;
    uint32 info = cf_word(string, 4);
    const Byte* contents = cf_string_contents(string, info);
    if (!contents)
        return 0;
    if (info & 0x10) {
        memmove(output, contents + (size_t)location * 2, (size_t)length * 2);
        return 1;
    }
    return cf_string_decode_bytes(reference, contents + !!(info & 4) + location, output, length);
}

static int cf_encoding_accepts_ascii(uint32 encoding);

/* Built-in UTF-8 callback 0x4d248 ignores flags and measures when capacity is
   zero, even with an output pointer. It accepts lone low surrogates, stops at
   lone high surrogates, and rolls back only one unit if a pair will not fit. */
static uint32 cf_utf8_encode(const void* toc, const Byte* units, uint32 length, Byte* output, uint32 capacity,
                             void* used)
{
    uint32 count = 0, bytes = 0;
    while (count < length && (!capacity || bytes < capacity)) {
        uint16 encoded;
        memcpy(&encoded, units + (size_t)count++ * 2, sizeof encoded);
        uint32 value = CW(encoded);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (count == length) {
                --count;
                break;
            }
            memcpy(&encoded, units + (size_t)count * 2, sizeof encoded);
            uint32 low = CW(encoded);
            if (low < 0xdc00 || low > 0xdfff) {
                --count;
                break;
            }
            value = ((value - 0xd800) << 10) + low + 0x2400;
            ++count;
        }
        uint32 size = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
        if (capacity) {
            if (size > capacity - bytes) {
                --count;
                break;
            }
            uint32 remaining = value;
            for (uint32 i = size - 1; i; --i) {
                output[bytes + i] = (Byte)((remaining | 0x80) & 0xbf);
                remaining >>= 6;
            }
            output[bytes] = (Byte)(remaining | ((const Byte*)toc)[0x5934 + size]);
        }
        bytes += size;
    }
    if (used)
        cf_store_word(used, 0, bytes);
    return count;
}

static int cf_utf8_is_builtin(const ROMlib_carbon_reference* reference, uint32 procedure)
{
    return cf_callback_is_builtin(reference, procedure, 0x1f5c, 0x4d248);
}

/* Partial migration of __CFStringEncodeByteStream 0x419b8. Keep its export
   unregistered until generic converter dispatch is native. Unknown profiles
   and unported cases invoke the original entry with all nine guest words. */
int ROMlib_carbon_cf_encode_stream(const uint32 arguments[9], uint32* result)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "__CFStringEncodeByteStream", &reference))
        return 0;
    if (!cf_reference_is_14(&reference, 0x419b8))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 9, result);
    const void* string = SYN68K_TO_US_CHECK0(arguments[0]);
    uint32 location = arguments[1], length = arguments[2], external = arguments[3] & 255, encoding = arguments[4];
    Byte* output = SYN68K_TO_US_CHECK0(arguments[6]);
    uint32 capacity = arguments[7];
    void* used = SYN68K_TO_US_CHECK0(arguments[8]);
    LONGINT total = cf_string_length_value(string, 0);
    if (!string || total < 0 || location > (uint32)total || length > (uint32)total - location ||
        (output && capacity > INT32_MAX))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 9, result);
    uint32 info = cf_word(string, 4);
    if (encoding == 0x100 && (!output || !external || capacity >= 2)) {
        uint32 mark = external ? 2 : 0, count = length;
        if (output && (uint64)count * 2 + mark > capacity)
            count = capacity > mark ? (capacity - mark) / 2 : 0;
        if (output) {
            if (mark) {
                output[0] = 0xfe;
                output[1] = 0xff;
            }
            /* The 1.4 Unicode branch reads from zero, even when location is
               nonzero. Preserve this reference behavior while migrating. */
            if (!cf_string_read_units(&reference, string, 0, count, output + mark))
                return 0;
        }
        if (used)
            cf_store_word(used, 0, count * 2 + mark);
        *result = count;
        return 1;
    }
    if (encoding == 0xbff) {
        const Byte* literals = cf_pointer(reference.toc, 0x5a8);
        if (literals) {
            uint32 count = 0, bytes = 0, cursor = location;
            Ptr buffer = NULL;
            if (cursor < length) {
                buffer = cf_scratch_allocate(64);
                if (!buffer)
                    return 0;
            }
            int ok = 1;
            while (cursor < length) {
                uint32 chunk = MIN(UINT32_C(32), length - cursor);
                if (!cf_string_read_units(&reference, string, cursor, chunk, (Byte*)buffer)) {
                    ok = 0;
                    break;
                }
                for (uint32 i = 0; i < chunk; ++i) {
                    uint16 encoded;
                    memcpy(&encoded, buffer + i * 2, sizeof encoded);
                    uint32 value = CW(encoded);
                    if (value == 0xffff)
                        goto nonlossy_done;
                    Byte token[6];
                    uint32 size;
                    if ((value >= 0x20 && value <= 0x7e && value != '\\') || value == 9 || value == 10 || value == 13) {
                        token[0] = (Byte)value;
                        size = 1;
                    }
                    else {
                        token[0] = '\\';
                        if (value == '\\') {
                            token[1] = '\\';
                            size = 2;
                        }
                        else if (value < 256) {
                            token[1] = (Byte)('0' + (value >> 6));
                            token[2] = (Byte)('0' + ((value >> 3) & 7));
                            token[3] = (Byte)('0' + (value & 7));
                            size = 4;
                        }
                        else {
                            token[1] = 'u';
                            for (unsigned digit = 0; digit < 4; ++digit)
                                token[digit + 2] = literals[0x1b + ((value >> (12 - digit * 4)) & 15)];
                            size = 6;
                        }
                    }
                    if (output) {
                        if ((uint64)bytes + size > capacity)
                            goto nonlossy_done;
                        memcpy(output + bytes, token, size);
                    }
                    bytes += size;
                    ++count;
                }
                cursor += chunk;
            }
        nonlossy_done:
            if (buffer)
                cf_scratch_dispose(buffer);
            if (!ok)
                return 0;
            if (used)
                cf_store_word(used, 0, bytes);
            *result = count;
            return 1;
        }
    }
    if (encoding == 0x8000100 && (info & 0x10) && used) {
        uint32 procedure = cf_word(reference.toc, 0x9ac);
        if (!procedure && cf_word(reference.toc, 0x3e88) == 0x8000100) {
            procedure = cf_word(reference.toc, 0x593c);
            if (procedure)
                cf_store_word((void*)reference.toc, 0x9ac, procedure);
        }
        const Byte* contents = cf_string_contents(string, info);
        if (procedure && contents) {
            const void* callback = SYN68K_TO_US_CHECK0(procedure);
            /* Only replace the verified built-in descriptor. A changed cache
               or descriptor still dispatches its live original callback. */
            if (cf_utf8_is_builtin(&reference, procedure)) {
                *result = cf_utf8_encode(reference.toc, contents + (size_t)location * 2, length, output,
                                         output ? capacity : 0, used);
                return 1;
            }
            uint32 convert[] = {external ? 0x100 : 0,
                                US_TO_SYN68K(contents + (size_t)location * 2),
                                length,
                                arguments[6],
                                output ? capacity : 0,
                                arguments[8]};
            return ROMlib_carbon_invoke_callback((void*)callback, convert, 6, result);
        }
    }
    uint32 ascii_prefix = 0;
    const Byte* prefix_input = NULL;
    if (!(info & 0x10) && cf_encoding_accepts_ascii(encoding)) {
        uint32 current = cf_word(reference.toc, 0x3a70);
        /* Cold initialization and unported mixed tails remain on the original path.
           Do not publish prefix output/counts before delegating the whole
           call: invalid generic converters can return without publishing. */
        if (current != UINT32_MAX) {
            const Byte* bytes = ROMlib_carbon_cf_string_c_pointer((void*)string, current);
            if (!bytes) {
                bytes = ROMlib_carbon_cf_string_pascal_pointer((void*)string, current);
                if (bytes)
                    ++bytes;
            }
            if (bytes) {
                bytes += location;
                uint32 prefix = length;
                if (encoding != cf_word(reference.toc, 0x3a70)) {
                    prefix = 0;
                    while (prefix < length && bytes[prefix] < 128)
                        ++prefix;
                }
                if (prefix == length || (output && capacity <= prefix)) {
                    uint32 count = output ? MIN(prefix, capacity) : prefix;
                    if (output && count)
                        memmove(output, bytes, count);
                    if (used)
                        cf_store_word(used, 0, count);
                    *result = count;
                    return 1;
                }
                ascii_prefix = prefix;
                prefix_input = bytes;
            }
        }
    }
    if (!(info & 0x10) && encoding == 0x8000100 && cf_word(reference.toc, 0x3a70) != UINT32_MAX &&
        cf_word(reference.toc, 0x9a8) != UINT32_MAX) {
        uint32 procedure = cf_word(reference.toc, 0x9c4);
        if (!procedure && cf_word(reference.toc, 0x3e88) == encoding)
            procedure = cf_word(reference.toc, 0x593c);
        if (cf_utf8_is_builtin(&reference, procedure)) {
            /* The generic byte path decodes at most 512 units per call.
               Keep cold preference initialization and other codecs on fallback. */
            uint32 count = ascii_prefix, bytes = ascii_prefix, remaining = length - ascii_prefix;
            Ptr scratch = remaining ? cf_scratch_allocate(1028) : NULL;
            if (remaining && !scratch)
                return 0;
            if (output && ascii_prefix)
                memmove(output, prefix_input, ascii_prefix);
            int ok = 1;
            uint32 available = output ? capacity - ascii_prefix : 0;
            while (remaining) {
                uint32 chunk = MIN(remaining, UINT32_C(512));
                if (!cf_string_read_units(&reference, string, location + count, chunk, (Byte*)scratch)) {
                    ok = 0;
                    break;
                }
                /* Decoding can invoke guest code. Observe a replaced cache or
                   descriptor before each encode, as the original helper does. */
                procedure = cf_word(reference.toc, 0x9c4);
                if (!procedure && cf_word(reference.toc, 0x3e88) == encoding) {
                    procedure = cf_word(reference.toc, 0x593c);
                    cf_store_word((void*)reference.toc, 0x9c4, procedure);
                }
                Byte* destination = output && available ? output + bytes : output;
                uint32 converted;
                if (cf_utf8_is_builtin(&reference, procedure))
                    converted =
                        cf_utf8_encode(reference.toc, (Byte*)scratch, chunk, destination, available, scratch + 1024);
                else {
                    uint32 convert[] = {0,         US_TO_SYN68K(scratch),       chunk, US_TO_SYN68K(destination),
                                        available, US_TO_SYN68K(scratch + 1024)};
                    if (!ROMlib_carbon_invoke_callback(SYN68K_TO_US_CHECK0(procedure), convert, 6, &converted)) {
                        ok = 0;
                        break;
                    }
                }
                uint32 written = cf_word(scratch, 1024);
                count += converted;
                bytes += written;
                if (converted != chunk)
                    break;
                remaining -= converted;
                if (available) {
                    available -= written;
                    if ((int32)available <= 0)
                        break;
                }
            }
            if (scratch)
                cf_scratch_dispose(scratch);
            if (!ok)
                return 0;
            if (used)
                cf_store_word(used, 0, bytes);
            *result = count;
            return 1;
        }
    }
    return ROMlib_carbon_invoke_callback(reference.function, arguments, 9, result);
}

/* The ASCII-superset test at 0x39398 operates on the encoding's group byte,
   but compares complete encoding words for each group's exclusions. */
static int cf_encoding_accepts_ascii(uint32 encoding)
{
    switch (encoding & 0xff00) {
    case 0:
        return encoding != 1 && encoding != 4 && encoding != 5 && encoding != 0x98 && encoding != 0x21 &&
               encoding != 0x22;
    case 0x100:
        return encoding != 0x100;
    case 0x200:
        return encoding != 0x206;
    case 0x600:
        return encoding == 0x600;
    case 0x800:
    case 0xc00:
        return 0;
    case 0xa00:
        return encoding != 0xa01 && encoding != 0xafe;
    case 0xb00:
        return encoding != 0xbff;
    default:
        return (encoding & 0xff00) <= 0xc00;
    }
}

static int cf_encoding_matches(const void* toc, uint32 encoding)
{
    uint32 current = cf_word(toc, 0x3a70), result;
    if (current == UINT32_MAX && !cf_call_reference("__CFStringComputeEightBitStringEncoding", NULL, 0, &result))
        return -1;
    current = cf_word(toc, 0x3a70);
    if (encoding == current)
        return 1;
    if (current == UINT32_MAX && !cf_call_reference("__CFStringComputeEightBitStringEncoding", NULL, 0, &result))
        return -1;
    return cf_word(toc, 0x3a70) == 0x600 && cf_encoding_accepts_ascii(encoding);
}

static const void* cf_string_byte_pointer(void* string, uint32 encoding, int pascal_format)
{
    const char* name = pascal_format ? "CFStringGetPascalStringPtr" : "CFStringGetCStringPtr";
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(string), encoding}, result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference))
        return NULL;
    if (!cf_reference_is_14(&reference, pascal_format ? 0x38d70 : 0x38ff4))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 2, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                        : NULL;
    if (!string)
        return NULL;
    uint32 info = cf_word(string, 4);
    /* Pascal format rejection precedes encoding initialization. CStringPtr
       initializes first, then checks its current null-termination flag. */
    if (pascal_format && (!(info & 4) || (info & 0x10)))
        return NULL;
    if (cf_encoding_matches(reference.toc, encoding) != 1)
        return NULL;
    info = cf_word(string, 4);
    if (!pascal_format && !(info & 8))
        return NULL;
    const Byte* contents = cf_string_contents(string, info);
    if (!contents)
        return NULL;
    if (!pascal_format)
        return contents + !!(info & 4);
    if ((info & 5) != 4 && contents[0] != cf_string_length_value(string, 1))
        return NULL;
    return contents;
}

const void* ROMlib_carbon_cf_string_c_pointer(void* string, uint32 encoding)
{
    return cf_string_byte_pointer(string, encoding, 0);
}

const void* ROMlib_carbon_cf_string_pascal_pointer(void* string, uint32 encoding)
{
    return cf_string_byte_pointer(string, encoding, 1);
}

/* CFStringGetPascalString 0x39274 caps source character count at 255;
   CFStringGetCString 0x395a0 instead appends NUL at the encoded-byte count. Byte
   strings can copy directly for the shared eight-bit encoding or compatible
   ASCII targets. Other conversions retain the real byte-stream engine. */
static uint32 cf_string_copy(void* string, void* output, LONGINT capacity, uint32 encoding, int pascal_format)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(string), US_TO_SYN68K(output), (uint32)capacity, encoding}, result;
    const char* name = pascal_format ? "CFStringGetPascalString" : "CFStringGetCString";
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference))
        return 0;
    if (!cf_reference_is_14(&reference, pascal_format ? 0x39274 : 0x395a0))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 4, &result) ? result & 255 : 0;
    if (capacity < 1 || !string || !output)
        return 0;
    uint32 info = cf_word(string, 4);
    const Byte* contents = cf_string_contents(string, info);
    LONGINT length = cf_string_length_value(string, 1);
    if (length < 0 || (pascal_format && length > 255))
        return 0;
    if (!(info & 0x10)) {
        int copy = cf_encoding_matches(reference.toc, encoding);
        if (copy < 0)
            return 0;
        if (copy) {
            if (length >= capacity || (length && !contents))
                return 0;
            if (length)
                memmove((Byte*)output + pascal_format, contents + !!(cf_word(string, 4) & 4), (size_t)length);
            ((Byte*)output)[pascal_format ? 0 : length] = pascal_format ? (Byte)length : 0;
            return 1;
        }
    }
    Ptr used_bytes = cf_scratch_allocate(4);
    if (!used_bytes)
        return 0;
    uint32 convert[] = {US_TO_SYN68K(string),
                        0,
                        (uint32)length,
                        0,
                        encoding,
                        0,
                        US_TO_SYN68K((Byte*)output + pascal_format),
                        (uint32)capacity - 1,
                        US_TO_SYN68K(used_bytes)};
    int ok = ROMlib_carbon_cf_encode_stream(convert, &result);
    if (ok) {
        if (result == (uint32)length) {
            uint32 bytes = cf_word(used_bytes, 0);
            ((Byte*)output)[pascal_format ? 0 : bytes] = pascal_format ? (Byte)bytes : 0;
        }
        else
            ((Byte*)output)[0] = 0;
    }
    cf_scratch_dispose(used_bytes);
    return ok && result == (uint32)length;
}

uint32 ROMlib_carbon_cf_string_pascal(void* string, void* output, LONGINT capacity, uint32 encoding)
{
    return cf_string_copy(string, output, capacity, encoding, 1);
}

uint32 ROMlib_carbon_cf_string_c(void* string, void* output, LONGINT capacity, uint32 encoding)
{
    return cf_string_copy(string, output, capacity, encoding, 0);
}

LONGINT ROMlib_carbon_cf_string_bytes(void* string, LONGINT location, LONGINT length, uint32 encoding, uint32 loss_byte,
                                      uint32 external, void* output, LONGINT capacity, void* used)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(string), (uint32)location, (uint32)length,    encoding, loss_byte, external,
                          US_TO_SYN68K(output), (uint32)capacity, US_TO_SYN68K(used)},
           result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFStringGetBytes", &reference))
        return 0;
    if (!cf_reference_is_14(&reference, 0x38ac8))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 9, &result) ? result : 0;
    LONGINT total = cf_string_length_value(string, 0);
    if (!string || location < 0 || length < 0 || total < 0 || location > total || length > total - location ||
        (output && capacity < 0))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 9, &result) ? result : 0;
    if (!(cf_word(string, 4) & 0x10)) {
        int copy = cf_encoding_matches(reference.toc, encoding);
        if (copy < 0)
            return 0;
        if (copy) {
            uint32 info = cf_word(string, 4);
            const Byte* contents = cf_string_contents(string, info);
            uint32 count = output ? MIN((uint32)length, (uint32)capacity) : (uint32)length;
            if (output && count) {
                if (!contents)
                    return 0;
                memmove(output, contents + !!(info & 4) + location, count);
            }
            if (used)
                cf_store_word(used, 0, count);
            return count;
        }
    }
    uint32 convert[] = {arguments[0], arguments[1], arguments[2], external,    encoding,
                        loss_byte,    arguments[6], arguments[7], arguments[8]};
    return ROMlib_carbon_cf_encode_stream(convert, &result) ? result : 0;
}

uint32 ROMlib_carbon_cf_string_fastest(const void* string)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(string)}, result;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFStringGetFastestEncoding", &reference))
        return UINT32_MAX;
    if (!cf_reference_is_14(&reference, 0x3cfa8))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 1, &result) ? result : UINT32_MAX;
    if (!string)
        return UINT32_MAX;
    if (cf_word(string, 4) & 0x10)
        return 0x100;
    if (cf_word(reference.toc, 0x3a70) == UINT32_MAX &&
        !cf_call_reference("__CFStringComputeEightBitStringEncoding", NULL, 0, &result))
        return UINT32_MAX;
    return cf_word(reference.toc, 0x3a70);
}

/* The CFM bundle helpers at 0xf5b0/0xf7bc load on demand, convert names to
   ASCII Pascal strings and require the exact CFM symbol class. The real
   loader, string converter and logging remain dependencies, as in the PEF. */
static void* cf_bundle_symbol(void* bundle, const void* name, int data_symbol)
{
    const char* export_name = data_symbol ? "CFBundleGetDataPointerForName" : "CFBundleGetFunctionPointerForName";
    ROMlib_carbon_reference reference;
    uint32 result, arguments[] = {US_TO_SYN68K(bundle), US_TO_SYN68K(name), 0, 0};
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", export_name, &reference))
        return NULL;
    if (!cf_reference_is_14(&reference, data_symbol ? 0xf7bc : 0xf5b0))
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 2, &result) ? SYN68K_TO_US_CHECK0(result)
                                                                                        : NULL;
    if (!bundle)
        return NULL;
    if (!((Byte*)bundle)[36] &&
        (!cf_call_reference("CFBundleLoadExecutable", arguments, 1, &result) || !(result & 255)))
        return NULL;
    if (cf_word(bundle, 32) != 1)
        return NULL;
    /* Guest-backed scratch avoids alias-page boundaries and leaves application
       partition ownership alone. */
    Ptr scratch = cf_scratch_allocate(256);
    if (!scratch)
        return NULL;
    void* answer = NULL;
    result = ROMlib_carbon_cf_string_pascal((void*)name, scratch, 255, 0x600);
    if (result & 255) {
        uint32 address = 0;
        unsigned symbol_class = 0;
        int32 error = ROMlib_carbon_find_symbol(cf_word(bundle, 40), (unsigned char*)scratch, &address, &symbol_class);
        if (!error && symbol_class == (data_symbol ? 1 : 2))
            answer = SYN68K_TO_US_CHECK0(address);
        else {
            const Byte* messages = cf_pointer(reference.toc, 0x408);
            arguments[0] = US_TO_SYN68K(messages + (error ? 0x50f : 0x52c));
            if (cf_call_reference("__CFStringMakeConstantString", arguments, 1, &result)) {
                arguments[0] = 21;
                arguments[1] = result;
                arguments[2] = error ? (int16)error : symbol_class;
                arguments[3] = US_TO_SYN68K(scratch);
                cf_call_reference("CFLog", arguments, 4, &result);
            }
        }
    }
    cf_scratch_dispose(scratch);
    return answer;
}

void* ROMlib_carbon_cf_bundle_function(void* bundle, const void* name)
{
    return cf_bundle_symbol(bundle, name, 0);
}

void* ROMlib_carbon_cf_bundle_data(void* bundle, const void* name)
{
    return cf_bundle_symbol(bundle, name, 1);
}

/* CFUniCharIsMemberOf 0x461fc dispatches three algorithmic sets and twelve
   live packed sets. Their summary bits select 1024-character blocks; sorted
   singletons precede inclusive ranges after the 36-byte header. */
static int cf_unichar_packed_member(const void* set, uint32 character)
{
    if (!set || cf_word(set, 0) != 1)
        return 0;
    uint32 summary = cf_word(set, 4 + (character >> 15) * 4);
    if (!(summary & (UINT32_C(1) << ((character >> 10) & 31))))
        return 0;
    uint32 singles = cf_word(set, 20), ranges = cf_word(set, 24);
    if (singles > 65536 || ranges > 65536)
        return 0;
    if (character >= cf_word(set, 28) && character <= cf_word(set, 32)) {
        uint32 low = 0, high = singles;
        while (low < high) {
            uint32 middle = low + (high - low) / 2, value = cf_word(set, 36 + (size_t)middle * 4);
            if (character == value)
                return 1;
            if (character < value)
                high = middle;
            else
                low = middle + 1;
        }
    }
    uint32 low = 0, high = ranges;
    size_t start = 36 + (size_t)singles * 4;
    while (low < high) {
        uint32 middle = low + (high - low) / 2;
        size_t offset = start + (size_t)middle * 8;
        if (character < cf_word(set, offset))
            high = middle;
        else if (character > cf_word(set, offset + 4))
            low = middle + 1;
        else
            return 1;
    }
    return 0;
}

uint32 ROMlib_carbon_cf_unichar_member(uint32 character, uint32 charset)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", "CFUniCharIsMemberOf", &reference))
        return 0;
    if (!cf_reference_is_14(&reference, 0x461fc)) {
        uint32 arguments[] = {character, charset}, result;
        return ROMlib_carbon_invoke_callback(reference.function, arguments, 2, &result) ? result & 255 : 0;
    }
    character &= 65535;
    if (charset == 1)
        return character <= 0x1f || (character >= 0x7f && character <= 0x9f);
    if (charset == 2 || charset == 3) {
        int whitespace = character == 0x20 || character == 0xa0 || (character >= 0x2000 && character <= 0x200b) ||
                         character == 0x3000;
        return whitespace ||
               (charset == 3 && ((character >= 9 && character <= 13) || character == 0x2028 || character == 0x2029));
    }
    static const uint16 offsets[] = {0x348, 0x344, 0x340, 0x33c, 0x338, 0x330,
                                     0x334, 0x32c, 0x328, 0x324, 0x320, 0x31c};
    return charset >= 4 && charset <= 15 &&
           cf_unichar_packed_member(cf_pointer(reference.toc, offsets[charset - 4]), character);
}

/* CFArray's storage form uses four-byte elements in a three-way CFStorage
   tree. Read the existing leaves directly; an untouched lookup cache remains
   valid for subsequent calls into the guest implementation. */
static const Byte* cf_array_storage_slot(const Byte* storage, uint32 index)
{
    if (!storage || cf_word(storage, 8) != 4)
        return NULL;
    uint64 offset = (uint64)index * 4;
    const Byte* node = storage + 24;
    for (unsigned depth = 0; depth < 64; ++depth) {
        if (offset >= cf_word(node, 0))
            return NULL;
        if (node[4]) {
            const Byte* bytes = cf_pointer(node, 12);
            return bytes ? bytes + (size_t)offset : NULL;
        }
        const Byte* next = NULL;
        for (unsigned child = 0; child < 3; ++child) {
            const Byte* candidate = cf_pointer(node, 8 + child * 4);
            if (!candidate)
                return NULL;
            uint32 size = cf_word(candidate, 0);
            if (offset < size) {
                next = candidate;
                break;
            }
            offset -= size;
        }
        if (!next)
            return NULL;
        node = next;
    }
    return NULL;
}

uint32 ROMlib_carbon_cf_array_value(const void* array, LONGINT index)
{
    if (!array || index < 0 || (uint32)index >= cf_word(array, 8))
        return 0;
    uint32 info = cf_word(array, 4), kind = info & 3;
    const Byte* slot;
    if (kind < 2) {
        unsigned header = kind ? 16 : 12;
        if (((info >> 2) & 3) == 3)
            header += 20;
        slot = (const Byte*)array + header + (size_t)(uint32)index * 4;
    }
    else if (kind == 2) {
        const Byte* deque = cf_pointer(array, 12);
        if (!deque)
            return 0;
        slot = deque + 4 + ((size_t)deque[0] + (uint32)index) * 4;
    }
    else
        slot = cf_array_storage_slot(cf_pointer(array, 12), index);
    /* Values are opaque guest bits, not necessarily mapped memory addresses.
       Return them as a scalar so the bridge preserves even tagged/sentinel
       pointers without attempting a host pointer translation. */
    return slot ? cf_word(slot, 0) : 0;
}

void ROMlib_carbon_cf_array_values(const void* array, LONGINT location, LONGINT length, void* output)
{
    if (!array || location < 0 || length <= 0 || !output)
        return;
    uint32 count = cf_word(array, 8);
    if ((uint32)location > count || (uint32)length > count - (uint32)location)
        return;
    for (uint32 i = 0; i < (uint32)length; ++i) {
        uint32 encoded = CL(ROMlib_carbon_cf_array_value(array, location + i));
        memcpy((Byte*)output + (size_t)i * 4, &encoded, sizeof encoded);
    }
}

LONGINT ROMlib_carbon_cf_array_count(const void* array)
{
    return array ? (LONGINT)cf_word(array, 8) : 0;
}

/* CFArrayApplyFunction 0x1c328 borrows each element and passes it, followed
   by the opaque context, to the caller's native C function pointer. */
void ROMlib_carbon_cf_array_apply(const void* array, LONGINT location, LONGINT length, void* function, uint32 context)
{
    if (!array || location < 0 || length <= 0)
        return;
    uint32 count = cf_word(array, 8);
    if ((uint32)location > count || (uint32)length > count - (uint32)location)
        return;
    for (uint32 i = 0; i < (uint32)length; ++i) {
        uint32 arguments[] = {ROMlib_carbon_cf_array_value(array, location + i), context};
        if (!ROMlib_carbon_invoke_callback(function, arguments, 2, NULL))
            return;
    }
}

static int cf_array_compare(void* function, uint32 first, uint32 second, uint32 context, LONGINT* order)
{
    uint32 arguments[] = {first, second, context}, result;
    if (!ROMlib_carbon_invoke_callback(function, arguments, 3, &result))
        return 0;
    *order = (LONGINT)result;
    return 1;
}

/* CFArrayBSearchValues 0x1dc2c and its contiguous search helper 0x1a124.
   Keep the verified 1.4 CFStorage pivot quirk for nonzero range locations. */
LONGINT ROMlib_carbon_cf_array_search(const void* array, LONGINT location, LONGINT length, uint32 value, void* function,
                                      uint32 context)
{
    if (length <= 0 || !array || location < 0)
        return location;
    uint32 count = cf_word(array, 8);
    if ((uint32)location > count || (uint32)length > count - (uint32)location || length > INT32_MAX - location)
        return location;
    BOOLEAN storage = (cf_word(array, 4) & 3) == 3;
    LONGINT order;
    if (!storage && length < 4) {
        for (LONGINT i = length; i > 0; --i) {
            if (!cf_array_compare(function, ROMlib_carbon_cf_array_value(array, location + i - 1), value, context,
                                  &order))
                return location;
            if (order < 0)
                return location + i;
        }
        return location;
    }
    if (!cf_array_compare(function, ROMlib_carbon_cf_array_value(array, location + length - 1), value, context, &order))
        return location;
    if (order < 0)
        return location + length;
    if (!cf_array_compare(function, value, ROMlib_carbon_cf_array_value(array, location), context, &order))
        return location;
    if (order < 0)
        return location;
    LONGINT step = 1;
    while (step <= length / 2)
        step <<= 1;
    if (!cf_array_compare(function, ROMlib_carbon_cf_array_value(array, location + step - 1), value, context, &order))
        return location;
    LONGINT base = order < 0 || (storage && location != 0) ? length - step : -1;
    for (step >>= 1; step; step >>= 1) {
        if (!cf_array_compare(function, ROMlib_carbon_cf_array_value(array, location + base + step), value, context,
                              &order))
            return location;
        if (order < 0)
            base += step;
    }
    return location + base + 1;
}

uint32 ROMlib_carbon_cf_type_id(const void* object)
{
    return object ? (cf_word(object, 4) >> 8) & 255 : 0;
}

LONGINT ROMlib_carbon_cf_data_length(const void* data)
{
    return data ? (LONGINT)cf_word(data, 8) : 0;
}

Byte* ROMlib_carbon_cf_data_bytes(const void* data)
{
    return data ? (Byte*)SYN68K_TO_US_CHECK0(cf_word(data, 20)) : NULL;
}

void ROMlib_carbon_cf_data_get_bytes(const void* data, LONGINT location, LONGINT length, void* output)
{
    if (!data || location < 0 || length <= 0 || !output)
        return;
    uint32 size = cf_word(data, 8);
    if ((uint32)location > size || (uint32)length > size - (uint32)location)
        return;
    const Byte* bytes = ROMlib_carbon_cf_data_bytes(data);
    if (bytes)
        memmove(output, bytes + location, length);
}
