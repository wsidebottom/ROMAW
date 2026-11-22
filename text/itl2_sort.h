/*+===================================================================
  File:      itl2_sort.h

  Summary:   Native comparison driver for guest itl2 callbacks. Included by iu.c so the trap entry points and their hook
             driver share one translation unit.

  Classes:   None.

  Functions: Interface declarations and inline definitions; see the declarations below.

  Origin:    Existing repository source in executor/src/text; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native comparison driver for guest itl2 callbacks. Included by iu.c so the
   trap entry points and their hook driver share one translation unit. */
#include "rsys/itl2.h"

typedef struct {
    romaw_itl2_frame* sort_frame;
    syn68k_addr_t frame_address, resource_address;
    uint16_t offsets[5];
    const unsigned char* text[2];
    uint32 addresses[2];
    unsigned lengths[2], budget;
} itl2_context;

static int itl2_call(itl2_context* context, unsigned hook)
{
    if (hook != 4) {
        if (!context->budget)
            return 0;
        --context->budget;
    }
    EM_A4 = context->resource_address;
    EM_A6 = context->frame_address + 66;
    EM_A7 = context->frame_address;
    EM_D0 = (EM_D0 & 0xffff0000) | context->offsets[hook];
    /* MOVE.W of the dispatch offset sets N/Z and clears V/C, preserving X. */
    cpu_state.ccnz = context->offsets[hook];
    cpu_state.ccn = cpu_state.ccc = cpu_state.ccv = 0;
    CALL_EMULATOR(context->resource_address + context->offsets[hook]);
    return EM_A7 == context->frame_address;
}

static romaw_itl2_string* itl2_string(itl2_context* context, unsigned which)
{
    return which ? &context->sort_frame->b : &context->sort_frame->a;
}

static int itl2_fetch(itl2_context* context, unsigned which, int* ended)
{
    romaw_itl2_string* s = itl2_string(context, which);
    uint32 offset, address;
    unsigned remaining;
    EM_A2 = context->frame_address + (which ? 34 : 50);
    EM_D5 = 0;
    s->ignored = 0;
    *ended = 0;
    if (s->no_fetch) {
        s->no_fetch = 0;
        EM_D4 = (EM_D4 & 0xffff0000) | CW(s->current);
        return 1;
    }
    if (s->buffered) {
        s->current = CW((uint16)(s->buffered * 256));
        s->buffered = 0;
        EM_D4 = (EM_D4 & 0xffff0000) | CW(s->current);
        return 1;
    }
    address = CL(s->text);
    offset = address - context->addresses[which];
    remaining = CW(s->remaining);
    if (offset > context->lengths[which] ||
        (remaining && (offset == context->lengths[which] || remaining > context->lengths[which] - offset)))
        return 0;
    /* FetchHook still receives the end indication; no sentinel byte is read
     outside the caller's explicit text extent. */
    EM_D4 = (EM_D4 & 0xffff0000) | (remaining ? context->text[which][offset] * 256 : 0);
    EM_D5 = remaining == 0;
    EM_A3 = address + 1;
    s->remaining = CW((uint16)(remaining - 1));
    if (!itl2_call(context, 1))
        return 0;
    *ended = (EM_D5 & 255) != 0;
    offset = EM_A3 - context->addresses[which];
    if (offset > context->lengths[which] + (unsigned)*ended ||
        (!*ended && CW(s->remaining) > context->lengths[which] - offset))
        return 0;
    s->text = CL(EM_A3);
    s->current = CW((uint16)EM_D4);
    return 1;
}

