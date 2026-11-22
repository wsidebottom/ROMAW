/*+===================================================================
  File:      textension.c

  Summary:   Read and update native Textension fields shared with the classic fragment.

  Classes:   None.

  Functions: ROMlib_carbon_textension.

  Origin:    CarbonLib 1.4 and Textension_CL 1.3.2 disassembly and the
             Universal Interfaces 3.4.2 MacTextEditor declarations.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "internal.h"
#include "cf_batch.h"
#include "textension.h"

/* Textension_CL stores its document and editor members on two-byte boundaries. */
static void* txn_memory(uint32_t object, unsigned offset, unsigned size, int writable)
{
    return object && object <= UINT32_MAX - offset ? ROMlib_carbon_try_guest_memory(object + offset, size, writable)
                                                   : NULL;
}

static uint32_t txn_word(const void* bytes)
{
    uint32_t word = 0;
    if (bytes)
        memcpy(&word, bytes, sizeof word);
    return CL(word);
}

static void txn_store(void* bytes, uint32_t word)
{
    word = CL(word);
    memcpy(bytes, &word, sizeof word);
}

static uint32_t txn_field(uint32_t object, unsigned offset)
{
    return txn_word(txn_memory(object, offset, sizeof(uint32_t), 0));
}

static Byte txn_byte(uint32_t object, unsigned offset)
{
    const Byte* value = txn_memory(object, offset, 1, 0);
    return value ? *value : 0;
}

static int txn_status(ROMlib_carbon_ppc_frame* call, uint32_t status)
{
    call->words[0] = status;
    return 1;
}

static int txn_reference_matches(ROMlib_carbon_reference* ref)
{
    return ROMlib_carbon_probe_reference("Textension_CL", "TXNVersionInformation", ref) &&
           ref->code_offset == 0x13a1c && ref->code_size == 0x6e21c && ref->toc && ref->toc_offset == 0xd10 &&
           ref->toc_size == 0x69f0;
}

static int txn_saved(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference carbon;
    return ROMlib_carbon_reference_function("CarbonLib", name, &carbon) &&
           ROMlib_carbon_invoke_frame(carbon.function, call);
}

static int txn_uses_unicode_offsets(const ROMlib_carbon_reference* ref)
{
    const uint32_t mode = txn_word((const Byte*)ref->toc - 0x37c);
    const Byte* unicode = txn_memory(mode, 0, 1, 0);
    return unicode && *unicode;
}

static const Byte* txn_selection_bytes(uint32_t editor)
{
    return txn_memory(txn_field(editor, 0x24), 0x48, 12, 0);
}

static uint16_t txn_short(const void* bytes)
{
    uint16_t word = 0;
    if (bytes)
        memcpy(&word, bytes, sizeof word);
    return CW(word);
}

static int txn_init_defaults(const ROMlib_carbon_reference* ref, uint32_t settings, uint32_t supplied, uint32_t count,
                             Byte defaults[16])
{
    _Alignas(2) uint16_t script = CW(UINT16_MAX);
    if (supplied && count) {
        const uint32_t install[] = {settings, supplied, count};
        const uint32_t encoding[] = {txn_field(supplied, 8), US_TO_SYN68K(&script), 0, 0};
        if (!cfp_private(ref, 0x5da4c, install, NELEM(install), NULL) ||
            !cfp_private(ref, 0x6b508, encoding, NELEM(encoding), NULL))
            return 0;
    }
    const uint32_t find[] = {settings, (int16_t)CW(script), US_TO_SYN68K(defaults)};
    return cfp_private(ref, 0x5d9cc, find, NELEM(find), NULL);
}

static int txn_init_default_run(const ROMlib_carbon_reference* ref, const Byte defaults[16], uint32_t run)
{
    _Alignas(8) uint32_t color[2] = {0};
    memcpy(color, (const Byte*)ref->toc + 0x3d58, 6);
    _Alignas(8) uint32_t face = CL(defaults[12]);
    _Alignas(4) uint16_t font = CW((uint16_t)txn_word(defaults)), size = CW(txn_short(defaults + 4));
    const uint32_t attributes[][3] = {
        {run, T('f', 'o', 'n', 't'), US_TO_SYN68K(&font)},
        {run, T('s', 'i', 'z', 'e'), US_TO_SYN68K(&size)},
        {run, T('f', 'a', 'c', 'e'), US_TO_SYN68K(&face)},
        {run, T('k', 'l', 'o', 'r'), US_TO_SYN68K(color)},
    };
    for (unsigned i = 0; i < NELEM(attributes); ++i) {
        const uint32_t set_attribute = txn_field(txn_field(run, 0), 0x2c);
        if (!cfp_callback(set_attribute, attributes[i], NELEM(attributes[i]), NULL))
            return 0;
    }
    return 1;
}

static int txn_carbon_reference_matches(const char* name, uint32_t offset, ROMlib_carbon_reference* ref)
{
    return ROMlib_carbon_probe_reference("CarbonLib", name, ref) && ref->code_offset == offset &&
           ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 && ref->toc_size == 0x10f0c;
}

static int txn_load_textension(const ROMlib_carbon_reference* carbon)
{
    const Byte* toc = carbon->toc;
    return txn_byte(txn_word(toc - 0x1154), 0) || cfp_private(carbon, 0x2e5f8, NULL, 0, NULL);
}

static int txn_initialize_textension(const char* name, ROMlib_carbon_ppc_frame* call)
{
    enum { missing_worker = -4, already_initialized = -22012 };

    ROMlib_carbon_reference carbon, ref;
    if (!txn_carbon_reference_matches(name, 0x2e91c, &carbon))
        return txn_saved(name, call);

    const Byte* carbon_toc = carbon.toc;
    if (!txn_load_textension(&carbon))
        return 0;
    if (!txn_field(txn_word(carbon_toc - 0x1140), 0))
        return txn_status(call, missing_worker);
    if (!txn_reference_matches(&ref))
        return txn_saved(name, call);

    Byte* toc = (Byte*)ref.toc;
    Byte* pending = txn_memory(txn_word(toc - 0x370), 0, 1, 1);
    void* settings_slot = txn_memory(txn_word(toc - 0x36c), 0, 4, 1);
    if (!pending || !settings_slot)
        return txn_saved(name, call);
    if (!*pending)
        return txn_status(call, already_initialized);

    const uint32_t options = call->words[2] | 0x20, toc_address = US_TO_SYN68K(toc);
    if (!(options & 8))
        toc[0x3d30] = 1;
    /* Initialize the empty run list and install its factory vectors. */
    const uint32_t list = toc_address + 0x6358, sentinel = list + 4;
    const uint32_t head[] = {CL(sentinel),
                             0,
                             CL(sentinel),
                             CL(sentinel),
                             CL(toc_address + 0x16e0),
                             CL(toc_address + 0x16e8),
                             CL(0x8000),
                             0,
                             0,
                             0};
    memcpy(toc + 0x6358, head, sizeof head);
    *pending = 0;

    uint32_t settings;
    const uint32_t settings_size = 0x1c;
    if (!cfp_private(&ref, 0x5d5c8, NULL, 0, NULL) || !cfp_private(&ref, 0x3e1c0, &settings_size, 1, &settings))
        return 0;
    if (!settings)
        return txn_status(call, memFullErr);
    if (!cfp_private(&ref, 0x5d4d4, &settings, 1, NULL))
        return 0;
    txn_store(settings_slot, settings);
    _Alignas(16) Byte defaults[16] = {0};
    if (!cfp_private(&ref, 0x5d64c, &settings, 1, NULL) ||
        !txn_init_defaults(&ref, settings, call->words[0], call->words[1], defaults))
        return 0;

    uint32_t status;
    const uint32_t format_count = 2;
    if (!cfp_private(&ref, toc[0x3d30] ? 0x5048c : 0x8b04, &format_count, 1, &status))
        return 0;
    if (status)
        return txn_status(call, status);
    if (txn_byte(txn_word(toc - 0x384), 0)) {
        Byte* feature = txn_memory(txn_word(toc - 0x380), 0, 1, 1);
        if (feature)
            *feature = 1;
    }

    uint32_t scrap;
    const uint32_t scrap_size = 0x2c;
    if (!cfp_private(&ref, 0x3e1c0, &scrap_size, 1, &scrap))
        return 0;
    if (!scrap)
        return txn_status(call, memFullErr);
    const uint32_t initialize_scrap[] = {scrap, 0, 1};
    if (!cfp_private(&ref, 0x3510, &scrap, 1, NULL) ||
        !cfp_private(&ref, 0xcb74, initialize_scrap, NELEM(initialize_scrap), NULL) ||
        !cfp_private(&ref, 0x7f4c, &scrap, 1, NULL))
        return 0;

    Byte registration = 0;
    const uint32_t pictures[] = {txn_word(toc + 0x1b9c), US_TO_SYN68K(&registration)};
    const uint32_t movies = txn_word(toc + 0x1f88);
    if (((options & 1) && !cfp_private(&ref, 0xb28, pictures, NELEM(pictures), NULL)) ||
        ((options & 2) && !cfp_private(&ref, 0x1704, &movies, 1, NULL)))
        return 0;
    uint32_t run;
    if (!cfp_private(&ref, 0x1352c, &options, 1, NULL) || !cfp_private(&ref, 0x8014, NULL, 0, &run) ||
        !txn_init_default_run(&ref, defaults, run))
        return 0;
    txn_store(toc + 0x3df8, run);
    toc[0x3d24] = 1;
    uint32_t resource_state;
    if (!cfp_private(&ref, 0x3d31c, NULL, 0, &resource_state))
        return 0;
    txn_store(toc + 0x3d28, resource_state);
    return txn_status(call, txn_field(txn_word(toc - 0x374), 0));
}

static int txn_terminate_textension(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference carbon, companion, terminate;
    if (!txn_carbon_reference_matches(name, 0x2e9a0, &carbon))
        return txn_saved(name, call);
    if (!txn_load_textension(&carbon))
        return 0;
    const uint32_t worker = txn_field(txn_word((const Byte*)carbon.toc - 0x113c), 0);
    if (!worker)
        return 1;
    /* This Textension version has an empty termination hook. Carbon still
       resolves the fragment and can dispatch an installed replacement. */
    if (txn_reference_matches(&companion) && ROMlib_carbon_probe_reference("Textension_CL", name, &terminate) &&
        terminate.code_offset == 0x1421c && US_TO_SYN68K(terminate.function) == worker &&
        txn_field(txn_field(worker, 0), 0) == 0x4e800020)
        return 1;
    return cfp_callback(worker, NULL, 0, NULL);
}

static int txn_init_document_fields(const ROMlib_carbon_reference* ref, uint32_t document, Byte* fields)
{
    txn_store(fields + 0x32, 0);
    txn_store(fields + 0x52, 0);
    txn_store(fields + 0xe8, 0);
    txn_store(fields + 0xf6, 0);
    memset(fields + 0x134, 0, 0x14);
    const uint32_t drag = document + 0xbc;
    const uint32_t initializer = US_TO_SYN68K(ref->toc) + 0xeb8;
    const uint32_t first[] = {document + 0xfa, 4, 3, initializer};
    const uint32_t second[] = {document + 0x106, 4, 3, initializer};
    return cfp_private(ref, 0x23cb4, &drag, 1, NULL) && cfp_private(ref, 0x6b054, first, NELEM(first), NULL) &&
           cfp_private(ref, 0x6b054, second, NELEM(second), NULL);
}

static int txn_init_document_layout(const ROMlib_carbon_reference* ref, uint32_t document, uint32_t window,
                                    uint32_t requested_rect, uint32_t options, void* output_frame)
{
    const void* source =
        requested_rect ? txn_memory(requested_rect, 0, sizeof(Rect), 0) : txn_memory(window, 0x10, sizeof(Rect), 0);
    if (!source)
        return 0;
    _Alignas(8) Rect bounds;
    memcpy(&bounds, source, sizeof bounds);
    if (options & (8 | 1))
        bounds.right = CW((uint16_t)CW(bounds.right) - 15);
    if (options & (4 | 1))
        bounds.bottom = CW((uint16_t)CW(bounds.bottom) - 15);

    const uint32_t layout = txn_field(txn_field(document, 2), 0x20), surface = txn_field(layout, 0xa);
    _Alignas(512) Byte result[0x118] = {0};
    txn_store(result + 0x110, -2);
    const uint32_t rect = US_TO_SYN68K(&bounds);
    const uint32_t set_bounds[] = {layout, rect};
    const uint32_t create_frame[] = {surface, rect, US_TO_SYN68K(result)};
    if (!cfp_private(ref, 0x279e0, set_bounds, NELEM(set_bounds), NULL) ||
        !cfp_callback(txn_field(txn_field(surface, 0), 0x34), create_frame, NELEM(create_frame), NULL))
        return 0;
    if (output_frame)
        txn_store(output_frame, txn_word(result + ((int16_t)txn_short(result + 0xe) < 0 ? 0 : 0x10)));
    return 1;
}

static int txn_create_document(const char* name, ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t window = call->words[1], requested_rect = call->words[2], options = call->words[3];
    const uint32_t error_slot = txn_word((const Byte*)ref->toc - 0x374);
    void* last_error = txn_memory(error_slot, 0, 4, 1);
    if (!last_error)
        return txn_saved(name, call);
    if (txn_word(last_error) != (uint32_t)memFullErr)
        txn_store(last_error, noErr);

    void* output = txn_memory(call->words[7], 0, 4, 1);
    if (!output)
        return txn_status(call, paramErr);
    txn_store(output, 0);
    if ((options & 0x10) && txn_uses_unicode_offsets(ref))
        return txn_status(call, -22013);

    uint32_t frame_slot, context;
    if (!ROMlib_carbon_frame_word(call, 8, &frame_slot) || !ROMlib_carbon_frame_word(call, 9, &context))
        return 0;
    void* output_frame = txn_memory(frame_slot, 0, 4, 1);
    if ((frame_slot && !output_frame) || (requested_rect && !txn_memory(requested_rect, 0, sizeof(Rect), 0)))
        return txn_status(call, paramErr);
    if (!requested_rect && !window)
        return txn_saved(name, call);

    uint32_t type = call->words[5];
    if (type != T('T', 'E', 'X', 'T') && type != T('t', 'x', 't', 'n'))
        type = T('t', 'x', 't', 'n');
    const uint32_t size = 0x158;
    uint32_t document;
    if (!cfp_private(ref, 0x3e1c0, &size, 1, &document))
        return 0;
    Byte* fields = txn_memory(document, 0, size, 1);
    if (!fields)
        return txn_status(call, memFullErr);
    if (!txn_init_document_fields(ref, document, fields))
        return 0;

    const uint32_t initialize[] = {document,       (options >> 1) & 1, 0,    options,        window,
                                   requested_rect, call->words[4],     type, call->words[0], call->words[6]};
    uint32_t status;
    if (!cfp_private(ref, 0x1efd4, initialize, NELEM(initialize), &status))
        return 0;
    txn_store(fields + 0x14c, context);
    if (status)
        return cfp_private(ref, 0x233e4, &document, 1, NULL) && cfp_private(ref, 0x3e218, &document, 1, NULL) &&
               txn_status(call, status);
    if (!txn_init_document_layout(ref, document, window, requested_rect, options, output_frame))
        return 0;
    txn_store(output, document);
    const uint32_t activate[] = {document, !(options & 0x8400)};
    return cfp_private(ref, 0x23334, activate, NELEM(activate), NULL) && txn_status(call, txn_word(last_error));
}

static int txn_has_provisional_edit(uint32_t editor)
{
    const void* start = txn_memory(editor, 0x56, 4, 0);
    return start && (int32_t)txn_word(start) >= 0;
}

static int txn_set_selection_range(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], editor = txn_field(object, 2);
    const Byte* selection = txn_selection_bytes(editor);
    const Byte* layout = txn_memory(txn_field(editor, 0x20), 0x2e, 1, 0);
    const Byte* mode = txn_memory(editor, 0x41, 1, 0);
    const void* length = txn_memory(txn_field(editor, 0xc), 6, 4, 0);
    const uint32_t setter = txn_field(txn_field(editor, 0), 0x2c);
    uint32_t start = call->words[1], end = call->words[2];
    if (!selection || !layout || !mode || !length || !setter || end < start)
        return txn_status(call, paramErr);

    const uint32_t document_length = txn_word(length);
    const uint32_t end_of_text = INT32_MAX;
    if (start == end_of_text)
        start = document_length;
    if (end == end_of_text)
        end = document_length;
    const int8_t direction = *layout;
    const int local_selection = !*mode;
    Byte previous[12];
    memcpy(previous, selection, sizeof previous);

    if (txn_field(object, 0xaa) == 2 && !((const Byte*)ref->toc)[0x3d30] && start != UINT32_MAX && (start || end)) {
        _Alignas(4) uint32_t unicode_range[2] = {CL(start * 2), CL(end == end_of_text ? end : end * 2)};
        _Alignas(4) uint32_t legacy_range[2] = {CL(start), CL(end)};
        const uint32_t arguments[] = {txn_word((const Byte*)ref->toc - 0x350), object, US_TO_SYN68K(unicode_range),
                                      US_TO_SYN68K(legacy_range)};
        if (!cfp_private(ref, 0x585a8, arguments, NELEM(arguments), NULL))
            return 0;
        start = CL(legacy_range[0]);
        end = CL(legacy_range[1]);
    }
    if (start != end_of_text)
        start = MIN(start, document_length);
    if (end != end_of_text)
        end = MIN(end, document_length);

    _Alignas(4) Byte next[12] = {0};
    txn_store(next, start);
    next[4] = direction == -1;
    txn_store(next + 6, end);
    next[10] = direction == 0;
    const int unchanged = txn_word(previous) == start && previous[4] == next[4] && txn_word(previous + 6) == end &&
                          previous[10] == next[10];
    int normalize = FALSE;
    if (local_selection) {
        ROMlib_carbon_ppc_frame script = {.words = {0x1e}};
        if (!ROMlib_carbon_call_romaw_service("GetScriptManagerVariable", &script))
            return 0;
        normalize = !(script.words[0] & 0x08000000);
    }
    const uint32_t arguments[] = {editor, US_TO_SYN68K(next), unchanged, normalize};
    return cfp_callback(setter, arguments, NELEM(arguments), NULL) && txn_status(call, noErr);
}

static uint32_t txn_action_key(uint32_t record, int undo)
{
    const uint16_t kind = txn_short(txn_memory(record, 0xa, 2, 0));
    switch (kind) {
    case 1:
    case 0x100:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    case 0x10:
        switch (txn_field(record, 0x1e)) {
        case T('f', 'o', 'n', 't'):
            return 4;
        case T('k', 'l', 'o', 'r'):
            return 5;
        case T('s', 'i', 'z', 'e'):
            return 6;
        case T('f', 'a', 'c', 'e'):
            return 7;
        case T('a', 't', 'f', 'e'):
            return 13;
        case T('a', 't', 'v', 'a'):
            return 14;
        default:
            return 0x400;
        }
    case 0x80:
        return 12;
    case 0x200: {
        const Byte* bytes = txn_memory(record, 0xc, 0x17, 0);
        if (!bytes)
            return 0x400;
        switch (bytes[undo && bytes[1] == 2 ? 2 : 0x16]) {
        case 2:
            return 10;
        case 4:
            return 9;
        case 8:
        case 16:
            return 0x400;
        default:
            return 8;
        }
    }
    case 0x1389: {
        const Byte* flag = txn_memory(record, 0x62, 1, 0);
        return flag && *flag ? 12 : 11;
    }
    default:
        return 0x400;
    }
}

