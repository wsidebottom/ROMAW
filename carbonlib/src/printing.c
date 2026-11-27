/*+===================================================================
  File:      printing.c

  Summary:   Implements Carbon Printing Manager ticket, session, printer and dialog operations.

  Classes:   None.

  Functions: ROMlib_carbon_PMBegin, ROMlib_carbon_PMEnd, ROMlib_carbon_PMCreateSession,
             ROMlib_carbon_PMBeginDocument, ROMlib_carbon_PMEndDocument, ROMlib_carbon_PMBeginPage,
             ROMlib_carbon_PMEndPage, ROMlib_carbon_PMGetGrafPtr, ROMlib_carbon_PMIsPostScriptDriver,
             ROMlib_carbon_PMSessionMakeOldPrintRecord, ROMlib_carbon_PMSessionConvertOldPrintRecord,
             and native Printing Manager frame entry points from the printing entry tables.

  Origin:    CarbonLib 1.4 Printing Manager ticket routines and Universal
             Interfaces 3.4.2 PMCore.h and PMDefinitions.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#include "cf_batch.h"
#include "FileMgr.h"
#include "internal.h"
#include "MemoryMgr.h"
#include <stdio.h>

enum print_operation_kind {
    pm_plain,
    pm_settings,
    pm_page_format,
    pm_copy_settings,
    pm_copy_page_format,
    pm_get_settings_data,
    pm_get_format_data,
    pm_set_range,
    pm_set_rect,
    pm_set_paper_rect,
    pm_get_scale,
    pm_set_scale,
    pm_lifetime,
    pm_dispose_settings,
    pm_dispose_page_format,
};

enum print_ticket_type { print_settings, page_format };

enum {
    invalid_settings = -30875,
    invalid_page_format = -30876,
    invalid_page_range = -30877,
};

struct print_operation {
    const char* name;
    uint32_t entry_offset;
    uint32_t worker_offset;
    enum print_operation_kind kind;
    unsigned argument_count;
    unsigned required_arguments;
    unsigned short_arguments;
    unsigned byte_arguments;
};

static int print_result(ROMlib_carbon_ppc_frame* call, uint32_t result)
{
    call->words[0] = result;
    return 1;
}

/* These private workers operate on the saved library's own tickets. The public
   fallback and native entries can therefore exchange the same objects. */
static int use_print_profile(const ROMlib_carbon_reference* ref, uint32_t entry_offset)
{
    return ref->code_offset == entry_offset && ref->code_size == 0xb8fb4 && ref->toc && ref->toc_offset == 0x2f78 &&
           ref->toc_size == 0x10f0c;
}

static int print_worker(const ROMlib_carbon_reference* ref, uint32_t offset, const uint32_t* arguments, unsigned count,
                        uint32_t* result)
{
    return cfp_private(ref, offset, arguments, count, result);
}

static int print_floating_worker(const ROMlib_carbon_reference* ref, uint32_t offset, uint32_t object, double value,
                                 const ROMlib_carbon_ppc_frame* caller, uint32_t* status)
{
    _Alignas(16) uint32_t vector[2];
    memcpy(vector, ref->function, sizeof vector);
    vector[0] = CL(CL(vector[0]) - ref->code_offset + offset);
    ROMlib_carbon_ppc_frame worker = *caller;
    worker.words[0] = object;
    memcpy(&worker.floating[0], &value, sizeof value);
    if (!ROMlib_carbon_invoke_frame(vector, &worker))
        return 0;
    *status = worker.words[0];
    return 1;
}

static double read_print_double(const uint32_t words[2])
{
    const uint64_t bits = (uint64_t)CL(words[0]) << 32 | CL(words[1]);
    double value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void put_print_double(uint32_t words[2], double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    words[0] = CL(bits >> 32);
    words[1] = CL((uint32_t)bits);
}

static int print_ticket_worker(const ROMlib_carbon_reference* ref, const struct print_operation* op, uint32_t* args,
                               ROMlib_carbon_ppc_frame* call, uint32_t* status)
{
    if (op->kind == pm_set_scale) {
        double percent;
        memcpy(&percent, &call->floating[0], sizeof percent);
        return print_floating_worker(ref, op->worker_offset, args[0], percent * 0.01, call, status);
    }
    if (op->kind == pm_get_scale) {
        uint32_t* output = ROMlib_carbon_guest_memory(args[1], sizeof(double), 1);
        if (!output)
            return 0;
        _Alignas(16) uint32_t scale[2] = {0};
        args[1] = US_TO_SYN68K(scale);
        if (!print_worker(ref, op->worker_offset, args, 2, status))
            return 0;
        if (!*status) {
            put_print_double(scale, read_print_double(scale) * 100.0);
            memcpy(output, scale, sizeof scale);
        }
        return 1;
    }
    if (op->kind == pm_set_paper_rect) {
        _Alignas(32) uint32_t rectangle[4][2];
        const void* source = ROMlib_carbon_guest_memory(args[1], sizeof rectangle, 0);
        if (!source)
            return 0;
        memcpy(rectangle, source, sizeof rectangle);
        for (unsigned axis = 0; axis < 2; ++axis) {
            put_print_double(rectangle[axis + 2],
                             read_print_double(rectangle[axis + 2]) - read_print_double(rectangle[axis]));
            put_print_double(rectangle[axis], 0.0);
        }
        args[1] = US_TO_SYN68K(rectangle);
        return print_worker(ref, op->worker_offset, args, 2, status);
    }
    return print_worker(ref, op->worker_offset, args, op->argument_count, status);
}

static int ticket_check(const ROMlib_carbon_reference* ref, enum print_ticket_type type, uint32_t address,
                        uint32_t* result)
{
    return print_worker(ref, type == print_settings ? 0x8716c : 0x871f0, &address, 1, result);
}

static int ticket_payload(const ROMlib_carbon_reference* ref, enum print_ticket_type type, uint32_t* address)
{
    const uint32_t object = *address;
    return print_worker(ref, type == print_settings ? 0x85cc8 : 0x88bcc, &object, 1, address);
}

static int finish_print_operation(const ROMlib_carbon_reference* ref, uint32_t result, ROMlib_carbon_ppc_frame* call)
{
    _Alignas(16) uint32_t status = CL(result);
    const uint32_t argument = US_TO_SYN68K(&status);
    uint32_t ignored;
    if (!print_worker(ref, 0x815a0, &argument, 1, &ignored))
        return 0;
    return print_result(call, CL(status));
}

static int print_lifetime(const ROMlib_carbon_reference* ref, const struct print_operation* op,
                          ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0];
    const int dispose = op->kind != pm_lifetime;
    const enum print_ticket_type type = op->kind == pm_dispose_settings ? print_settings : page_format;
    const int invalid_type = type == print_settings ? invalid_settings : invalid_page_format;
    uint32_t valid, result;
    if (dispose) {
        if (!ticket_check(ref, type, object, &valid))
            return 0;
    }
    else if (!print_worker(ref, 0x81670, &object, 1, &valid))
        return 0;

    if (!(uint8_t)valid)
        return print_result(call, dispose ? invalid_type : paramErr);
    if (!print_worker(ref, op->worker_offset, &object, 1, &result))
        return 0;
    return print_result(call, dispose && (int32_t)result == paramErr ? invalid_type : result);
}

