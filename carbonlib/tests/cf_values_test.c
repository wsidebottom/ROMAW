/*+===================================================================
  File:      cf_values_test.c

  Summary:   Shared guest-memory, call-frame and foreign-profile checks for the CarbonLib cohorts, including the
             service entries and native Gregorian vectors. Real worker behavior uses grouped staged probes.

  Classes:   None.

  Functions: ROMlib_carbon_invoke_frame, ROMlib_carbon_report_failure, ROMlib_carbon_values_test_callback, ROMlib_carbon_values_test_memory,
             ROMlib_carbon_values_test_reference, ROMlib_carbon_values_tests

  Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Isolated guest extents and foreign-profile fallback tests for the large
   collection/value cohort. Real allocator and Unicode behavior is checked by
   staged probes against the installed original library. */
#include "../src/cf_batch.h"
#include "../src/cfc_cohort.h"
#include "../src/cfw_internal.h"
#include "../src/cfx_internal.h"
#include "cf_values_test.h"
#include "cf-values-cases.inc"
#include "cf-collections-cases.inc"
#include "cf-frameworks-cases.inc"
#include "cf-services-cases.inc"
#undef NDEBUG
#include <assert.h>

static _Alignas(16) Byte memory[0x20000];
static int active, foreign_profile, missing_reference, callback_ok, failure;
static const values_case* current;
static unsigned frame_test, frame_calls;
static ROMlib_carbon_ppc_frame expected_frame;
static const service_case* current_service;
static unsigned callback_count;
static uint32 callback_arguments[7];
static int search_indices[8];

enum { search_key = 0x6100, search_base = 0x6200, search_context = 0x6300 };

static _Alignas(16) uint32 foreign_vector[2];
static _Alignas(16) uint32 owner_vector[2];
static _Alignas(16) uint32 bitmap_allocate_vector[2], bitmap_free_vector[2];
static unsigned bitmap_test, bitmap_allocated, bitmap_allocations, bitmap_frees;
static int bitmap_allocate_ok, bitmap_worker_ok;

int ROMlib_carbon_report_failure(const char* message)
{
    assert(active && message && *message);
    failure = 1;
    return 0;
}

int ROMlib_carbon_values_test_memory(uint32_t address, size_t length, void** result)
{
    if (!active)
        return -1;
    *result = NULL;
    if (failure || length > sizeof memory || address > sizeof memory - length) {
        failure = 1;
        return 0;
    }
    *result = memory + address;
    return 1;
}

int ROMlib_carbon_values_test_reference(const char* name, ROMlib_carbon_reference* reference)
{
    if (!active)
        return -1;
    if (missing_reference)
        return 0;
    if (current_service && !strcmp(name, current_service->name)) {
        reference->function = foreign_vector;
        reference->code_offset = current_service->offset;
        reference->code_size = 0x5dee0 + foreign_profile * 4;
        reference->toc = memory;
        reference->toc_offset = 0;
        reference->toc_size = 0x72a8;
        return 1;
    }
    if (frame_test && !strcmp(name, "CFDateGetAbsoluteTime")) {
        reference->function = foreign_vector;
        reference->code_offset = foreign_profile ? 0 : 0x23474;
        reference->code_size = 0x5dee0;
        reference->toc = memory;
        reference->toc_offset = 0;
        reference->toc_size = 0x72a8;
        return 1;
    }
    if (!strcmp(name, "CFGetAllocator")) {
        reference->function = owner_vector;
        return 1;
    }
    if (bitmap_test && (!strcmp(name, "CFAllocatorAllocate") || !strcmp(name, "CFAllocatorDeallocate"))) {
        reference->function = !strcmp(name, "CFAllocatorAllocate") ? bitmap_allocate_vector : bitmap_free_vector;
        return 1;
    }
    const values_case* row = NULL;
    for (unsigned i = 0; i < NELEM(values_cases); ++i)
        if (!strcmp(name, values_cases[i].name))
            row = &values_cases[i];
    for (unsigned i = 0; i < NELEM(collections_cases); ++i)
        if (!strcmp(name, collections_cases[i].name))
            row = &collections_cases[i];
    for (unsigned i = 0; i < NELEM(frameworks_cases); ++i)
        if (!strcmp(name, frameworks_cases[i].name))
            row = &frameworks_cases[i];
    assert(row);
    reference->function = foreign_vector;
    reference->code_offset = row->offset;
    reference->code_size = 0x5dee0 + foreign_profile * 4;
    reference->toc = memory + 0x10000;
    reference->toc_size = 0x72a8;
    reference->toc_offset = 0;
    foreign_vector[0] = CL(0x100000 + row->offset);
    return 1;
}