static int txn_can_edit_history(ROMlib_carbon_ppc_frame* call, int undo)
{
    const uint32_t object = call->words[0];
    void* action = txn_memory(call->words[1], 0, 4, 1);
    if (action)
        txn_store(action, 0x400);
    if (undo && txn_has_provisional_edit(txn_field(object, 2)))
        return txn_status(call, TRUE);
    const uint32_t history = txn_field(object, 0x130);
    const Byte* state = txn_memory(history, 0xc, 2, 0);
    if (!state || !state[0])
        return txn_status(call, FALSE);
    const uint32_t entry = txn_field(history, undo ? 0x10 : 0x14);
    if (!entry && (undo ? state[1] != 1 : state[1] == 1))
        return txn_status(call, FALSE);
    if (action)
        txn_store(action, txn_action_key(state[1] == (undo ? 1 : 2) ? history : entry, undo));
    return txn_status(call, TRUE);
}

static int txn_is_typing_edit(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0];
    if (!object)
        return txn_status(call, FALSE);
    const uint32_t history = txn_field(object, 0x130);
    if (!history)
        return txn_status(call, TRUE);
    const uint32_t editor = txn_field(object, 2);
    const Byte* selection = txn_selection_bytes(editor);
    if (!selection)
        return txn_status(call, FALSE);
    _Alignas(4) Byte current[12], last[12] = {0};
    memcpy(current, selection, sizeof current);
    const uint32_t query = txn_field(txn_field(history, 0), 0x18);
    const uint32_t arguments[] = {history, US_TO_SYN68K(last)};
    uint32_t action;
    if (!query || !cfp_callback(query, arguments, NELEM(arguments), &action))
        return 0;
    if (txn_has_provisional_edit(editor))
        return txn_status(call, FALSE);
    return txn_status(call, action == 1 || action == 2 || action == 3 || action == 0x19 ||
                                txn_word(current) != txn_word(current + 6) ||
                                txn_word(current + 6) != txn_word(last + 6));
}

static int txn_font_menu_object(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                                const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0];
    if (operation == txn_new_font_menu) {
        void* output = txn_memory(call->words[3], 0, 4, 1);
        if (!output || !object)
            return txn_status(call, paramErr);
        txn_store(output, 0);
        const uint32_t size = 0x16;
        uint32_t menu;
        if (!cfp_private(ref, 0x3e1c0, &size, 1, &menu))
            return 0;
        if (!menu)
            return txn_status(call, memFullErr);
        const uint32_t initialization[] = {menu, object, (int16_t)call->words[1], (int16_t)call->words[2]};
        uint32_t initialized;
        if (!cfp_private(ref, 0x5b4d0, initialization, NELEM(initialization), &initialized))
            return 0;
        if (!initialized)
            return txn_status(call, memFullErr);
        txn_store(output, menu);
        return txn_status(call, noErr);
    }
    if (!txn_memory(object, 0, 0x16, 0))
        return txn_status(call, paramErr);
    if (operation == txn_dispose_font_menu)
        return cfp_private(ref, 0x5b458, &object, 1, NULL) && cfp_private(ref, 0x3e218, &object, 1, NULL) &&
               txn_status(call, noErr);
    void* output = txn_memory(call->words[1], 0, 4, 1);
    if (output)
        txn_store(output, txn_field(object, 2));
    return txn_status(call, noErr);
}

static int txn_apply_font_menu(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                               const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], menu = call->words[1];
    if (!txn_memory(menu, 0, 0x16, 0))
        return txn_status(call, paramErr);
    const void* global_error = (const Byte*)ref->toc + 0x3d2c;
    if (operation == txn_prepare_font_menu) {
        const uint32_t arguments[] = {menu, document};
        return cfp_private(ref, 0x5bb88, arguments, NELEM(arguments), NULL) && txn_status(call, txn_word(global_error));
    }
    void* editor_error = txn_memory(txn_field(document, 2), 0x42, 4, 1);
    if (!editor_error)
        return txn_status(call, paramErr);

    const uint32_t arguments[] = {menu, document, (int16_t)call->words[2], (int16_t)call->words[3]};
    uint32_t result;
    if (!cfp_private(ref, 0x5b77c, arguments, NELEM(arguments), &result))
        return 0;
    /* The worker can replace the editor. Read the current error slot after editing. */
    editor_error = txn_memory(txn_field(document, 2), 0x42, 4, 1);
    if (!editor_error)
        return 0;
    if (!result)
        result = txn_word(editor_error);
    txn_store(editor_error, 0);
    return txn_status(call, result ? result : txn_word(global_error));
}

static int txn_get_font_defaults(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t array = txn_field(txn_field(call->words[0], 2), 0x14);
    const void* size = txn_memory(array, 6, 4, 0);
    void* count = txn_memory(call->words[1], 0, 4, 1);
    if (!size || !count)
        return txn_status(call, paramErr);
    const uint32_t items = txn_word(size);
    if (items && call->words[2]) {
        enum { font_default_size = 14 };

        if (items > UINT32_MAX / font_default_size)
            return txn_status(call, paramErr);
        const unsigned extent = items * font_default_size;
        const void* records = txn_memory(txn_field(txn_field(array, 0xc), 0), 0, extent, 0);
        void* output = txn_memory(call->words[2], 0, extent, 1);
        if (!records || !output)
            return txn_status(call, paramErr);
        txn_store(count, items);
        memmove(output, records, extent);
    }
    else
        txn_store(count, items);
    return txn_status(call, noErr);
}

static int txn_apply_default_attribute(uint32_t document, uint32_t range, uint32_t tag, uint32_t value, unsigned size)
{
    _Alignas(32) Byte attribute[32] = {0};
    const uint32_t editor = txn_field(document, 2);
    const uint32_t setter = txn_field(txn_field(editor, 0), 0x40);
    txn_store(attribute + 4, tag);
    if (size == 2)
        value <<= 16;
    txn_store(attribute + 8, value);
    const uint32_t args[] = {editor, range, US_TO_SYN68K(attribute), 0};
    return cfp_callback(setter, args, NELEM(args), NULL);
}

static int txn_find_keyboard_default(const ROMlib_carbon_reference* ref, uint32_t settings, Byte* output)
{
    const uint32_t keyboard_script = 0x16;
    uint32_t script;
    if (!cfp_private(ref, 0x6b478, &keyboard_script, 1, &script))
        return 0;
    const uint32_t find[] = {settings, (int16_t)script, US_TO_SYN68K(output)};
    return cfp_private(ref, 0x5d9cc, find, NELEM(find), NULL);
}

static int txn_apply_font_defaults(const ROMlib_carbon_reference* ref, uint32_t document, uint32_t range,
                                   uint32_t settings, uint32_t records, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t script = (int16_t)txn_field(records + 14 * i, 8);
        _Alignas(16) Byte record[16] = {0};
        const uint32_t find[] = {settings, script, US_TO_SYN68K(record)};
        if (!cfp_private(ref, 0x5d9cc, find, NELEM(find), NULL) ||
            !txn_apply_default_attribute(document, range, T('f', 'o', 'n', 't'), txn_word(record), 2) ||
            !txn_apply_default_attribute(document, range, T('s', 'i', 'z', 'e'), txn_short(record + 4), 2) ||
            !txn_apply_default_attribute(document, range, T('f', 'a', 'c', 'e'), record[12], 4))
            return 0;
    }
    return 1;
}

static int txn_update_font_defaults(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], count = call->words[1], records = call->words[2];
    const uint32_t editor = txn_field(document, 2), settings = txn_field(editor, 0x14);
    if (!editor || !records)
        return txn_status(call, paramErr);
    if (txn_byte(document, 0xa0))
        return txn_status(call, -5250);
    const Byte* last_error = (const Byte*)ref->toc + 0x3d2c;
    if (!settings)
        return txn_status(call, txn_word(last_error));
    if (count > INT16_MAX || (count && !txn_memory(records, 0, 14 * count, 0)))
        return txn_status(call, paramErr);

    _Alignas(16) Byte previous[16] = {0};
    const uint32_t install[] = {settings, records, count};
    uint32_t status;
    if (!txn_find_keyboard_default(ref, settings, previous) ||
        !cfp_private(ref, 0x5da4c, install, NELEM(install), &status))
        return 0;
    if (status)
        return txn_status(call, status);

    _Alignas(16) Byte range[16] = {0};
    txn_store(range + 6, INT32_MAX);
    const uint32_t range_address = US_TO_SYN68K(range);
    if (txn_field(txn_field(editor, 0xc), 6)) {
        const Byte* selection = txn_selection_bytes(editor);
        Byte* mode = txn_memory(editor, 0x41, 1, 1);
        if (!selection || !mode)
            return 0;
        _Alignas(16) Byte saved[12];
        memcpy(saved, selection, sizeof saved);
        const Byte prior_mode = *mode;
        *mode = 1;
        const int applied = txn_apply_font_defaults(ref, document, range_address, settings, records, count);
        const uint32_t current = txn_field(document, 2);
        const uint32_t restore[] = {current, US_TO_SYN68K(saved), 1, 1};
        const int restored =
            applied && cfp_callback(txn_field(txn_field(current, 0), 0x2c), restore, NELEM(restore), NULL);
        *mode = prior_mode;
        if (!restored)
            return 0;
    }
    else {
        _Alignas(16) Byte updated[16];
        memcpy(updated, previous, sizeof updated);
        if (!txn_find_keyboard_default(ref, settings, updated) ||
            (txn_word(previous) != txn_word(updated) &&
             !txn_apply_default_attribute(document, range_address, T('f', 'o', 'n', 't'), txn_word(updated), 2)) ||
            (txn_word(previous + 4) != txn_word(updated + 4) &&
             !txn_apply_default_attribute(document, range_address, T('s', 'i', 'z', 'e'), txn_short(updated + 4), 2)) ||
            (previous[12] != updated[12] &&
             !txn_apply_default_attribute(document, range_address, T('f', 'a', 'c', 'e'), updated[12], 4)))
            return 0;
    }
    return txn_status(call, txn_word(last_error));
}

static int txn_select_document(uint32_t object)
{
    const uint32_t editor = txn_field(object, 2);
    const uint32_t selection_method = txn_field(txn_field(editor, 0), 0x2c);
    const void* length = txn_memory(txn_field(editor, 0xc), 6, 4, 0);
    if (!object)
        return 1;
    if (!length || !selection_method)
        return 0;
    _Alignas(16) Byte selection[12] = {0};
    txn_store(selection + 6, txn_word(length));
    selection[10] = 1;
    const uint32_t arguments[] = {editor, US_TO_SYN68K(selection), 1, 1};
    return cfp_callback(selection_method, arguments, NELEM(arguments), NULL);
}

static int txn_empty_history(uint32_t object, const ROMlib_carbon_reference* ref)
{
    uint32_t record = txn_field(object, 0x130);
    if (!record)
        return 1;
    void* root = txn_memory(object, 0x130, 4, 1);
    void* depth = txn_memory(object, 0xa8, 2, 1);
    if (!root || !depth)
        return 0;
    /* History is a doubly linked chain with the current action stored at its root. */
    uint32_t previous;
    while ((previous = txn_field(record, 0x10))) {
        if (txn_field(previous, 0x14) != record || previous == txn_word(root))
            return 0;
        record = previous;
    }
    while (record) {
        const uint32_t next = txn_field(record, 0x14);
        void* back = next ? txn_memory(next, 0x10, 4, 1) : NULL;
        const uint32_t destroy = txn_field(txn_field(record, 0), 0);
        if ((next && (!back || txn_word(back) != record)) || !destroy)
            return 0;
        if (!cfp_callback(destroy, &record, 1, NULL) || !cfp_private(ref, 0x3e218, &record, 1, NULL))
            return 0;
        if (back)
            txn_store(back, 0);
        txn_store(root, next);
        record = next;
    }
    memset(depth, 0, 2);
    return 1;
}

static int txn_apply_history(uint32_t object, int undo, const ROMlib_carbon_reference* ref)
{
    const Byte* pending = txn_memory(object, 0xa3, 1, 0);
    if (!pending)
        return !object;
    if (*pending) {
        const uint32_t editor = txn_field(object, 2);
        if (txn_has_provisional_edit(editor) && !cfp_private(ref, 0x49244, &editor, 1, NULL))
            return 0;
    }
    void* history = txn_memory(object, 0x130, 4, 1);
    uint32_t record = txn_word(history);
    if (!record)
        return 1;
    const Byte* phase = txn_memory(record, 0xd, 1, 0);
    if (!phase)
        return 0;
    if (undo != (*phase == 1)) {
        record = txn_field(record, undo ? 0x10 : 0x14);
        if (!record)
            return 1;
        txn_store(history, record);
    }
    return cfp_private(ref, 0x1df50, &object, 1, NULL);
}

static int txn_scroll_selection(uint32_t object, uint32_t reveal, uint32_t redraw, const ROMlib_carbon_reference* ref)
{
    if (!object)
        return 1;
    const uint32_t editor = txn_field(object, 2);
    const uint32_t layout = txn_field(editor, 0x20);
    const Byte* options = txn_memory(editor, 0x48, 1, 0);
    _Alignas(8) Rect bounds, selection;
    const uint32_t read[] = {layout, US_TO_SYN68K(&bounds)};
    if (!layout || !options || !cfp_private(ref, 0x254d8, read, NELEM(read), NULL))
        return 0;
    const uint32_t scroll[] = {layout, read[1], US_TO_SYN68K(&selection), reveal, redraw, 0, *options};
    uint32_t visible;
    if (!cfp_private(ref, 0x26d3c, scroll, NELEM(scroll), &visible))
        return 0;
    if (visible)
        return 1;
    const uint32_t adjust[] = {layout, scroll[2]};
    if (!cfp_private(ref, 0x267cc, adjust, NELEM(adjust), NULL))
        return 0;
    const Byte* bars = txn_memory(object, 7, 2, 0);
    if (!bars || ((!txn_field(object, 0xa) || !bars[0]) && !bars[1]))
        return 1;
    const uint32_t horizontal[] = {object, 1, 0}, vertical[] = {object, 0, 0};
    return cfp_private(ref, 0x1c410, horizontal, NELEM(horizontal), NULL) &&
           cfp_private(ref, 0x1c410, vertical, NELEM(vertical), NULL);
}

static int txn_replace_layout_bounds(uint32_t layout, const Rect* requested, const ROMlib_carbon_reference* ref)
{
    Byte* bounds = txn_memory(layout, 0x35, 1 + sizeof(Rect), 1);
    if (!bounds)
        return 0;
    if (memcmp(bounds + 1, requested, sizeof *requested)) {
        void* cached_line = txn_memory(txn_field(layout, 0x30), 0x16, 4, 1);
        if (!cached_line)
            return 0;
        memmove(bounds + 1, requested, sizeof *requested);
        *bounds = 1;
        const uint32_t reset[] = {txn_field(layout, 6), 0};
        const uint32_t measure[] = {layout, 1};
        uint32_t measured;
        if (!cfp_private(ref, 0x45124, reset, NELEM(reset), NULL))
            return 0;
        txn_store(cached_line, UINT32_MAX);
        if (!cfp_private(ref, 0x269d0, measure, NELEM(measure), &measured))
            return 0;
        const uint32_t reflow[] = {layout, 0};
        if (!measured && !cfp_private(ref, 0x25650, reflow, NELEM(reflow), NULL))
            return 0;
    }
    return 1;
}

static int txn_scroll_from_edge(uint32_t object, uint32_t edge, const ROMlib_carbon_reference* ref)
{
    _Alignas(8) uint32_t saved_scroll[2];
    const uint32_t read[] = {object, US_TO_SYN68K(saved_scroll), US_TO_SYN68K(saved_scroll + 1)};
    if (!cfp_private(ref, 0x6b2c8, read, NELEM(read), NULL))
        return 0;
    const uint32_t set[] = {object, edge, edge};
    if (!cfp_private(ref, 0x6b2f8, set, NELEM(set), NULL))
        return 0;
    const int result = txn_scroll_selection(object, 0, 1, ref);
    const uint32_t restore[] = {object, txn_word(saved_scroll), txn_word(saved_scroll + 1)};
    return cfp_private(ref, 0x6b2f8, restore, NELEM(restore), NULL) && result;
}

static int txn_update_view_rect(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0];
    if (!object || !call->words[1])
        return 1;
    const uint32_t layout = txn_field(txn_field(object, 2), 0x20);
    const Rect* requested = txn_memory(call->words[1], 0, sizeof *requested, 0);
    const Byte* direction = txn_memory(layout, 0x2e, 1, 0);
    if (!requested || !direction || !txn_replace_layout_bounds(layout, requested, ref))
        return 0;
    return txn_scroll_from_edge(object, (int8_t)*direction == -1 ? INT32_MAX : 0, ref);
}

static int txn_convert_position(ROMlib_carbon_ppc_frame* call, int offset_to_point, const ROMlib_carbon_reference* ref)
{
    const uint32_t editor = txn_field(call->words[0], 2);
    const uint32_t layout = txn_field(editor, 0x20);
    void* output = txn_memory(call->words[2], 0, 4, 1);
    if (!layout || !output)
        return txn_status(call, paramErr);
    uint32_t converted = 0;
    if (offset_to_point) {
        const void* length = txn_memory(txn_field(editor, 0xc), 6, 4, 0);
        if (!length || call->words[1] > txn_word(length))
            return txn_status(call, paramErr);
        const uint32_t args[] = {US_TO_SYN68K(&converted), layout, call->words[1], 0, 0};
        if (!cfp_private(ref, 0x27fdc, args, NELEM(args), NULL))
            return 0;
        memcpy(output, &converted, sizeof converted);
        return txn_status(call, noErr);
    }

    enum { txn_outside_of_frame = -22018, txn_outside_of_line = -22017 };

    _Alignas(2) Byte outside[2] = {0};
    const uint32_t args[] = {layout, call->words[1], US_TO_SYN68K(&converted), US_TO_SYN68K(outside),
                             US_TO_SYN68K(outside + 1)};
    if (!cfp_private(ref, 0x27d80, args, NELEM(args), NULL))
        return 0;
    memcpy(output, &converted, sizeof converted);
    return txn_status(call, outside[0] ? txn_outside_of_frame : outside[1] ? txn_outside_of_line : noErr);
}

static int txn_normalize_run_range(uint32_t object, uint32_t first, uint32_t last, uint32_t range[2],
                                   const ROMlib_carbon_reference* ref)
{
    txn_store(range, first);
    txn_store(range + 1, last);
    const uint32_t args[] = {object, US_TO_SYN68K(range), US_TO_SYN68K(range + 1)};
    return cfp_private(ref, 0x15040, args, NELEM(args), NULL);
}