static int ticket_arguments(const ROMlib_carbon_reference* ref, const struct print_operation* op,
                            const uint32_t* arguments, uint32_t* status)
{
    if (op->kind == pm_set_range && arguments[1] > arguments[2]) {
        *status = invalid_page_range;
        return 1;
    }
    if ((op->kind == pm_get_settings_data || op->kind == pm_get_format_data) && !arguments[2] && !arguments[3]) {
        *status = paramErr;
        return 1;
    }
    if (op->kind == pm_set_rect || op->kind == pm_set_paper_rect) {
        uint32_t valid;
        if (!print_worker(ref, 0x89dd4, arguments + 1, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            *status = paramErr;
    }
    for (unsigned i = 0; i < op->argument_count; ++i)
        if ((op->required_arguments & (1U << i)) && !arguments[i]) {
            *status = paramErr;
            break;
        }
    return 1;
}

static int print_ticket(const struct print_operation* op, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    if (op->kind >= pm_lifetime)
        return print_lifetime(&ref, op, call);

    uint32_t arguments[4], ignored;
    memcpy(arguments, call->words, op->argument_count * sizeof(*arguments));
    if (!print_worker(&ref, 0x81614, NULL, 0, &ignored))
        return 0;

    const int settings = op->kind == pm_settings || op->kind == pm_copy_settings || op->kind == pm_get_settings_data ||
                         op->kind == pm_set_range;
    const enum print_ticket_type type = settings ? print_settings : page_format;
    const int copy = op->kind == pm_copy_settings || op->kind == pm_copy_page_format;
    uint32_t status = noErr;
    if (op->kind != pm_plain) {
        for (unsigned i = 0; i < (copy ? 2U : 1U); ++i) {
            uint32_t valid;
            if (!ticket_check(&ref, type, arguments[i], &valid))
                return 0;
            if (!(uint8_t)valid) {
                status = settings ? invalid_settings : invalid_page_format;
                break;
            }
        }
    }
    if (!status && !ticket_arguments(&ref, op, arguments, &status))
        return 0;
    if (!status) {
        if (op->kind != pm_plain && !copy && !ticket_payload(&ref, type, arguments))
            return 0;
        for (unsigned i = 1; i < op->argument_count; ++i) {
            const unsigned bit = 1U << i;
            if (op->short_arguments & bit)
                arguments[i] = (uint16_t)arguments[i];
            if (op->byte_arguments & bit)
                arguments[i] = (uint8_t)arguments[i];
        }
        if (!print_ticket_worker(&ref, op, arguments, call, &status))
            return 0;
    }
    return finish_print_operation(&ref, status, call);
}

#define CARBON_PRINT(name, offset, worker, kind, count, required, short_words, byte_words)   \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                                  \
    {                                                                                        \
        static const struct print_operation op = {#name, offset,   worker,      kind,        \
                                                  count, required, short_words, byte_words}; \
        return print_ticket(&op, call);                                                      \
    }
#include "printing_entries.h"
#undef CARBON_PRINT

enum {
    invalid_session = -30879,
    session_not_open = -30871,
    print_internal_error = -30870,
    print_cancel = 128,
};

enum print_phase { print_idle = 1, print_document, print_page };

static int read_print_phase(uint32_t session, uint16_t* phase)
{
    const void* field = ROMlib_carbon_guest_memory(session + 0x58, sizeof *phase, 0);
    if (!field)
        return 0;
    memcpy(phase, field, sizeof *phase);
    *phase = CW(*phase);
    return 1;
}

static int write_print_phase(uint32_t session, uint16_t phase)
{
    void* field = ROMlib_carbon_guest_memory(session + 0x58, sizeof phase, 1);
    if (!field)
        return 0;
    phase = CW(phase);
    memcpy(field, &phase, sizeof phase);
    return 1;
}

enum session_state {
    session_any,
    session_preserve_error,
    session_open,
    session_idle,
    session_in_page,
};

enum session_ticket {
    no_session_ticket,
    session_settings,
    session_page_format,
};

struct session_operation {
    const char* name;
    uint32_t entry_offset;
    uint32_t worker_offset;
    unsigned argument_count;
    unsigned required_arguments;
    unsigned required_after_reset;
    enum session_state state;
    enum session_ticket ticket;
    unsigned normalize_result;
};

static int session_state_matches(uint16_t actual, enum session_state expected)
{
    switch (expected) {
    case session_any:
    case session_preserve_error:
        return 1;
    case session_open:
        return actual != 0;
    case session_idle:
        return actual == print_idle;
    case session_in_page:
        return actual == print_page;
    }
    return 0;
}

static int print_session(const struct session_operation* op, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t arguments[4], valid, session = 0, status = noErr;
    memcpy(arguments, call->words, op->argument_count * sizeof(*arguments));
    if (!print_worker(&ref, 0x85768, arguments, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, invalid_session);
    if (op->ticket != no_session_ticket) {
        const enum print_ticket_type type = op->ticket == session_settings ? print_settings : page_format;
        if (!ticket_check(&ref, type, arguments[1], &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, type == print_settings ? invalid_settings : invalid_page_format);
    }
    for (unsigned i = 1; i < op->argument_count; ++i)
        if ((op->required_arguments & (1u << i)) && !arguments[i])
            return print_result(call, paramErr);

    if (!print_worker(&ref, 0x8448c, arguments, 1, &session))
        return 0;
    if (!session)
        return print_result(call, print_internal_error);
    arguments[0] = session;

    if (op->worker_offset == 0x89894) {
        uint32_t dictionary;
        if (!cfp_get(session + 4, &dictionary))
            return 0;
        if (!dictionary)
            return print_result(call, -30874);
    }

    /* SessionError reads the legacy status without changing the session status. */
    if (op->worker_offset == 0x84d00) {
        if (!print_worker(&ref, op->worker_offset, arguments, op->argument_count, &status))
            return 0;
        return print_result(call, status);
    }

    if (op->state != session_preserve_error && !print_worker(&ref, 0x895b8, arguments, 1, &valid))
        return 0;
    if (op->state != session_any && op->state != session_preserve_error) {
        uint16_t state;
        if (!read_print_phase(session, &state))
            return 0;
        if (!session_state_matches(state, op->state))
            status = session_not_open;
    }
    if (!status)
        for (unsigned i = 1; i < op->argument_count; ++i)
            if ((op->required_after_reset & (1u << i)) && !arguments[i]) {
                status = paramErr;
                break;
            }
    if (!status && op->worker_offset == 0x847d8) {
        uint16_t encoded_opcode;
        const void* data = ROMlib_carbon_guest_memory(arguments[1], sizeof(encoded_opcode), 0);
        if (!data)
            return 0;
        memcpy(&encoded_opcode, data, sizeof encoded_opcode);
        const uint16_t opcode = CW(encoded_opcode);
        if (opcode < 6 || opcode == 8 || opcode == 12 || opcode == 13 || opcode > 26)
            status = paramErr;
    }
    if (!status) {
        if (op->ticket != no_session_ticket &&
            !ticket_payload(&ref, op->ticket == session_settings ? print_settings : page_format, arguments + 1))
            return 0;
        if (op->worker_offset == 0) {
            uint32_t printer;
            if (!cfp_get(session + 0x54, &printer) || !cfp_write(arguments[1], printer))
                return 0;
        }
        else if (!print_worker(&ref, op->worker_offset, arguments, op->argument_count, &status))
            return 0;
    }
    const uint32_t status_arguments[] = {session, status};
    if (op->normalize_result && !print_worker(&ref, 0x895d0, status_arguments, 2, &status))
        return 0;
    return print_result(call, status);
}

#define CARBON_PRINT_SESSION(name, offset, worker, count, required, after_reset, state, ticket, normalize) \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                                                \
    {                                                                                                      \
        static const struct session_operation op = {#name,       offset, worker, count,    required,       \
                                                    after_reset, state,  ticket, normalize};               \
        return print_session(&op, call);                                                                   \
    }
#include "printing_session_entries.h"
#undef CARBON_PRINT_SESSION

enum dialog_field_access {
    dialog_reference,
    dialog_read_word,
    dialog_write_word,
    dialog_read_byte,
    dialog_write_byte,
};

struct dialog_operation {
    const char* name;
    uint32_t entry_offset;
    unsigned dialog_offset;
    enum dialog_field_access access;
};

static int print_dialog_access(const struct dialog_operation* op, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t ignored, default_session_slot, default_session;
    if (!print_worker(&ref, 0x81614, NULL, 0, &ignored) ||
        !cfp_get(US_TO_SYN68K(ref.toc) - 0x3c4, &default_session_slot) ||
        !cfp_get(default_session_slot, &default_session))
        return 0;
    if (!default_session)
        return finish_print_operation(&ref, session_not_open, call);
    const uint32_t object = call->words[0], argument = call->words[1];
    const int reads =
        op->access == dialog_reference || op->access == dialog_read_word || op->access == dialog_read_byte;
    if (!object || ((reads || op->access == dialog_write_word) && !argument))
        return finish_print_operation(&ref, paramErr, call);

    const uint32_t field = object + op->dialog_offset;
    switch (op->access) {
    case dialog_reference:
        if (!cfp_write(argument, object))
            return 0;
        break;
    case dialog_read_word: {
        uint32_t value;
        if (!cfp_get(field, &value) || !cfp_write(argument, value))
            return 0;
    } break;
    case dialog_write_word:
        if (!cfp_write(field, argument))
            return 0;
        break;
    case dialog_read_byte: {
        const uint8_t* value = ROMlib_carbon_guest_memory(field, 1, 0);
        uint8_t* output = ROMlib_carbon_guest_memory(argument, 1, 1);
        if (!value || !output)
            return 0;
        *output = *value;
    } break;
    case dialog_write_byte: {
        uint8_t* value = ROMlib_carbon_guest_memory(field, 1, 1);
        if (!value)
            return 0;
        *value = (uint8_t)argument;
    } break;
    }
    return finish_print_operation(&ref, noErr, call);
}

#define CARBON_PRINT_DIALOG(name, offset, field, access)                          \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                       \
    {                                                                             \
        static const struct dialog_operation op = {#name, offset, field, access}; \
        return print_dialog_access(&op, call);                                    \
    }
#include "printing_dialog_entries.h"
#undef CARBON_PRINT_DIALOG

static int default_print_session(const ROMlib_carbon_reference* ref, uint32_t* session)
{
    uint32_t slot;
    return cfp_get(US_TO_SYN68K(ref->toc) - 0x3c4, &slot) && cfp_get(slot, session);
}

struct printer_operation {
    const char* name;
    uint32_t entry_offset;
    uint32_t worker_offset;
    unsigned argument_count;
    int normalize_result;
};

static int printer_result(const ROMlib_carbon_reference* ref, const struct printer_operation* op, uint32_t status,
                          ROMlib_carbon_ppc_frame* call)
{
    return op->normalize_result ? finish_print_operation(ref, status, call) : print_result(call, status);
}

static int print_printer(const struct printer_operation* op, ROMlib_carbon_ppc_frame* call)
{
    enum { invalid_printer = -30880 };

    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t arguments[3], valid, session, status = noErr;
    memcpy(arguments, call->words, op->argument_count * sizeof(*arguments));
    if (!print_worker(&ref, 0x81614, NULL, 0, &valid) || !default_print_session(&ref, &session))
        return 0;
    if (!session)
        status = session_not_open;
    else {
        _Alignas(16) uint32_t current = 0;
        ROMlib_carbon_ppc_frame get_current = *call;
        get_current.words[0] = session;
        get_current.words[1] = US_TO_SYN68K(&current);
        if (!ROMlib_carbon_PMSessionGetCurrentPrinter(&get_current))
            return 0;
        if (CL(current) != arguments[0])
            status = invalid_printer;
    }
    if (!print_worker(&ref, 0x841dc, arguments, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return printer_result(&ref, op, invalid_printer, call);
    /* The description request checks its output even if the printer differs. */
    if ((!status || op->worker_offset == 0x840b4) && !arguments[op->argument_count - 1])
        status = paramErr;
    if (status)
        return printer_result(&ref, op, status, call);
    if (!print_worker(&ref, 0x84258, arguments, 1, &arguments[0]))
        return 0;
    if (!arguments[0])
        return printer_result(&ref, op, paramErr, call);
    if (!print_worker(&ref, op->worker_offset, arguments, op->argument_count, &status))
        return 0;
    return printer_result(&ref, op, status, call);
}

#define CARBON_PRINT_PRINTER(name, offset, worker, count, normalize)                          \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                                   \
    {                                                                                         \
        static const struct printer_operation op = {#name, offset, worker, count, normalize}; \
        return print_printer(&op, call);                                                      \
    }

static int print_default_printer(const char* name, uint32_t offset, unsigned count, int output_first,
                                 ROMlib_carbon_frame_entry query, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t session, valid;
    if (!default_print_session(&ref, &session) || !print_worker(&ref, 0x85768, &session, 1, &valid))
        return 0;
    if (!(uint8_t)valid && (!output_first || call->words[count - 1]))
        return print_result(call, session_not_open);
    if (!call->words[count - 1])
        return print_result(call, paramErr);
    _Alignas(16) uint32_t output = 0;
    ROMlib_carbon_ppc_frame current = *call;
    current.words[0] = session;
    current.words[1] = US_TO_SYN68K(&output);
    if (!ROMlib_carbon_PMSessionGetCurrentPrinter(&current))
        return 0;
    if (current.words[0])
        return print_result(call, current.words[0]);
    ROMlib_carbon_ppc_frame arguments = *call;
    memmove(arguments.words + 1, arguments.words, count * sizeof(uint32_t));
    arguments.words[0] = CL(output);
    ROMlib_carbon_ppc_frame release = {.words = {arguments.words[0]}};
    const int ok = query(&arguments);
    return ok && ROMlib_carbon_PMRelease(&release) && print_result(call, arguments.words[0]);
}

#define CARBON_PRINT_DEFAULT_PRINTER(name, offset, target, count, output_first)                         \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                                             \
    {                                                                                                   \
        return print_default_printer(#name, offset, count, output_first, ROMlib_carbon_##target, call); \
    }
#include "printing_printer_entries.h"
#undef CARBON_PRINT_DEFAULT_PRINTER
#undef CARBON_PRINT_PRINTER

int ROMlib_carbon_PMCreateSession(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "PMCreateSession", &ref))
        return 0;
    if (!use_print_profile(&ref, 0x8e280))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t slot, active, status, object, session;
    if (!cfp_get(US_TO_SYN68K(ref.toc) - 0x3c4, &slot) || !cfp_get(slot, &active))
        return 0;
    if (active)
        return print_result(call, session_not_open);
    const uint32_t output = call->words[0];
    if (!output)
        return print_result(call, paramErr);
    if (!print_worker(&ref, 0x845a8, &output, 1, &status))
        return 0;
    if (status)
        return print_result(call, status);
    if (!cfp_get(output, &object) || !cfp_write(slot, object) || !print_worker(&ref, 0x8448c, &object, 1, &session))
        return 0;
    if (!session)
        return print_result(call, print_internal_error);
    if (!print_worker(&ref, 0x895b8, &session, 1, &status))
        return 0;
    uint16_t* state = ROMlib_carbon_guest_memory(session + 0x58, 2, 1);
    if (!state)
        return 0;
    const uint16_t idle = CWC(1);
    memcpy(state, &idle, sizeof idle);
    const uint32_t reset[] = {session, noErr};
    if (!print_worker(&ref, 0x895d0, reset, 2, &status))
        return 0;
    return print_result(call, status);
}

#define CARBON_PRINT_LIFECYCLE(name, ...) int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call);
#define CARBON_PRINT_FORMAT(name, ...) CARBON_PRINT_LIFECYCLE(name)
#include "printing_lifecycle_entries.h"
#undef CARBON_PRINT_FORMAT
#undef CARBON_PRINT_LIFECYCLE

int ROMlib_carbon_PMSessionMakeOldPrintRecord(ROMlib_carbon_ppc_frame* call);
int ROMlib_carbon_PMSessionConvertOldPrintRecord(ROMlib_carbon_ppc_frame* call);

struct legacy_print_operation {
    const char* name;
    uint32_t entry_offset;
    int (*target)(ROMlib_carbon_ppc_frame*);
    unsigned argument_count;
};

enum staged_print_dialog_kind {
    staged_setup_init,
    staged_job_init,
    staged_legacy_job_init,
    staged_setup_main,
    staged_job_main,
};

static int finish_session_operation(const ROMlib_carbon_reference* ref, uint32_t session, uint32_t status,
                                    ROMlib_carbon_ppc_frame* call);

static int print_staged_dialog(const char* name, uint32_t offset, uint32_t worker, enum staged_print_dialog_kind kind,
                               ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const int legacy = kind == staged_legacy_job_init;
    const int initializing = kind <= staged_legacy_job_init;
    const int job = kind == staged_job_init || kind == staged_job_main || legacy;
    uint32_t arguments[5];
    const unsigned count = initializing ? (job ? 4 : 3) : (job ? 5 : 4);
    uint32_t valid, status = noErr;
    if (legacy) {
        if (!print_worker(&ref, 0x81614, NULL, 0, &valid) || !default_print_session(&ref, arguments))
            return 0;
        arguments[1] = call->words[0];
        arguments[2] = 0;
        arguments[3] = call->words[1];
        if (!arguments[0])
            return finish_print_operation(&ref, session_not_open, call);
    }
    else {
        memcpy(arguments, call->words, count * sizeof(*arguments));
        if (!print_worker(&ref, 0x85768, arguments, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, invalid_session);
    }

    const unsigned format_index = job ? 2 : 1;
    if (job) {
        if (!ticket_check(&ref, print_settings, arguments[1], &valid))
            return 0;
        if (!(uint8_t)valid)
            return legacy ? finish_print_operation(&ref, invalid_settings, call) : print_result(call, invalid_settings);
    }
    if (!legacy) {
        if (!ticket_check(&ref, page_format, arguments[format_index], &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, invalid_page_format);
    }
    if (!arguments[count - 1])
        return legacy ? finish_print_operation(&ref, paramErr, call) : print_result(call, paramErr);
    if (initializing && !ROMlib_carbon_guest_memory(arguments[count - 1], 4, 1))
        return 0;
    if (!initializing && arguments[count - 2] && !ROMlib_carbon_guest_memory(arguments[count - 2], 1, 1))
        return 0;

    if (!print_worker(&ref, 0x8448c, arguments, 1, arguments) || !arguments[0])
        return 0;
    if (!legacy && !print_worker(&ref, 0x895b8, arguments, 1, &valid))
        return 0;
    uint16_t phase;
    if (!read_print_phase(arguments[0], &phase))
        return 0;
    if (phase != print_idle)
        status = session_not_open;
    else {
        if (initializing && ((job && !ticket_payload(&ref, print_settings, arguments + 1)) ||
                             (!legacy && !ticket_payload(&ref, page_format, arguments + format_index))))
            return 0;
        if (!print_worker(&ref, worker, arguments, count, &status))
            return 0;
    }
    return legacy ? finish_print_operation(&ref, status, call)
                  : finish_session_operation(&ref, arguments[0], status, call);
}

#define CARBON_PRINT_STAGED_DIALOG(name, offset, worker, kind)         \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)            \
    {                                                                  \
        return print_staged_dialog(#name, offset, worker, kind, call); \
    }
#include "printing_staged_dialog_entries.h"
#undef CARBON_PRINT_STAGED_DIALOG

static int print_legacy_session(const struct legacy_print_operation* op, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t session, valid;
    if (!default_print_session(&ref, &session))
        return 0;
    /* The print-record adapters support a null session. */
    if (op->target != ROMlib_carbon_PMSessionMakeOldPrintRecord &&
        op->target != ROMlib_carbon_PMSessionConvertOldPrintRecord) {
        if (!print_worker(&ref, 0x85768, &session, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, session_not_open);
    }
    memmove(call->words + 1, call->words, op->argument_count * sizeof(uint32_t));
    call->words[0] = session;
    return op->target(call);
}

#define CARBON_PRINT_LEGACY(name, offset, target, count)                                                \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                                             \
    {                                                                                                   \
        static const struct legacy_print_operation op = {#name, offset, ROMlib_carbon_##target, count}; \
        return print_legacy_session(&op, call);                                                         \
    }
#include "printing_legacy_entries.h"
#undef CARBON_PRINT_LEGACY

int ROMlib_carbon_PMBegin(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "PMBegin", &ref))
        return 0;
    if (!use_print_profile(&ref, 0x90998))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t slot;
    if (!cfp_get(US_TO_SYN68K(ref.toc) - 0x3c4, &slot))
        return 0;
    call->words[0] = slot;
    return ROMlib_carbon_PMCreateSession(call);
}

int ROMlib_carbon_PMEnd(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "PMEnd", &ref))
        return 0;
    if (!use_print_profile(&ref, 0x90a34))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t object, valid, session;
    if (!default_print_session(&ref, &object) || !print_worker(&ref, 0x85768, &object, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, session_not_open);
    if (!print_worker(&ref, 0x8448c, &object, 1, &session))
        return 0;
    const void* state_field = ROMlib_carbon_guest_memory(session + 0x58, 2, 0);
    if (!state_field)
        return 0;
    uint16_t state;
    memcpy(&state, state_field, sizeof state);
    if (CW(state) != 1)
        return print_result(call, session_not_open);
    call->words[0] = object;
    return ROMlib_carbon_PMRelease(call);
}

int ROMlib_carbon_PMIsPostScriptDriver(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "PMIsPostScriptDriver", &ref))
        return 0;
    if (!use_print_profile(&ref, 0x937fc))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t session, status, ignored;
    if (!print_worker(&ref, 0x81614, NULL, 0, &ignored) || !default_print_session(&ref, &session))
        return 0;
    if (!session)
        status = session_not_open;
    else if (!call->words[0])
        status = paramErr;
    else if (!print_worker(&ref, 0x84368, call->words, 1, &status))
        return 0;
    return finish_print_operation(&ref, status, call);
}

static int print_record_conversion(const char* name, uint32_t offset, int make_record, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t arguments[4], status = noErr, valid, session = 0;
    memcpy(arguments, call->words, sizeof arguments);
    if (make_record && !print_worker(&ref, 0x81614, NULL, 0, &valid))
        return 0;
    if (arguments[0]) {
        if (!print_worker(&ref, 0x85768, arguments, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, invalid_session);
    }
    if (make_record) {
        for (unsigned i = 1; i <= 2; ++i) {
            const enum print_ticket_type type = i == 1 ? print_settings : page_format;
            if (!ticket_check(&ref, type, arguments[i], &valid))
                return 0;
            if (!(uint8_t)valid)
                return finish_print_operation(&ref, i == 1 ? invalid_settings : invalid_page_format, call);
        }
    }
    if (!arguments[3] || (!make_record && (!arguments[1] || !arguments[2])))
        return make_record ? finish_print_operation(&ref, paramErr, call) : print_result(call, paramErr);
    if (make_record) {
        if (!ticket_payload(&ref, print_settings, arguments + 1) || !ticket_payload(&ref, page_format, arguments + 2) ||
            !print_worker(&ref, 0x84f24, arguments + 1, 3, &status))
            return 0;
        return finish_print_operation(&ref, status, call);
    }
    if (arguments[0]) {
        if (!print_worker(&ref, 0x8448c, arguments, 1, &session) || !print_worker(&ref, 0x895b8, &session, 1, &valid))
            return 0;
    }
    arguments[0] = session;
    if (!print_worker(&ref, 0x84d50, arguments, 4, &status))
        return 0;
    if (session) {
        const uint32_t normalized[] = {session, status};
        if (!print_worker(&ref, 0x895d0, normalized, 2, &status))
            return 0;
    }
    return print_result(call, status);
}

int ROMlib_carbon_PMSessionMakeOldPrintRecord(ROMlib_carbon_ppc_frame* call)
{
    return print_record_conversion("PMSessionMakeOldPrintRecord", 0x8f340, 1, call);
}

int ROMlib_carbon_PMSessionConvertOldPrintRecord(ROMlib_carbon_ppc_frame* call)
{
    return print_record_conversion("PMSessionConvertOldPrintRecord", 0x8f238, 0, call);
}

enum print_value_kind {
    from_cstring,
    from_pascal_string,
    from_double,
    from_int32,
    from_page_rect,
    from_resolution,
    to_page_rect,
    to_resolution,
    from_index,
    from_array,
    to_cstring,
    is_string_or_array,
    copy_preference_array,
    get_pascal_job_name,
    set_pascal_job_name,
    read_print_data,
    write_print_data,
    read_print_plist,
    write_print_plist,
    get_ticket_typed_item,
    find_ticket_type,
    create_subticket_key,
    find_subticket,
    find_all_subtickets,
    search_ticket_leaf,
    search_ticket_node,
    validate_ticket_item,
    build_ticket_item_dictionary,
    add_ticket_item,
    update_ticket_item,
    get_ticket_item_array,
    get_ticket_item_lock,
    set_ticket_item_lock,
    replace_ticket_item_lock,
    set_ticket_retain_count,
    lock_print_ticket,
    unlock_print_ticket,
    remove_ticket_metadata,
    set_print_profile,
    create_print_mutex,
    retain_print_mutex,
    release_print_mutex,
    lock_print_mutex,
    try_print_mutex,
    unlock_print_mutex,
};

struct print_value_operation {
    const char* name;
    uint32_t entry_offset;
    enum print_value_kind kind;
};

static int write_print_value(ROMlib_carbon_ppc_frame* call, uint32_t output, uint32_t value)
{
    if (!cfp_write(output, value)) {
        if (value)
            cfp_call("CFRelease", &value, 1, NULL);
        return 0;
    }
    return print_result(call, value ? noErr : memFullErr);
}

static int make_print_array(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, unsigned count)
{
    const uint32_t allocator = call->words[0], record = call->words[1], output = call->words[2];
    if (!record || !output)
        return print_result(call, paramErr);
    const void* fields = ROMlib_carbon_guest_memory(record, 8 * count, 0);
    if (!fields)
        return 0;
    uint32_t callbacks;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x2e68, &callbacks))
        return 0;
    const uint32_t array = ROMlib_cfarray_create_mutable(allocator, 0, callbacks);
    if (!array)
        return write_print_value(call, output, 0);
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t number = ROMlib_cfnumber_create(allocator, 13, record + 8 * i);
        if (!number) {
            if (!cfp_call("CFRelease", &array, 1, NULL))
                return 0;
            return write_print_value(call, output, 0);
        }
        ROMlib_cfarray_append(array, number);
        if (!cfp_call("CFRelease", &number, 1, NULL))
            return 0;
    }
    return write_print_value(call, output, array);
}

static int read_print_array(ROMlib_carbon_ppc_frame* call, unsigned count)
{
    const uint32_t array = call->words[0], output = call->words[1];
    if (!array || !output)
        return print_result(call, paramErr);
    void* fields = ROMlib_carbon_guest_memory(output, 8 * count, 1);
    if (!fields)
        return 0;
    uint32_t length, number;
    if (!cfp_call("CFArrayGetCount", &array, 1, &length))
        return 0;
    if (length < count)
        return print_result(call, print_internal_error);
    for (unsigned i = 0; i < count; ++i) {
        const uint32_t arguments[] = {array, i};
        if (!cfp_call("CFArrayGetValueAtIndex", arguments, 2, &number))
            return 0;
        if (!(uint8_t)ROMlib_cfnumber_value(number, 13, output + 8 * i))
            return print_result(call, print_internal_error);
    }
    return print_result(call, noErr);
}

static int make_print_object_array(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t allocator = call->words[0], values = call->words[1], output = call->words[3];
    const int32_t count = call->words[2];
    if (!output || (count > 0 && (!values || (uint32_t)count > SIZE_MAX / 4)))
        return print_result(call, paramErr);
    const uint8_t* elements = NULL;
    if (count > 0) {
        elements = ROMlib_carbon_guest_memory(values, (size_t)count * 4, 0);
        if (!elements)
            return 0;
    }
    uint32_t callbacks;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x2e68, &callbacks))
        return 0;
    const uint32_t array = ROMlib_cfarray_create_mutable(allocator, 0, callbacks);
    if (array) {
        for (int32_t i = 0; i < count; ++i) {
            uint32_t word;
            memcpy(&word, elements + (size_t)i * 4, sizeof word);
            ROMlib_cfarray_append(array, CL(word));
        }
    }
    return write_print_value(call, output, array);
}

static int print_string_or_array(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t object = call->words[0];
    uint32_t type, string_type, array_type, count;
    if (!object)
        return print_result(call, 0);
    if (!cfp_call("CFGetTypeID", &object, 1, &type) || !cfp_call("CFStringGetTypeID", NULL, 0, &string_type))
        return 0;
    if (type == string_type)
        return print_result(call, 1);
    if (!cfp_call("CFArrayGetTypeID", NULL, 0, &array_type))
        return 0;
    if (type != array_type)
        return print_result(call, 0);
    if (!cfp_call("CFArrayGetCount", &object, 1, &count))
        return 0;
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t arguments[] = {object, index};
        uint32_t value, value_type;
        if (!cfp_call("CFArrayGetValueAtIndex", arguments, 2, &value))
            return 0;
        if (!value)
            return print_result(call, 0);
        if (!cfp_call("CFGetTypeID", &value, 1, &value_type))
            return 0;
        if (value_type != string_type)
            return print_result(call, 0);
    }
    return print_result(call, count != 0);
}

static int copy_print_preference_array(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[4];
    if (!output)
        return print_result(call, paramErr);
    uint32_t value, type, array_type;
    if (!cfp_call("CFPreferencesCopyValue", call->words, 4, &value))
        return 0;
    if (!value)
        return print_result(call, paramErr);
    if (!cfp_call("CFGetTypeID", &value, 1, &type) || !cfp_call("CFArrayGetTypeID", NULL, 0, &array_type))
        return 0;
    const uint32_t array = type == array_type ? ROMlib_cfarray_create_mutable_copy(0, 0, value) : 0;
    if (!cfp_call("CFRelease", &value, 1, NULL))
        return 0;
    return type == array_type ? write_print_value(call, output, array) : print_result(call, paramErr);
}

static int make_print_cstring(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t text = call->words[0], output = call->words[1], encoding = call->words[2];
    if (!text || !output)
        return print_result(call, print_internal_error);
    if (!ROMlib_carbon_guest_memory(output, 4, 1))
        return 0;
    uint32_t length, size, buffer;
    if (!cfp_call("CFStringGetLength", &text, 1, &length))
        return 0;
    const uint32_t encoding_arguments[] = {length, encoding};
    if (!cfp_call("CFStringGetMaximumSizeForEncoding", encoding_arguments, 2, &size))
        return 0;
    if (size >= INT32_MAX)
        return print_result(call, print_internal_error);
    ++size;
    if (!print_worker(ref, 0xb6fd8, &size, 1, &buffer)) /* StdCLib malloc */
        return 0;
    if (!buffer)
        return print_result(call, memFullErr);
    const uint32_t copy[] = {text, buffer, size, encoding};
    uint32_t converted;
    const int ok = cfp_call("CFStringGetCString", copy, 4, &converted);
    if (ok && (uint8_t)converted && cfp_write(output, buffer))
        return print_result(call, noErr);
    if (!print_worker(ref, 0xb7008, &buffer, 1, NULL)) /* StdCLib free */
        return 0;
    return ok && !(uint8_t)converted && print_result(call, print_internal_error);
}

static int print_pascal_job_name(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int setting)
{
    const uint32_t settings = call->words[0], name_buffer = call->words[1];
    if (setting) {
        uint32_t session;
        if (!default_print_session(ref, &session))
            return 0;
        if (!session)
            return print_result(call, session_not_open);
        if (!name_buffer)
            return print_result(call, paramErr);
        const uint8_t* length = ROMlib_carbon_guest_memory(name_buffer, 1, 0);
        if (!length)
            return 0;
        const unsigned size = *length + 1;
        uint8_t* bytes = ROMlib_carbon_guest_memory(name_buffer, size, 1);
        if (!bytes)
            return 0;
        for (unsigned i = 1; i < size; ++i) {
            if (bytes[i] == ':')
                bytes[i] = '-';
        }
        uint32_t text = ROMlib_cfstring_pascal(0, name_buffer, 0);
        if (!text)
            return print_result(call, print_internal_error);
        ROMlib_carbon_ppc_frame worker = {.words = {settings, text}};
        const int ok = ROMlib_carbon_PMSetJobNameCFString(&worker);
        return cfp_call("CFRelease", &text, 1, NULL) && ok && print_result(call, worker.words[0]);
    }

    _Alignas(16) uint32_t text = 0;
    ROMlib_carbon_ppc_frame worker = {.words = {settings, name_buffer ? US_TO_SYN68K(&text) : 0}};
    if (!ROMlib_carbon_PMGetJobNameCFString(&worker))
        return 0;
    if (worker.words[0])
        return print_result(call, worker.words[0]);
    const uint32_t arguments[] = {CL(text), name_buffer, 256, 0};
    uint32_t converted;
    const int ok = cfp_call("CFStringGetPascalString", arguments, 4, &converted);
    return cfp_call("CFRelease", arguments, 1, NULL) && ok &&
           print_result(call, (uint8_t)converted ? noErr : print_internal_error);
}

/* Interpret the guest C string as a Classic filename or colon-delimited
   path. File Manager supplies the current volume and working directory. */
static OSErr print_file_spec(uint32_t path, FSSpec* spec)
{
    Str255 name;
    for (unsigned length = 0; length <= 255; ++length) {
        if (path > UINT32_MAX - length)
            break;
        const Byte* character = ROMlib_carbon_try_guest_memory(path + length, 1, 0);
        if (!character)
            break;
        if (!*character) {
            if (!length)
                break;
            name[0] = length;
            return C_FSMakeFSSpec(0, 0, name, spec);
        }
        if (length < 255)
            name[length + 1] = *character;
    }
    return bdNamErr;
}

static uint32_t read_print_file(FSSpec* spec)
{
    INTEGER reference;
    if (C_FSpOpenDF(spec, fsRdPerm, &reference))
        return 0;
    reference = CW(reference);
    LONGINT length;
    uint32_t data = 0;
    if (!GetEOF(reference, &length) && length > 0) {
        Ptr bytes = NewPtr(length);
        if (bytes) {
            LONGINT transferred = length;
            if (!FSRead(reference, &transferred, bytes) && transferred == length)
                data = ROMlib_cfdata_create(0, US_TO_SYN68K(bytes), length);
            DisposPtr(bytes);
        }
    }
    const OSErr error = FSClose(reference);
    if (error && data) {
        cfp_call("CFRelease", &data, 1, NULL);
        data = 0;
    }
    return data;
}

static OSErr write_print_file(FSSpec* spec, OSErr lookup, uint32_t data)
{
    uint32_t length, bytes = 0;
    if (!cfp_call("CFDataGetLength", &data, 1, &length) || length > INT32_MAX ||
        (length && !cfp_call("CFDataGetBytePtr", &data, 1, &bytes)))
        return print_internal_error;
    void* contents = bytes && length ? ROMlib_carbon_guest_memory(bytes, length, 0) : NULL;
    if (length && !contents)
        return print_internal_error;
    if (lookup == fnfErr && C_FSpCreate(spec, 0, 0, -1))
        return print_internal_error;
    INTEGER reference;
    if (C_FSpOpenDF(spec, fsWrPerm, &reference))
        return print_internal_error;
    reference = CW(reference);
    /* Classic stdio opens in write mode before rejecting a zero-byte payload. */
    LONGINT amount = length;
    OSErr status = SetEOF(reference, 0);
    if (!status && length)
        status = FSWrite(reference, &amount, contents);
    const OSErr close_status = FSClose(reference);
    return !status && !close_status && length && amount == (LONGINT)length ? noErr : print_internal_error;
}

static int print_file_value(ROMlib_carbon_ppc_frame* call, int reading, int property_list)
{
    const uint32_t path = call->words[0], argument = call->words[1];
    if (!path || !argument) {
        if (reading && argument && !cfp_write(argument, 0))
            return 0;
        return print_result(call, paramErr);
    }
    if (reading && !ROMlib_carbon_guest_memory(argument, 4, 1))
        return 0;
    FSSpec spec;
    const OSErr lookup = print_file_spec(path, &spec);
    uint32_t data = 0;
    if (reading) {
        if (!lookup)
            data = read_print_file(&spec);
        if (property_list && data) {
            const uint32_t arguments[] = {0, data, 0, 0};
            uint32_t value;
            const int ok = cfp_call("CFPropertyListCreateFromXMLData", arguments, 4, &value);
            if (!cfp_call("CFRelease", &data, 1, NULL) || !ok)
                return 0;
            data = value;
        }
        if (!cfp_write(argument, data)) {
            if (data)
                cfp_call("CFRelease", &data, 1, NULL);
            return 0;
        }
        return print_result(call, data ? noErr : print_internal_error);
    }

    if (lookup && lookup != fnfErr)
        return print_result(call, print_internal_error);
    data = argument;
    if (property_list) {
        const uint32_t arguments[] = {0, argument};
        if (!cfp_call("CFPropertyListCreateXMLData", arguments, 2, &data))
            return 0;
        if (!data)
            return print_result(call, print_internal_error);
    }
    const OSErr status = write_print_file(&spec, lookup, data);
    return (!property_list || cfp_call("CFRelease", &data, 1, NULL)) && print_result(call, status);
}

static int print_typed_ticket_item(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[5];
    const uint32_t arguments[] = {call->words[0], call->words[1], call->words[2], call->words[3], output};
    uint32_t status, item, type;
    if (!print_worker(ref, 0x807cc, arguments, 5, &status))
        return 0;
    if (status || !output)
        return print_result(call, status);
    if (!cfp_get(output, &item))
        return 0;
    if (!item)
        return print_result(call, noErr);
    if (!cfp_call("CFGetTypeID", &item, 1, &type))
        return 0;
    return print_result(call, type == call->words[4] ? noErr : -30893);
}

static int print_ticket_retain_count(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0];
    const int32_t target = call->words[1];
    if (target < 0)
        return print_result(call, paramErr);
    uint32_t status, count;
    if (!print_worker(ref, 0x7fed8, &ticket, 1, &status))
        return 0;
    if (status)
        return print_result(call, status);
    if (!cfp_call("CFGetRetainCount", &ticket, 1, &count))
        return 0;
    while (count != (uint32_t)target) {
        const int retain = count < (uint32_t)target;
        uint32_t retained;
        if (!cfp_call(retain ? "CFRetain" : "CFRelease", &ticket, 1, &retained))
            return 0;
        if (retain && !retained)
            return print_result(call, memFullErr);
        count = retain ? count + 1 : count - 1;
    }
    return print_result(call, noErr);
}

static int print_ticket_lock_flag(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int locked)
{
    const uint32_t ticket = call->words[0];
    if (!ticket)
        return print_result(call, paramErr);
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    uint32_t strings, key, boolean_global, value;
    if (!cfp_get(toc - 0xfc, &strings) || !strings || !cfp_get(toc - (locked ? 0x2e14 : 0x2dac), &boolean_global) ||
        !boolean_global || !cfp_get(boolean_global, &value))
        return 0;
    const uint32_t key_address = strings + 0x44;
    if (!cfp_call("__CFStringMakeConstantString", &key_address, 1, &key) || !key)
        return 0;
    const uint32_t arguments[] = {ticket, key, value};
    return cfp_call("CFDictionarySetValue", arguments, 3, NULL) && print_result(call, noErr);
}

static int print_item_key(const ROMlib_carbon_reference* ref, uint32_t offset, uint32_t* key)
{
    uint32_t strings;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x104, &strings) || !strings)
        return 0;
    const uint32_t address = strings + offset;
    return cfp_call("__CFStringMakeConstantString", &address, 1, key) && *key;
}