int ROMlib_carbon_values_test_callback(void* procedure, const uint32_t* arguments, unsigned count, uint32_t* result)
{
    if (!active)
        return -1;
    if (current_service && !foreign_profile && !strcmp(current_service->name, "CFBSearch")) {
        assert(count == 3 && arguments[2] == search_context && callback_count < NELEM(search_indices));
        int index = search_indices[callback_count++];
        assert(arguments[0] == (index < 0 ? search_key : search_base + index * 4));
        assert(arguments[1] == (index < 0 ? search_base : search_key));
        *result = cfp_read(arguments[0]) - cfp_read(arguments[1]);
        return 1;
    }
    if (procedure == owner_vector) {
        assert(count == 1);
        if (result)
            *result = 0x9876;
        return 1;
    }
    if (bitmap_test && procedure == bitmap_allocate_vector) {
        assert(count == 3 && arguments[0] == 0x9876 && arguments[1] == 8192 && !arguments[2]);
        ++bitmap_allocations;
        assert(!bitmap_allocated);
        bitmap_allocated = bitmap_allocate_ok;
        if (result)
            *result = bitmap_allocate_ok ? 0x17000 : 0;
        return 1;
    }
    if (bitmap_test && procedure == bitmap_free_vector) {
        assert(count == 2 && arguments[0] == 0x9876 && arguments[1] == 0x17000 && bitmap_allocated);
        bitmap_allocated = 0;
        ++bitmap_frees;
        return 1;
    }
    if (!foreign_profile) {
        uint32 code;
        memcpy(&code, procedure, 4);
        if (bitmap_test && CL(code) == 0x100000 + 0x2fbf4) {
            assert(count == 2 && bitmap_allocated);
            void* output = NULL;
            /* The old host-stack snapshot is not a guest span in this harness.
               Both callback sides must agree on all 8192 contiguous bytes. */
            assert(ROMlib_carbon_values_test_memory(arguments[1], 8192, &output));
            if (bitmap_worker_ok) {
                if (arguments[0] == 0xa000)
                    memcpy(output, memory + 0xb000, 8192);
                else
                    memset(output, 0xaa, 8192);
            }
            return bitmap_worker_ok;
        }
        assert(CL(code) == 0x100000 + 0x1ab04); /* Null callbacks: release range has no work. */
        return 1;
    }
    assert(current && procedure == foreign_vector && count == current->count && count <= 7);
    ++callback_count;
    memcpy(callback_arguments, arguments, count * 4);
    if (callback_ok && result)
        *result = 0x7654abcd;
    return callback_ok;
}