static int txn_count_runs_in_range(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], start = call->words[1], end = call->words[2];
    void* output = txn_memory(call->words[3], 0, 4, 1);
    if (!object || !output || (start != UINT32_MAX && end < start))
        return txn_status(call, paramErr);
    _Alignas(8) uint32_t range[2];
    if (!txn_normalize_run_range(object, start, end, range, ref))
        return 0;
    txn_store(output, 0);
    const uint32_t runs = txn_field(txn_field(object, 2), 0x10);
    if (!runs)
        return txn_status(call, noErr);
    const uint32_t first = txn_word(range), last = txn_word(range + 1);
    const uint32_t lower[] = {runs, first, 0};
    const uint32_t upper[] = {runs, last == first + 1 ? first : last, 0x01000000};
    uint32_t lower_run, upper_run;
    if (!cfp_private(ref, 0x3bb30, lower, NELEM(lower), &lower_run) ||
        !cfp_private(ref, 0x3bb30, upper, NELEM(upper), &upper_run))
        return 0;
    if ((int32_t)upper_run < (int32_t)lower_run)
        upper_run = lower_run;
    txn_store(output, upper_run - lower_run + 1);
    return txn_status(call, noErr);
}

static int txn_indexed_run_details(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    enum { txn_no_matching_run = -22011, txn_attribute_size = 12 };

    const uint32_t object = call->words[0], start = call->words[2], end = call->words[3];
    if (!object || (start != UINT32_MAX && end < start))
        return txn_status(call, paramErr);
    void* outputs[3];
    for (unsigned i = 0; i < NELEM(outputs); ++i) {
        outputs[i] = txn_memory(call->words[4 + i], 0, 4, 1);
        if (!outputs[i] && call->words[4 + i])
            return txn_status(call, paramErr);
    }
    const uint32_t attributes = call->words[7];
    uint32_t records;
    if (!ROMlib_carbon_frame_word(call, 8, &records))
        return 0;
    if (attributes &&
        (attributes > UINT32_MAX / txn_attribute_size || !txn_memory(records, 0, attributes * txn_attribute_size, 1)))
        return txn_status(call, paramErr);

    _Alignas(8) uint32_t range[2], limits[2];
    uint32_t index, run;
    if (!txn_normalize_run_range(object, start, end, range, ref))
        return 0;
    const uint32_t editor = txn_field(object, 2);
    const uint32_t lookup[] = {editor, txn_word(range), txn_word(range + 1), (int16_t)call->words[1],
                               US_TO_SYN68K(&index)};
    if (!editor || !cfp_private(ref, 0x1a0cc, lookup, NELEM(lookup), &run))
        return 0;
    if (!run)
        return txn_status(call, txn_no_matching_run);
    if (outputs[0] || outputs[1]) {
        const uint32_t bounds[] = {object, txn_word(&index), US_TO_SYN68K(limits), US_TO_SYN68K(limits + 1)};
        if (!cfp_private(ref, 0x1a204, bounds, NELEM(bounds), NULL))
            return 0;
        for (unsigned i = 0; i < 2; ++i)
            if (outputs[i])
                memcpy(outputs[i], limits + i, sizeof limits[i]);
    }
    const uint32_t get_type = txn_field(txn_field(run, 0), 0xc4);
    uint32_t type;
    if (!get_type || !cfp_callback(get_type, &run, 1, &type))
        return 0;
    if (outputs[2])
        txn_store(outputs[2], type);
    if (!attributes)
        return txn_status(call, noErr);
    const uint32_t fetch[] = {run, attributes, records, type};
    uint32_t status;
    return cfp_private(ref, 0x1a2b8, fetch, NELEM(fetch), &status) && txn_status(call, status);
}

static int txn_data_run(const ROMlib_carbon_reference* ref, uint32_t editor, uint32_t offset, uint32_t* run,
                        uint32_t* available, uint32_t* is_text)
{
    uint32_t extent = 0;
    const uint32_t args[] = {editor, offset, US_TO_SYN68K(&extent)};
    if (!cfp_private(ref, 0x24614, args, NELEM(args), run))
        return 0;
    *is_text = 0;
    *available = txn_word(&extent);
    if (!*run)
        return 1;
    const uint32_t query = txn_field(txn_field(*run, 0), 0x5c);
    return query && cfp_callback(query, run, 1, is_text);
}

static int txn_copy_data(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    enum { txn_mixed_data_types = -22003 };

    const uint32_t object = call->words[0], editor = txn_field(object, 2), buffer = txn_field(editor, 0xc);
    uint32_t start = call->words[1], end = call->words[2];
    void* output = txn_memory(call->words[3], 0, 4, 1);
    const void* length = txn_memory(buffer, 6, 4, 0);
    if (!output || !length || (start != UINT32_MAX && start > end))
        return txn_status(call, paramErr);
    const int unicode = txn_uses_unicode_offsets(ref);
    if (start == UINT32_MAX) {
        const Byte* selection = txn_selection_bytes(editor);
        if (!selection)
            return txn_status(call, paramErr);
        start = txn_word(selection);
        end = txn_word(selection + 6);
    }
    else if (!unicode && txn_field(object, 0xaa) == 2 && (start || end)) {
        _Alignas(8) uint32_t wide[2] = {CL(start * 2), CL(end * 2)}, legacy[2] = {CL(start), CL(end)};
        const uint32_t args[] = {txn_word((const Byte*)ref->toc - 0x350), object, US_TO_SYN68K(wide),
                                 US_TO_SYN68K(legacy)};
        if (!cfp_private(ref, 0x585a8, args, NELEM(args), NULL))
            return 0;
        start = txn_word(legacy);
        end = txn_word(legacy + 1);
    }
    txn_store(output, 0);
    end = MIN(end, txn_word(length));
    if (start > end)
        return txn_status(call, paramErr);

    uint32_t run, available, text;
    if (!txn_data_run(ref, editor, start, &run, &available, &text))
        return 0;
    if (!text) {
        if (available < end - start)
            return txn_status(call, txn_mixed_data_types);
        if (!run)
            return txn_status(call, noErr);
        const uint32_t copy = txn_field(txn_field(run, 0), 0xcc);
        const uint32_t args[] = {run, call->words[3]};
        uint32_t result;
        return copy && cfp_callback(copy, args, NELEM(args), &result) && txn_status(call, (int16_t)result);
    }

    uint32_t handle, copied = 0;
    int16_t failure;
    uint16_t allocation_error = 0;
    const uint32_t width = unicode ? 2 : 1;
    if (end - start > INT32_MAX / width)
        return txn_status(call, memFullErr);
    const uint32_t allocate[] = {(end - start) * width, 0, US_TO_SYN68K(&allocation_error)};
    if (!cfp_private(ref, 0x3d428, allocate, NELEM(allocate), &handle))
        return 0;
    failure = (int16_t)CW(allocation_error);
    if (!handle)
        return txn_status(call, failure);
    while (!failure && start < end) {
        if (!text || !available) {
            failure = txn_mixed_data_types;
            break;
        }
        const uint32_t length = MIN(available, end - start), bytes = length * width;
        const uint32_t source_args[] = {buffer, start};
        uint32_t source;
        if (!cfp_private(ref, 0x37c3c, source_args, NELEM(source_args), &source)) {
            failure = paramErr;
            break;
        }
        void* destination = txn_memory(txn_field(handle, 0), copied, bytes, 1);
        const void* data = txn_memory(source, 0, bytes, 0);
        if (!data || !destination) {
            failure = paramErr;
            break;
        }
        memmove(destination, data, bytes);
        start += length;
        copied += bytes;
        if (start < end && !txn_data_run(ref, editor, start, &run, &available, &text))
            failure = paramErr;
    }
    if (failure)
        return cfp_private(ref, 0x6b4c0, &handle, 1, NULL) && txn_status(call, failure);
    txn_store(output, handle);
    return txn_status(call, noErr);
}

static int txn_copy_encoded_data(const char* name, ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], format = call->words[4];
    void* output = txn_memory(call->words[3], 0, 4, 1);
    if (!txn_field(document, 2) || !output)
        return txn_status(call, paramErr);

    const Byte unicode = *((const Byte*)ref->toc + 0x3d30);
    if ((format == T('u', 't', 'x', 't') && !unicode) || (format == T('T', 'E', 'X', 'T') && unicode))
        return txn_saved(name, call);

    txn_store(output, 0);
    _Alignas(8) uint32_t range[] = {CL(call->words[1]), CL(call->words[2])};
    const uint32_t address = US_TO_SYN68K(range);
    const uint32_t normalize[] = {document, address, address + 4};
    if (!cfp_private(ref, 0x15040, normalize, NELEM(normalize), NULL))
        return 0;
    const uint32_t copy[] = {document, txn_word(range), txn_word(range + 1), call->words[3]};
    uint32_t status;
    if (!cfp_private(ref, 0x6b358, copy, NELEM(copy), &status))
        return 0;
    const int16_t error = (int16_t)status;
    return txn_status(call, error ? (uint32_t)error : txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_search_encoding(const ROMlib_carbon_reference* ref, uint32_t run, int unicode, uint32_t* encoding,
                               uint32_t* status)
{
    if (unicode) {
        const uint32_t utf16[] = {0x100, 0, 0};
        return cfp_private(ref, 0x6b6e8, utf16, NELEM(utf16), encoding);
    }
    _Alignas(2) uint16_t font;
    const uint32_t attribute[] = {run, T('f', 'o', 'n', 't'), US_TO_SYN68K(&font)};
    uint32_t found, script;
    if (!cfp_private(ref, 0x3684c, attribute, NELEM(attribute), &found))
        return 0;
    _Alignas(256) Str255 family = {0};
    if (found) {
        const uint32_t name[] = {(int16_t)CW(font), US_TO_SYN68K(family)};
        if (!cfp_private(ref, 0x6b700, name, NELEM(name), NULL))
            return 0;
    }
    if (!cfp_callback(txn_field(txn_field(run, 0), 0x74), &run, 1, &script))
        return 0;
    _Alignas(4) uint32_t converted = 0;
    const uint32_t upgrade[] = {script, -128, -128, US_TO_SYN68K(family), US_TO_SYN68K(&converted)};
    if (!cfp_private(ref, 0x6b718, upgrade, NELEM(upgrade), status))
        return 0;
    *encoding = txn_word(&converted);
    return 1;
}

static int txn_find_data(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    enum {
        not_found = -0x55f8,
        text_type = T('T', 'E', 'X', 'T'),
        unicode_type = T('u', 't', 'x', 't'),
        picture_type = T('P', 'I', 'C', 'T'),
        movie_type = T('m', 'o', 'o', 'v'),
        sound_type = T('s', 'n', 'd', ' ')
    };

    const uint32_t document = call->words[0], type = call->words[2], editor = txn_field(document, 2);
    const uint32_t query = call->words[1], options = call->words[3], callback = call->words[6],
                   context = call->words[7];
    const int search_text = type == text_type || type == unicode_type;
    const Byte* key = search_text ? txn_memory(query, 0, 12, 0) : NULL;
    if (!editor || call->words[4] >= call->words[5] || (search_text && !key) ||
        (!search_text && type != picture_type && type != movie_type && type != sound_type))
        return txn_status(call, paramErr);
    uint32_t first_result, last_result;
    if (!ROMlib_carbon_frame_word(call, 8, &first_result) || !ROMlib_carbon_frame_word(call, 9, &last_result))
        return 0;
    void* first_output = txn_memory(first_result, 0, 4, 1);
    void* last_output = txn_memory(last_result, 0, 4, 1);
    if (!first_output || !last_output)
        return txn_status(call, paramErr);
    const uint32_t pattern[] = {txn_word(key), key ? txn_word(key + 4) : 0, key ? txn_word(key + 8) : 0};
    if (search_text && pattern[1] && !txn_memory(pattern[0], 0, pattern[1], 0))
        return txn_status(call, paramErr);

    _Alignas(8) uint32_t range[] = {CL(call->words[4]), CL(call->words[5])};
    const uint32_t range_address = US_TO_SYN68K(range);
    const uint32_t normalize[] = {document, range_address, range_address + 4};
    if (!cfp_private(ref, 0x15040, normalize, NELEM(normalize), NULL))
        return 0;
    const uint32_t buffer = txn_field(editor, 0xc);
    const uint32_t last = MIN(txn_word(range + 1), txn_field(buffer, 6));
    const int unicode = txn_uses_unicode_offsets(ref);
    txn_store(last_output, UINT32_MAX);
    txn_store(first_output, UINT32_MAX);
    uint32_t position = txn_word(range), status = not_found;
    while (position < last) {
        uint32_t first = position, count = 0, encoding = 0;
        do {
            uint32_t run, available, is_text, run_encoding;
            if (!txn_data_run(ref, editor, position, &run, &available, &is_text))
                return 0;
            if (!run || !available) {
                position = last;
                break;
            }
            uint32_t encoding_status = noErr;
            if (!txn_search_encoding(ref, run, unicode, &run_encoding, &encoding_status))
                return 0;
            if (encoding_status)
                return txn_status(call, encoding_status);
            int match;
            if (is_text)
                match = search_text && pattern[2] == run_encoding;
            else {
                uint32_t run_type;
                if (!cfp_callback(txn_field(txn_field(run, 0), 0x14), &run, 1, &run_type))
                    return 0;
                match = (type == picture_type && (run_type == picture_type || run_type == T('p', 'i', 'c', 't'))) ||
                        (type == movie_type && run_type == movie_type) ||
                        (type == sound_type && run_type == T('A', 'I', 'F', 'F'));
            }
            if (!match && count)
                break;
            position += MIN(available, last - position);
            if (match) {
                encoding = run_encoding;
                count = position - first;
                if (!callback && !is_text) {
                    txn_store(first_output, first);
                    txn_store(last_output, first + 1);
                    return txn_status(call, noErr);
                }
            }
            else
                first = position;
        } while (position < last);
        if (!count)
            break;
        Byte matched = 0;
        const uint32_t match_flag = US_TO_SYN68K(&matched);
        if (callback) {
            uint32_t text;
            const uint32_t lock[] = {buffer, 0};
            if (first > UINT32_MAX / (unicode ? 2 : 1) || !cfp_private(ref, 0x37c7c, lock, NELEM(lock), &text))
                return 0;
            const uint32_t offset = first * (unicode ? 2 : 1);
            const uint32_t search[] = {query, type,         options,     text + offset, encoding, first,
                                       count, first_result, last_result, match_flag,    context,  callback};
            const int searched =
                text && text <= UINT32_MAX - offset && cfp_private(ref, 0x151e8, search, NELEM(search), &status);
            const int unlocked = cfp_private(ref, 0x37cf4, &buffer, 1, NULL);
            if (!searched || !unlocked)
                return 0;
            if (!matched)
                status = not_found;
        }
        else {
            uint32_t text;
            const uint32_t source[] = {buffer, first};
            if (!cfp_private(ref, 0x37c3c, source, NELEM(source), &text))
                return 0;
            const uint32_t search[] = {pattern[0], pattern[1],   pattern[2],  type,       text,   encoding, first,
                                       count,      first_result, last_result, match_flag, editor, options};
            if (!cfp_private(ref, unicode ? 0x15260 : 0x154ac, search, NELEM(search), &status))
                return 0;
        }
        if (!status || position == last)
            break;
    }
    return txn_status(call, status);
}

static int txn_finish_set_data(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref, uint32_t document,
                               uint32_t status)
{
    if ((int16_t)status)
        return txn_status(call, (int16_t)status);
    void* count = txn_memory(document, 0xf6, 4, 1);
    if (!count)
        return 0;
    txn_store(count, txn_word(count) + 1);
    const uint16_t dirty = txn_short((const Byte*)ref->toc + 0x34a0);
    const uint32_t update[] = {document, 0, 1};
    if (((dirty & 0x10) && !cfp_private(ref, 0x1c85c, update, NELEM(update), NULL)) ||
        ((dirty & 0x40) && !cfp_private(ref, 0x1de58, update, NELEM(update), NULL)))
        return 0;
    return txn_status(call, txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_replace_plain_data(const char* name, ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], editor = txn_field(document, 2), type = call->words[1];
    const uint32_t source = call->words[2], size = call->words[3];
    uint32_t start = call->words[4], end = call->words[5];
    const int unicode = txn_uses_unicode_offsets(ref), inserting = source && size;
    const uint32_t plain_type = unicode ? T('u', 't', 'x', 't') : T('T', 'E', 'X', 'T');
    if (!editor || (end < start && start != UINT32_MAX))
        return txn_status(call, paramErr);
    if (txn_field(document, 0xaa) == 2 || (inserting && type != plain_type))
        return txn_saved(name, call);
    if (inserting && !txn_memory(source, 0, size, 0))
        return txn_status(call, paramErr);

    const uint32_t extent[] = {document, size};
    uint32_t status;
    if (!cfp_private(ref, 0x13d2c, extent, NELEM(extent), &status))
        return 0;
    if ((int16_t)status)
        return txn_status(call, memFullErr);
    if (txn_byte(document, 0xa0) == 1)
        return txn_status(call, -0x1482);
    if (start == end && !size)
        return txn_status(call, noErr);

    const uint32_t length = txn_field(txn_field(editor, 0xc), 6);
    if (!inserting && !length)
        return txn_status(call, txn_word((const Byte*)ref->toc + 0x3d2c));
    const Byte* selection = txn_selection_bytes(editor);
    if (!selection)
        return txn_status(call, paramErr);
    const uint32_t selected_start = txn_word(selection), selected_end = txn_word(selection + 6);
    if (start == 0x7fffffff)
        start = length;
    if (end == 0x7fffffff)
        end = length;
    if (end == UINT32_MAX) {
        if (start != UINT32_MAX)
            return txn_status(call, paramErr);
        start = selected_start;
        end = selected_end;
    }
    else if (start > length)
        start = end = length;
    else
        end = MIN(end, length);
    start = MIN(start, end);

    _Alignas(32) Byte range[32] = {0};
    uint32_t override_selection = 0;
    if (start != selected_start || end != selected_end) {
        txn_store(range + 14, start);
        txn_store(range + 18, end);
        txn_store(range + 24, 0x80000000);
        override_selection = US_TO_SYN68K(range);
    }

    _Alignas(32) Byte stream[32] = {0};
    if (inserting) {
        txn_store(stream + 4, source);
        txn_store(stream + 8, unicode ? size / 2 : size);
        stream[0xc] = 1;
    }
    stream[0x1d] = stream[0x1e] = 1;
    const uint32_t replace[] = {document, override_selection, US_TO_SYN68K(stream)};
    return cfp_private(ref, 0x19108, replace, NELEM(replace), &status) &&
           txn_finish_set_data(call, ref, document, status);
}

static int txn_public_scrap_has_data(const ROMlib_carbon_reference* ref, uint32_t scrap, uint32_t method, int* found)
{
    const unsigned formats_offset = 0x6288;
    uint32_t args[] = {scrap, T('T', 'E', 'X', 'T')}, status;
    if (!cfp_callback(method, args, NELEM(args), &status))
        return 0;

    const Byte* formats = (const Byte*)ref->toc + formats_offset;
    const int count = (int16_t)txn_short(formats);
    if (count > (int)((ref->toc_size - formats_offset - 2) / 4))
        return cfp_failure("Invalid Textension scrap format table");
    for (int i = 0; status && i < count; ++i) {
        const uint32_t record = txn_word(formats + 2 + 4 * i);
        if (!cfp_callback(txn_field(txn_field(record, 0), 0xc4), &record, 1, &args[1]))
            return 0;
        if (!args[1])
            continue;
        if (args[1] == T('m', 'o', 'o', 'v'))
            args[1] = T('M', 'o', 'o', 'V');
        if (!cfp_callback(method, args, NELEM(args), &status))
            return 0;
        if (status != noTypeErr)
            break;
    }
    *found = status == noErr;
    return 1;
}

static int txn_textension_scrap(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                                const ROMlib_carbon_reference* ref)
{
    uint32_t scrap;
    if (!cfp_private(ref, 0x2b138, NULL, 0, &scrap))
        return 0;
    Byte* record = txn_memory(scrap, 0, 0x2c, 1);
    if (!record)
        return txn_status(call, operation == txn_scrap_pastable ? 0 : memFullErr);
    const uint32_t methods = txn_word(record);
    if (operation != txn_scrap_pastable) {
        _Alignas(4) uint32_t scratch = 0;
        const uint32_t args[] = {scrap, US_TO_SYN68K(&scratch)};
        const unsigned selector = operation == txn_scrap_to_public ? 0x1c : 0x2c;
        uint32_t result;
        if (!cfp_callback(txn_field(methods, selector), args, NELEM(args), &result))
            return 0;
        if (!result)
            result = txn_word((const Byte*)ref->toc + 0x3d2c);
        return txn_status(call, result);
    }

    const PScrapStuff private_info = InfoScrap();
    if (!private_info)
        return txn_status(call, 0);
    const int16_t generation = txn_short(&private_info->scrapCount);
    if (generation <= (int16_t)txn_short(record + 0x24) && generation == (int16_t)txn_short(record + 0x26))
        return txn_status(call, txn_word(record + 0xa) != 0);

    const PScrapStuff public_info = InfoScrap();
    if (!public_info)
        return txn_status(call, 0);
    if (txn_short(&public_info->scrapCount) != txn_short(record + 0x2a)) {
        memcpy(record + 0x2a, &public_info->scrapCount, sizeof public_info->scrapCount);
        int found;
        if (!txn_public_scrap_has_data(ref, scrap, txn_field(methods, 0x38), &found))
            return 0;
        record[0x28] = found;
    }
    return txn_status(call, record[0x28]);
}

static int txn_type_attribute_range(const ROMlib_carbon_reference* ref, uint32_t document, int unicode, uint32_t start,
                                    uint32_t end, uint32_t* flags, Byte range[16])
{
    const uint32_t editor = txn_field(document, 2);
    if (start != UINT32_MAX) {
        if (end == INT32_MAX)
            end = txn_field(txn_field(editor, 0xc), 6);
        if (!unicode && txn_field(document, 0xaa) == 2 && (start || end)) {
            _Alignas(8) uint32_t utf16[] = {CL(start * 2), CL(end * 2)};
            _Alignas(8) uint32_t legacy[] = {CL(start), CL(end)};
            const uint32_t map[] = {txn_word((const Byte*)ref->toc - 0x350), document, US_TO_SYN68K(utf16),
                                    US_TO_SYN68K(legacy)};
            if (!cfp_private(ref, 0x585a8, map, NELEM(map), NULL))
                return 0;
            start = txn_word(legacy);
            end = txn_word(legacy + 1);
        }
        *flags = 0x80000000;
    }
    else if (unicode) {
        const Byte* selection = txn_selection_bytes(editor);
        if (!selection)
            return 0;
        start = txn_word(selection + 6);
        end = txn_word(selection);
    }
    txn_store(range + 6, start);
    txn_store(range + 10, end);
    return 1;
}

static int txn_apply_type_attribute(const ROMlib_carbon_reference* ref, uint32_t document, const Byte* record,
                                    Byte range[16], int unicode, uint32_t* flags, uint32_t* status)
{
    enum {
        font_name = T('f', 'f', 'n', 't'),
        font_id = T('f', 'o', 'n', 't'),
        font_size = T('s', 'i', 'z', 'e'),
        font_face = T('f', 'a', 'c', 'e'),
        font_color = T('k', 'l', 'o', 'r'),
    };

    uint32_t tag = txn_word(record), value = txn_word(record + 8), attribute_flags = *flags;
    if (tag == font_name && txn_word(record + 4) >= 5) {
        const Byte* name = txn_memory(value, 0, 1, 0);
        _Alignas(2) uint16_t font = 0;
        if (!name || !txn_memory(value, 0, *name + 1u, 0)) {
            *status = paramErr;
            return 1;
        }
        const uint32_t find[] = {value, US_TO_SYN68K(&font)};
        if (!cfp_private(ref, 0x6b688, find, NELEM(find), NULL))
            return 0;
        tag = font_id;
        value = CW(font);
    }

    _Alignas(32) Byte attribute[32] = {0};
    switch (tag) {
    case font_id:
        txn_store(range, value << 16);
        break;
    case font_size:
        if (value == 0x80000000 || value == 1) {
            *flags |= 2;
            attribute_flags = *flags;
            value = value == 1 ? 1 : UINT16_MAX;
        }
        else
            value = (value + 0x8000) >> 16;
        txn_store(range, value << 16);
        break;
    case font_face:
        attribute_flags |= 4;
        txn_store(range, value);
        break;
    case font_color: {
        const void* color = txn_memory(value, 0, 6, 0);
        if (!color) {
            *status = paramErr;
            return 1;
        }
        attribute_flags |= 0x40;
        memcpy(range, color, 6);
        break;
    }
    default:
        if (!unicode) {
            *status = paramErr;
            return 1;
        }
        txn_store(range, value);
        break;
    }
    const uint32_t descriptor = US_TO_SYN68K(attribute);
    const uint32_t build[] = {descriptor, tag, US_TO_SYN68K(range), 14, attribute_flags};
    const uint32_t apply[] = {document, 0x10, descriptor, 0, 0};
    return cfp_private(ref, 0x371c8, build, NELEM(build), NULL) &&
           cfp_private(ref, 0x1e0e0, apply, NELEM(apply), status);
}

static int txn_update_type_attributes(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], count = call->words[1], records = call->words[2];
    const uint32_t editor = txn_field(document, 2);
    if (!editor || !count || !records || count > UINT32_MAX / 12)
        return txn_status(call, paramErr);
    if (txn_byte(document, 0xa0))
        return txn_status(call, -5250);
    const Byte* attributes = txn_memory(records, 0, count * 12, 0);
    if (!attributes)
        return txn_status(call, paramErr);

    uint32_t flags = 0, status = 0;
    const int unicode = txn_uses_unicode_offsets(ref);
    _Alignas(16) Byte range[16] = {0};
    if (!txn_type_attribute_range(ref, document, unicode, call->words[3], call->words[4], &flags, range))
        return 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!txn_apply_type_attribute(ref, document, attributes + i * 12, range, unicode, &flags, &status))
            return 0;
    }
    void* editor_error = txn_memory(txn_field(document, 2), 0x42, 4, 1);
    if (!editor_error)
        return 0;
    if (!status)
        status = txn_word(editor_error);
    txn_store(editor_error, 0);
    return txn_status(call, status ? status : txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_continuous_attributes(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], editor = txn_field(document, 2), count = call->words[2];
    void* continuity = txn_memory(call->words[1], 0, 4, 1);
    if (!editor || !continuity || count > UINT32_MAX / 12 || (count && !txn_memory(call->words[3], 0, count * 12, 1)))
        return txn_status(call, paramErr);

    uint32_t style;
    if (!cfp_private(ref, 0x2dc98, &editor, 1, &style))
        return 0;
    txn_store(continuity, 0);
    if (!style)
        return txn_status(call, noErr);
    uint32_t text;
    if (!cfp_callback(txn_field(txn_field(style, 0), 0x5c), &style, 1, &text))
        return 0;
    if (!text)
        return txn_status(call, noErr);
    const Byte* selection = txn_selection_bytes(editor);
    if (!selection)
        return txn_status(call, paramErr);
    const uint32_t query[] = {
        style, count, call->words[3], call->words[1], txn_word(selection), txn_word(selection + 6), document};
    return cfp_private(ref, 0x16c08, query, NELEM(query), NULL) && txn_status(call, noErr);
}