static int print_find_ticket_type(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t type = call->words[0];
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 2, 1) : NULL;
    if (!output)
        return print_result(call, paramErr);
    static const uint16_t names[] = {0x1e7, 0x201, 0x220, 0x23b, 0x25c, 0x280, 0x2a2, 0x2c4, 0x2e9, 0x30a, 0x325};
    int16_t result = -1;
    if (type) {
        for (unsigned i = 0; i < NELEM(names); ++i) {
            uint32_t name, order;
            if (!print_item_key(ref, names[i], &name))
                return 0;
            const uint32_t compare[] = {type, name, 0};
            if (!cfp_call("CFStringCompare", compare, 3, &order))
                return 0;
            if (!order) {
                result = i + 1;
                break;
            }
        }
    }
    const uint16_t encoded = CW(result);
    memcpy(output, &encoded, sizeof encoded);
    return print_result(call, type ? noErr : paramErr);
}

static int print_item_valid(uint32_t item, uint32_t* valid)
{
    *valid = 0;
    if (!item)
        return 1;
    uint32_t type;
    if (!cfp_call("CFGetTypeID", &item, 1, &type))
        return 0;
    static const char* const allowed[] = {
        "CFStringGetTypeID", "CFNumberGetTypeID", "CFDataGetTypeID",       "CFBooleanGetTypeID",
        "CFDateGetTypeID",   "CFArrayGetTypeID",  "CFDictionaryGetTypeID",
    };
    for (unsigned i = 0; i < NELEM(allowed); ++i) {
        uint32_t candidate;
        if (!cfp_call(allowed[i], NULL, 0, &candidate))
            return 0;
        if (type == candidate) {
            *valid = 1;
            break;
        }
    }
    return 1;
}