static int itl2_project(itl2_context* context, unsigned which)
{
    static const unsigned char expansions[][4] = {{0xae, 'A', 'A', 'E'}, {0xbe, 'a', 'A', 'e'}, {0xce, 'O', 'O', 'E'},
                                                  {0xcf, 'o', 'O', 'e'}, {0xa7, 's', 'S', 's'}, {0xde, 'f', 'F', 'i'},
                                                  {0xdf, 'f', 'F', 'l'}};
    romaw_itl2_string* s = itl2_string(context, which);
    unsigned projected;
    EM_A2 = context->frame_address + (which ? 34 : 50);
    EM_D4 = (EM_D4 & 0xffff0000) | CW(s->current);
    s->mapped = 0;
    if (!itl2_call(context, 3))
        return 0;
    if (cpu_state.ccnz)
        projected = EM_D4 & 65535;
    else {
        unsigned c = (EM_D4 >> 8) & 255;
        projected = itl2_projection(c) * 256;
        for (unsigned i = 0; i < NELEM(expansions); ++i)
            if (expansions[i][0] == c) {
                s->current = CW((uint16)((CW(s->current) & 255) | expansions[i][1] * 256));
                projected = expansions[i][2] * 256;
                s->buffered = expansions[i][3];
                if (!context->sort_frame->weak_equal)
                    ++s->just_after;
                break;
            }
        EM_D4 = (EM_D4 & 0xffff0000) | projected;
    }
    s->mapped = CW((uint16)projected);
    return 1;
}

static int itl2_tie(itl2_context* context, unsigned which, unsigned* key)
{
    romaw_itl2_string* s = itl2_string(context, which);
    unsigned decision = CW(s->decision);
    EM_A2 = context->frame_address + (which ? 39 : 55);
    EM_D4 = (EM_D4 & 0xffffff00) | (decision >> 8);
    EM_D5 = (EM_D5 & 0xffffff00) | (decision & 255);
    if (!itl2_call(context, 2))
        return 0;
    *key = cpu_state.ccnz ? ((EM_D4 & 255) * 256 + (EM_D5 & 255)) : itl2_vernier(EM_D4 & 255, EM_D5 & 255);
    EM_D4 = (EM_D4 & 0xffff0000) | *key;
    return 1;
}

static int itl2_compare_streams(itl2_context* context, INTEGER* result)
{
    romaw_itl2_frame* sort_frame = context->sort_frame;
    int a_end, b_end;
    unsigned a, b;
    if (!itl2_call(context, 0))
        return 0;
    for (;;) {
        if (!itl2_fetch(context, 0, &a_end))
            return 0;
        EM_D6 = (EM_D6 & 0xffff0000) | CW(sort_frame->a.current);
        EM_D3 = (EM_D3 & 0xffffff00) | a_end;
        if (!itl2_fetch(context, 1, &b_end))
            return 0;
        EM_D3 = (EM_D3 & 0xffffff00) | (a_end + b_end);
        if (a_end && b_end)
            break;
        if (a_end || b_end) {
            int all_ignored = sort_frame->weak_equal != 0;
            unsigned which = a_end ? 1 : 0;
            int ended = 0;
            while (all_ignored && !ended) {
                if (!itl2_project(context, which))
                    return 0;
                if (!itl2_string(context, which)->ignored)
                    all_ignored = 0;
                else if (!itl2_fetch(context, which, &ended))
                    return 0;
            }
            if (all_ignored)
                break;
            *result = sort_frame->want_magnitude || sort_frame->weak_magnitude ? (a_end ? -1 : 1) : 1;
            return 1;
        }
        if (sort_frame->a.current == sort_frame->b.current)
            continue;
        /* The original driver projects B before A; callbacks can share scratch. */
        if (!itl2_project(context, 1) || !itl2_project(context, 0))
            return 0;
        a = CW(sort_frame->a.mapped);
        b = CW(sort_frame->b.mapped);
        if (a != b) {
            if (!sort_frame->a.ignored && !sort_frame->b.ignored) {
                *result = sort_frame->want_magnitude || sort_frame->weak_magnitude ? (a < b ? -1 : 1) : 1;
                return 1;
            }
            sort_frame->a.no_fetch = !sort_frame->a.ignored;
            sort_frame->b.no_fetch = !sort_frame->b.ignored;
        }
        if (!sort_frame->weak_equal) {
            sort_frame->weak_equal = 1;
            sort_frame->a.decision = sort_frame->a.current;
            sort_frame->b.decision = sort_frame->b.current;
        }
    }
    *result = 0;
    if (sort_frame->weak_equal && sort_frame->want_magnitude) {
        if (!itl2_tie(context, 0, &a))
            return 0;
        EM_D6 = (EM_D6 & 0xffff0000) | a;
        if (!itl2_tie(context, 1, &b))
            return 0;
        *result = a < b   ? -1
                  : a > b ? 1
                  : sort_frame->a.just_after < sort_frame->b.just_after
                      ? -1
                      : sort_frame->a.just_after > sort_frame->b.just_after;
    }
    return 1;
}