static int txn_insert_file_data(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], format = call->words[2], bytes = call->words[3];
    const uint32_t editor = txn_field(document, 2);
    const Byte* read_only = txn_memory(document, 0xa0, 1, 0);
    const uint32_t first = call->words[4], last = call->words[5];
    if (!editor || !read_only || first > last ||
        (format != T('t', 'x', 't', 'n') && format != T('T', 'E', 'X', 'T') && format != T('u', 't', 'x', 't') &&
         format != T('P', 'I', 'C', 'T') && format != T('M', 'o', 'o', 'V') && format != T('s', 'f', 'i', 'l') &&
         format != T('A', 'I', 'F', 'F')))
        return txn_status(call, paramErr);
    if (*read_only == 1)
        return txn_status(call, -0x1482);
    const int16_t file = call->words[1];
    const int unicode = txn_uses_unicode_offsets(ref);
    const uint32_t length = txn_field(txn_field(editor, 0xc), 6);
    uint32_t available;
    if (!cfp_private(ref, 0x6b820, NULL, 0, &available))
        return 0;
    const uint32_t budget = (available / 5 * 3) / 2;
    if ((uint64_t)length * (unicode ? 2 : 1) + bytes >= budget)
        return txn_status(call, memFullErr);

    _Alignas(128) FSSpec spec = {0};
    _Alignas(256) Str255 filename = {0};
    _Alignas(64) FCBPBRec block = {0};
    block.ioRefNum = CW(file);
    txn_store((Byte*)&block + offsetof(FCBPBRec, ioNamePtr), US_TO_SYN68K(filename));
    const uint32_t parameter_block = US_TO_SYN68K(&block);
    uint32_t status;
    if (!cfp_private(ref, 0x6b6d0, &parameter_block, 1, &status))
        return 0;
    if (status)
        return txn_status(call, status);
    if (filename[0] >= sizeof spec.name)
        return txn_status(call, bdNamErr);
    memcpy(spec.name, filename, filename[0] + 1);
    spec.vRefNum = block.ioFCBVRefNum;
    spec.parID = block.ioFCBParID;

    _Alignas(16) Byte range[12] = {0};
    txn_store(range, first);
    txn_store(range + 6, last);
    range[10] = 1;
    if (!unicode && txn_field(document, 0xaa) == 2 && first != UINT32_MAX && (first || last)) {
        _Alignas(8) uint32_t source[] = {CL(first), CL(last)}, result[2];
        const uint32_t translate[] = {txn_word((const Byte*)ref->toc - 0x350), document, US_TO_SYN68K(source),
                                      US_TO_SYN68K(result)};
        if (!cfp_private(ref, 0x585a8, translate, NELEM(translate), NULL))
            return 0;
        txn_store(range, txn_word(result));
        txn_store(range + 6, txn_word(result + 1));
    }
    if (txn_word(range + 6) == UINT32_MAX) {
        if (txn_word(range) != UINT32_MAX)
            return txn_status(call, paramErr);
    }
    else {
        txn_store(range, MIN(txn_word(range), length));
        txn_store(range + 6, MIN(txn_word(range + 6), length));
    }
    const Byte* selection = txn_selection_bytes(editor);
    if (!selection)
        return txn_status(call, paramErr);
    if (first == UINT32_MAX)
        memcpy(range, selection, sizeof range);
    const int empty = length == 0;
    if (!empty && (memcmp(range, selection, 5) || memcmp(range + 6, selection + 6, 5))) {
        const uint32_t select[] = {editor, US_TO_SYN68K(range), 1, 1};
        if (!cfp_callback(txn_field(txn_field(editor, 0), 0x2c), select, NELEM(select), NULL))
            return 0;
    }
    const uint32_t insert[] = {document,
                               US_TO_SYN68K(&spec),
                               file,
                               format,
                               txn_word(range),
                               txn_word(range + 4),
                               txn_word(range + 8),
                               0,
                               bytes,
                               empty};
    if (!cfp_private(ref, 0x1e52c, insert, NELEM(insert), &status))
        return 0;
    const uint32_t reset[] = {document, 1, 1};
    return cfp_private(ref, 0x1de58, reset, NELEM(reset), NULL) && txn_status(call, status);
}

enum {
    txn_character = T('c', 'h', 'a', ' '),
    txn_line = T('c', 'l', 'i', 'n'),
    txn_paragraph = T('c', 'p', 'a', 'r'),
    txn_word_element = T('c', 'w', 'o', 'r'),
    txn_style = T('t', 's', 't', 'y')
};

static int txn_element_range(const ROMlib_carbon_reference* ref, uint32_t editor, uint32_t type, uint32_t position,
                             uint32_t* first, uint32_t* last, uint32_t* found)
{
    _Alignas(16) Byte range[12] = {0};
    const uint32_t pointer = US_TO_SYN68K(range);
    *found = 1;
    if (type == txn_style) {
        const uint32_t lookup[] = {editor, position, pointer};
        if (!cfp_private(ref, 0x24614, lookup, NELEM(lookup), found))
            return 0;
        *first = position;
        *last = position + txn_word(range);
        return *last >= position;
    }
    if (type == txn_paragraph) {
        const uint32_t lookup[] = {editor, position, pointer};
        if (!cfp_private(ref, 0x2be64, lookup, NELEM(lookup), NULL))
            return 0;
    }
    else {
        const uint32_t lookup[] = {editor, position, 0, pointer};
        if (type == txn_line) {
            if (!cfp_private(ref, 0x2bdbc, lookup, NELEM(lookup), NULL))
                return 0;
        }
        else {
            const uint32_t get_word = txn_field(txn_field(editor, 0), 0x10);
            if (!get_word || !cfp_callback(get_word, lookup, NELEM(lookup), NULL))
                return 0;
        }
    }
    *first = txn_word(range);
    *last = txn_word(range + 6);
    return 1;
}

static int txn_element_query(ROMlib_carbon_ppc_frame* call, int find, const ROMlib_carbon_reference* ref)
{
    const uint32_t editor = txn_field(call->words[0], 2), type = call->words[3];
    uint32_t start = call->words[1], end = call->words[2];
    const void* length = txn_memory(txn_field(editor, 0xc), 6, 4, 0);
    void* first = txn_memory(call->words[find ? 5 : 4], 0, 4, 1);
    void* last = find ? txn_memory(call->words[6], 0, 4, 1) : NULL;
    if (!length || !first || (find && !last))
        return txn_status(call, paramErr);
    const uint32_t limit = txn_word(length);
    if (!limit) {
        txn_store(first, 0);
        if (find)
            txn_store(last, 0);
        return txn_status(call, noErr);
    }
    const uint32_t index = find ? call->words[4] : 0;
    if (end < start || (int32_t)index < 0)
        return txn_status(call, paramErr);
    if (start == UINT32_MAX || end == UINT32_MAX) {
        const Byte* selection = txn_selection_bytes(editor);
        if (!selection)
            return txn_status(call, paramErr);
        start = txn_word(selection);
        end = txn_word(selection + 6);
    }
    if (!find)
        end = MIN(end, limit);
    if (start >= end) {
        txn_store(first, 0);
        if (find)
            txn_store(last, 0);
        return txn_status(call, noErr);
    }

    if (type == txn_character) {
        if (find) {
            if (index >= UINT32_MAX - start)
                return txn_status(call, paramErr);
            txn_store(first, start + index);
            txn_store(last, start + index + 1);
        }
        else
            txn_store(first, end - start);
        return txn_status(call, noErr);
    }
    if (!find && type == txn_line) {
        const uint32_t arguments[] = {txn_field(editor, 0x28), start, end};
        uint32_t lines;
        if (!cfp_private(ref, 0x4061c, arguments, NELEM(arguments), &lines))
            return 0;
        txn_store(first, lines);
        return txn_status(call, noErr);
    }
    if (type != txn_line && type != txn_word_element && type != txn_style && type != txn_paragraph) {
        if (find) {
            txn_store(first, 0);
            txn_store(last, 0);
        }
        return txn_status(call, noErr);
    }

    uint32_t count = 0, element_start, element_end;
    for (;;) {
        uint32_t found;
        if (!txn_element_range(ref, editor, type, start, &element_start, &element_end, &found))
            return 0;
        ++count;
        if (find && count > index)
            break;
        if (element_end <= element_start || (!find && (!found || (type == txn_paragraph && element_end >= end))))
            break;
        const uint32_t extent = element_end - element_start;
        if (extent > UINT32_MAX - start)
            return txn_status(call, paramErr);
        start += extent;
        if ((!find && start >= end) || start >= limit)
            break;
    }
    if (find) {
        txn_store(first, count > index ? element_start : 0);
        txn_store(last, count > index ? element_end : 0);
    }
    else
        txn_store(first, count);
    return txn_status(call, noErr);
}

static int txn_set_activation(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], active = (Byte)call->words[2];
    Byte* state = txn_memory(object, 0xa5, 1, 1);
    if (!state)
        return txn_status(call, paramErr);
    *state = active;
    const uint32_t update[] = {object, active};
    if (!cfp_private(ref, 0x20e74, update, NELEM(update), NULL))
        return 0;
    if (!active) {
        const uint32_t hide[] = {txn_field(txn_field(object, 2), 0x24), 0, 1};
        if (!cfp_private(ref, 0x47e00, hide, NELEM(hide), NULL))
            return 0;
    }
    return txn_status(call, noErr);
}

static int txn_invalidate_frame(uint32_t object, const ROMlib_carbon_reference* ref)
{
    const Byte* scrollbars = txn_memory(object, 6, 3, 0);
    if (!scrollbars || !scrollbars[0])
        return 1;
    const uint32_t editor = txn_field(object, 2);
    const uint32_t layout = txn_field(editor, 0x20);
    const Byte* drag = txn_memory(object, 0x9f, 1, 0);
    _Alignas(8) Rect bounds;
    const uint32_t bounds_arguments[] = {layout, US_TO_SYN68K(&bounds)};
    uint32_t port;
    if (!drag || !layout || !cfp_private(ref, 0x254d8, bounds_arguments, NELEM(bounds_arguments), NULL) ||
        !cfp_private(ref, 0x24420, &editor, 1, &port))
        return 0;
    if (scrollbars[2] || *drag)
        bounds.bottom = CW((uint16_t)CW(bounds.bottom) + 15);
    if (scrollbars[1] || *drag)
        bounds.right = CW((uint16_t)CW(bounds.right) + 15);
    uint32_t saved;
    const uint32_t get_port[] = {US_TO_SYN68K(&saved)};
    if (!cfp_private(ref, 0x6b4d8, get_port, NELEM(get_port), NULL))
        return 0;
    SetPort((GrafPtr)SYN68K_TO_US(port));
    const uint32_t invalidate[] = {US_TO_SYN68K(&bounds)};
    const int result = cfp_private(ref, 0x6b9a0, invalidate, NELEM(invalidate), NULL);
    SetPort((GrafPtr)SYN68K_TO_US(txn_word(&saved)));
    return result;
}