static int print_item_add(uint32_t dictionary, uint32_t key, uint32_t value)
{
    const uint32_t args[] = {dictionary, key, value};
    return cfp_call("CFDictionaryAddValue", args, 3, NULL);
}

static int print_build_item(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t allocator = call->words[0], client = call->words[1], key = call->words[2], value = call->words[3];
    const uint32_t output = call->words[6];
    if (!client || !key || !value || !output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return print_result(call, paramErr);

    const uint32_t toc = US_TO_SYN68K(ref->toc);
    uint32_t callbacks[2];
    if (!cfp_get(toc - 0x2dec, callbacks) || !cfp_get(toc - 0x2e34, callbacks + 1))
        return 0;
    const uint32_t create[] = {allocator, 0, callbacks[0], callbacks[1]};
    uint32_t item;
    if (!cfp_call("CFDictionaryCreateMutable", create, 4, &item))
        return 0;
    if (!item)
        return print_result(call, memFullErr);

    ROMlib_carbon_ppc_frame stamp = {.words = {allocator}, .stack = call->stack};
    const double now = ROMlib_carbon_cf_absolute_time();
    memcpy(stamp.floating, &now, sizeof now);
    ROMlib_carbon_reference date_function;
    uint32_t date = 0, state = 0, date_key, client_key, state_key;
    int ok = ROMlib_carbon_reference_function("CoreFoundationLib", "CFDateCreate", &date_function) &&
             ROMlib_carbon_invoke_frame(date_function.function, &stamp);
    if (ok)
        date = stamp.words[0];
    const unsigned flags = ((uint8_t)call->words[4] ? 1 : 0) | ((uint8_t)call->words[5] ? 2 : 0);
    _Alignas(16) uint32_t word = CL(flags);
    if (ok && date)
        state = ROMlib_cfnumber_create(allocator, 3, US_TO_SYN68K(&word));
    if (ok && date && state)
        ok = print_item_key(ref, 0, &date_key) && print_item_key(ref, 0x1f, &client_key) &&
             print_item_key(ref, 0x3d, &state_key) && print_item_add(item, date_key, date) &&
             print_item_add(item, client_key, client) && print_item_add(item, key, value) &&
             print_item_add(item, state_key, state);
    if (date)
        ok = cfp_call("CFRelease", &date, 1, NULL) && ok;
    if (state)
        ok = cfp_call("CFRelease", &state, 1, NULL) && ok;
    if (ok && date && state && cfp_write(output, item))
        return print_result(call, noErr);
    const int released = cfp_call("CFRelease", &item, 1, NULL);
    return ok && (!date || !state) && released && print_result(call, memFullErr);
}

static int print_add_ticket_item(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0], client = call->words[1], key = call->words[2], value = call->words[3];
    if (!ticket || !client || !key)
        return print_result(call, paramErr);
    const uint32_t lookup[] = {ticket, key};
    uint32_t present, valid;
    if (!cfp_call("CFDictionaryContainsKey", lookup, 2, &present))
        return 0;
    if (present)
        return print_result(call, -9590);
    if (!print_item_valid(value, &valid))
        return 0;
    if (!valid)
        return print_result(call, -30892);

    _Alignas(16) uint32_t allocator_word = 0, item_word = 0;
    const uint32_t get_allocator[] = {ticket, US_TO_SYN68K(&allocator_word)};
    uint32_t status;
    if (!print_worker(ref, 0x7ffc8, get_allocator, 2, &status))
        return 0;
    if (status)
        return print_result(call, status);
    const uint32_t allocator = CL(allocator_word);
    ROMlib_carbon_ppc_frame builder = {
        .words = {allocator, client, key, value, call->words[4], 0, US_TO_SYN68K(&item_word)}, .stack = call->stack};
    if (!print_build_item(ref, &builder))
        return 0;
    if (builder.words[0])
        return print_result(call, builder.words[0]);

    const uint32_t item = CL(item_word);
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    uint32_t array_callbacks, key_callbacks, value_callbacks;
    uint32_t versions = 0, entry = 0, client_key, versions_key;
    int ok = cfp_get(toc - 0x2e68, &array_callbacks) && cfp_get(toc - 0x2dec, &key_callbacks) &&
             cfp_get(toc - 0x2e34, &value_callbacks) && print_item_key(ref, 0x7f, &client_key) &&
             print_item_key(ref, 0x5e, &versions_key);
    if (ok) {
        const uint32_t create_array[] = {allocator, 0, array_callbacks};
        ok = cfp_call("CFArrayCreateMutable", create_array, 3, &versions);
    }
    if (ok && versions) {
        const uint32_t append[] = {versions, item};
        const uint32_t create_dict[] = {allocator, 0, key_callbacks, value_callbacks};
        ok = cfp_call("CFArrayAppendValue", append, 2, NULL) &&
             cfp_call("CFDictionaryCreateMutable", create_dict, 4, &entry);
    }
    if (ok && entry)
        ok = print_item_add(entry, client_key, client) && print_item_add(entry, versions_key, versions) &&
             print_item_add(ticket, key, entry);
    if (entry)
        ok = cfp_call("CFRelease", &entry, 1, NULL) && ok;
    if (versions)
        ok = cfp_call("CFRelease", &versions, 1, NULL) && ok;
    return cfp_call("CFRelease", &item, 1, NULL) && ok && print_result(call, entry ? noErr : memFullErr);
}