static uint32 invoke(void* function, unsigned count, int returns_value, const uint32* a)
{
    if (returns_value) {
        switch (count) {
        case 0:
            return ((uint32(*)(void))function)();
        case 1:
            return ((uint32(*)(uint32))function)(a[0]);
        case 2:
            return ((uint32(*)(uint32, uint32))function)(a[0], a[1]);
        case 3:
            return ((uint32(*)(uint32, uint32, uint32))function)(a[0], a[1], a[2]);
        case 4:
            return ((uint32(*)(uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3]);
        case 5:
            return ((uint32(*)(uint32, uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3], a[4]);
        case 6:
            return ((uint32(*)(uint32, uint32, uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3], a[4],
                                                                                         a[5]);
        case 7:
            return ((uint32(*)(uint32, uint32, uint32, uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3],
                                                                                                 a[4], a[5], a[6]);
        }
    }
    else {
        switch (count) {
        case 1:
            ((void (*)(uint32))function)(a[0]);
            break;
        case 2:
            ((void (*)(uint32, uint32))function)(a[0], a[1]);
            break;
        case 3:
            ((void (*)(uint32, uint32, uint32))function)(a[0], a[1], a[2]);
            break;
        case 4:
            ((void (*)(uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3]);
            break;
        case 5:
            ((void (*)(uint32, uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3], a[4]);
            break;
        case 6:
            ((void (*)(uint32, uint32, uint32, uint32, uint32, uint32))function)(a[0], a[1], a[2], a[3], a[4], a[5]);
            break;
        default:
            assert(0);
        }
    }
    return 0;
}

int ROMlib_carbon_invoke_frame(void* procedure, ROMlib_carbon_ppc_frame* call_frame)
{
    assert(active && frame_test && procedure == foreign_vector);
    assert(!memcmp(call_frame, &expected_frame, sizeof *call_frame));
    ++frame_calls;
    if (!callback_ok)
        return 0;
    call_frame->words[0] = 0xfedcba98;
    call_frame->floating[0] = UINT64_C(0x8000000000000000);
    call_frame->fpscr = 0x76543210;
    return 1;
}

static int frame_entry(ROMlib_carbon_ppc_frame* call_frame)
{
    for (unsigned i = 0; i < 8; ++i)
        assert(call_frame->words[i] == 0x12340000 + i);
    for (unsigned i = 0; i < 13; ++i)
        assert(call_frame->floating[i] == UINT64_C(0x7ff0000000000001) + i);
    assert(call_frame->stack == 0x18000 && call_frame->fpscr == 0x12345678);
    for (unsigned i = 0; i < 32; ++i) {
        uint32 argument = 0;
        assert(ROMlib_carbon_frame_word(call_frame, i, &argument));
        assert(argument == (i < 8 ? 0x12340000 : 0xabcd0000) + i);
    }
    call_frame->words[0] = 0x87654321;
    call_frame->floating[0] = UINT64_C(0x8000000000000000);
    call_frame->fpscr = 0x76543210;
    return 1;
}

static int frame_failed_entry(ROMlib_carbon_ppc_frame* call_frame)
{
    memset(call_frame, 0, sizeof *call_frame);
    return 0;
}

static int frame_changed_stack(ROMlib_carbon_ppc_frame* call_frame)
{
    call_frame->stack += 16;
    return 1;
}

static void call_frame_tests(void)
{
    ROMlib_carbon_ppc_frame call_frame = {0};
    uint32 words[8], fpscr = 0x12345678;
    uint64 floating[13];
    for (unsigned i = 0; i < 8; ++i)
        words[i] = 0x12340000 + i;
    /* Signalling NaN payloads must survive transport without FP evaluation. */
    for (unsigned i = 0; i < 13; ++i)
        floating[i] = UINT64_C(0x7ff0000000000001) + i;
    for (unsigned i = 8; i < 32; ++i)
        assert(cfp_write(0x18000 + 24 + i * 4, 0xabcd0000 + i));
    assert(ROMlib_carbon_dispatch_frame(frame_entry, words, floating, 0x18000, &fpscr));
    assert(words[0] == 0x87654321 && floating[0] == UINT64_C(0x8000000000000000) && fpscr == 0x76543210);
    for (unsigned i = 1; i < 8; ++i)
        assert(words[i] == 0x12340000 + i);
    for (unsigned i = 1; i < 13; ++i)
        assert(floating[i] == UINT64_C(0x7ff0000000000001) + i);
    uint32 saved_words[8];
    uint64 saved_floating[13];
    memcpy(saved_words, words, sizeof words);
    memcpy(saved_floating, floating, sizeof floating);
    assert(!ROMlib_carbon_dispatch_frame(frame_failed_entry, words, floating, 0x18000, &fpscr));
    assert(!memcmp(saved_words, words, sizeof words) && !memcmp(saved_floating, floating, sizeof floating));
    assert(fpscr == 0x76543210 && !failure);
    assert(!ROMlib_carbon_dispatch_frame(frame_changed_stack, words, floating, 0x18000, &fpscr) && failure);
    assert(!memcmp(saved_words, words, sizeof words) && !memcmp(saved_floating, floating, sizeof floating));
    assert(fpscr == 0x76543210);
    failure = 0;
    uint32 output = 0xa5a5a5a5;
    call_frame.stack = UINT32_MAX - 3;
    call_frame.words[7] = 0x87654321;
    assert(ROMlib_carbon_frame_word(&call_frame, 7, &output) && output == 0x87654321);
    output = 0xa5a5a5a5;
    assert(!ROMlib_carbon_frame_word(&call_frame, 8, &output) && failure && output == 0xa5a5a5a5);
    failure = 0;
    call_frame.stack = 0;
    assert(!ROMlib_carbon_frame_word(&call_frame, UINT32_MAX, &output) && failure && output == 0xa5a5a5a5);
    failure = 0;
    /* Last readable word, then a truncated word: no speculative stack reads. */
    call_frame.stack = sizeof memory - 4 - 24 - 8 * 4;
    assert(cfp_write(sizeof memory - 4, 0x11223344));
    assert(ROMlib_carbon_frame_word(&call_frame, 8, &output) && output == 0x11223344);
    call_frame.stack++;
    assert(!ROMlib_carbon_frame_word(&call_frame, 8, &output) && failure && output == 0x11223344);
    failure = 0;
    assert(!ROMlib_carbon_frame_word(NULL, 0, &output) && failure);
    failure = 0;
    assert(!ROMlib_carbon_frame_word(&call_frame, 0, NULL) && failure);
    failure = 0;
    assert(!ROMlib_carbon_dispatch_frame(NULL, words, floating, 0x18000, &fpscr) && failure);
    failure = 0;
    frame_test = 1;
    frame_calls = 0;
    call_frame = (ROMlib_carbon_ppc_frame){0};
    call_frame.words[0] = 0x19000;
    call_frame.stack = 0x18000;
    call_frame.fpscr = 0x12345678;
    const ROMlib_carbon_export* entry = ROMlib_carbon_find_export("CFDateGetAbsoluteTime", 2);
    assert(entry && (entry->signature & ROMLIB_CARBON_PPC_FRAME) && (entry->flags & ROMLIB_CARBON_PPC_ONLY));
    ROMlib_carbon_frame_entry get_date = (ROMlib_carbon_frame_entry)entry->function;
    const uint64 payloads[] = {0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000001),
                               UINT64_C(0xfff8000000001234), UINT64_C(0x3ff4000000000000)};
    for (unsigned i = 0; i < NELEM(payloads); ++i) {
        assert(cfp_write(0x19008, payloads[i] >> 32) && cfp_write(0x1900c, payloads[i]));
        assert(get_date(&call_frame) && call_frame.floating[0] == payloads[i] && call_frame.words[0] == 0x19000);
        assert(!frame_calls && call_frame.fpscr == 0x12345678);
    }
    call_frame.words[0] = 0;
    assert(get_date(&call_frame) && !call_frame.floating[0]);
    call_frame.words[0] = UINT32_MAX - 3;
    assert(!get_date(&call_frame) && failure);
    failure = 0;
    call_frame.words[0] = sizeof memory - 12;
    assert(!get_date(&call_frame) && failure);
    failure = 0;
    foreign_profile = 1;
    expected_frame = call_frame;
    callback_ok = 1;
    assert(get_date(&call_frame) && frame_calls == 1 && call_frame.words[0] == 0xfedcba98);
    assert(call_frame.floating[0] == UINT64_C(0x8000000000000000) && call_frame.fpscr == 0x76543210);
    call_frame = expected_frame;
    callback_ok = 0;
    assert(!get_date(&call_frame) && frame_calls == 2 && !memcmp(&call_frame, &expected_frame, sizeof call_frame));
    missing_reference = 1;
    assert(!get_date(&call_frame) && frame_calls == 2);
    missing_reference = foreign_profile = frame_test = 0;
    callback_ok = 1;
    puts(
        "CarbonLib PPC call frames: 32 argument positions, raw FP payloads, FPSCR, rollback and bounded stack reads passed");
}

static void service_search_tests(void)
{
    for (unsigned i = 0; i < NELEM(service_cases); ++i)
        if (!strcmp(service_cases[i].name, "CFBSearch"))
            current_service = &service_cases[i];
    assert(current_service);
    const uint32 items[] = {2, 4, 4, 8, 10, 12, 14};
    for (unsigned i = 0; i < NELEM(items); ++i)
        cfp_write(search_base + i * 4, items[i]);

    /* These exact operand sequences are also checked against the real
       CoreFoundation fragment by the framework reporting application. */
    const struct {
        unsigned count, key, insertion, calls;
        int indices[8];
    } vectors[] = {
        {0, 4, 0, 0, {0}},     {3, 4, 1, 3, {2, 1, 0}},        {4, 8, 3, 5, {3, -1, 3, 1, 2}},
        {7, 1, 0, 2, {6, -1}}, {7, 4, 1, 5, {6, -1, 3, 1, 0}}, {7, 11, 5, 5, {6, -1, 3, 5, 4}},
        {7, 15, 7, 1, {6}},
    };

    for (unsigned i = 0; i < NELEM(vectors); ++i) {
        ROMlib_carbon_ppc_frame call_frame = {
            .words = {search_key, 4, search_base, vectors[i].count, 0x6400, search_context}};
        cfp_write(search_key, vectors[i].key);
        memcpy(search_indices, vectors[i].indices, sizeof search_indices);
        callback_count = 0;
        assert(current_service->entry(&call_frame) && !failure);
        assert(call_frame.words[0] == vectors[i].insertion && callback_count == vectors[i].calls);
    }
    current_service = NULL;
    puts("CarbonLib binary search: insertion indices and ordered comparator operands passed");
}

static void invoke_calendar(const char* name, ROMlib_carbon_ppc_frame* call_frame)
{
    for (unsigned index = 0; index < NELEM(service_cases); ++index) {
        if (strcmp(service_cases[index].name, name))
            continue;
        current_service = &service_cases[index];
        assert(current_service->entry(call_frame) && !failure);
        current_service = NULL;
        return;
    }
    assert(0 && "calendar entry missing from manifest");
}

static void service_calendar_units_tests(void)
{
    const struct {
        double absolute;
        unsigned year, packed, weekday, day, week;
        double second;
    } dates[] = {
        {-26478898.75, 2000, 0x021d0c2d, 2, 60, 9, 1.25},
        {-0.25, 2000, 0x0c1f173b, 7, 366, 52, 59.75},
        {0, 2001, 0x01010000, 1, 1, 1, -0.0},
        {157766400, 2006, 0x01010000, 7, 1, 53, -0.0},
        {220752000, 2007, 0x0c1f0000, 1, 365, 1, -0.0},
    };

    const uint32 output = 0x6600;
    const char* queries[] = {"CFAbsoluteTimeGetDayOfWeek", "CFAbsoluteTimeGetDayOfYear", "CFAbsoluteTimeGetWeekOfYear"};
    for (unsigned i = 0; i < NELEM(dates); ++i) {
        ROMlib_carbon_ppc_frame input = {0};
        memcpy(input.floating, &dates[i].absolute, sizeof(double));
        const uint32 expected[] = {dates[i].weekday, dates[i].day, dates[i].week};
        for (unsigned j = 0; j < NELEM(queries); ++j) {
            ROMlib_carbon_ppc_frame call_frame = input;
            invoke_calendar(queries[j], &call_frame);
            assert(call_frame.words[0] == expected[j]);
        }
        input.words[0] = output;
        invoke_calendar("CFAbsoluteTimeGetGregorianDate", &input);
        uint64 bits;
        memcpy(&bits, &dates[i].second, sizeof bits);
        assert(cfp_read(output) == dates[i].year && cfp_read(output + 4) == dates[i].packed);
        assert(cfp_read(output + 8) == bits >> 32 && cfp_read(output + 12) == (uint32)bits);
    }
    ROMlib_carbon_ppc_frame add = {.words = {0, 0, 0, 0, 1, 1, 2, 3}, .stack = 0x18000};
    double january_end = 2592000.0;
    memcpy(add.floating, &january_end, sizeof january_end);
    cfp_write(add.stack + 56, 0x3fe80000);
    cfp_write(add.stack + 60, 0);
    invoke_calendar("CFAbsoluteTimeAddGregorianUnits", &add);
    double result;
    memcpy(&result, add.floating, sizeof result);
    assert(result == 5104980.75);

    const unsigned scales[] = {31622400, 2678400, 86400, 3600, 60};
    for (unsigned i = 0; i < NELEM(scales); ++i)
        cfp_write(0x353c + 4 * i, scales[i]);
    for (int sign = -1; sign <= 1; sign += 2) {
        ROMlib_carbon_ppc_frame difference = {.words = {output, 0, 0, 0, 0, 0, 60}};
        double interval = sign * 90061.25;
        memcpy(difference.floating, &interval, sizeof interval);
        invoke_calendar("CFAbsoluteTimeGetDifferenceAsGregorianUnits", &difference);
        assert(cfp_read(output) == 0 && cfp_read(output + 4) == 0);
        for (unsigned offset = 8; offset <= 16; offset += 4)
            assert(cfp_read(output + offset) == (uint32)sign);
        assert(cfp_read(output + 20) == (sign < 0 ? 0xbff40000 : 0x3ff40000) && cfp_read(output + 24) == 0);
    }
    puts("CarbonLib calendar: real-library date fields, signed zero, week boundaries and Gregorian units passed");
}

static void service_calendar_tests(void)
{
    for (unsigned index = 0; index < NELEM(service_cases); ++index)
        if (!strcmp(service_cases[index].name, "CFGregorianDateIsValid"))
            current_service = &service_cases[index];
    assert(current_service);

    static const struct {
        unsigned year, month, day, valid;
    } calendar[] = {{1900, 2, 28, 1}, {1900, 2, 29, 0}, {2000, 2, 29, 1}, {2001, 2, 29, 0},
                    {2100, 2, 29, 0}, {2400, 2, 29, 1}, {2001, 0, 1, 0},  {2001, 13, 1, 0},
                    {2001, 1, 0, 0},  {2001, 4, 31, 0}, {0, 1, 1, 0}};

    for (unsigned index = 0; index < NELEM(calendar); ++index) {
        ROMlib_carbon_ppc_frame call_frame = {0};
        call_frame.words[0] = calendar[index].year;
        call_frame.words[1] = (calendar[index].month << 24) | (calendar[index].day << 16);
        call_frame.words[4] = UINT32_MAX; /* The reference ignores validation flags. */
        assert(current_service->entry(&call_frame) && call_frame.words[0] == calendar[index].valid && !failure);
    }
    ROMlib_carbon_ppc_frame call_frame = {0};
    call_frame.words[0] = 2001;
    call_frame.words[1] = 0x01010000;
    call_frame.words[2] = 0x7ff80000; /* Ordered comparisons accept a quiet NaN in the reference. */
    call_frame.words[3] = 0x12345678;
    assert(current_service->entry(&call_frame) && call_frame.words[0] == 1);
    assert((call_frame.fpscr & 0xa0080000) == 0xa0080000 && !failure);

    current_service = NULL;
    for (unsigned index = 0; index < NELEM(service_cases); ++index)
        if (!strcmp(service_cases[index].name, "CFGregorianDateGetAbsoluteTime"))
            current_service = &service_cases[index];
    assert(current_service);

    /* Independent Gregorian epoch vectors, including both leap-century rules. */
    static const struct {
        unsigned year, month, day;
        double absolute;
    } epochs[] = {{1900, 2, 28, -3182284800.0}, {1900, 3, 1, -3182198400.0}, {1970, 1, 1, -978307200.0},
                  {2000, 2, 29, -26524800.0},   {2001, 1, 1, 0.0},           {2100, 3, 1, 3129235200.0},
                  {2400, 2, 29, 12596256000.0}};

    for (unsigned index = 0; index < NELEM(epochs); ++index) {
        for (unsigned rounding = 0; rounding < 4; ++rounding) {
            call_frame = (ROMlib_carbon_ppc_frame){0};
            call_frame.words[0] = epochs[index].year;
            call_frame.words[1] = (epochs[index].month << 24) | (epochs[index].day << 16) | 0x00000c00;
            /* 12:00:00.25: the exact fractional result is independent of RN. */
            call_frame.words[2] = 0x3fd00000;
            call_frame.fpscr = rounding;
            assert(current_service->entry(&call_frame) && !failure);
            double result;
            memcpy(&result, call_frame.floating, sizeof result);
            assert(result == epochs[index].absolute + 43200.25);
            assert((call_frame.fpscr & 3) == rounding);
        }
    }
    current_service = NULL;
    puts("CarbonLib services: Gregorian validation, NaN status and epoch/leap-century vectors passed");
}

void ROMlib_carbon_values_tests(void)
{
    uintptr_t saved_offset = ROMlib_offset;
    ROMlib_offset = (uintptr_t)memory;
    active = 1;
    foreign_profile = 1;
    const uint32 arguments[] = {0xfedc0123, 0x87654321, 0xffffffff, 0x10203040, 0x81928374, 0x12345678, 0xabcdef01};
    assert(NELEM(values_cases) == 102);
    assert(NELEM(collections_cases) == 100);
    const unsigned frameworks_count = NELEM(frameworks_cases);
    assert(frameworks_count == 106);
    for (unsigned i = 0; i < NELEM(values_cases) + NELEM(collections_cases) + frameworks_count; ++i) {
        current = i < NELEM(values_cases) ? &values_cases[i]
                  : i < NELEM(values_cases) + NELEM(collections_cases)
                      ? &collections_cases[i - NELEM(values_cases)]
                      : &frameworks_cases[i - NELEM(values_cases) - NELEM(collections_cases)];
        const ROMlib_carbon_export* entry = ROMlib_carbon_find_library_export("CoreFoundationLib", current->name, 2);
        assert(entry && (entry->flags & ROMLIB_CARBON_PPC_ONLY));
        for (unsigned mode = 0; mode < 3; ++mode) {
            missing_reference = mode == 2;
            callback_ok = mode == 0;
            callback_count = 0;
            uint32 result = invoke(entry->function, current->count, current->returns_value, arguments);
            assert(callback_count == (mode != 2));
            if (callback_count)
                assert(!memcmp(callback_arguments, arguments, current->count * 4));
            if (current->returns_value)
                assert(result ==
                       (mode == 0 ? 0x7654abcd
                        : current->name[0] && !strcmp(current->name, "CFStringConvertIANACharSetNameToEncoding")
                            ? UINT32_MAX
                            : 0));
        }
    }
    puts("CarbonLib frameworks: 106 entry ABIs, foreign results, callback failures and missing references passed");
    missing_reference = 0;
    foreign_profile = 0;
    failure = 0;
    memset(memory, 0, sizeof memory);
    cfp_write(0x10924, 0x123);
    assert(ROMlib_cfvalue_boolean_type() == 0x123);
    cfp_write(0x10924, 0x456);
    assert(ROMlib_cfvalue_boolean_type() == 0x456);
    cfp_write(0x1356c, 0x2000);
    assert(ROMlib_cfvalue_boolean(0x2000) == 1 && !ROMlib_cfvalue_boolean(0x2004));
    assert(ROMlib_cfvalue_equal(0, 0));
    assert(!ROMlib_cfstring_create_mutable(0, 0x02000000) && !failure);
    assert(!ROMlib_cfstring_create_mutable(0, UINT32_MAX) && !failure);

    /* Candidate framework accessors share the same protected guest-memory harness. */
    cfp_write(0x10a58, 47);
    cfp_write(0x10968, 48);
    cfp_write(0x10894, 49);
    assert(ROMlib_cfw_XMLNodeGetTypeID() == 47);
    assert(ROMlib_cfw_XMLParserGetTypeID() == 48);
    assert(ROMlib_cfw_PlugInInstanceGetTypeID() == 49);
    cfp_write(0x8008, 1);
    cfp_write(0x800c, 6);
    cfp_write(0x8010, 0x8100);
    cfp_write(0x8014, 0x8200);
    assert(ROMlib_cfw_XMLNodeGetVersion(0x8000) == 1);
    assert(ROMlib_cfw_XMLNodeGetTypeCode(0x8000) == 6);
    assert(ROMlib_cfw_XMLNodeGetString(0x8000) == 0x8100);
    assert(ROMlib_cfw_XMLNodeGetInfoPtr(0x8000) == 0x8200);
    cfp_write(0x8030, 123);
    cfp_write(0x8034, 7);
    cfp_write(0x8090, UINT32_MAX);
    assert(ROMlib_cfw_XMLParserGetLocation(0x8000) == 123);
    assert(ROMlib_cfw_XMLParserGetLineNumber(0x8000) == 7);
    assert(ROMlib_cfw_XMLParserGetStatusCode(0x8000) == UINT32_MAX);
    cfp_write(0x8050, 0);
    assert(!ROMlib_cfw_XMLParserGetDocument(0x8000));
    cfp_write(0x8050, 16);
    cfp_write(0x8048, 0x8300);
    cfp_write(0x8300, 0x12345678);
    assert(ROMlib_cfw_XMLParserGetDocument(0x8000) == 0x12345678);
    for (unsigned i = 0; i < 6; ++i)
        cfp_write(0x8064 + 4 * i, i + 1);
    ROMlib_cfw_XMLParserGetCallBacks(0x8000, 0x8068);
    const unsigned paired_overlap[] = {1, 2, 2, 4, 4, 6};
    for (unsigned i = 0; i < 6; ++i)
        assert(cfp_read(0x8068 + 4 * i) == paired_overlap[i]);
    ROMlib_cfw_XMLParserGetContext(0xfffffff0, 0);
    assert(!failure); /* Optional null outputs never inspect an invalid parser. */
    assert(ROMlib_cfw_PlugInGetBundle(0x87654321) == 0x87654321);
    assert(ROMlib_cfw_PlugInInstanceGetInstanceData(0x8000) == 0x8014);
    cfp_write(0x8008, 0x8400);
    cfp_write(0x8404, 0x456789ab);
    assert(ROMlib_cfw_PlugInInstanceGetFactoryName(0x8000) == 0x456789ab);
    cfp_write(0x800c, 0);
    assert(!ROMlib_cfw_PlugInInstanceGetInterfaceFunctionTable(0x8000, 0, 0x8500));
    assert(!cfp_read(0x8500));
    cfp_write(0x16f28, 0x9000);
    ROMlib_cfw_AllocatorSetDefault(0);
    ROMlib_cfw_AllocatorSetDefault(0x9000);
    assert(cfp_read(0x16f28) == 0x9000 && !failure);
    ROMlib_cfw_XMLNodeGetVersion(0x1fffc);
    assert(failure);
    failure = 0;
    assert(!ROMlib_cfw_PlugInInstanceCreateWithInstanceDataSize(0, UINT32_MAX, 0, 0, 0) && failure);
    failure = 0;
    assert(!ROMlib_cfw_PlugInInstanceCreateWithInstanceDataSize(0, INT32_MAX, 0, 0, 0) && failure);
    failure = 0;
    puts("CarbonLib frameworks: guest fields, paired overlap, optional outputs and protected extents passed");

    /* Fixed-capacity data: overlap, shrinking and zero-fill retain guards. */
    cfp_write(0x2004, 12);
    cfp_write(0x2008, 5);
    cfp_write(0x200c, 16);
    cfp_write(0x2014, 0x3000);
    memcpy(memory + 0x3000, "abcde", 5);
    memory[0x2fff] = 0xa5;
    memory[0x3010] = 0x5a;
    ROMlib_cfdata_replace(0x2000, 1, 1, 0x3000, 3);
    assert(cfp_read(0x2008) == 7 && !memcmp(memory + 0x3000, "aabccde", 7));
    ROMlib_cfdata_delete(0x2000, 2, 3);
    assert(cfp_read(0x2008) == 4 && !memcmp(memory + 0x3000, "aade", 4));
    ROMlib_cfdata_set_length(0x2000, 8);
    assert(!memcmp(memory + 0x3004, "\0\0\0\0", 4));
    ROMlib_cfdata_increase_length(0x2000, (uint32)-4);
    assert(cfp_read(0x2008) == 4 && memory[0x2fff] == 0xa5 && memory[0x3010] == 0x5a);
    ROMlib_cfdata_set_length(0x2000, 17);
    assert(failure && cfp_read(0x2008) == 0); /* Reads preserve the active diagnostic. */
    failure = 0;
    assert(cfp_read(0x2008) == 4);

    /* Null-callback fixed array: searches use absolute indices and swapping
       does not retain/release values. Removal releases before shifting. */
    cfp_write(0x4004, 1);
    cfp_write(0x4008, 4);
    cfp_write(0x400c, 8);
    const uint32 values[] = {7, 8, 7, 9};
    for (unsigned i = 0; i < NELEM(values); ++i)
        cfp_write(0x4010 + i * 4, values[i]);
    assert(ROMlib_cfarray_count_value(0x4000, 1, 3, 7) == 1);
    assert(ROMlib_cfarray_first(0x4000, 1, 3, 7) == 2);
    assert(ROMlib_cfarray_last(0x4000, 0, 4, 7) == 2);
    assert(ROMlib_cfarray_first(0x4000, 0, 4, 6) == UINT32_MAX);
    ROMlib_cfarray_exchange(0x4000, 0, 3);
    assert(cfp_read(0x4010) == 9 && cfp_read(0x401c) == 7);
    ROMlib_cfarray_remove(0x4000, 1);
    assert(cfp_read(0x4008) == 3 && cfp_read(0x4014) == 7);
    ROMlib_cfarray_remove(0x4000, UINT32_MAX);
    assert(failure);
    failure = 0;
    assert(cfp_read(0x4008) == 3);
    assert(!ROMlib_carbon_guest_memory(UINT32_MAX - 1, 4, 1) && failure);
    failure = 0;
    /* The new cohort operates on the live table, rather than cached type IDs. */
    cfp_write(0x10a68, 30);
    cfp_write(0x10a6c, 1);
    assert(ROMlib_cfc_CFRuntimeRegisterClass(0x9000) == 30);
    assert(cfp_read(0x10a68) == 31 && ROMlib_cfc_CFRuntimeGetClassWithTypeID(30) == 0x9000);
    cfp_write(0x8004, 0x12345678);
    ROMlib_cfc_CFRuntimeSetInstanceTypeID(0x8000, 30);
    assert(cfp_read(0x8004) == 0x12341e78);
    ROMlib_cfc_CFRuntimeSetInstanceTypeID(0x8000, 31);
    assert(cfp_read(0x8004) == 0x12341e78);
    assert(!ROMlib_cfc_CFRuntimeGetClassWithTypeID(UINT32_MAX) && failure);
    failure = 0;
    cfp_write(0x10a68, 255);
    assert(ROMlib_cfc_CFRuntimeRegisterClass(0x9100) == 255 && cfp_read(0x10a68) == 256);
    assert(!ROMlib_cfc_CFRuntimeRegisterClass(0x9200) && failure);
    failure = 0;
    assert(cfp_read(0x10a68) == 256 && ROMlib_cfc_CFRuntimeGetClassWithTypeID(255) == 0x9100);

    /* A required values output at guest zero is low memory, not an omitted
       optional output. Bag multiplicities must repeat the canonical value. */
    cfp_write(0x9014, 2);
    cfp_write(0x9030, 0xa000);
    cfp_write(0xa000, 0xc0000000);
    cfp_write(0xa004, 0x1234);
    cfp_write(0xa008, 2);
    cfp_write(8, 0xa5a5a5a5);
    ROMlib_cfc_CFBagGetValues(0x9000, 0);
    assert(cfp_read(0) == 0x1234 && cfp_read(4) == 0x1234 && cfp_read(8) == 0xa5a5a5a5);

    /* Ordered UUID struct copies can overlap the input object. */
    cfp_write(0x5008, 0x11223344);
    cfp_write(0x500c, 0xaabbccdd);
    cfp_write(0x5010, 0x55667788);
    cfp_write(0x5014, 0x99aabbcc);
    assert(ROMlib_cfc_CFUUIDGetUUIDBytes(0x500c, 0x5000) == 0x500c);
    assert(cfp_read(0x500c) == 0x11223344 && cfp_read(0x5010) == 0xaabbccdd && cfp_read(0x5014) == 0xaabbccdd &&
           cfp_read(0x5018) == 0x99aabbcc);

    /* Storage cache hits copy only the requested elements and keep range outputs. */
    cfp_write(0x6008, 4);
    cfp_write(0x600c, 3);
    cfp_write(0x6010, 2);
    cfp_write(0x6014, 0x7000);
    cfp_write(0x6018, 40);
    cfp_write(0x7000, 0x1234);
    cfp_write(0x7004, 0x5678);
    assert(ROMlib_cfc_CFStorageGetCount(0x6000) == 10);
    assert(ROMlib_cfc_CFStorageGetValueAtIndex(0x6000, 4, 0x7100) == 0x7004);
    assert(cfp_read(0x7100) == 3 && cfp_read(0x7104) == 2);
    cfp_write(0x7204, 0xa5a5a5a5);
    ROMlib_cfc_CFStorageGetValues(0x6000, 4, 1, 0x7200);
    assert(cfp_read(0x7200) == 0x5678 && cfp_read(0x7204) == 0xa5a5a5a5);
    cfp_write(0x6008, 0);
    assert(!ROMlib_cfc_CFStorageGetCount(0x6000) && failure);
    failure = 0;
    /* Character-set workers must not write through a point-mapped host stack
       array that can cross a 128-KiB alias boundary into a different host page. */
    bitmap_test = bitmap_allocate_ok = bitmap_worker_ok = 1;
    bitmap_allocations = bitmap_frees = bitmap_allocated = 0;
    cfp_write(0xa004, 0x31);
    cfp_write(0xa00c, 0xb000);
    memset(memory + 0xb000, 0x55, 8192);
    ROMlib_cfcharset_union(0xa000, 0xa100);
    assert(bitmap_allocations == 1 && bitmap_frees == 1 && !bitmap_allocated && !failure);
    for (unsigned i = 0; i < 8192; ++i)
        assert(memory[0xb000 + i] == 0xff);
    ROMlib_cfcharset_intersect(0xa000, 0xa100);
    assert(bitmap_allocations == 2 && bitmap_frees == 2 && !bitmap_allocated && !failure);
    for (unsigned i = 0; i < 8192; ++i)
        assert(memory[0xb000 + i] == 0xaa);
    ROMlib_cfcharset_union(0xa000, 0xa000);
    ROMlib_cfcharset_intersect(0xa000, 0xa000);
    assert(bitmap_allocations == 4 && bitmap_frees == 4 && !bitmap_allocated && !failure);
    for (unsigned i = 0; i < 8192; ++i)
        assert(memory[0xb000 + i] == 0xaa);
    bitmap_worker_ok = 0;
    ROMlib_cfcharset_union(0xa000, 0xa100);
    assert(bitmap_allocations == 5 && bitmap_frees == 5 && !bitmap_allocated && !failure);
    bitmap_allocate_ok = 0;
    ROMlib_cfcharset_union(0xa000, 0xa100);
    assert(bitmap_allocations == 6 && bitmap_frees == 5 && !bitmap_allocated && failure);
    failure = 0;
    for (unsigned i = 0; i < 8192; ++i)
        assert(memory[0xb000 + i] == 0xaa);
    bitmap_test = 0;
    puts("CarbonLib character sets: contiguous guest snapshots and allocation/callback failures passed");

    call_frame_tests();
    assert(NELEM(service_cases) == 100);
    foreign_profile = frame_test = 1;
    for (unsigned index = 0; index < NELEM(service_cases); ++index) {
        current_service = &service_cases[index];
        const ROMlib_carbon_export* binding =
            ROMlib_carbon_find_library_export("CoreFoundationLib", current_service->name, 2);
        assert(binding && binding->signature == ROMLIB_CARBON_PPC_FRAME &&
               binding->function == (void*)current_service->entry);
        for (unsigned mode = 0; mode < 3; ++mode) {
            /* Invalid guest pointers and a stack beyond the memory fixture
               must remain untouched until the reference profile is accepted. */
            expected_frame = (ROMlib_carbon_ppc_frame){0};
            for (unsigned word = 0; word < 8; ++word)
                expected_frame.words[word] = 0xffff0000 + word;
            for (unsigned fp = 0; fp < 13; ++fp)
                expected_frame.floating[fp] = UINT64_C(0x7ff0000000000001) + fp;
            expected_frame.stack = 0xfffffff0;
            expected_frame.fpscr = 0x12345678;
            ROMlib_carbon_ppc_frame call_frame = expected_frame;
            missing_reference = mode == 2;
            callback_ok = mode == 0;
            frame_calls = failure = 0;
            assert(current_service->entry(&call_frame) == callback_ok);
            assert(frame_calls == (mode != 2) && !failure);
            if (callback_ok) {
                expected_frame.words[0] = 0xfedcba98;
                expected_frame.floating[0] = UINT64_C(0x8000000000000000);
                expected_frame.fpscr = 0x76543210;
            }
            assert(!memcmp(&call_frame, &expected_frame, sizeof call_frame));
        }
    }
    current_service = NULL;
    missing_reference = foreign_profile = frame_test = 0;
    puts("CarbonLib services: 100 foreign frames, raw FP state, failed callbacks and missing references passed");
    service_calendar_tests();
    service_calendar_units_tests();
    service_search_tests();
    active = 0;
    ROMlib_offset = saved_offset;
}