static int txn_background(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    enum { txn_background_rgb = 1, txn_read_only_text = -5250 };

    const uint32_t object = call->words[0];
    const Byte* disabled = txn_memory(object, 0xa0, 1, 0);
    if (!disabled)
        return txn_status(call, paramErr);
    if (*disabled)
        return txn_status(call, txn_read_only_text);
    const uint32_t layout = txn_field(txn_field(object, 2), 0x20);
    if (!layout)
        return txn_status(call, noErr);
    const Byte* background = txn_memory(call->words[1], 0, 4 + sizeof(RGBColor), 0);
    Byte* settings = txn_memory(layout, 0x1a, 4 + sizeof(RGBColor), 1);
    if (!background || txn_word(background) != txn_background_rgb || !settings)
        return txn_status(call, paramErr);
    memmove(settings + 4, background + 4, sizeof(RGBColor));
    txn_store(settings, txn_background_rgb);
    uint16_t dirty = CW(txn_short((const Byte*)ref->toc + 0x34a0) | 0x80);
    memcpy((Byte*)ref->toc + 0x34a0, &dirty, sizeof dirty);
    return txn_invalidate_frame(object, ref) && txn_status(call, noErr);
}

static int txn_draw_document(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], world = call->words[1];
    if (!txn_field(object, 0xa))
        return 1;
    const uint32_t editor = txn_field(object, 2);
    const uint32_t draw = txn_field(txn_field(editor, 0), 0x1c);
    const uint32_t layout = txn_field(editor, 0x20);
    _Alignas(8) Rect bounds;
    const uint32_t bounds_arguments[] = {layout, US_TO_SYN68K(&bounds)};
    if (!draw || !layout || !cfp_private(ref, 0x254d8, bounds_arguments, NELEM(bounds_arguments), NULL))
        return 0;
    const uint32_t drawing[] = {editor, US_TO_SYN68K(&bounds), world ? 0x10 : 4};
    if (world) {
        uint32_t saved;
        const uint32_t get_port[] = {US_TO_SYN68K(&saved)};
        const uint32_t set_world[] = {world, 0};
        if (!cfp_private(ref, 0x6b4d8, get_port, NELEM(get_port), NULL) ||
            !cfp_private(ref, 0x6b640, set_world, NELEM(set_world), NULL))
            return 0;
        const int result = cfp_callback(draw, drawing, NELEM(drawing), NULL);
        SetPort((GrafPtr)SYN68K_TO_US(txn_word(&saved)));
        return result;
    }
    _Alignas(8) RGBColor foreground;
    const uint32_t color[] = {US_TO_SYN68K(&foreground)};
    if (!cfp_private(ref, 0x6b658, color, NELEM(color), NULL))
        return 0;
    const int result = cfp_callback(draw, drawing, NELEM(drawing), NULL);
    const int restored = cfp_private(ref, 0x6b670, color, NELEM(color), NULL);
    return result && restored && cfp_private(ref, 0x1cb4c, &object, 1, NULL);
}

static int txn_draw_document_in_rectangle(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0];
    const uint32_t editor = txn_field(document, 2), layout = txn_field(editor, 0x20);
    const uint32_t lines = txn_field(layout, 0xa), methods = txn_field(lines, 0);
    void* x = txn_memory(lines, 0x28, 4, 1);
    void* y = txn_memory(lines, 0x24, 4, 1);
    const uint32_t save = txn_field(methods, 0xc), restore = txn_field(methods, 0x10);
    if (!x || !y || !save || !restore)
        return 1;

    _Alignas(8) Rect bounds;
    const uint32_t rectangle = US_TO_SYN68K(&bounds);
    if (call->words[1]) {
        const void* supplied = txn_memory(call->words[1], 0, sizeof bounds, 0);
        if (!supplied)
            return 1;
        memcpy(&bounds, supplied, sizeof bounds);
    }
    else {
        const uint32_t get_bounds[] = {layout, rectangle};
        if (!cfp_private(ref, 0x254d8, get_bounds, NELEM(get_bounds), NULL))
            return 0;
    }
    const uint32_t old_x = txn_word(x), old_y = txn_word(y);
    _Alignas(8) Byte line_state[8] = {0};
    const uint32_t state = US_TO_SYN68K(line_state);
    const uint32_t save_state[] = {lines, state, 1, 0};
    if (!cfp_callback(save, save_state, NELEM(save_state), NULL))
        return 0;
    const int16_t top = txn_short(&bounds.top), left = txn_short(&bounds.left);
    txn_store(x, left);
    txn_store(y, top);
    const uint32_t area[] = {document, top, left, (int16_t)txn_short(&bounds.bottom), (int16_t)txn_short(&bounds.right),
                             0};
    const uint32_t invalidate[] = {document, 1, 1};
    int drawn = cfp_private(ref, 0x20880, area, NELEM(area), NULL) &&
                cfp_private(ref, 0x1c85c, invalidate, NELEM(invalidate), NULL);
    const uint32_t current_editor = txn_field(document, 2);
    if (drawn) {
        const uint32_t draw[] = {current_editor, rectangle, 4};
        drawn = cfp_callback(txn_field(txn_field(current_editor, 0), 0x1c), draw, NELEM(draw), NULL);
    }
    const uint32_t current_lines = txn_field(txn_field(txn_field(document, 2), 0x20), 0xa);
    const uint32_t restore_state[] = {current_lines, state, 0, 0};
    const int restored =
        cfp_callback(txn_field(txn_field(current_lines, 0), 0x10), restore_state, NELEM(restore_state), NULL);
    txn_store(x, old_x);
    txn_store(y, old_y);
    const uint32_t resume[] = {lines, state, 0, 0};
    return cfp_callback(restore, resume, NELEM(resume), NULL) && restored && drawn;
}

static int txn_draw_scrollbar(uint32_t object, unsigned offset, const ROMlib_carbon_reference* ref)
{
    const uint32_t port = txn_field(object, offset);
    const uint32_t control = txn_field(object, offset + 4);
    if (!port || !control)
        return 0;
    uint32_t previous;
    const uint32_t get_port[] = {US_TO_SYN68K(&previous)};
    if (!cfp_private(ref, 0x6b4d8, get_port, NELEM(get_port), NULL))
        return 0;
    SetPort((GrafPtr)SYN68K_TO_US(port));
    const int result = cfp_private(ref, 0x6bb68, &control, 1, NULL);
    SetPort((GrafPtr)SYN68K_TO_US(txn_word(&previous)));
    return result;
}

static int txn_draw_unicode_box(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t text = call->words[0], count = call->words[1], rect = call->words[2], options = call->words[4];
    const Byte* bounds = txn_memory(rect, 0, sizeof(Rect), 0);
    const void* option_flags = options ? txn_memory(options, 0, 4, 0) : NULL;
    if (!bounds || !count || count > UINT32_MAX / 2 || !txn_memory(text, 0, count * 2, 0) ||
        txn_short(bounds) == txn_short(bounds + 4) || txn_short(bounds + 2) == txn_short(bounds + 6) ||
        (options && !option_flags))
        return txn_status(call, paramErr);

    uint32_t clip;
    if (!cfp_private(ref, 0x6b748, NULL, 0, &clip))
        return 0;
    if (!clip)
        return txn_status(call, memFullErr);
    const int clip_box = !(txn_word(option_flags) & 8);
    const int saved = !clip_box || cfp_private(ref, 0x6b760, &clip, 1, NULL);
    const int clipped = saved && (!clip_box || cfp_private(ref, 0x6b778, &rect, 1, NULL));
    _Alignas(64) Byte state[0x30] = {0};
    const uint32_t state_address = US_TO_SYN68K(state);
    const uint32_t setup[] = {state_address, call->words[3], rect};
    const uint32_t content[] = {state_address, text, count};
    const uint32_t styling[] = {state_address, options};
    const uint32_t drawing[] = {state_address, rect};
    const int initialized = clipped && cfp_private(ref, 0x4dfb0, setup, NELEM(setup), NULL);
    uint32_t status;
    const int drawn = initialized && cfp_private(ref, 0x4e8dc, content, NELEM(content), NULL) &&
                      (!options || cfp_private(ref, 0x4e068, styling, NELEM(styling), NULL)) &&
                      cfp_private(ref, 0x4e310, drawing, NELEM(drawing), &status);
    const int restored = !clip_box || !saved || cfp_private(ref, 0x6b790, &clip, 1, NULL);
    const int disposed = cfp_private(ref, 0x6b7a8, &clip, 1, NULL);
    const int cleaned = !initialized || cfp_private(ref, 0x4e014, &state_address, 1, NULL);
    return drawn && restored && disposed && cleaned && txn_status(call, status);
}

static int txn_draw_cf_box(const char* name, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t string = call->words[0];
    if (!string)
        return txn_status(call, paramErr);
    ROMlib_carbon_reference carbon;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &carbon))
        return 0;
    if (carbon.code_offset != 0x9b7a8 || carbon.code_size != 0xb8fb4 || !carbon.toc || carbon.toc_offset != 0x2f78 ||
        carbon.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(carbon.function, call);
    uint32_t count;
    if (!cfp_private(&carbon, 0xb6288, &string, 1, &count))
        return 0;
    if ((int32_t)count <= 0)
        return txn_status(call, string);
    if (count > INT32_MAX / 2)
        return txn_status(call, memFullErr);

    _Alignas(512) uint16_t local[256];
    uint32_t text = US_TO_SYN68K(local), allocation = 0;
    const uint32_t allocator = txn_field(txn_word((const Byte*)carbon.toc - 0x2de4), 0);
    if (count > NELEM(local)) {
        const uint32_t allocate[] = {allocator, count * 2, 0};
        if (!cfp_private(&carbon, 0xb65e8, allocate, NELEM(allocate), &allocation))
            return 0;
        if (!allocation)
            return txn_status(call, memFullErr);
        text = allocation;
    }
    const uint32_t copy[] = {string, 0, count, text};
    const uint32_t draw[] = {text, count, call->words[1], call->words[2], call->words[3]};
    uint32_t status;
    const int drawn = txn_memory(text, 0, count * 2, 1) && cfp_private(&carbon, 0xb62a0, copy, NELEM(copy), NULL) &&
                      cfp_private(&carbon, 0xb3fd8, draw, NELEM(draw), &status);
    const uint32_t release[] = {allocator, allocation};
    return (!allocation || cfp_private(&carbon, 0xb6600, release, NELEM(release), NULL)) && drawn &&
           txn_status(call, status);
}

static int txn_window_update(uint32_t object, const ROMlib_carbon_reference* ref)
{
    const uint32_t window = txn_field(object, 0xa);
    if (!window)
        return 1;
    const Rect* window_bounds = txn_memory(window, 0x10, sizeof *window_bounds, 0);
    const Byte* options = txn_memory(object, 6, 3, 0);
    const uint32_t editor = txn_field(object, 2), layout = txn_field(editor, 0x20);
    const uint32_t draw = txn_field(txn_field(editor, 0), 0x1c);
    if (!options || !window_bounds || !layout || !draw)
        return 0;
    SetPort((GrafPtr)SYN68K_TO_US(window));
    if (options[0] && !cfp_private(ref, 0x6bb38, &window, 1, NULL))
        return 0;
    _Alignas(8) Rect bounds;
    memcpy(&bounds, window_bounds, sizeof bounds);
    const uint32_t clip[] = {layout, US_TO_SYN68K(&bounds)};
    const uint32_t draw_arguments[] = {editor, window + 0x10, 4};
    int result = cfp_private(ref, 0x25908, clip, NELEM(clip), NULL) &&
                 cfp_callback(draw, draw_arguments, NELEM(draw_arguments), NULL) &&
                 cfp_private(ref, 0x20924, &object, 1, NULL) && cfp_private(ref, 0x6bb50, &window, 1, NULL);
    if (result && options[1])
        result = txn_draw_scrollbar(object, 0x20, ref);
    if (result && options[2])
        result = txn_draw_scrollbar(object, 0x40, ref);
    return (!options[0] || cfp_private(ref, 0x6bb80, &window, 1, NULL)) && result;
}

static int txn_frame_resize(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0];
    if (!object)
        return 1;
    const uint32_t editor = txn_field(object, 2), layout = txn_field(editor, 0x20);
    const uint32_t lines = txn_field(layout, 0xa);
    Rect* frame_rect = txn_memory(object, 0xe, sizeof *frame_rect, 1);
    const Byte* bars = txn_memory(object, 7, 2, 0);
    const Byte* drag = txn_memory(object, 0x9f, 1, 0);
    const uint32_t line_layout = txn_field(txn_field(lines, 0), 0x10);
    const uint32_t resize = txn_field(txn_field(editor, 0), 0x18);
    if (!frame_rect || !bars || !drag || !line_layout || !resize)
        return 0;

    _Alignas(8) Rect bounds;
    const uint32_t current_bounds[] = {layout, US_TO_SYN68K(&bounds)};
    if (!cfp_private(ref, 0x254d8, current_bounds, NELEM(current_bounds), NULL))
        return 0;
    bounds.right = CW((uint16_t)CW(bounds.left) + (int16_t)call->words[1]);
    bounds.bottom = CW((uint16_t)CW(bounds.top) + (int16_t)call->words[2]);
    memcpy(frame_rect, &bounds, sizeof bounds);
    if (bars[1] || *drag)
        bounds.bottom = CW((uint16_t)CW(bounds.bottom) - 15);
    if (bars[0] || *drag)
        bounds.right = CW((uint16_t)CW(bounds.right) - 15);
    if (!txn_replace_layout_bounds(layout, &bounds, ref))
        return 0;

    _Alignas(16) Byte change[16] = {0};
    txn_store(change + 2, INT32_MAX);
    _Alignas(8) const uint32_t width[] = {0, CL((int16_t)CW(bounds.right) - (int16_t)CW(bounds.left))};
    const uint32_t rewrap[] = {lines, US_TO_SYN68K(width), US_TO_SYN68K(change), call->words[3]};
    const uint32_t update[] = {editor, rewrap[2]};
    uint32_t effects;
    if (!cfp_callback(line_layout, rewrap, NELEM(rewrap), NULL) ||
        !cfp_callback(resize, update, NELEM(update), &effects))
        return 0;
    if ((effects & 0x28) && !txn_invalidate_frame(object, ref))
        return 0;
    if (!(effects & 0x10) || ((!txn_field(object, 0xa) || !bars[0]) && !bars[1]))
        return 1;
    const uint32_t horizontal[] = {object, 1, 1}, vertical[] = {object, 0, 1};
    return cfp_private(ref, 0x1c0e4, &object, 1, NULL) &&
           cfp_private(ref, 0x1c410, horizontal, NELEM(horizontal), NULL) &&
           cfp_private(ref, 0x1c410, vertical, NELEM(vertical), NULL);
}

static int txn_attach_document_window(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], window = call->words[1];
    const uint32_t layout = txn_field(txn_field(document, 2), 0x20);
    Byte* owner = txn_memory(document, 6, 1, 1);
    void* window_slot = txn_memory(document, 0xa, 4, 1);
    Byte* attached = txn_memory(layout, 0x3f, 1, 1);
    if (!owner || !window_slot || !attached)
        return txn_status(call, paramErr);
    _Alignas(4) uint32_t port;
    const uint32_t get_port = US_TO_SYN68K(&port);
    if (!cfp_private(ref, 0x6b4d8, &get_port, 1, NULL))
        return 0;
    int updated = 1;
    uint32_t result = noErr;
    if (window) {
        const uint32_t arguments[] = {document, window, (uint8_t)call->words[2]};
        updated = cfp_private(ref, 0x1cbec, arguments, NELEM(arguments), &result);
    }
    else {
        *owner = 0;
        txn_store(window_slot, 0);
        *attached = 0;
    }
    if (updated && !result)
        result = txn_field(txn_word((const Byte*)ref->toc - 0x374), 0);
    const uint32_t saved_port = txn_word(&port);
    return cfp_private(ref, 0x6b4f0, &saved_port, 1, NULL) && updated && txn_status(call, result);
}

static int txn_window_grow(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], event = call->words[1];
    if (!object || !event)
        return 1;
    const uint32_t window = txn_field(object, 0xa);
    if (!window || !txn_memory(event, 0xa, 4, 0))
        return 1;
    _Alignas(8) const Rect limits = {CWC(64), CWC(64), CWC(32000), CWC(32000)};
    const uint32_t track[] = {window, txn_field(event, 0xa), US_TO_SYN68K(&limits)};
    uint32_t size;
    if (!cfp_private(ref, 0x6bb98, track, NELEM(track), &size))
        return 0;
    if (!size)
        return 1;

    const uint32_t scroll[] = {object, 0, 1};
    const uint32_t resize[] = {window, (int16_t)size, (int16_t)(size >> 16), 1};
    return cfp_private(ref, 0x20cf8, scroll, NELEM(scroll), NULL) &&
           cfp_private(ref, 0x6bbb0, resize, NELEM(resize), NULL) &&
           cfp_private(ref, 0x20cf8, scroll, NELEM(scroll), NULL) && cfp_private(ref, 0x1c8e8, &object, 1, NULL);
}

static int txn_click_document(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], event = call->words[1];
    const void* event_point = txn_memory(event, 0xa, 4, 0);
    const uint32_t editor = txn_field(document, 2), layout = txn_field(editor, 0x20);
    const Byte* options = txn_memory(document, 7, 2, 0);
    const Byte* drag_enabled = txn_memory(document, 0xa6, 1, 0);
    if (!event_point || !layout || !options || !drag_enabled)
        return 1;

    _Alignas(4) uint32_t point = CL(txn_word(event_point)), old_port;
    _Alignas(8) Rect bounds;
    const uint32_t position = US_TO_SYN68K(&point), port_output = US_TO_SYN68K(&old_port);
    const uint32_t view[] = {layout, US_TO_SYN68K(&bounds)};
    if (!cfp_private(ref, 0x6baf0, &position, 1, NULL) || !cfp_private(ref, 0x254d8, view, NELEM(view), NULL) ||
        !cfp_private(ref, 0x6b4d8, &port_output, 1, NULL))
        return 0;
    const uint32_t window = txn_field(document, 0xa);
    if (window && !cfp_private(ref, 0x6b4f0, &window, 1, NULL))
        return 0;
    const uint32_t hit[] = {txn_word(&point), view[1]};
    uint32_t inside;
    int handled = cfp_private(ref, 0x6bad8, hit, NELEM(hit), &inside);
    if (handled && inside) {
        if (*drag_enabled) {
            Byte changed = 0;
            const uint32_t drag[] = {document + 0xbc, event, US_TO_SYN68K(&changed)};
            uint32_t consumed;
            handled = cfp_private(ref, 0x33334, drag, NELEM(drag), &consumed);
            if (handled && changed) {
                void* modifications = txn_memory(document, 0xf6, 12, 1);
                const uint32_t redraw[] = {document, 0, 1};
                handled = modifications && cfp_private(ref, 0x1ddbc, &document, 1, NULL);
                if (handled) {
                    txn_store(modifications, txn_word(modifications) + 1);
                    txn_store((Byte*)modifications + 8, txn_word((Byte*)modifications + 8) + 1);
                    handled = cfp_private(ref, 0x1c85c, redraw, NELEM(redraw), NULL);
                }
            }
            /* A consuming drag owns the port at this point. */
            if (handled && consumed)
                return 1;
        }
        if (handled) {
            _Alignas(2) Byte selection[2] = {0};
            const uint32_t click[] = {editor, event, US_TO_SYN68K(selection),
                                      US_TO_SYN68K((const Byte*)ref->toc + 0x1318), document};
            handled = cfp_callback(txn_field(txn_field(editor, 0), 0x38), click, NELEM(click), NULL);
        }
    }
    else if (handled) {
        for (unsigned index = 0; index < 2; ++index) {
            if (!options[index])
                continue;
            const uint32_t control = txn_field(document, index ? 0x44 : 0x24);
            const uint32_t test[] = {control, position};
            uint32_t part;
            handled = cfp_private(ref, 0x6bb08, test, NELEM(test), &part);
            if (!handled || part) {
                const uint32_t track[] = {document + (index ? 0x36 : 0x16), hit[0],
                                          txn_word((const Byte*)ref->toc - 0x274)};
                handled = handled && cfp_private(ref, 0x2230c, track, NELEM(track), NULL);
                break;
            }
        }
    }
    const uint32_t saved = txn_word(&old_port);
    return cfp_private(ref, 0x6b4f0, &saved, 1, NULL) && handled;
}