static int print_item_array(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t array_out = call->words[2], item_out = call->words[3];
    if (!call->words[0] || !call->words[1] || !array_out || !item_out ||
        !ROMlib_carbon_try_guest_memory(array_out, 4, 1) || !ROMlib_carbon_try_guest_memory(item_out, 4, 1))
        return print_result(call, paramErr);

    uint32_t entry, array = 0, item = 0, count;
    if (!cfp_call("CFDictionaryGetValue", call->words, 2, &entry))
        return 0;
    if (entry) {
        uint32_t key;
        if (!print_item_key(ref, 0x5e, &key))
            return 0;
        const uint32_t lookup[] = {entry, key};
        if (!cfp_call("CFDictionaryGetValue", lookup, 2, &array))
            return 0;
        if (array) {
            if (!cfp_call("CFArrayGetCount", &array, 1, &count))
                return 0;
            if (count) {
                const uint32_t last[] = {array, count - 1};
                if (!cfp_call("CFArrayGetValueAtIndex", last, 2, &item))
                    return 0;
            }
        }
    }
    return cfp_write(array_out, item ? array : 0) && cfp_write(item_out, item) &&
           print_result(call, item    ? noErr
                              : entry ? -30889
                                      : -9589);
}

static int print_item_lock(const ROMlib_carbon_reference* ref, uint32_t item, uint8_t* output, unsigned setting,
                           int32_t* status)
{
    *status = noErr;
    uint32_t key, number;
    if (!print_item_key(ref, 0x3d, &key))
        return 0;
    const uint32_t lookup[] = {item, key};
    if (!cfp_call("CFDictionaryGetValue", lookup, 2, &number))
        return 0;
    if (!number) {
        if (output)
            *output = 0;
        *status = -30889;
        return 1;
    }

    _Alignas(16) uint32_t word = 0;
    const uint32_t read[] = {number, 3, US_TO_SYN68K(&word)};
    if (!cfp_call("CFNumberGetValue", read, 3, NULL))
        return 0;
    uint32_t flags = CL(word);
    if (output) {
        *output = flags & 1;
        return 1;
    }

    word = CL((flags & ~UINT32_C(1)) | (setting == 1));
    uint32_t allocator;
    if (!cfp_call("CFGetAllocator", &item, 1, &allocator))
        return 0;
    number = ROMlib_cfnumber_create(allocator, 3, US_TO_SYN68K(&word));
    if (!number) {
        *status = memFullErr;
        return 1;
    }
    const uint32_t write[] = {item, key, number};
    const int ok = cfp_call("CFDictionarySetValue", write, 3, NULL);
    const int released = cfp_call("CFRelease", &number, 1, NULL);
    return ok && released;
}

static int print_item_lock_state(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call, int reading)
{
    const uint32_t item = call->words[0];
    uint8_t* output = NULL;
    if (reading && call->words[1])
        output = ROMlib_carbon_try_guest_memory(call->words[1], 1, 1);
    if (!item || (reading && !output))
        return print_result(call, paramErr);
    int32_t status;
    return print_item_lock(ref, item, output, (uint8_t)call->words[1], &status) && print_result(call, status);
}