static int itl2_custom_compare(Ptr a, Ptr b, INTEGER a_length, INTEGER b_length, Handle resource, BOOLEAN identity,
                               INTEGER* result)
{
    M68kReg saved[16];
    CCRElement flags[5];
    itl2_context context = {0};
    Size size;
    SignedByte state = 0;
    int locked = 0, handled = 0;
    unsigned prefix = 0;
    if (!a || a_length < 0)
        a_length = 0;
    if (!b || b_length < 0)
        b_length = 0;
    while (prefix < (unsigned)a_length && prefix < (unsigned)b_length && a[prefix] == b[prefix])
        ++prefix;
    if (prefix == (unsigned)a_length || prefix == (unsigned)b_length) {
        *result = a_length < b_length ? -1 : a_length > b_length;
        if (identity)
            *result = *result != 0;
        return 1;
    }
    memcpy(saved, cpu_state.regs, sizeof saved);
    flags[0] = cpu_state.ccnz;
    flags[1] = cpu_state.ccn;
    flags[2] = cpu_state.ccc;
    flags[3] = cpu_state.ccv;
    flags[4] = cpu_state.ccx;
    if (!STARH(resource))
        LoadResource(resource);
    if (!STARH(resource) || (size = GetHandleSize(resource)) < 12)
        goto done;
    state = HGetState(resource);
    HLock(resource);
    locked = 1;
    if (!itl2_vectors((unsigned char*)STARH(resource), size, context.offsets) ||
        saved[15].ul.n < sizeof(romaw_itl2_frame) + 4 || (saved[15].ul.n & 1))
        goto done;
    context.frame_address = saved[15].ul.n - sizeof(romaw_itl2_frame);
    if (!syn68k_memory_span(context.frame_address - 4, sizeof(romaw_itl2_frame) + 4))
        goto done;
    context.sort_frame = (romaw_itl2_frame*)SYN68K_TO_US(context.frame_address);
    context.resource_address = US_TO_SYN68K(STARH(resource));
    context.text[0] = (unsigned char*)a;
    context.text[1] = (unsigned char*)b;
    context.addresses[0] = US_TO_SYN68K(a);
    context.addresses[1] = US_TO_SYN68K(b);
    context.lengths[0] = a_length;
    context.lengths[1] = b_length;
    if ((uint64)context.resource_address + size > UINT32_MAX || (uint64)context.addresses[0] + a_length > UINT32_MAX ||
        (uint64)context.addresses[1] + b_length > UINT32_MAX)
        goto done;
    memset(context.sort_frame, 0, sizeof *context.sort_frame);
    context.sort_frame->old_a6 = CL(saved[14].ul.n);
    context.sort_frame->return_address = CLC(MAGIC_EXIT_EMULATOR_ADDRESS);
    context.sort_frame->resource = CL(US_TO_SYN68K(resource));
    context.sort_frame->a_length = CW(a_length);
    context.sort_frame->b_length = CW(b_length);
    context.sort_frame->a_text = CL(context.addresses[0]);
    context.sort_frame->b_text = CL(context.addresses[1]);
    context.sort_frame->a.text = CL(context.addresses[0] + prefix);
    context.sort_frame->b.text = CL(context.addresses[1] + prefix);
    context.sort_frame->a.remaining = CW((uint16)(a_length - prefix));
    context.sort_frame->b.remaining = CW((uint16)(b_length - prefix));
    context.sort_frame->want_magnitude = !identity;
    context.budget = ((unsigned)a_length + b_length + 2) * 8;
    handled = itl2_compare_streams(&context, result);
    context.sort_frame->result = CW(handled ? *result : 0);
    if (!itl2_call(&context, 4))
        handled = 0;
    if (handled)
        *result = CW(context.sort_frame->result);
done:
    if (locked)
        HSetState(resource, state);
    memcpy(cpu_state.regs, saved, sizeof saved);
    cpu_state.ccnz = flags[0];
    cpu_state.ccn = flags[1];
    cpu_state.ccc = flags[2];
    cpu_state.ccv = flags[3];
    cpu_state.ccx = flags[4];
    if (!handled)
        warning_unexpected("invalid or non-progressing itl2 hooks; using native collation");
    return handled;
}