static int txn_drag(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                    const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0];
    if (!txn_memory(document, 0xbc, 4, 0))
        return txn_status(call, paramErr);
    const uint32_t context = document + 0xbc, methods = txn_field(context, 0);
    uint32_t status;
    if (operation == txn_drag_tracker) {
        const uint32_t arguments[] = {context, (uint16_t)call->words[2], call->words[4], (uint8_t)call->words[5]};
        return cfp_callback(txn_field(methods, 4), arguments, NELEM(arguments), &status) && txn_status(call, status);
    }

    const uint32_t window = call->words[2], drag = call->words[3];
    const uint32_t arguments[] = {context, drag, (uint8_t)call->words[4]};
    if (!cfp_callback(txn_field(methods, 8), arguments, NELEM(arguments), &status))
        return 0;
    if (status)
        return txn_status(call, status);

    _Alignas(4) uint32_t attributes = 0;
    const uint32_t get_attributes[] = {drag, US_TO_SYN68K(&attributes)};
    if (!cfp_private(ref, 0x6bda8, get_attributes, NELEM(get_attributes), NULL))
        return 0;
    if (txn_word(&attributes) & 4) {
        uint32_t front;
        if (!cfp_private(ref, 0x6b538, NULL, 0, &front) ||
            (front != window && !cfp_private(ref, 0x6bdc0, &window, 1, NULL)))
            return 0;
    }
    return txn_status(call, noErr);
}

static int txn_idle_document(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference carbon;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &carbon))
        return 0;
    if (carbon.code_offset != 0x2eddc || carbon.code_size != 0xb8fb4 || !carbon.toc || carbon.toc_offset != 0x2f78 ||
        carbon.toc_size != 0x10f0c)
        return ROMlib_carbon_invoke_frame(carbon.function, call);

    const uint32_t installed_vector = txn_word((const Byte*)carbon.toc - 0x1114);
    if (!txn_field(installed_vector, 0))
        return ROMlib_carbon_invoke_frame(carbon.function, call);
    _Alignas(16) EventRecord event;
    const uint32_t event_address = US_TO_SYN68K(&event);
    const uint32_t fetch[] = {0, event_address};
    return cfp_private(&carbon, 0xb6540, fetch, NELEM(fetch), NULL) &&
           cfp_private(&carbon, 0xb5250, &event_address, 1, NULL) &&
           cfp_callback(txn_field(installed_vector, 0), call->words, 1, NULL);
}

static int txn_editor_command_key(uint8_t character)
{
    return (character >= 28 && character <= 31) || character == 1 || character == 4 || character == 8 ||
           character == 11 || character == 12;
}

static int txn_key_down(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], event_address = call->words[1];
    const Byte* event = txn_memory(event_address, 0, sizeof(EventRecord), 0);
    if (!txn_field(document, 2) || !event)
        return 1;
    const uint32_t arguments[] = {document, event_address};
    if (!txn_uses_unicode_offsets(ref))
        return cfp_private(ref, 0x203d4, arguments, NELEM(arguments), NULL);

    const uint8_t character = txn_word(event + 2);
    uint32_t consumed;
    if (!cfp_private(ref, 0x6b208, arguments, NELEM(arguments), &consumed))
        return 0;
    if (consumed)
        return 1;

    /* The editor fills a packed keyboard translation record through its virtual method. */
    _Alignas(128) Byte key[0x4c] = {0};
    const uint32_t editor = txn_field(document, 2), translation = US_TO_SYN68K(key);
    const uint32_t translate[] = {editor, event_address, 0, translation};
    if (!cfp_callback(txn_field(txn_field(editor, 0), 0x20), translate, NELEM(translate), NULL))
        return 0;
    const uint8_t current_character = txn_word(event + 2);
    if (current_character == 9 || ((character >= 32 || character == 13) && !(txn_short(key + 0x44) & 0x800)))
        key[0x12] = character;
    uint32_t ascii_path;
    if (!cfp_private(ref, 0x14220, &event_address, 1, &ascii_path))
        return 0;
    const int editing_key = txn_editor_command_key(character) || character == 127;
    if (!ascii_path || (!editing_key && !(txn_short(event + 14) & 0x100))) {
        const uint16_t code = CW((uint16_t)(int16_t)(int8_t)character);
        memcpy(key + 0x12, &code, sizeof code);
    }
    const uint32_t edit[] = {document, translation, event_address};
    return cfp_private(ref, 0x2015c, edit, NELEM(edit), NULL);
}

static int txn_copy_unicode_key(Byte* key, const Byte* text, uint32_t size, uint8_t character)
{
    if (character == 127) {
        if (!text || size < 3)
            return 0;
        key[0x12] = text[2];
        return 1;
    }
    if (character < 32 && character != 9 && character != 13) {
        key[0x12] = character;
        return 1;
    }
    if (size && !text)
        return 0;
    const unsigned capacity = 0x32;
    const int16_t units = txn_short(key + 0x10);
    if (units < 2) {
        const uint16_t count = CW(size / 2);
        memcpy(key + 0x10, &count, sizeof count);
    }
    else {
        const unsigned previous = 2u * units;
        if (previous > capacity)
            return 0;
        const uint16_t next = size >= 4 ? txn_short(text + 2) : 0;
        if ((next == 13 || next == 10) && size <= previous) {
            const unsigned tail = previous - size;
            memmove(key + 0x10 + size, key + 0x13, tail);
            memcpy(key + 0x12, text, 2);
            memcpy(key + 0x12 + tail, text + 4, size - 4);
            memset(key + 0x0e + previous, 0, 2);
            return 1;
        }
        memmove(key + 0x14, key + 0x13, previous - 2);
    }
    if (size)
        memcpy(key + 0x12, text, size);
    return 1;
}

static int txn_unicode_key_down(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], editor = txn_field(document, 2);
    const uint32_t supplied_event = call->words[1], text_address = call->words[2], bytes = call->words[3];
    if (!editor || txn_byte(txn_word((const Byte*)ref->toc - 0x37c), 0) != 1)
        return txn_status(call, paramErr);

    _Alignas(16) Byte event[sizeof(EventRecord)] = {0};
    const uint32_t event_address = US_TO_SYN68K(event);
    if (supplied_event) {
        const void* source_event = txn_memory(supplied_event, 0, sizeof event, 0);
        if (!source_event)
            return txn_status(call, paramErr);
        memcpy(event, source_event, sizeof event);
    }
    else {
        const void* first = txn_memory(text_address, 0, 2, 0);
        if (!first || bytes < 2)
            return txn_status(call, paramErr);
        const uint32_t fetch[] = {0, event_address};
        if (!cfp_private(ref, 0x6b730, fetch, NELEM(fetch), NULL))
            return 0;
        const uint16_t key_down = CW(keyDown);
        memcpy(event, &key_down, sizeof key_down);
        txn_store(event + 2, txn_short(first));
    }

    const uint8_t character = txn_word(event + 2);
    uint32_t ascii;
    if (!cfp_private(ref, 0x14220, &event_address, 1, &ascii))
        return 0;
    const uint32_t key_event[] = {document, event_address};
    if (ascii && (txn_editor_command_key(character) || (txn_short(event + 14) & 0x100)))
        return cfp_private(ref, 0x6b1d8, key_event, NELEM(key_event), NULL) && txn_status(call, noErr);

    _Alignas(128) Byte key[0x4c] = {0};
    const uint32_t translation = US_TO_SYN68K(key);
    const uint32_t translate[] = {editor, event_address, 0, translation};
    if (!cfp_callback(txn_field(txn_field(editor, 0), 0x20), translate, NELEM(translate), NULL))
        return 0;
    const uint16_t flags = txn_short(key + 0x44);
    if ((flags & 0x800) && txn_byte(txn_word((const Byte*)ref->toc - 0x384), 0) && txn_has_provisional_edit(editor)) {
        if (bytes && !txn_memory(text_address, 0, bytes, 0))
            return txn_status(call, paramErr);
        _Alignas(32) Byte input[32] = {0};
        txn_store(input + 4, text_address);
        txn_store(input + 8, bytes / 2);
        input[0xc] = input[0x1d] = input[0x1e] = 1;
        const uint32_t start = txn_field(editor, 0x56);
        const uint32_t insert[] = {editor, start, start, US_TO_SYN68K(input), 0};
        return cfp_private(ref, 0x22820, &document, 1, NULL) &&
               cfp_private(ref, 0x49650, insert, NELEM(insert), &call->words[0]);
    }
    if (flags) {
        if (bytes > 0x32)
            return txn_status(call, noErr);
        const Byte* text = txn_memory(text_address, 0, bytes, 0);
        if (!txn_copy_unicode_key(key, text, bytes, character))
            return txn_status(call, paramErr);
        const uint16_t insert = CW(flags | 0x10);
        memcpy(key + 0x44, &insert, sizeof insert);
    }
    const uint32_t edit[] = {document, translation, event_address};
    return cfp_private(ref, 0x2015c, edit, NELEM(edit), NULL) && txn_status(call, noErr);
}

static int txn_revert_document(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0];
    const Byte* read_only = txn_memory(document, 0xa0, 1, 0);
    if (!read_only || !txn_field(document, 2))
        return txn_status(call, paramErr);
    if (*read_only)
        return txn_status(call, -0x1482);

    uint32_t status;
    if (!cfp_private(ref, 0x219f4, &document, 1, &status))
        return 0;
    if (status)
        return txn_status(call, status);
    const uint32_t reset[] = {document, 1, 1};
    return cfp_private(ref, 0x1cdf8, &document, 1, NULL) && cfp_private(ref, 0x1de58, reset, NELEM(reset), NULL) &&
           txn_status(call, txn_field(txn_word((const Byte*)ref->toc - 0x374), 0));
}

static int txn_save_document(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], forks = call->words[4];
    const uint32_t editor = txn_field(document, 2);
    void* formats = txn_memory(document, 0xaa, 8, 1);
    void* spec = txn_memory(document, 0x56, sizeof(FSSpec), 1);
    const void* supplied = forks ? txn_memory(forks, 0, sizeof(FSSpec), 0) : NULL;
    const Byte* provisional = txn_memory(document, 0xa3, 1, 0);
    void* change_count = txn_memory(document, 0xf6, 4, 1);
    if (!formats || !spec || (forks && !supplied) || !provisional || !editor || !change_count)
        return txn_status(call, paramErr);

    txn_store(formats, call->words[3]);
    const uint32_t resource_type = call->words[2];
    if (resource_type == T('s', 't', 'y', 'l') || resource_type == T('M', 'P', 'S', 'R'))
        txn_store((Byte*)formats + 4, resource_type);
    if (supplied)
        memmove(spec, supplied, sizeof(FSSpec));
    const int16_t data_fork = call->words[5], resource_fork = call->words[6];
    if (data_fork == -1)
        return txn_status(call, paramErr);
    if (*provisional && !cfp_private(ref, 0x4a370, &editor, 1, NULL))
        return 0;

    Byte* saving_flattened = (Byte*)ref->toc + 0x4120;
    *saving_flattened = txn_byte(txn_field(document, 2), 0x3f);
    const uint32_t save[] = {document, call->words[1], data_fork, resource_fork};
    uint32_t status;
    const int saved = cfp_private(ref, 0x21364, save, NELEM(save), &status);
    *saving_flattened = 0;
    if (!saved)
        return 0;
    if (!status) {
        if (!cfp_private(ref, 0x1ddbc, &document, 1, NULL))
            return 0;
        txn_store(change_count, 0);
        status = txn_word((const Byte*)ref->toc + 0x3d2c);
    }
    else if (status == UINT32_MAX)
        status = -0x55f4;
    return txn_status(call, status);
}

static int txn_control_justification(uint32_t editor, const ROMlib_carbon_reference* ref, uint32_t* value)
{
    const uint32_t paragraph = txn_field(editor, 0x30);
    const uint32_t lookup[] = {paragraph, 0, 0};
    uint32_t style;
    if (!cfp_callback(txn_field(txn_field(paragraph, 0), 0xc), lookup, NELEM(lookup), &style))
        return 0;
    if (style) {
        Byte justification = 0;
        const uint32_t get[] = {style, T('j', 'u', 's', 't'), US_TO_SYN68K(&justification)};
        if (!cfp_private(ref, 0x3684c, get, NELEM(get), NULL))
            return 0;
        *value = (int8_t)justification;
    }
    else
        *value = (int16_t)txn_short(ROMlib_carbon_try_guest_memory(0xbac, 2, 0));
    return 1;
}

static int txn_get_object_controls(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], count = call->words[1];
    const uint32_t tags = call->words[2], values = call->words[3];
    if (count > UINT32_MAX / 4 || !txn_memory(tags, 0, count ? count * 4 : 1, 0) ||
        !txn_memory(values, 0, count ? count * 4 : 1, 1))
        return txn_status(call, paramErr);
    const uint32_t editor = txn_field(document, 2);
    if (document && !editor)
        return txn_status(call, paramErr);

    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t tag = txn_field(tags, index * 4), output_address = values + index * 4;
        void* output = txn_memory(output_address, 0, 4, 1);
        uint32_t value = 0;
        switch (tag) {
        case T('i', 'o', 'p', 'v'):
            value = txn_byte(document, 0xa0);
            break;
        case T('d', 'r', 'a', 'g'):
            value = txn_byte(document, 0xa6);
            break;
        case T('r', 'f', 'c', 'n'):
            if (!document)
                return txn_status(call, paramErr);
            value = txn_field(document, 0x14c);
            break;
        case T('n', 'u', 'i', 'o'):
            value = txn_byte(editor, 0x3d);
            break;
        case T('a', 'u', 'i', 'n'):
            value = txn_byte(editor, 0x3e);
            break;
        case T('f', 'l', 'a', 't'):
            value = txn_byte(editor, 0x3f);
            break;
        case T('k', 'b', 's', 'y'):
            value = txn_byte(editor, 0x41);
            break;
        case T('d', 'c', 'r', 't'):
            value = editor ? txn_byte(editor, 0x46) : 1;
            break;
        case T('d', 's', 'l', 'n'):
            value = editor ? txn_byte(editor, 0x47) : 1;
            break;
        case T('w', 'w', 'r', 's'):
            value = txn_byte(txn_field(editor, 0x28), 0x1e);
            break;
        case T('s', 'l', 's', 't'):
            value = !editor || (int8_t)txn_byte(txn_field(editor, 0x24), 0x1c) > 0;
            break;
        case T('l', 'n', 'd', 'r'):
            if (editor)
                value = (int8_t)txn_byte(txn_field(editor, 0x20), 0x2e);
            else if (!cfp_private(ref, 0x6b6a0, NULL, 0, &value))
                return 0;
            if (value == UINT32_MAX)
                value = 1;
            break;
        case T('j', 'u', 's', 't'):
            if (editor && !txn_control_justification(editor, ref, &value))
                return 0;
            break;
        case T('i', 'n', 's', 't'):
            if (editor && !cfp_callback(txn_field(txn_field(editor, 0), 0x54), &editor, 1, &value))
                return 0;
            break;
        case T('v', 'i', 's', 'b'):
            value = 1;
            if (editor && !cfp_private(ref, 0x8738, &editor, 1, &value))
                return 0;
            break;
        case T('t', 'a', 'b', 's'): {
            const uint32_t tab[] = {document, output_address};
            uint32_t status;
            if (!cfp_private(ref, 0x1832c, tab, NELEM(tab), &status))
                return 0;
            if (status)
                return txn_status(call, status);
            continue;
        }
        case T('m', 'a', 'r', 'g'):
        case T('c', 'b', 'c', 'b'): {
            const uint32_t destination = txn_word(output);
            const uint32_t copy[] = {tag == T('m', 'a', 'r', 'g') ? document : editor, destination};
            if (!copy[0] && tag == T('c', 'b', 'c', 'b'))
                return txn_status(call, paramErr);
            if (!destination && tag == T('c', 'b', 'c', 'b'))
                continue;
            if (!txn_memory(destination, 0, 8, 1))
                return txn_status(call, paramErr);
            if (!cfp_private(ref, tag == T('m', 'a', 'r', 'g') ? 0x183f0 : 0x2e5bc, copy, NELEM(copy), NULL))
                return 0;
            continue;
        }
        default: {
            const uint32_t query[] = {document, tag, output_address};
            uint32_t status;
            if (!cfp_private(ref, 0x1847c, query, NELEM(query), &status))
                return 0;
            if (status)
                return txn_status(call, status);
            continue;
        }
        }
        txn_store(output, value);
    }
    return txn_status(call, txn_field(txn_word((const Byte*)ref->toc - 0x374), 0));
}

static int txn_write_control_byte(uint32_t object, unsigned offset, Byte value)
{
    Byte* field = txn_memory(object, offset, 1, 1);
    if (!field)
        return 0;
    *field = value;
    return 1;
}

static int txn_reset_object_controls(uint32_t document, const ROMlib_carbon_reference* ref, uint32_t* status)
{
    uint32_t direction;
    if (!cfp_private(ref, 0x6b6a0, NULL, 0, &direction))
        return 0;
    const uint32_t direct[] = {document, direction}, defaults[] = {document, 0};
    if (!cfp_private(ref, 0x17660, direct, NELEM(direct), NULL) ||
        !cfp_private(ref, 0x17724, defaults, NELEM(defaults), status) ||
        !cfp_private(ref, 0x1efc4, defaults, NELEM(defaults), NULL))
        return 0;
    uint32_t editor = txn_field(document, 2);
    const uint32_t edit_defaults[] = {editor, 0};
    if (!txn_write_control_byte(txn_field(editor, 0x24), 0x1c, 1) ||
        !cfp_callback(txn_field(txn_field(editor, 0), 0x50), edit_defaults, NELEM(edit_defaults), NULL) ||
        !cfp_private(ref, 0x1d06c, defaults, NELEM(defaults), NULL))
        return 0;
    editor = txn_field(document, 2);
    const uint32_t keyboard[] = {editor, 0};
    if (!cfp_private(ref, 0x8a80, keyboard, NELEM(keyboard), NULL))
        return 0;
    editor = txn_field(document, 2);
    if (txn_byte(txn_field(editor, 0x28), 0x1e) == 1 && !txn_write_control_byte(editor, 0x3e, 0))
        return 0;
    _Alignas(8) Byte margins[8];
    memcpy(margins, (const Byte*)ref->toc + 0x3d90, sizeof margins);
    const uint32_t margin[] = {document, US_TO_SYN68K(margins)};
    return cfp_private(ref, 0x1790c, defaults, NELEM(defaults), NULL) &&
           cfp_private(ref, 0x239b4, defaults, NELEM(defaults), NULL) &&
           cfp_private(ref, 0x23ab0, defaults, NELEM(defaults), NULL) &&
           cfp_private(ref, 0x17a30, margin, NELEM(margin), NULL) &&
           txn_write_control_byte(txn_field(document, 2), 0x46, 1) &&
           txn_write_control_byte(txn_field(document, 2), 0x47, 1);
}