static int print_replace_item_lock(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t item = call->words[0], array = call->words[1], client = call->words[3];
    if (!item || !array || !client)
        return print_result(call, paramErr);
    const uint32_t copy_args[] = {0, 0, item};
    uint32_t copy;
    if (!cfp_call("CFDictionaryCreateMutableCopy", copy_args, 3, &copy))
        return 0;
    if (!copy)
        return print_result(call, memFullErr);

    int32_t status;
    int ok = print_item_lock(ref, copy, NULL, (uint8_t)call->words[4], &status);
    if (ok && !status) {
        uint32_t key;
        if (!print_item_key(ref, 0x1f, &key))
            ok = 0;
        else {
            const uint32_t insert[] = {copy, key, client};
            const uint32_t replace[] = {array, 0, copy};
            ok = cfp_call("CFDictionaryAddValue", insert, 3, NULL) &&
                 cfp_call("CFArraySetValueAtIndex", replace, 3, NULL);
        }
    }
    const int released = cfp_call("CFRelease", &copy, 1, NULL);
    return released && ok && print_result(call, status);
}

static int print_update_ticket_item(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0], client = call->words[1], key = call->words[2], value = call->words[3];
    if (!ticket || !client || !key || !value)
        return print_result(call, paramErr);

    _Alignas(16) uint32_t allocator = 0, array = 0, previous = 0, replacement = 0;
    const uint32_t get_allocator[] = {ticket, US_TO_SYN68K(&allocator)};
    uint32_t status;
    if (!print_worker(ref, 0x7ffc8, get_allocator, 2, &status))
        return 0;
    if (status)
        return print_result(call, status);

    ROMlib_carbon_ppc_frame lookup = {.words = {ticket, key, US_TO_SYN68K(&array), US_TO_SYN68K(&previous)}};
    if (!print_item_array(ref, &lookup))
        return 0;
    if (lookup.words[0])
        return print_result(call, lookup.words[0]);

    uint8_t locked;
    int32_t lock_status;
    if (!print_item_lock(ref, CL(previous), &locked, 0, &lock_status))
        return 0;
    if (lock_status)
        return print_result(call, lock_status);
    if (locked)
        return print_result(call, -9586);

    ROMlib_carbon_ppc_frame builder = {
        .words = {CL(allocator), client, key, value, call->words[4], call->words[5], US_TO_SYN68K(&replacement)},
        .stack = call->stack};
    if (!print_build_item(ref, &builder))
        return 0;
    if (builder.words[0])
        return print_result(call, builder.words[0]);

    const uint32_t replace[] = {CL(array), 0, CL(replacement)};
    const int ok = cfp_call("CFArraySetValueAtIndex", replace, 3, NULL);
    return cfp_call("CFRelease", replace + 2, 1, NULL) && ok && print_result(call, noErr);
}

/* The key names are indexed by the saved ticket type enumeration (1–11). */
static int print_create_subticket_key(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    static const uint16_t names[] = {0x11a, 0x125, 0x135, 0x141, 0x154, 0x16a, 0x17e, 0x191, 0x1a8, 0x1bb, 0x1d1};
    const uint32_t allocator = call->words[0], output = call->words[2];
    const int16_t type = call->words[1];
    const int32_t index = call->words[3];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return print_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    if (type < 1 || (unsigned)type > NELEM(names))
        return print_result(call, type == -1 ? paramErr : memFullErr);

    uint32_t prefix, name, text;
    if (!print_item_key(ref, 0x9e, &prefix) || !print_item_key(ref, names[type - 1], &name))
        return 0;
    const uint32_t create[] = {allocator, 0};
    if (!cfp_call("CFStringCreateMutable", create, 2, &text))
        return 0;
    if (!text)
        return print_result(call, memFullErr);
    const uint32_t append_prefix[] = {text, prefix}, append_name[] = {text, name};
    int ok = cfp_call("CFStringAppend", append_prefix, 2, NULL) && cfp_call("CFStringAppend", append_name, 2, NULL);
    if (ok && index) {
        _Alignas(16) char suffix[16];
        snprintf(suffix, sizeof suffix, " %d", index);
        const uint32_t append_number[] = {text, US_TO_SYN68K(suffix), 0x08000100};
        ok = cfp_call("CFStringAppendCString", append_number, 3, NULL);
    }
    if (ok && cfp_write(output, text))
        return print_result(call, noErr);
    cfp_call("CFRelease", &text, 1, NULL);
    return 0;
}

static int print_find_subticket(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0], output = call->words[4];
    if (!ticket || !output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return print_result(call, paramErr);
    _Alignas(16) uint32_t key_word = 0;
    ROMlib_carbon_ppc_frame key_call = {
        .words = {call->words[1], call->words[2], US_TO_SYN68K(&key_word), call->words[3]}};
    if (!print_create_subticket_key(ref, &key_call))
        return 0;
    if (key_call.words[0])
        return print_result(call, key_call.words[0]);
    const uint32_t lookup[] = {ticket, CL(key_word)};
    uint32_t result;
    const int ok = cfp_call("CFDictionaryGetValue", lookup, 2, &result);
    return cfp_call("CFRelease", lookup + 1, 1, NULL) && ok && cfp_write(output, result) &&
           print_result(call, result ? noErr : -9583);
}

static int print_search_leaf(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0], key = call->words[1], output = call->words[2];
    if (!ticket || !key || (output && !ROMlib_carbon_try_guest_memory(output, 4, 1)))
        return print_result(call, paramErr);

    _Alignas(16) uint32_t array_word = 0, item_word = 0, flags = 0;
    ROMlib_carbon_ppc_frame lookup = {.words = {ticket, key, US_TO_SYN68K(&array_word), US_TO_SYN68K(&item_word)}};
    if (!print_item_array(ref, &lookup))
        return 0;
    uint32_t status = lookup.words[0], value = 0;
    const uint32_t item = CL(item_word);
    if (!status) {
        uint32_t state_key, state;
        if (!print_item_key(ref, 0x3d, &state_key))
            return 0;
        const uint32_t get_state[] = {item, state_key};
        if (!cfp_call("CFDictionaryGetValue", get_state, 2, &state))
            return 0;
        if (!state)
            status = -30889;
        else {
            const uint32_t read[] = {state, 3, US_TO_SYN68K(&flags)};
            if (!cfp_call("CFNumberGetValue", read, 3, NULL))
                return 0;
            if (CL(flags) & 2)
                status = -9589;
            else if (output) {
                const uint32_t read_item[] = {item, key};
                if (!cfp_call("CFDictionaryGetValue", read_item, 2, &value))
                    return 0;
            }
        }
    }
    return (!output || cfp_write(output, value)) && print_result(call, status);
}

static int print_search_node(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    enum { item_missing = -9589, subticket_missing = -9583 };

    ROMlib_carbon_ppc_frame leaf = {.words = {call->words[0], call->words[2], call->words[3]}};
    if (!print_search_leaf(ref, &leaf))
        return 0;
    if (leaf.words[0] != (uint32_t)item_missing)
        return print_result(call, leaf.words[0]);

    static const uint16_t branches[] = {4, 5}; /* Page format, then print settings. */
    _Alignas(16) uint32_t child = 0, paper = 0;
    for (unsigned i = 0; i < NELEM(branches); ++i) {
        ROMlib_carbon_ppc_frame sub = {.words = {call->words[0], call->words[1], branches[i], 0, US_TO_SYN68K(&child)}};
        if (!print_find_subticket(ref, &sub))
            return 0;
        if (sub.words[0] == (uint32_t)subticket_missing)
            continue;
        if (sub.words[0])
            return print_result(call, sub.words[0]);

        leaf.words[0] = CL(child);
        if (!print_search_leaf(ref, &leaf))
            return 0;
        if (leaf.words[0] != (uint32_t)item_missing)
            return print_result(call, leaf.words[0]);

        sub.words[0] = CL(child);
        sub.words[2] = 11; /* Paper information. */
        sub.words[4] = US_TO_SYN68K(&paper);
        if (!print_find_subticket(ref, &sub))
            return 0;
        if (sub.words[0] == (uint32_t)subticket_missing)
            continue;
        if (sub.words[0])
            return print_result(call, sub.words[0]);

        leaf.words[0] = CL(paper);
        if (!print_search_leaf(ref, &leaf))
            return 0;
        if (leaf.words[0] != (uint32_t)item_missing)
            return print_result(call, leaf.words[0]);
    }
    return print_result(call, item_missing);
}

struct print_ticket_ancestor {
    const struct print_ticket_ancestor* parent;
    uint32_t ticket;
};

static int print_append_subtickets(const ROMlib_carbon_reference* ref, uint32_t ticket, uint32_t tail_out,
                                   uint32_t prefix, const struct print_ticket_ancestor* parent, int32_t* status)
{
    for (const struct print_ticket_ancestor* item = parent; item; item = item->parent) {
        if (item->ticket == ticket) {
            *status = paramErr;
            return 1;
        }
    }
    uint32_t count;
    if (!cfp_call("CFDictionaryGetCount", &ticket, 1, &count))
        return 0;
    if (!count) {
        if (!parent)
            *status = -30889;
        return 1;
    }
    if (count > INT32_MAX / 4) {
        *status = memFullErr;
        return 1;
    }

    const uint32_t size = count * 4;
    uint32_t keys;
    if (!print_worker(ref, 0xb6fd8, &size, 1, &keys))
        return 0;
    if (!keys) {
        *status = memFullErr;
        return 1;
    }

    const struct print_ticket_ancestor ancestor = {parent, ticket};
    const uint32_t collect[] = {ticket, keys, 0};
    int ok = cfp_call("CFDictionaryGetKeysAndValues", collect, 3, NULL);
    for (uint32_t i = 0; ok && !*status && i < count; ++i) {
        uint32_t key, matches;
        if (!cfp_get(keys + 4 * i, &key)) {
            ok = 0;
            break;
        }
        const uint32_t compare[] = {key, prefix};
        if (!cfp_call("CFStringHasPrefix", compare, 2, &matches)) {
            ok = 0;
            break;
        }
        if (!matches)
            continue;

        uint32_t child, node, tail;
        const uint32_t lookup[] = {ticket, key}, node_size = 8;
        if (!cfp_call("CFDictionaryGetValue", lookup, 2, &child) || !cfp_get(tail_out, &tail) ||
            !print_worker(ref, 0xb6fd8, &node_size, 1, &node)) {
            ok = 0;
            break;
        }
        if (!node) {
            *status = memFullErr;
            break;
        }
        void* record = ROMlib_carbon_guest_memory(node, 8, 1);
        char* tail_record = ROMlib_carbon_guest_memory(tail, 8, 1);
        void* out = ROMlib_carbon_guest_memory(tail_out, 4, 1);
        if (!record || !tail_record || !out) {
            print_worker(ref, 0xb7008, &node, 1, NULL);
            ok = 0;
            break;
        }
        const uint32_t entry[] = {CL(child), 0};
        const uint32_t address = CL(node);
        memcpy(record, entry, sizeof entry);
        memcpy(tail_record + 4, &address, sizeof address);
        memcpy(out, &address, sizeof address);
        ok = print_append_subtickets(ref, child, tail_out, prefix, &ancestor, status);
    }
    return print_worker(ref, 0xb7008, &keys, 1, NULL) && ok;
}

static int print_find_all_subtickets(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t ticket = call->words[0], tail_out = call->words[1];
    if (!ticket || !tail_out || !ROMlib_carbon_try_guest_memory(tail_out, 4, 1))
        return print_result(call, paramErr);
    uint32_t tail, prefix;
    if (!cfp_get(tail_out, &tail))
        return 0;
    if (!tail || !ROMlib_carbon_try_guest_memory(tail, 8, 1))
        return print_result(call, paramErr);
    int32_t status = noErr;
    return print_item_key(ref, 0x9e, &prefix) &&
           print_append_subtickets(ref, ticket, tail_out, prefix, NULL, &status) && print_result(call, status);
}

static int print_remove_item_metadata(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t key = call->words[0], value = call->words[1], metadata = call->words[2];
    if (!key || !metadata)
        return 1;
    uint32_t protected;
    if (!print_worker(ref, 0xb4848, &key, 1, &protected))
        return 0;
    if (protected)
        return 1;

    if (value) {
        uint32_t type, dictionary;
        if (!cfp_call("CFGetTypeID", &value, 1, &type) || !cfp_call("CFDictionaryGetTypeID", NULL, 0, &dictionary))
            return 0;
        if (type == dictionary) {
            const uint32_t keys[] = {0xb9, 0x7f};
            for (unsigned i = 0; i < NELEM(keys); ++i) {
                uint32_t marker, contains;
                if (!print_item_key(ref, keys[i], &marker))
                    return 0;
                const uint32_t lookup[] = {value, marker};
                if (!cfp_call("CFDictionaryContainsKey", lookup, NELEM(lookup), &contains))
                    return 0;
                if (contains)
                    return 1;
            }
        }
    }
    const uint32_t remove[] = {metadata, key};
    return cfp_call("CFDictionaryRemoveValue", remove, NELEM(remove), NULL);
}

static int print_mutex_free(const ROMlib_carbon_reference* ref, uint32_t mutex, uint32_t data)
{
    const int released = !data || print_worker(ref, 0x9b768, &data, 1, NULL);
    return (!mutex || print_worker(ref, 0x9b768, &mutex, 1, NULL)) && released;
}

static int print_mutex_create(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t output = call->words[0];
    if (!output || !ROMlib_carbon_try_guest_memory(output, 4, 1))
        return print_result(call, paramErr);
    if (!cfp_write(output, 0))
        return 0;
    const uint32_t table = cfp_toc(ref, (unsigned)-0x38c);
    if (!table)
        return ROMlib_carbon_invoke_frame(ref->function, call);

    const uint32_t size = 8;
    uint32_t mutex = 0, data = 0;
    if (!print_worker(ref, 0x9b6dc, &size, 1, &mutex))
        return 0;
    if (!mutex)
        return print_result(call, memFullErr);
    const int allocated = print_worker(ref, 0x9b6dc, &size, 1, &data);
    if (allocated && data && cfp_write(mutex, table + 4) && cfp_write(mutex + 4, data) && cfp_write(data, 1) &&
        cfp_write(data + 4, 0) && cfp_write(output, mutex))
        return print_result(call, noErr);
    return print_mutex_free(ref, mutex, data) && allocated && !data && print_result(call, memFullErr);
}

static int print_mutex_lifetime(const ROMlib_carbon_reference* ref, uint32_t mutex, uint32_t table, uint32_t holder,
                                ROMlib_carbon_ppc_frame* call)
{
    uint32_t data, references;
    if (!cfp_get(mutex + 4, &data) || !data || !cfp_get(data, &references))
        return print_result(call, paramErr);
    const uint32_t lock = cfp_read(table + 12), unlock = cfp_read(table + 20);
    if (!lock || !unlock || !cfp_callback(lock, &mutex, 1, NULL))
        return 0;
    if (!cfp_get(data, &references)) {
        cfp_callback(unlock, &mutex, 1, NULL);
        return 0;
    }

    int32_t status = noErr;
    bool changed = false;
    if (holder) {
        const uint32_t busy = references == 1 ? cfp_read(data + 4) : 0;
        if (!references || (int32_t)busy > 0)
            status = -909;
        else
            changed = cfp_write(data, --references);
    }
    else if (references >= INT32_MAX)
        status = -909;
    else
        changed = cfp_write(data, ++references);

    if (!cfp_callback(unlock, &mutex, 1, NULL))
        return 0;
    if (status)
        return print_result(call, status);
    if (!changed || (holder && !cfp_write(holder, 0)))
        return 0;
    return (references || print_mutex_free(ref, mutex, data)) && print_result(call, noErr);
}

static int print_mutex_operation(const ROMlib_carbon_reference* ref, enum print_value_kind operation,
                                 ROMlib_carbon_ppc_frame* call)
{
    if (operation == create_print_mutex)
        return print_mutex_create(ref, call);

    const uint32_t argument = call->words[0], output = call->words[1];
    const bool releasing = operation == release_print_mutex;
    unsigned char* locked = NULL;
    if (operation == try_print_mutex)
        locked = output ? ROMlib_carbon_try_guest_memory(output, 1, 1) : NULL;
    if (!argument || (operation == try_print_mutex && !locked)) {
        if (locked)
            *locked = 0;
        return print_result(call, paramErr);
    }

    const uint32_t mutex = releasing ? cfp_read(argument) : argument;
    const uint32_t table = mutex ? cfp_read(mutex) : 0;
    if (!table || (releasing && !ROMlib_carbon_try_guest_memory(argument, 4, 1))) {
        if (locked)
            *locked = 0;
        return print_result(call, paramErr);
    }
    const uint32_t standard = cfp_toc(ref, (unsigned)-0x38c);
    if (standard && table == standard + 4 && (operation == retain_print_mutex || releasing))
        return print_mutex_lifetime(ref, mutex, table, releasing ? argument : 0, call);

    const uint32_t method_offsets[] = {4, 8, 12, 16, 20};
    const uint32_t method = cfp_read(table + method_offsets[operation - retain_print_mutex]);
    const uint32_t arguments[] = {mutex, argument};
    uint32_t result;
    if (!method || !cfp_callback(method, arguments, releasing ? 2 : 1, &result))
        return 0;
    if (locked)
        *locked = (uint8_t)result;
    return print_result(call, noErr);
}

static int print_set_profile(const ROMlib_carbon_reference* ref, ROMlib_carbon_ppc_frame* call)
{
    const uint32_t settings = call->words[0], source = call->words[1], location = call->words[2];
    uint32_t session, valid;
    if (!print_worker(ref, 0x81614, NULL, 0, NULL) || !default_print_session(ref, &session) ||
        !print_worker(ref, 0x85768, &session, 1, &valid))
        return 0;
    if (!valid)
        return finish_print_operation(ref, -30871, call);
    if (!print_worker(ref, 0x8716c, &settings, 1, &valid))
        return 0;
    if (!valid)
        return finish_print_operation(ref, invalid_settings, call);
    if (!location)
        return finish_print_operation(ref, paramErr, call);

    uint32_t context;
    if (!print_worker(ref, 0x8448c, &session, 1, &context))
        return 0;
    const unsigned char* step = context ? ROMlib_carbon_try_guest_memory(context + 0x58, 2, 0) : NULL;
    if (!step || step[0] || step[1] != 3)
        return finish_print_operation(ref, -30871, call);
    if (source != TICK("srcp"))
        return finish_print_operation(ref, paramErr, call);

    _Alignas(4) uint32_t encoded = 0;
    const uint32_t open[] = {US_TO_SYN68K(&encoded), location};
    uint32_t status;
    if (!print_worker(ref, 0xb7a58, open, NELEM(open), &status))
        return 0;
    if (status)
        return finish_print_operation(ref, status, call);
    const uint32_t profile = CL(encoded);
    _Alignas(128) uint32_t header[32];
    const uint32_t get[] = {profile, US_TO_SYN68K(header)};
    int ok = print_worker(ref, 0xb7a70, get, NELEM(get), &status);
    if (ok && !status) {
        if (CL(header[2]) <= 0x100)
            status = -30873;
        else {
            const uint32_t comment[] = {profile, 0};
            ok = print_worker(ref, 0xb7a88, comment, NELEM(comment), &status);
        }
    }
    return print_worker(ref, 0xb7aa0, &profile, 1, NULL) && ok && finish_print_operation(ref, status, call);
}