static int txn_set_object_controls(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], count = call->words[2];
    const uint32_t tags = call->words[3], values = call->words[4];
    if (!txn_field(document, 2) || count > UINT32_MAX / 4 || !txn_memory(tags, 0, count ? count * 4 : 1, 0) ||
        !txn_memory(values, 0, count ? count * 4 : 1, 0))
        return txn_status(call, paramErr);
    const int read_only = txn_byte(document, 0xa0) != 0;
    const void* last_error = (const Byte*)ref->toc + 0x3d2c;
    if ((uint8_t)call->words[1]) {
        if (read_only)
            return txn_status(call, -0x1482);
        uint32_t status;
        return txn_reset_object_controls(document, ref, &status) &&
               txn_status(call, status ? status : txn_word(last_error));
    }

    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t tag = txn_field(tags, index * 4), value = txn_field(values, index * 4);
        const int reset = value == UINT32_MAX;
        const uint32_t editor = txn_field(document, 2);
        uint32_t status = 0;
        uint32_t worker = 0;
        uint32_t args[] = {document, reset ? 0 : (uint8_t)value, 0};
        unsigned argc = 2;
        switch (tag) {
        case T('l', 'n', 'd', 'r'):
        case T('j', 'u', 's', 't'):
            if (read_only)
                return txn_status(call, -0x1482);
            worker = tag == T('l', 'n', 'd', 'r') ? 0x17660 : 0x17724;
            args[1] = reset ? 0 : value;
            if (worker == 0x17660 && reset && !cfp_private(ref, 0x6b6a0, NULL, 0, &args[1]))
                return 0;
            break;
        case T('n', 'u', 'i', 'o'):
            if (!cfp_private(ref, 0x23690, args, argc, NULL))
                return 0;
            continue;
        case T('i', 'o', 'p', 'v'):
            if (!cfp_private(ref, 0x1efc4, args, argc, NULL))
                return 0;
            continue;
        case T('s', 'l', 's', 't'):
            if (!txn_write_control_byte(txn_field(editor, 0x24), 0x1c, reset || (uint8_t)value ? 1 : 255))
                return 0;
            continue;
        case T('i', 'n', 's', 't'):
            args[0] = editor;
            if (!cfp_callback(txn_field(txn_field(editor, 0), 0x50), args, argc, &status))
                return 0;
            break;
        case T('w', 'w', 'r', 's'):
            if (!cfp_private(ref, 0x1d06c, args, argc, NULL))
                return 0;
            continue;
        case T('k', 'b', 's', 'y'):
            args[0] = editor;
            if (!cfp_private(ref, 0x8a80, args, argc, NULL))
                return 0;
            continue;
        case T('a', 'u', 'i', 'n'):
            if (txn_byte(txn_field(editor, 0x28), 0x1e) != 1)
                return txn_status(call, -0x55f6);
            if (!txn_write_control_byte(editor, 0x3e, 0))
                return 0;
            status = txn_word(last_error);
            break;
        case T('t', 'a', 'b', 's'):
            args[1] = reset ? 0 : values + index * 4;
            worker = 0x1790c;
            break;
        case T('r', 'f', 'c', 'n'): {
            void* refcon = txn_memory(document, 0x14c, 4, 1);
            if (!refcon)
                return 0;
            txn_store(refcon, value);
            continue;
        }
        case T('m', 'a', 'r', 'g'):
            args[1] = reset ? 0 : value;
            if ((args[1] && !txn_memory(args[1], 0, 8, 0)) || !cfp_private(ref, 0x17a30, args, argc, NULL))
                return 0;
            continue;
        case T('v', 'i', 's', 'b'):
            args[0] = editor;
            args[1] = reset ? 1 : (uint8_t)value;
            if (!cfp_private(ref, 0x86f0, args, argc, NULL))
                return 0;
            continue;
        case T('f', 'l', 'a', 't'):
        case T('d', 'c', 'r', 't'):
        case T('d', 's', 'l', 'n'): {
            const unsigned offset = tag == T('f', 'l', 'a', 't') ? 0x3f : tag == T('d', 'c', 'r', 't') ? 0x46 : 0x47;
            if (!txn_write_control_byte(editor, offset, reset ? offset != 0x3f : (uint8_t)value))
                return 0;
            continue;
        }
        case T('d', 'r', 'a', 'g'):
            args[1] = args[1] == 0;
            if (!txn_write_control_byte(document, 0xa6, args[1]) || !cfp_private(ref, 0x23334, args, argc, NULL))
                return 0;
            continue;
        case T('c', 'b', 'c', 'b'):
            if (!txn_memory(value, 0, 8, 0))
                return txn_status(call, paramErr);
            args[1] = value;
            worker = 0x175a8;
            break;
        default:
            if (read_only)
                return txn_status(call, -0x1482);
            args[1] = tag;
            args[2] = value;
            argc = 3;
            worker = 0x17b2c;
            break;
        }
        if (worker && !cfp_private(ref, worker, args, argc, &status))
            return 0;
        if (status)
            return txn_status(call, status);
    }
    return txn_status(call, txn_word(last_error));
}

static int txn_print_document(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                              const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0];
    if (!txn_field(document, 2))
        return txn_status(call, paramErr);
    const uint32_t prepare[] = {document, 0, 0};
    if (!cfp_private(ref, 0x6b220, prepare, 2, NULL) || !cfp_private(ref, 0x6b370, prepare, NELEM(prepare), NULL))
        return 0;
    const uint32_t settings = document + 0xe8;
    uint32_t status;
    if (operation == txn_page_setup) {
        if (!cfp_private(ref, 0x4d650, &settings, 1, &status))
            return 0;
        const uint32_t reflow[] = {document, 1, UINT32_MAX};
        if (!status && !cfp_private(ref, 0x1cf4c, reflow, NELEM(reflow), NULL))
            return 0;
    }
    else {
        const uint32_t selection = txn_field(txn_field(document, 2), 0x24);
        Byte* printing = txn_memory(selection, 0x1c, 1, 1);
        if (!printing)
            return txn_status(call, paramErr);
        *printing = UINT8_MAX;
        const int printed = cfp_private(ref, 0x4dda0, &settings, 1, &status);
        printing = txn_memory(txn_field(txn_field(document, 2), 0x24), 0x1c, 1, 1);
        if (!printing)
            return 0;
        *printing = 1;
        if (!printed)
            return 0;
    }
    return txn_status(call, status ? status : txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_print_layout_width(uint32_t editor, uint32_t layout, uint32_t width, uint32_t* status)
{
    _Alignas(8) uint32_t state[2] = {0};
    const uint32_t save[] = {layout, US_TO_SYN68K(state), 1, 0};
    const uint32_t methods = txn_field(layout, 0);
    if (!cfp_callback(txn_field(methods, 0xc), save, NELEM(save), NULL))
        return 0;
    txn_store(state + 1, width);
    const uint32_t apply[] = {layout, save[1], 0, 0};
    const uint32_t reflow[] = {editor, 1, UINT32_MAX, UINT32_MAX};
    return cfp_callback(txn_field(methods, 0x10), apply, NELEM(apply), NULL) &&
           cfp_callback(txn_field(txn_field(editor, 0), 0x24), reflow, NELEM(reflow), status);
}

static int txn_begin_printing(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t document = call->words[0], output_address = call->words[1], width = call->words[2];
    const uint32_t editor = txn_field(document, 2), layout = txn_field(txn_field(editor, 0x20), 0xa);
    Byte* output = txn_memory(output_address, 0, 0x1e, 1);
    if (!layout || !output)
        return txn_status(call, paramErr);
    *((Byte*)ref->toc + 0x55e0) = 1;
    txn_store(output, 0);
    _Alignas(8) uint32_t state[2] = {0};
    const uint32_t save[] = {layout, US_TO_SYN68K(state), 1, 0};
    const uint32_t methods = txn_field(layout, 0);
    if (!cfp_callback(txn_field(methods, 0xc), save, NELEM(save), NULL))
        return 0;
    const uint32_t old_width = txn_word(state + 1);
    uint16_t saved_width = CW((uint16_t)(old_width == width ? 0 : old_width));
    memcpy(output + 0x1c, &saved_width, sizeof saved_width);
    if (old_width != width) {
        txn_store(state + 1, width);
        const uint32_t apply[] = {layout, save[1], 0, 0};
        const uint32_t reflow[] = {editor, 1, UINT32_MAX, UINT32_MAX};
        uint32_t status;
        if (!cfp_callback(txn_field(methods, 0x10), apply, NELEM(apply), NULL) ||
            !cfp_callback(txn_field(txn_field(editor, 0), 0x24), reflow, NELEM(reflow), &status))
            return 0;
        if (status)
            return txn_status(call, status);
    }
    const uint32_t build[] = {document, (int16_t)call->words[3], (int16_t)call->words[4], (int16_t)call->words[5]};
    uint32_t pages;
    if (!cfp_private(ref, 0x1b814, build, NELEM(build), &pages))
        return 0;
    txn_store(output, pages);
    if (!pages)
        return txn_status(call, memFullErr);
    txn_store(output + 4, txn_field(layout, 0x20));
    txn_store(output + 8, txn_field(layout, 0x1c));
    txn_store(output + 0xc, txn_field(layout, 0x28));
    txn_store(output + 0x10, txn_field(layout, 0x24));
    _Alignas(8) Rect empty = {0};
    const uint32_t save_clip[] = {layout, output_address + 0x14, 1};
    const uint32_t clear_clip[] = {layout, US_TO_SYN68K(&empty), 0};
    const uint32_t reset_origin[] = {layout, 0, 0};
    return cfp_callback(txn_field(txn_field(layout, 0), 0x1c), save_clip, NELEM(save_clip), NULL) &&
           cfp_callback(txn_field(txn_field(layout, 0), 0x18), clear_clip, NELEM(clear_clip), NULL) &&
           cfp_private(ref, 0x396a4, reset_origin, NELEM(reset_origin), NULL) && txn_status(call, noErr);
}

static int txn_end_printing(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t editor = txn_field(call->words[0], 2), state_address = call->words[1];
    const uint32_t layout = txn_field(txn_field(editor, 0x20), 0xa);
    const Byte* state = txn_memory(state_address, 0, 0x1e, 0);
    if (!layout || !state)
        return 1;
    Byte* printing = txn_memory(txn_word((const Byte*)ref->toc - 0x2f0), 0, 1, 1);
    if (!printing)
        return 0;
    *printing = 0;
    const int16_t width = txn_short(state + 0x1c);
    if (width && !txn_print_layout_width(editor, layout, width, NULL))
        return 0;
    const uint32_t pages = txn_word(state);
    if (!pages)
        return 1;
    if (!cfp_callback(txn_field(txn_field(pages, 0), 0), &pages, 1, NULL) ||
        !cfp_private(ref, 0x3e218, &pages, 1, NULL))
        return 0;
    const uint32_t origin[] = {layout, txn_word(state + 0xc), txn_word(state + 0x10)};
    const uint32_t extents[] = {layout, txn_word(state + 4), txn_word(state + 8)};
    const uint32_t clip[] = {layout, state_address + 0x14, 0};
    return cfp_private(ref, 0x396a4, origin, NELEM(origin), NULL) &&
           cfp_private(ref, 0x39684, extents, NELEM(extents), NULL) &&
           cfp_callback(txn_field(txn_field(layout, 0), 0x18), clip, NELEM(clip), NULL);
}

static int txn_draw_print_page(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t editor = txn_field(call->words[0], 2), page = call->words[2];
    const uint32_t layout = txn_field(txn_field(editor, 0x20), 0xa), pages = txn_field(call->words[1], 0);
    const uint16_t stride = txn_short(txn_memory(pages, 0xa, 2, 0));
    if (!layout || !pages || page >= txn_field(pages, 6) || stride < 6 || stride > 8)
        return 1;
    _Alignas(8) Byte line[8] = {0};
    const uint32_t copy[] = {pages, page, US_TO_SYN68K(line), 1};
    if (!cfp_private(ref, 0x37e0c, copy, NELEM(copy), NULL))
        return 0;
    const uint32_t extents[] = {layout, 0, txn_word(line)};
    _Alignas(8) const Rect bounds = {0, 0, (int16_t)CW(txn_short(line + 4)), (int16_t)CW((uint16_t)call->words[3])};
    const uint32_t drawing[] = {editor, US_TO_SYN68K(&bounds), 0x10};
    return cfp_private(ref, 0x39684, extents, NELEM(extents), NULL) &&
           cfp_callback(txn_field(txn_field(editor, 0), 0x1c), drawing, NELEM(drawing), NULL);
}

static int txn_window_zoom(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], window = txn_field(object, 0xa);
    if (!object)
        return 1;
    const Byte* visible = txn_memory(window, 0x6e, 1, 0);
    if (!visible)
        return 0;
    if (*visible) {
        const uint32_t bounds = window + 0x10;
        if (!cfp_private(ref, 0x6bbc8, &bounds, 1, NULL))
            return 0;
    }
    const uint32_t zoom[] = {window, (int16_t)call->words[1], 1};
    return cfp_private(ref, 0x6bbe0, zoom, NELEM(zoom), NULL) && cfp_private(ref, 0x1c8e8, &object, 1, NULL);
}

static int txn_set_frame_rect(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0];
    if (!object)
        return 1;
    const uint32_t editor = txn_field(object, 2), layout = txn_field(editor, 0x20);
    const uint32_t lines = txn_field(layout, 0xa);
    Rect* frame_rect = txn_memory(object, 0xe, sizeof *frame_rect, 1);
    Byte* origin = txn_memory(lines, 0x24, 8, 1);
    const Byte* bars = txn_memory(object, 7, 2, 0);
    const Byte* drag = txn_memory(object, 0x9f, 1, 0);
    const Byte* wraps = txn_memory(object, 0xa2, 1, 0);
    if (!frame_rect || !origin || !bars || !drag || !wraps)
        return 0;
    txn_store(origin, call->words[1]);
    txn_store(origin + 4, call->words[2]);
    Rect bounds = {CW(call->words[1]), CW(call->words[2]), CW(call->words[3]), CW(call->words[4])};
    memcpy(frame_rect, &bounds, sizeof bounds);
    if (bars[1] || *drag)
        bounds.bottom = CW((uint16_t)CW(bounds.bottom) - 15);
    if (bars[0] || *drag)
        bounds.right = CW((uint16_t)CW(bounds.right) - 15);
    if (!txn_replace_layout_bounds(layout, &bounds, ref))
        return 0;
    if (*wraps) {
        const uint32_t width = (int16_t)((int16_t)CW(bounds.right) - (int16_t)CW(bounds.left));
        const uint32_t rewrap[] = {object, 1, width};
        if (!cfp_private(ref, 0x1cf4c, rewrap, NELEM(rewrap), NULL))
            return 0;
    }
    const uint32_t redraw[] = {object, 1, 1};
    if (!cfp_private(ref, 0x1c85c, redraw, NELEM(redraw), NULL) || !txn_invalidate_frame(object, ref))
        return 0;
    if (txn_field(lines, 0x14) || txn_field(lines, 0x18))
        return 1;
    Byte* mode = txn_memory(editor, 0x41, 1, 1);
    if (!mode)
        return 0;
    const Byte previous = *mode;
    *mode = 1;
    const int result = txn_scroll_from_edge(object, 0, ref);
    *mode = previous;
    return result;
}

static int txn_cursor_region(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], region = call->words[1];
    if (!object || !txn_field(region, 0))
        return 1;
    const uint32_t window = txn_field(object, 0xa);
    const Byte* manages_window = txn_memory(object, 6, 1, 0);
    uint32_t front_window;
    const unsigned front_query = txn_word((const Byte*)ref->toc - 0x884) ? 0x6b550 : 0x6b538;
    if (!manages_window || !cfp_private(ref, front_query, NULL, 0, &front_window))
        return 0;
    const int front = window && window == front_window;
    if (window && (front || !*manages_window)) {
        uint32_t point;
        const uint32_t mouse[] = {US_TO_SYN68K(&point)};
        SetPort((GrafPtr)SYN68K_TO_US(window));
        if (!cfp_private(ref, 0x6b568, mouse, NELEM(mouse), NULL))
            return 0;
        uint32_t empty = 0, adjusted = 0;
        const uint32_t visible = txn_field(window, 0x76);
        if (*manages_window && !cfp_private(ref, 0x6b580, &visible, 1, &empty))
            return 0;
        const uint32_t position[] = {object, txn_word(&point)};
        if (!empty && !cfp_private(ref, 0x21cac, position, NELEM(position), &adjusted))
            return 0;
        if (adjusted) {
            if (!cfp_private(ref, 0x6b598, mouse, NELEM(mouse), NULL))
                return 0;
            const uint32_t global = txn_word(&point);
            const int16_t h = global, v = global >> 16;
            const uint32_t small[] = {region, (int16_t)(h - 1), (int16_t)(v - 1), (int16_t)(h + 1), (int16_t)(v + 1)};
            return cfp_private(ref, 0x6b5b0, small, NELEM(small), NULL);
        }
    }
    uint32_t globals;
    if (!cfp_private(ref, 0x6b5c8, NULL, 0, &globals))
        return 0;
    const uint32_t arrow = globals + 0x5e;
    const uint32_t default_area[] = {region, -32000, -32000, 32000, 32000};
    if (!cfp_private(ref, 0x6b5e0, &arrow, 1, NULL) ||
        !cfp_private(ref, 0x6b5b0, default_area, NELEM(default_area), NULL))
        return 0;
    if (!front)
        return 1;

    _Alignas(8) Rect bounds;
    const uint32_t measure[] = {txn_field(txn_field(object, 2), 0x20), US_TO_SYN68K(&bounds)};
    const uint32_t top_left = measure[1], bottom_right = top_left + sizeof(Point);
    const uint32_t region_pool = txn_word((const Byte*)ref->toc + 0x55e4);
    uint32_t document_area;
    if (!cfp_private(ref, 0x254d8, measure, NELEM(measure), NULL) || !cfp_private(ref, 0x6b598, &top_left, 1, NULL) ||
        !cfp_private(ref, 0x6b598, &bottom_right, 1, NULL) ||
        !cfp_private(ref, 0x3d854, &region_pool, 1, &document_area))
        return 0;
    const uint32_t rect[] = {document_area, top_left};
    const uint32_t diff[] = {region, document_area, region};
    const int result =
        cfp_private(ref, 0x6b610, rect, NELEM(rect), NULL) && cfp_private(ref, 0x6b628, diff, NELEM(diff), NULL);
    const uint32_t release[] = {region_pool, document_area};
    return cfp_private(ref, 0x3d8f8, release, NELEM(release), NULL) && result;
}