static int print_value(const struct print_value_operation* op, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", op->name, &ref))
        return 0;
    if (!use_print_profile(&ref, op->entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t allocator = call->words[0], input = call->words[1];
    switch (op->kind) {
    case from_cstring:
        if (!input || !call->words[2])
            return print_result(call, paramErr);
        return write_print_value(call, call->words[2], ROMlib_cfstring_cstring(allocator, input, 0x08000100));
    case from_pascal_string:
        if (!input || !call->words[2])
            return print_result(call, paramErr);
        return write_print_value(call, call->words[2], ROMlib_cfstring_pascal(allocator, input, 0));
    case from_double: {
        if (!call->words[3])
            return print_result(call, paramErr);
        const uint64_t bits = call->floating[0];
        _Alignas(16) uint32_t be_double[2] = {CL(bits >> 32), CL((uint32_t)bits)};
        return write_print_value(call, call->words[3], ROMlib_cfnumber_create(allocator, 13, US_TO_SYN68K(be_double)));
    }
    case from_int32: {
        if (!call->words[2])
            return print_result(call, paramErr);
        _Alignas(16) uint32_t be_int32 = CL(input);
        return write_print_value(call, call->words[2], ROMlib_cfnumber_create(allocator, 3, US_TO_SYN68K(&be_int32)));
    }
    case from_page_rect:
        return make_print_array(&ref, call, 4);
    case from_resolution:
        return make_print_array(&ref, call, 2);
    case to_page_rect:
        return read_print_array(call, 4);
    case to_resolution:
        return read_print_array(call, 2);
    case from_index: {
        if (!call->words[2])
            return print_result(call, paramErr);
        _Alignas(16) char decimal[16];
        snprintf(decimal, sizeof decimal, "%d", (int32_t)input);
        return write_print_value(call, call->words[2], ROMlib_cfstring_cstring(allocator, US_TO_SYN68K(decimal), 0));
    }
    case from_array:
        return make_print_object_array(&ref, call);
    case to_cstring:
        return make_print_cstring(&ref, call);
    case is_string_or_array:
        return print_string_or_array(call);
    case copy_preference_array:
        return copy_print_preference_array(call);
    case get_pascal_job_name:
    case set_pascal_job_name:
        return print_pascal_job_name(&ref, call, op->kind == set_pascal_job_name);
    case read_print_data:
    case read_print_plist:
        return print_file_value(call, 1, op->kind == read_print_plist);
    case write_print_data:
    case write_print_plist:
        return print_file_value(call, 0, op->kind == write_print_plist);
    case get_ticket_typed_item:
        return print_typed_ticket_item(&ref, call);
    case find_ticket_type:
        return print_find_ticket_type(&ref, call);
    case create_subticket_key:
        return print_create_subticket_key(&ref, call);
    case find_subticket:
        return print_find_subticket(&ref, call);
    case find_all_subtickets:
        return print_find_all_subtickets(&ref, call);
    case search_ticket_leaf:
        return print_search_leaf(&ref, call);
    case search_ticket_node:
        return print_search_node(&ref, call);
    case validate_ticket_item: {
        uint32_t valid;
        return print_item_valid(call->words[0], &valid) && print_result(call, valid);
    }
    case build_ticket_item_dictionary:
        return print_build_item(&ref, call);
    case add_ticket_item:
        return print_add_ticket_item(&ref, call);
    case update_ticket_item:
        return print_update_ticket_item(&ref, call);
    case get_ticket_item_array:
        return print_item_array(&ref, call);
    case get_ticket_item_lock:
    case set_ticket_item_lock:
        return print_item_lock_state(&ref, call, op->kind == get_ticket_item_lock);
    case replace_ticket_item_lock:
        return print_replace_item_lock(&ref, call);
    case set_print_profile:
        return print_set_profile(&ref, call);
    case remove_ticket_metadata:
        return print_remove_item_metadata(&ref, call);
    case create_print_mutex:
    case retain_print_mutex:
    case release_print_mutex:
    case lock_print_mutex:
    case try_print_mutex:
    case unlock_print_mutex:
        return print_mutex_operation(&ref, op->kind, call);
    case set_ticket_retain_count:
        return print_ticket_retain_count(&ref, call);
    case lock_print_ticket:
    case unlock_print_ticket:
        return print_ticket_lock_flag(&ref, call, op->kind == lock_print_ticket);
    }
    return 0;
}

#define CARBON_PRINT_VALUE(name, offset, kind)                                \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)                   \
    {                                                                         \
        static const struct print_value_operation op = {#name, offset, kind}; \
        return print_value(&op, call);                                        \
    }
#include "printing_value_entries.h"
#undef CARBON_PRINT_VALUE

enum print_transition {
    begin_document,
    end_document,
    begin_page,
    end_page,
    begin_postscript,
    end_postscript,
    page_setup_dialog,
    job_dialog,
};

static int finish_session_operation(const ROMlib_carbon_reference* ref, uint32_t session, uint32_t status,
                                    ROMlib_carbon_ppc_frame* call)
{
    const uint32_t arguments[] = {session, status};
    return print_worker(ref, 0x895d0, arguments, 2, &status) && print_result(call, status);
}

static int change_print_document(const ROMlib_carbon_reference* ref, enum print_transition op, uint32_t session,
                                 const uint32_t* call_args, uint32_t* status)
{
    uint32_t ignored;
    switch (op) {
    case begin_document: {
        const uint32_t inputs[] = {session, call_args[1], call_args[2]};
        if (!print_worker(ref, 0x89344, inputs, 3, status))
            return 0;
        if (*status)
            return 1;

        /* Opening a job creates private copies for the duration of the job. */
        uint32_t tickets[] = {session, 0, 0};
        if (!cfp_get(session + 0x20, tickets + 1) || !cfp_get(session + 0x1c, tickets + 2) ||
            !ticket_payload(ref, page_format, tickets + 2) || !ticket_payload(ref, print_settings, tickets + 1) ||
            !print_worker(ref, 0x8499c, tickets, 3, status))
            return 0;
        return *status || write_print_phase(session, print_document);
    }
    case end_document:
        return write_print_phase(session, print_idle) && print_worker(ref, 0x848d4, &session, 1, status) &&
               print_worker(ref, 0x89428, &session, 1, &ignored);
    case begin_page: {
        uint32_t recorded;
        if (!cfp_get(session + 0x3c, &recorded))
            return 0;
        if (recorded == print_cancel)
            return 1;

        const uint32_t page[] = {session, call_args[1]};
        const uint32_t rectangle[] = {session, call_args[2]};
        if (!print_worker(ref, 0x8948c, page, 2, status))
            return 0;
        if (*status)
            return 1;
        if (!print_worker(ref, 0x84c68, rectangle, 2, status))
            return 0;
        return *status || write_print_phase(session, print_page);
    }
    case end_page:
        return write_print_phase(session, print_document) && print_worker(ref, 0x84c38, &session, 1, status) &&
               print_worker(ref, 0x8954c, &session, 1, &ignored);
    default:
        return 0;
    }
}

static int run_print_dialog(const ROMlib_carbon_reference* ref, enum print_transition op, uint32_t session,
                            const uint32_t* call_args, uint32_t* status)
{
    /* The driver receives the original ticket identities. Mark both tickets
       busy while the modal dialog and any guest callbacks are running. */
    const int print = op == job_dialog;
    const uint32_t arguments[] = {session, call_args[1], call_args[2], print ? call_args[3] : 0};
    uint32_t slot, table, key;
    if (!cfp_get(US_TO_SYN68K(ref->toc) - 0x3b4, &slot) || !cfp_get(slot, &table) || !cfp_get(table + 4, &key))
        return 0;
    uint16_t* format_lock = ROMlib_carbon_guest_memory((call_args[print ? 2 : 1] ^ key) + 0x14, 2, 1);
    uint16_t* settings_lock = print ? ROMlib_carbon_guest_memory((call_args[1] ^ key) + 0x14, 2, 1) : NULL;
    if (!format_lock || (print && !settings_lock))
        return 0;

    const uint16_t busy = CW(1), idle = CW(0);
    memcpy(format_lock, &busy, sizeof busy);
    if (settings_lock)
        memcpy(settings_lock, &busy, sizeof busy);
    const int ok = print_worker(ref, print ? 0x835fc : 0x83474, arguments, print ? 4 : 3, status);
    memcpy(format_lock, &idle, sizeof idle);
    if (settings_lock)
        memcpy(settings_lock, &idle, sizeof idle);
    return ok;
}

static int print_lifecycle(const char* name, uint32_t entry_offset, enum print_transition op, uint16_t required_phase,
                           ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t valid, session, status = noErr;
    if (!print_worker(&ref, 0x85768, call->words, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, invalid_session);
    const int has_settings = op == begin_document || op == job_dialog;
    if (has_settings) {
        if (!ticket_check(&ref, print_settings, call->words[1], &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, invalid_settings);
    }
    if (has_settings || op == page_setup_dialog || (op == begin_page && call->words[1])) {
        if (!ticket_check(&ref, page_format, call->words[has_settings ? 2 : 1], &valid))
            return 0;
        if (!(uint8_t)valid)
            return print_result(call, invalid_page_format);
    }
    if (!print_worker(&ref, 0x8448c, call->words, 1, &session) || !session)
        return 0;
    if (required_phase == print_idle && !print_worker(&ref, 0x895b8, &session, 1, &valid))
        return 0;

    uint16_t phase;
    if (!read_print_phase(session, &phase))
        return 0;
    if (phase != required_phase)
        status = session_not_open;
    if (op == begin_page && !status && call->words[2]) {
        if (!print_worker(&ref, 0x89dd4, call->words + 2, 1, &valid))
            return 0;
        if (!(uint8_t)valid)
            status = paramErr;
    }
    if (!status) {
        if (op <= end_page) {
            if (!change_print_document(&ref, op, session, call->words, &status))
                return 0;
        }
        else if (op == begin_postscript || op == end_postscript) {
            if (!print_worker(&ref, op == begin_postscript ? 0x85280 : 0x852f0, &session, 1, &status))
                return 0;
        }
        else if (!run_print_dialog(&ref, op, session, call->words, &status))
            return 0;
    }

    if (op <= end_page) {
        uint32_t recorded;
        if (!cfp_get(session + 0x3c, &recorded))
            return 0;
        if (recorded == print_cancel)
            return print_result(call, print_cancel);
    }
    return finish_session_operation(&ref, session, status, call);
}

static int print_legacy_context(const char* name, uint32_t offset, enum print_transition operation,
                                ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t context = call->words[0], valid;
    if (operation == begin_document && !default_print_session(&ref, &context))
        return 0;
    if (!print_worker(&ref, 0x85768, &context, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, session_not_open);
    ROMlib_carbon_ppc_frame arguments = *call;
    if (operation == begin_document) {
        const uint32_t output = call->words[2];
        if (!output)
            return print_result(call, paramErr);
        if (!ROMlib_carbon_guest_memory(output, 4, 1))
            return 0;
        arguments.words[0] = context;
        arguments.words[1] = call->words[0];
        arguments.words[2] = call->words[1];
        if (!ROMlib_carbon_PMSessionBeginDocument(&arguments))
            return 0;
        if (!arguments.words[0] && (!default_print_session(&ref, &context) || !cfp_write(output, context)))
            return 0;
    }
    else if (operation == begin_page) {
        arguments.words[1] = 0;
        arguments.words[2] = call->words[1];
        if (!ROMlib_carbon_PMSessionBeginPage(&arguments))
            return 0;
    }
    else if (!(operation == end_page ? ROMlib_carbon_PMSessionEndPage(&arguments)
                                     : ROMlib_carbon_PMSessionEndDocument(&arguments)))
        return 0;
    return print_result(call, arguments.words[0]);
}

int ROMlib_carbon_PMBeginDocument(ROMlib_carbon_ppc_frame* call)
{
    return print_legacy_context("PMBeginDocument", 0x951fc, begin_document, call);
}

int ROMlib_carbon_PMEndDocument(ROMlib_carbon_ppc_frame* call)
{
    return print_legacy_context("PMEndDocument", 0x952a4, end_document, call);
}

int ROMlib_carbon_PMBeginPage(ROMlib_carbon_ppc_frame* call)
{
    return print_legacy_context("PMBeginPage", 0x95310, begin_page, call);
}

int ROMlib_carbon_PMEndPage(ROMlib_carbon_ppc_frame* call)
{
    return print_legacy_context("PMEndPage", 0x953a0, end_page, call);
}

int ROMlib_carbon_PMGetGrafPtr(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", "PMGetGrafPtr", &ref))
        return 0;
    if (!use_print_profile(&ref, 0x90ae8))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    uint32_t valid, session, port;
    if (!print_worker(&ref, 0x85768, call->words, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, session_not_open);
    if (!call->words[1])
        return print_result(call, paramErr);
    uint16_t phase;
    if (!print_worker(&ref, 0x8448c, call->words, 1, &session) || !read_print_phase(session, &phase))
        return 0;
    if (phase != print_page)
        return print_result(call, session_not_open);
    if (!cfp_get(session + 0x30, &port) || !cfp_write(call->words[1], port))
        return 0;
    return print_result(call, noErr);
}

static int set_print_document_format(const char* name, uint32_t entry_offset, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_reference_function("CarbonLib", name, &ref))
        return 0;
    if (!use_print_profile(&ref, entry_offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    uint32_t constant, type, valid;
    if (!cfp_get(US_TO_SYN68K(ref.toc) - 0xd4, &constant) ||
        !cfp_call("__CFStringMakeConstantString", &constant, 1, &type) ||
        !print_worker(&ref, 0x85768, call->words, 1, &valid))
        return 0;
    if (!(uint8_t)valid)
        return print_result(call, invalid_session);
    if (!call->words[1])
        return print_result(call, paramErr);

    uint32_t context_types = call->words[2];
    const int owns_context_types = !context_types;
    if (owns_context_types) {
        _Alignas(16) const uint32_t encoded_type = CL(type);
        uint32_t allocator, callbacks;
        if (!cfp_call("CFAllocatorGetDefault", NULL, 0, &allocator) ||
            !cfp_get(US_TO_SYN68K(ref.toc) - 0x2e68, &callbacks))
            return 0;
        const uint32_t array_args[] = {allocator, US_TO_SYN68K(&encoded_type), 1, callbacks};
        if (!cfp_call("CFArrayCreate", array_args, 4, &context_types))
            return 0;
        if (!context_types)
            return print_result(call, noErr);
    }

    uint32_t arguments[] = {0, call->words[1], context_types, call->words[3]};
    uint32_t status;
    const int ok = print_worker(&ref, 0x8448c, call->words, 1, arguments) && arguments[0] &&
                   print_worker(&ref, 0x895b8, arguments, 1, &valid) &&
                   print_worker(&ref, 0x858a8, arguments, 4, &status);
    if (owns_context_types && !cfp_call("CFRelease", &context_types, 1, NULL))
        return 0;
    return ok && finish_session_operation(&ref, arguments[0], status, call);
}

#define CARBON_PRINT_LIFECYCLE(name, offset, transition, phase)         \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)             \
    {                                                                   \
        return print_lifecycle(#name, offset, transition, phase, call); \
    }
#define CARBON_PRINT_FORMAT(name, offset)                      \
    int ROMlib_carbon_##name(ROMlib_carbon_ppc_frame* call)    \
    {                                                          \
        return set_print_document_format(#name, offset, call); \
    }
#include "printing_lifecycle_entries.h"
#undef CARBON_PRINT_FORMAT
#undef CARBON_PRINT_LIFECYCLE