static int txn_line_metrics(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t line = call->words[1];
    const uint32_t layout = txn_field(txn_field(call->words[0], 2), 0x20);
    const uint32_t editor = txn_field(layout, 0xe);
    const void* last_line = txn_memory(txn_field(editor, 0x28), 0x2a, 4, 0);
    void* height = txn_memory(call->words[3], 0, 4, 1);
    if (!txn_uses_unicode_offsets(ref) || !last_line || line >= txn_word(last_line) + 1 || !height ||
        !txn_memory(call->words[2], 0, 4, 1))
        return txn_status(call, paramErr);

    const uint32_t text = txn_field(editor, 0xc);
    if (!text || !txn_field(layout, 0x16) || !txn_field(layout, 0x30))
        return txn_status(call, paramErr);
    const uint32_t lock[] = {text, 0}, measure[] = {layout, line};
    if (!cfp_private(ref, 0x37c7c, lock, NELEM(lock), NULL))
        return 0;
    int measured = cfp_private(ref, 0x2556c, measure, NELEM(measure), NULL);
    uint32_t pixels;
    if (measured) {
        const uint32_t line_height[] = {txn_field(layout, 0x16), line, line, 1};
        measured = cfp_private(ref, 0x409dc, line_height, NELEM(line_height), &pixels);
    }
    if (measured) {
        txn_store(height, pixels << 16);
        const uint32_t line_width[] = {txn_field(layout, 0x30), call->words[2]};
        measured = cfp_private(ref, 0x2eaec, line_width, NELEM(line_width), NULL);
    }
    return cfp_private(ref, 0x37cf4, &text, 1, NULL) && measured && txn_status(call, noErr);
}

static int txn_referenced_data(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], kind = call->words[2], output = call->words[3];
    const void* token = txn_memory(object, 0x13c, 4, 0);
    if (!token || txn_word(token) != call->words[1] || kind > 2 || !txn_memory(output, 0, 4, 1))
        return txn_status(call, paramErr);

    uint32_t error;
    if (kind == 0) {
        const uint32_t serialize[] = {object, output};
        if (!cfp_private(ref, 0x23724, serialize, NELEM(serialize), &error))
            return 0;
    }
    else if (kind == 1) {
        const uint32_t unused = 0;
        uint32_t converter;
        if (!cfp_private(ref, 0x2b138, &unused, 1, &converter))
            return 0;
        if (!converter)
            return txn_status(call, memFullErr);
        _Alignas(16) Byte position[16] = {0};
        const uint32_t save[] = {converter, US_TO_SYN68K(position)};
        const uint32_t export_data[] = {converter, output, txn_field(object, 0xaa)};
        const uint32_t methods = txn_field(converter, 0);
        if (!cfp_callback(txn_field(methods, 0x2c), save, NELEM(save), NULL) ||
            !cfp_callback(txn_field(methods, 0x34), export_data, NELEM(export_data), &error))
            return 0;
    }
    else {
        const uint32_t attachment = txn_field(object, 0x148);
        if (!attachment)
            return txn_status(call, memFullErr);
        const uint32_t export_data[] = {attachment, output, txn_field(object, 0xaa)};
        if (!cfp_private(ref, 0xc9bc, export_data, NELEM(export_data), &error))
            return 0;
    }
    return txn_status(call, error ? error : txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_scrap_edit_status(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref, uint32_t status)
{
    return txn_status(call, status ? status : txn_word((const Byte*)ref->toc + 0x3d2c));
}

static int txn_copy_selection(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t editor = txn_field(call->words[0], 2);
    const Byte* selection = txn_selection_bytes(editor);
    if (!selection)
        return txn_status(call, paramErr);
    const uint32_t layout = txn_field(editor, 0x20);
    if (txn_field(txn_field(layout, 0x30), 0xa))
        return txn_status(call, -22014);

    const uint32_t first = txn_word(selection), last = txn_word(selection + 6);
    uint32_t scrap;
    if (!cfp_private(ref, 0x2b138, NULL, 0, &scrap))
        return 0;
    if (last == first)
        return txn_scrap_edit_status(call, ref, noErr);
    Byte* copied = txn_memory(scrap, 0x15, 1, 1);
    if (!copied)
        return txn_status(call, memFullErr);
    if (!cfp_private(ref, 0x9d34, &scrap, 1, NULL))
        return 0;
    *copied = 1;
    const uint32_t copy[] = {
        scrap, first, last - first, txn_field(editor, 0x10), txn_field(editor, 0x30), txn_field(editor, 0xc), 7};
    uint32_t status;
    return cfp_callback(txn_field(txn_field(scrap, 0), 4), copy, NELEM(copy), &status) &&
           txn_scrap_edit_status(call, ref, status);
}

static int txn_edit_selection(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                              const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], editor = txn_field(object, 2);
    const Byte* read_only = txn_memory(object, 0xa0, 1, 0);
    const Byte* disabled = txn_memory(editor, 0x3d, 1, 0);
    if (!read_only || !disabled)
        return txn_status(call, paramErr);
    const uint32_t action = operation == txn_cut ? 2 : operation == txn_paste ? 4 : 8;
    if (operation == txn_paste && txn_field(object, 0x134)) {
        const uint32_t filter[] = {object, action, 0, 0, 0, 0, UINT32_MAX, UINT32_MAX, 0};
        uint32_t consumed;
        if (!cfp_private(ref, 0x1d738, filter, NELEM(filter), &consumed))
            return 0;
        if (consumed)
            return txn_scrap_edit_status(call, ref, noErr);
    }
    if (*read_only || *disabled)
        return txn_scrap_edit_status(call, ref, noErr);
    const uint32_t size = 0x5e;
    uint32_t record;
    if (!cfp_private(ref, 0x3e1c0, &size, 1, &record))
        return 0;
    if (!record)
        return txn_status(call, memFullErr);
    const uint32_t attach[] = {object, record};
    if (!cfp_private(ref, 0x4c460, &record, 1, NULL) || !cfp_private(ref, 0x1da54, attach, NELEM(attach), NULL))
        return 0;
    Byte retry = 0;
    const uint32_t setup[] = {record, editor, action, US_TO_SYN68K(&retry), 0};
    uint32_t status;
    if (!cfp_private(ref, 0x4c4b8, setup, NELEM(setup), &status))
        return 0;
    if (!status && retry &&
        (!cfp_private(ref, 0x1dce0, attach, NELEM(attach), NULL) ||
         !cfp_private(ref, 0x4c4b8, setup, NELEM(setup), &status)))
        return 0;
    if (status || retry) {
        if (!cfp_private(ref, 0x1ddbc, &object, 1, NULL))
            return 0;
        return txn_status(call, status ? status : memFullErr);
    }
    if (!cfp_private(ref, 0x1df50, &object, 1, &status))
        return 0;
    const Byte* applied = txn_memory(txn_field(object, 0x130), 0xc, 1, 0);
    if (!status && applied && !*applied && !cfp_private(ref, 0x1ddbc, &object, 1, NULL))
        return 0;
    return txn_scrap_edit_status(call, ref, status);
}

static int txn_focus_document(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], focused = (Byte)call->words[1];
    if (!txn_field(object, 0xa))
        return 1;
    const uint32_t editor = txn_field(object, 2);
    const uint32_t method = txn_field(txn_field(editor, 0), 0x28);
    Byte* state = txn_memory(object, 0x9d, 1, 1);
    const uint32_t arguments[] = {editor, focused, 1};
    if (!state || !method || !cfp_callback(method, arguments, NELEM(arguments), NULL))
        return 0;
    if (*state == focused)
        return 1;
    *state = focused;
    const uint32_t change[] = {object, focused};
    if (!cfp_private(ref, 0x20e74, change, NELEM(change), NULL))
        return 0;
    if (focused)
        return cfp_private(ref, 0x21148, &object, 1, NULL);
    void* timer_slot = txn_memory(object, 0x154, 4, 1);
    const uint32_t timer = txn_word(timer_slot);
    if (timer) {
        if (!cfp_private(ref, 0x6b958, &timer, 1, NULL))
            return 0;
        txn_store(timer_slot, 0);
    }
    return 1;
}

static int txn_echo(ROMlib_carbon_ppc_frame* call, const ROMlib_carbon_reference* ref)
{
    const uint32_t object = call->words[0], editor = txn_field(object, 2);
    Byte* normal = txn_memory(object, 0xc4, 1, 1);
    if (!normal || !editor)
        return txn_status(call, paramErr);
    const Byte enabled = call->words[3];
    const uint32_t arguments[] = {editor, (uint16_t)call->words[1], call->words[2], enabled};
    if (!cfp_private(ref, 0x2b254, arguments, NELEM(arguments), NULL))
        return 0;
    *normal = !enabled;
    return txn_invalidate_frame(object, ref) && txn_status(call, noErr);
}

static int txn_count_changes(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0], mask = call->words[1];
    const int clear = operation == txn_clear_action_count;
    Byte* counters = txn_memory(object, 0xfa, 24, clear);
    void* output = clear ? NULL : txn_memory(call->words[2], 0, 4, 1);
    if (!counters || !mask || (!clear && !output))
        return txn_status(call, paramErr);
    static const unsigned groups[] = {1, 1, 2, 1, 1, 2};
    uint32_t count = 0;
    for (unsigned i = 0; i < NELEM(groups); ++i) {
        if (mask & groups[i]) {
            Byte* counter = counters + 4 * i;
            if (clear)
                txn_store(counter, 0);
            else
                count += txn_word(counter);
        }
    }
    if (output)
        txn_store(output, count);
    return txn_status(call, noErr);
}

static int txn_data_filter(CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call,
                           const ROMlib_carbon_reference* ref)
{
    const int set = operation == txn_set_data_filter;
    const uint32_t object = call->words[0];
    Byte* state = txn_memory(object, 0x134, 8, set);
    if (!state)
        return txn_status(call, paramErr);
    if (set) {
        const uint32_t callback = call->words[1], types = callback ? call->words[2] : 0;
        if (callback && !types)
            return txn_status(call, paramErr);
        /* In the enabled mode the editor points back to the owning document
           so the real insertion worker can use the registered filter. */
        if (((const Byte*)ref->toc)[0x42e8]) {
            void* owner = txn_memory(txn_field(object, 2), 0x7c, 4, 1);
            if (!owner)
                return txn_status(call, paramErr);
            txn_store(owner, callback ? object : 0);
        }
        txn_store(state, callback);
        txn_store(state + 4, types);
    }
    else {
        void* callback = txn_memory(call->words[1], 0, 4, 1);
        void* types = txn_memory(call->words[2], 0, 4, 1);
        if (!callback || !types)
            return txn_status(call, paramErr);
        txn_store(types, txn_word(state + 4));
        txn_store(callback, txn_word(state));
    }
    return txn_status(call, noErr);
}

int ROMlib_carbon_textension(const char* name, CarbonTextensionOperation operation, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    /* The original Carbon wrapper owns lazy loading and initialization. Once
       it has loaded the known companion, both paths see the same records. */
    if (operation != txn_initialize && operation != txn_terminate && !txn_reference_matches(&ref))
        return txn_saved(name, call);

    const uint32_t object = call->words[0];
    switch (operation) {
    case txn_initialize:
        return txn_initialize_textension(name, call);
    case txn_terminate:
        return txn_terminate_textension(name, call);
    case txn_new_object:
        return txn_create_document(name, call, &ref);
    case txn_version: {
        void* features = txn_memory(object, 0, 4, 1);
        if (features)
            txn_store(features, 1);
        return txn_status(call, 0x01318000);
    }
    case txn_data_size: {
        const uint32_t editor = txn_field(object, 2);
        const uint32_t length = txn_field(txn_field(editor, 0xc), 6);
        return txn_status(call, txn_uses_unicode_offsets(&ref) ? length * 2 : length);
    }
    case txn_selection: {
        const Byte* selection = txn_selection_bytes(txn_field(object, 2));
        if (!selection)
            return 1;
        if (txn_field(object, 0xaa) == 2 && !txn_uses_unicode_offsets(&ref))
            return txn_saved(name, call);
        const uint32_t start = txn_word(selection), end = txn_word(selection + 6);
        void* first = txn_memory(call->words[1], 0, 4, 1);
        void* last = txn_memory(call->words[2], 0, 4, 1);
        if (first)
            txn_store(first, start);
        if (last)
            txn_store(last, end);
        return 1;
    }
    case txn_selection_empty: {
        const Byte* selection = txn_selection_bytes(txn_field(object, 2));
        return txn_status(call, !selection || txn_word(selection) == txn_word(selection + 6));
    }
    case txn_view_rect: {
        const void* rect = txn_memory(object, 0xe, 8, 0);
        void* output = txn_memory(call->words[1], 0, 8, 1);
        if (rect && output)
            memmove(output, rect, 8);
        return 1;
    }
    case txn_line_count: {
        const uint32_t editor = txn_field(object, 2);
        const void* lines = txn_memory(txn_field(editor, 0x28), 0x2a, 4, 0);
        void* output = txn_memory(call->words[1], 0, 4, 1);
        if (!lines || !output)
            return txn_status(call, paramErr);
        txn_store(output, txn_word(lines) + 1);
        return txn_status(call, noErr);
    }
    case txn_window_attached:
        return txn_status(call, txn_field(object, 0xa) != 0);
    case txn_specific_window_attached: {
        Byte* output = txn_memory(call->words[2], 0, 1, 1);
        const uint32_t editor = txn_field(object, 2);
        const Byte* attached = txn_memory(txn_field(editor, 0x20), 0x3f, 1, 0);
        if (!output || !attached)
            return txn_status(call, paramErr);
        *output = *attached && txn_field(object, 0xa) == call->words[1];
        return txn_status(call, noErr);
    }
    case txn_change_count: {
        const uint32_t count = txn_field(object, 0xf6);
        const Byte* pending = txn_memory(object, 0xa3, 1, 0);
        return txn_status(call, count ? count : pending && *pending && txn_has_provisional_edit(txn_field(object, 2)));
    }
    case txn_action_count:
    case txn_clear_action_count:
        return txn_count_changes(operation, call);
    case txn_get_data_filter:
    case txn_set_data_filter:
        return txn_data_filter(operation, call, &ref);
    case txn_sleep_ticks: {
        if (!object)
            return txn_status(call, INT32_MAX);
        const Byte* selection = txn_selection_bytes(txn_field(object, 2));
        if (!selection || txn_word(selection) != txn_word(selection + 6))
            return txn_saved(name, call);
        return txn_status(call, txn_word((const Byte*)ref.toc + 0x62c4));
    }
    case txn_set_selection:
        return txn_set_selection_range(call, &ref);
    case txn_can_undo:
    case txn_can_redo:
        return txn_can_edit_history(call, operation == txn_can_undo);
    case txn_is_typing:
        return txn_is_typing_edit(call);
    case txn_font_defaults:
        return txn_get_font_defaults(call);
    case txn_set_font_defaults:
        return txn_update_font_defaults(call, &ref);
    case txn_font_menu:
    case txn_new_font_menu:
    case txn_dispose_font_menu:
        return txn_font_menu_object(operation, call, &ref);
    case txn_prepare_font_menu:
    case txn_do_font_menu_selection:
        return txn_apply_font_menu(operation, call, &ref);
    case txn_select_all:
        return txn_select_document(object);
    case txn_empty_undo_stack:
        return txn_empty_history(object, &ref);
    case txn_force_update:
        return txn_invalidate_frame(object, &ref);
    case txn_set_background:
        return txn_background(call, &ref);
    case txn_draw:
        return txn_draw_document(call, &ref);
    case txn_draw_in_rectangle:
        return txn_draw_document_in_rectangle(call, &ref);
    case txn_draw_unicode_text_box:
        return txn_draw_unicode_box(call, &ref);
    case txn_draw_cf_text_box:
        return txn_draw_cf_box(name, call);
    case txn_focus:
        return txn_focus_document(call, &ref);
    case txn_echo_mode:
        return txn_echo(call, &ref);
    case txn_undo:
    case txn_redo:
        return txn_apply_history(object, operation == txn_undo, &ref);
    case txn_show_selection:
        return txn_scroll_selection(object, 1, (Byte)call->words[1], &ref);
    case txn_set_view_rect:
        return txn_update_view_rect(call, &ref);
    case txn_offset_to_point:
    case txn_point_to_offset:
        return txn_convert_position(call, operation == txn_offset_to_point, &ref);
    case txn_count_runs:
        return txn_count_runs_in_range(call, &ref);
    case txn_indexed_run:
        return txn_indexed_run_details(call, &ref);
    case txn_get_data:
        return txn_copy_data(call, &ref);
    case txn_get_data_encoded:
        return txn_copy_encoded_data(name, call, &ref);
    case txn_find:
        return txn_find_data(call, &ref);
    case txn_set_data:
        return txn_replace_plain_data(name, call, &ref);
    case txn_scrap_pastable:
    case txn_scrap_to_public:
    case txn_scrap_from_public:
        return txn_textension_scrap(operation, call, &ref);
    case txn_get_continuous_attributes:
        return txn_continuous_attributes(call, &ref);
    case txn_set_type_attributes:
        return txn_update_type_attributes(call, &ref);
    case txn_set_data_from_file:
        return txn_insert_file_data(call, &ref);
    case txn_count_elements:
    case txn_find_element:
        return txn_element_query(call, operation == txn_find_element, &ref);
    case txn_activate:
        return txn_set_activation(call, &ref);
    case txn_update:
        return txn_window_update(object, &ref);
    case txn_resize_frame:
        return txn_frame_resize(call, &ref);
    case txn_zoom_window:
        return txn_window_zoom(call, &ref);
    case txn_grow_window:
        return txn_window_grow(call, &ref);
    case txn_click:
        return txn_click_document(call, &ref);
    case txn_drag_tracker:
    case txn_drag_receiver:
        return txn_drag(operation, call, &ref);
    case txn_idle:
        return txn_idle_document(name, call);
    case txn_key_event:
        return txn_key_down(call, &ref);
    case txn_unicode_key_event:
        return txn_unicode_key_down(call, &ref);
    case txn_revert:
        return txn_revert_document(call, &ref);
    case txn_save:
        return txn_save_document(call, &ref);
    case txn_get_controls:
        return txn_get_object_controls(call, &ref);
    case txn_set_controls:
        return txn_set_object_controls(call, &ref);
    case txn_page_setup:
    case txn_print:
        return txn_print_document(operation, call, &ref);
    case txn_delete_object:
        return !object ||
               (cfp_private(&ref, 0x233e4, &object, 1, NULL) && cfp_private(&ref, 0x3e218, &object, 1, NULL));
    case txn_attach_window:
        return txn_attach_document_window(call, &ref);
    case txn_get_line_metrics:
        return txn_line_metrics(call, &ref);
    case txn_get_referenced_data:
        return txn_referenced_data(call, &ref);
    case txn_count_lines_to_print:
        return txn_status(call, txn_field(txn_field(object, 0), 6));
    case txn_begin_print:
        return txn_begin_printing(call, &ref);
    case txn_end_print:
        return txn_end_printing(call, &ref);
    case txn_print_page:
        return txn_draw_print_page(call, &ref);
    case txn_frame_bounds:
        return txn_set_frame_rect(call, &ref);
    case txn_adjust_cursor:
        return txn_cursor_region(call, &ref);
    case txn_clear:
    case txn_cut:
    case txn_paste:
        return txn_edit_selection(operation, call, &ref);
    case txn_copy:
        return txn_copy_selection(call, &ref);
    }
    return 0;
}
