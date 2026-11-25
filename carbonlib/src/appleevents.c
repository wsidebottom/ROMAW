/*+===================================================================
  File:      appleevents.c

  Summary:   Native AppleEvent descriptor inspection, flattening, streaming, arrays and format conversion.

  Classes:   None.

  Functions: ROMlib_carbon_AECheckIsRecord, ROMlib_carbon_AEFlattenDesc, ROMlib_carbon_AESizeOfFlattenedDesc,
             ROMlib_carbon_AEUnflattenDesc, ROMlib_carbon_ae_stream, ROMlib_carbon_ae_build,
             ROMlib_carbon_AEPrintDescToHandle, ROMlib_carbon_AEGetArray, ROMlib_carbon_AEPutArray,
             ROMlib_carbon_get_desc_data, ROMlib_carbon_get_desc_data_size,
             ROMlib_carbon_replace_desc_data, ROMlib_carbon_CreateCompDescriptor, ROMlib_carbon_CreateLogicalDescriptor,
             ROMlib_carbon_CreateObjSpecifier, ROMlib_carbon_CreateOffsetDescriptor, ROMlib_carbon_CreateRangeDescriptor

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.
             Format grammar follows Apple Technical Note TN2045 and the local CarbonLib 1.4 disassembly.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native descriptor data helpers, following CarbonLib 1.4 at 0x72454-0x7256c.
   Descriptor creation and disposal remain owned by the AppleEvent Manager. */
#include "internal.h"
#include "rsys/apple_events.h"
#include <ctype.h>
#include <stdio.h>

enum {
    flattened_descriptor_header_size = 16,
    descriptor_buffer_too_small = -1741,
};

static uint32 descriptor_word(const Byte* bytes)
{
    uint32 value;
    memcpy(&value, bytes, sizeof value);
    return CL(value);
}

static int descriptor_result(ROMlib_carbon_ppc_frame* call, int32 result)
{
    call->words[0] = result;
    return 1;
}

struct object_record_field {
    AEKeyword key;
    DescType scalar_type;
    uint32 value;
};

static int create_object_record(ROMlib_carbon_ppc_frame* call, uint32 output, DescType type,
                                const struct object_record_field* fields, size_t count, Boolean dispose_inputs)
{
    AEDesc* descriptor = output ? ROMlib_carbon_try_guest_memory(output, sizeof *descriptor, 1) : NULL;
    if (!descriptor)
        return descriptor_result(call, paramErr);

    AEDesc* inputs[4] = {0};
    for (size_t i = 0; i < count; ++i) {
        if (fields[i].scalar_type)
            continue;
        const uint32 address = fields[i].value;
        inputs[i] = address ? ROMlib_carbon_try_guest_memory(address, sizeof *descriptor, dispose_inputs) : NULL;
        if (!inputs[i])
            return descriptor_result(call, paramErr);
    }

    AERecord record = {CLC(typeNull), {0}};
    OSErr result = C_AECreateList(NULL, 0, TRUE, &record);
    for (size_t i = 0; i < count && !result; ++i) {
        AEDesc scalar = {CLC(typeNull), {0}};
        AEDesc* input = inputs[i];
        if (!input) {
            uint32 value = CL(fields[i].value);
            result = C_AECreateDesc(fields[i].scalar_type, (Ptr)&value, sizeof value, &scalar);
            input = &scalar;
        }
        if (!result)
            result = C_AEPutKeyDesc(&record, fields[i].key, input);
        C_AEDisposeDesc(&scalar);
    }
    if (result) {
        C_AEDisposeDesc(&record);
        return descriptor_result(call, result);
    }
    if (dispose_inputs) {
        for (size_t i = 0; i < count; ++i) {
            if (inputs[i])
                C_AEDisposeDesc(inputs[i]);
        }
    }
    DESC_TYPE_X(&record) = CL(type);
    *descriptor = record;
    return descriptor_result(call, noErr);
}

int ROMlib_carbon_CreateOffsetDescriptor(ROMlib_carbon_ppc_frame* call)
{
    AEDesc* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof *output, 1) : NULL;
    if (!output)
        return descriptor_result(call, paramErr);
    uint32 offset = CL(call->words[0]);
    return descriptor_result(call, C_AECreateDesc(TICK("long"), (Ptr)&offset, sizeof offset, output));
}

int ROMlib_carbon_CreateCompDescriptor(ROMlib_carbon_ppc_frame* call)
{
    const struct object_record_field fields[] = {
        {TICK("relo"), TICK("enum"), call->words[0]},
        {TICK("obj1"), 0, call->words[1]},
        {TICK("obj2"), 0, call->words[2]},
    };
    return create_object_record(call, call->words[4], TICK("cmpd"), fields, NELEM(fields), (Byte)call->words[3]);
}

int ROMlib_carbon_CreateLogicalDescriptor(ROMlib_carbon_ppc_frame* call)
{
    const struct object_record_field fields[] = {
        {TICK("term"), 0, call->words[0]},
        {TICK("logc"), TICK("enum"), call->words[1]},
    };
    return create_object_record(call, call->words[3], TICK("logi"), fields, NELEM(fields), (Byte)call->words[2]);
}

int ROMlib_carbon_CreateObjSpecifier(ROMlib_carbon_ppc_frame* call)
{
    const struct object_record_field fields[] = {
        {TICK("want"), typeType, call->words[0]},
        {TICK("from"), 0, call->words[1]},
        {TICK("form"), TICK("enum"), call->words[2]},
        {TICK("seld"), 0, call->words[3]},
    };
    return create_object_record(call, call->words[5], TICK("obj "), fields, NELEM(fields), (Byte)call->words[4]);
}

int ROMlib_carbon_CreateRangeDescriptor(ROMlib_carbon_ppc_frame* call)
{
    const struct object_record_field fields[] = {
        {TICK("star"), 0, call->words[0]},
        {TICK("stop"), 0, call->words[1]},
    };
    return create_object_record(call, call->words[3], TICK("rang"), fields, NELEM(fields), (Byte)call->words[2]);
}

/* Each entry has a keyword, a type and length, or a shared prefix supplying
   some of those fields. The saved format aligns entries to two bytes. */
static int flat_record_data(const Byte* bytes, size_t size)
{
    if (size < 16)
        return 0;
    uint32 count = descriptor_word(bytes), prefix = descriptor_word(bytes + 4);
    if (count > INT32_MAX || prefix > INT32_MAX)
        return 0;
    size_t padded = (size + 1) & ~(size_t)1;
    if (prefix >= 8) {
        size_t first = (prefix + 9) & ~1U;
        uint32 fixed = descriptor_word(bytes + 12);
        if (first > size || fixed < first || fixed > INT32_MAX)
            return 0;
        size_t item_size = (fixed - first + 5) & ~(size_t)1;
        return padded >= first && (padded - first) % item_size == 0 && (padded - first) / item_size == count;
    }
    if (prefix != 0 && prefix != 4)
        return 0;
    size_t cursor = (prefix + 9) & ~1U;
    while (count--) {
        size_t length_offset = cursor + 8 - prefix;
        if (length_offset > size || size - length_offset < sizeof(uint32))
            return 0;
        uint32 length = descriptor_word(bytes + length_offset);
        if (length < prefix || length > INT32_MAX)
            return 0;
        size_t item_size = (length - prefix + 13) & ~(size_t)1;
        if (item_size > padded - cursor)
            return 0;
        cursor += item_size;
    }
    return cursor == padded;
}

static int romaw_record_data(const Byte* bytes, size_t size)
{
    size_t cursor = sizeof(list_header_t);
    uint32 count = descriptor_word(bytes + offsetof(list_header_t, param_count));
    while (count--) {
        if (size - cursor < sizeof(inline_key_desc_t))
            return 0;
        const uint32 length = descriptor_word(bytes + cursor + offsetof(inline_key_desc_t, size));
        cursor += sizeof(inline_key_desc_t);
        if (length > size - cursor)
            return 0;
        cursor += length;
    }
    return cursor == size;
}

int ROMlib_carbon_AECheckIsRecord(ROMlib_carbon_ppc_frame* call)
{
    const AEDesc* descriptor =
        call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *descriptor, 0) : NULL;
    Handle data = descriptor ? DESC_DATA(descriptor) : NULL;
    if (!data || !STARH(data))
        return descriptor_result(call, 0);
    const Size size = GetHandleSize(data);
    if (size < 16)
        return descriptor_result(call, 0);
    const Byte* bytes = (const Byte*)STARH(data);
    const uint32 marker = descriptor_word(bytes + offsetof(list_header_t, attribute_count));
    if (size >= sizeof(list_header_t) &&
        descriptor_word(bytes + offsetof(list_header_t, param_offset)) == sizeof(list_header_t) &&
        (marker == typeAERecord || marker == typeAEList))
        return descriptor_result(call, marker == typeAERecord && romaw_record_data(bytes, size));
    return descriptor_result(call, flat_record_data(bytes, size));
}

static int flattened_payload_size(const AEDesc* descriptor, Size* size)
{
    *size = ROMlib_carbon_get_desc_data_size(descriptor);
    return *size >= 0 && *size <= INT32_MAX - flattened_descriptor_header_size;
}

int ROMlib_carbon_AESizeOfFlattenedDesc(ROMlib_carbon_ppc_frame* call)
{
    const AEDesc* descriptor =
        call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *descriptor, 0) : NULL;
    Size size;
    return descriptor_result(
        call, descriptor && flattened_payload_size(descriptor, &size) ? size + flattened_descriptor_header_size : 0);
}

int ROMlib_carbon_AEFlattenDesc(ROMlib_carbon_ppc_frame* call)
{
    const uint32 output = call->words[1], actual = call->words[3];
    const int32 capacity = call->words[2];
    const AEDesc* descriptor =
        call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *descriptor, 0) : NULL;
    Size size;
    if ((!output && !actual) || !descriptor || !flattened_payload_size(descriptor, &size))
        return descriptor_result(call, paramErr);
    const Size required = size + flattened_descriptor_header_size;
    if (actual) {
        void* slot = ROMlib_carbon_try_guest_memory(actual, sizeof(uint32), 1);
        if (!slot)
            return descriptor_result(call, paramErr);
        const uint32 encoded = CL(required);
        memcpy(slot, &encoded, sizeof encoded);
    }
    if (!output || capacity < required)
        return descriptor_result(call, output ? descriptor_buffer_too_small : noErr);
    Byte* buffer = ROMlib_carbon_try_guest_memory(output, required, 1);
    if (!buffer)
        return descriptor_result(call, paramErr);
    const uint32 header[] = {CLC(TICK("dle2")), 0, DESC_TYPE_X(descriptor), CL(size)};
    if (size) {
        const OSErr error = ROMlib_carbon_get_desc_data(descriptor, buffer + sizeof header, size);
        if (error)
            return descriptor_result(call, error);
    }
    memcpy(buffer, header, sizeof header);
    return descriptor_result(call, noErr);
}

int ROMlib_carbon_AEUnflattenDesc(ROMlib_carbon_ppc_frame* call)
{
    const uint32 address = call->words[0];
    AEDesc* output = ROMlib_carbon_try_guest_memory(call->words[1], sizeof *output, 1);
    const Byte* bytes = ROMlib_carbon_try_guest_memory(address, flattened_descriptor_header_size, 0);
    if (!call->words[1] || !output || !address)
        return descriptor_result(call, paramErr);
    AEDesc result = {0};
    DESC_TYPE_X(&result) = CLC(typeNull);
    OSErr status = paramErr;
    if (bytes) {
        const uint32 version = descriptor_word(bytes), type = descriptor_word(bytes + 8);
        const uint32 size = descriptor_word(bytes + 12);
        if (version == TICK("dle2") || version == TICK("dle1")) {
            if (type == typeNull)
                status = noErr;
            else if (size <= INT32_MAX - flattened_descriptor_header_size &&
                     ROMlib_carbon_try_guest_memory(address, size + flattened_descriptor_header_size, 0)) {
                if (version == TICK("dle1") && type == typeAppleEvent) {
                    ROMlib_carbon_reference ref;
                    if (size >= 0x80 && ROMlib_carbon_probe_reference("CarbonLib", "AEUnflattenDesc", &ref))
                        return ROMlib_carbon_invoke_frame(ref.function, call);
                }
                else
                    status = C_AECreateDesc(type, (Ptr)(bytes + flattened_descriptor_header_size), size, &result);
            }
        }
    }
    *output = result;
    return descriptor_result(call, status);
}

OSErr ROMlib_carbon_get_desc_data(const AEDesc* descriptor, void* output, Size maximum)
{
    if (!descriptor || maximum < 0)
        return paramErr;
    Handle data = DESC_DATA(descriptor);
    if (!data)
        return noErr;
    Size size = GetHandleSize(data);
    if (size < 0)
        return paramErr;
    if (size > maximum)
        size = maximum;
    if (size) {
        if (!output)
            return paramErr;
        if (!STARH(data))
            return nilHandleErr;
        memmove(output, STARH(data), size);
    }
    return noErr;
}

Size ROMlib_carbon_get_desc_data_size(const AEDesc* descriptor)
{
    if (!descriptor || DESC_TYPE(descriptor) == typeNull || !DESC_DATA(descriptor))
        return 0;
    return GetHandleSize(DESC_DATA(descriptor));
}

OSErr ROMlib_carbon_replace_desc_data(DescType type, const void* data, Size size, AEDesc* descriptor)
{
    if (!descriptor || size < 0)
        return paramErr;
    C_AEDisposeDesc(descriptor);
    /* The classic manager does not clear its disposed descriptor. Keep the
       public Carbon descriptor valid even if creation of its replacement fails. */
    DESC_TYPE_X(descriptor) = CL(typeNull);
    FIELD_ASSIGN_RAW(DESC_DATA_X(descriptor), 0);
    return C_AECreateDesc(type, (Ptr)data, size, descriptor);
}

enum stream_frame_kind { stream_scalar, stream_list, stream_record, stream_event };

struct stream_frame {
    struct stream_frame* parent;
    AEDesc descriptor;
    enum stream_frame_kind kind;
    DescType result_type;
    uint32 length;
    AEKeyword key;
    Boolean key_pending;
};

struct descriptor_stream {
    uint32 signature;
    struct stream_frame* top;
    AEDesc result;
    AEDesc optional_keys;
    Boolean complete;
};

enum {
    stream_signature = 0x41457374,
    stream_wrong_state = -1737,
    stream_optional_key = 0x6f70746b,
    stream_keyword_type = 0x6b657977,
};

static struct descriptor_stream* new_descriptor_stream(void)
{
    struct descriptor_stream* stream = (struct descriptor_stream*)NewPtrClear(sizeof *stream);
    if (stream)
        stream->signature = stream_signature;
    return stream;
}

static void dispose_stream_descriptor(AEDesc* descriptor)
{
    if (DESC_DATA(descriptor))
        C_AEDisposeDesc(descriptor);
}

static void dispose_descriptor_stream(struct descriptor_stream* stream)
{
    while (stream->top) {
        struct stream_frame* scope = stream->top;
        stream->top = scope->parent;
        dispose_stream_descriptor(&scope->descriptor);
        DisposPtr((Ptr)scope);
    }
    dispose_stream_descriptor(&stream->result);
    dispose_stream_descriptor(&stream->optional_keys);
    stream->signature = 0;
    DisposPtr((Ptr)stream);
}

static AEDesc null_stream_descriptor(void)
{
    AEDesc descriptor = {0};
    DESC_TYPE_X(&descriptor) = CLC(typeNull);
    return descriptor;
}

static int stream_can_insert(const struct descriptor_stream* stream)
{
    const struct stream_frame* scope = stream->top;
    if (!scope)
        return !stream->complete;
    return scope->kind == stream_list ||
           ((scope->kind == stream_record || scope->kind == stream_event) && scope->key_pending);
}

static OSErr stream_push(struct descriptor_stream* stream, enum stream_frame_kind kind, DescType type)
{
    if ((kind == stream_scalar && (type == typeAEList || type == typeAERecord)) ||
        (kind == stream_record && type == typeAEList))
        return errAEWrongDataType;
    if (!stream_can_insert(stream))
        return stream_wrong_state;
    struct stream_frame* scope = (struct stream_frame*)NewPtrClear(sizeof *scope);
    if (!scope)
        return memFullErr;
    const OSErr status = kind == stream_scalar ? C_AECreateDesc(type, NULL, 0, &scope->descriptor)
                                               : C_AECreateList(NULL, 0, kind == stream_record, &scope->descriptor);
    if (status) {
        DisposPtr((Ptr)scope);
        return status;
    }
    scope->kind = kind;
    scope->result_type = type;
    scope->parent = stream->top;
    stream->top = scope;
    return noErr;
}

static OSErr stream_insert(struct descriptor_stream* stream, AEDesc* descriptor)
{
    struct stream_frame* scope = stream->top;
    if (!scope) {
        stream->result = *descriptor;
        *descriptor = null_stream_descriptor();
        stream->complete = TRUE;
        return noErr;
    }
    OSErr status = scope->kind == stream_list ? C_AEPutDesc(&scope->descriptor, 0, descriptor)
                                              : C_AEPutKeyDesc(&scope->descriptor, scope->key, descriptor);
    if (!status)
        scope->key_pending = FALSE;
    return status;
}

static OSErr stream_write_data(struct stream_frame* scope, uint32 address, int32 length)
{
    if (!scope || scope->kind != stream_scalar)
        return stream_wrong_state;
    if (length <= 0)
        return noErr;
    const void* data = address ? ROMlib_carbon_try_guest_memory(address, length, 0) : NULL;
    if (!data || (uint32)length > INT32_MAX - scope->length)
        return paramErr;
    const Handle handle = DESC_DATA(&scope->descriptor);
    const uint32 required = scope->length + length;
    const Size capacity = GetHandleSize(handle);
    if (capacity < 0)
        return MemError();
    if ((uint32)capacity < required) {
        uint64 grown = (uint64)capacity + capacity / 2 + 256;
        const Size extent = grown > required && grown <= INT32_MAX ? grown : required;
        SetHandleSize(handle, extent);
        const OSErr status = MemError();
        if (status)
            return status;
    }
    memcpy((Byte*)STARH(handle) + scope->length, data, length);
    scope->length = required;
    return noErr;
}

static OSErr stream_pop(struct descriptor_stream* stream, enum stream_frame_kind kind)
{
    struct stream_frame* scope = stream->top;
    if (!scope || scope->kind != kind || scope->key_pending)
        return stream_wrong_state;
    if (kind == stream_scalar) {
        SetHandleSize(DESC_DATA(&scope->descriptor), scope->length);
        const OSErr status = MemError();
        if (status)
            return status;
    }
    DESC_TYPE_X(&scope->descriptor) = CL(scope->result_type);
    stream->top = scope->parent;
    const OSErr status = stream_insert(stream, &scope->descriptor);
    if (status) {
        stream->top = scope;
        return status;
    }
    dispose_stream_descriptor(&scope->descriptor);
    DisposPtr((Ptr)scope);
    return noErr;
}

static OSErr stream_write_key(struct descriptor_stream* stream, AEKeyword key)
{
    struct stream_frame* scope = stream->top;
    if (!scope || (scope->kind != stream_record && scope->kind != stream_event) || scope->key_pending)
        return stream_wrong_state;
    scope->key = key;
    scope->key_pending = TRUE;
    return noErr;
}

static OSErr stream_write_descriptor(struct descriptor_stream* stream, DescType type, uint32 address, int32 length)
{
    OSErr status = stream_push(stream, stream_scalar, type);
    if (status)
        return status;
    status = stream_write_data(stream->top, address, length);
    if (!status)
        status = stream_pop(stream, stream_scalar);
    if (status) {
        struct stream_frame* scope = stream->top;
        stream->top = scope->parent;
        dispose_stream_descriptor(&scope->descriptor);
        DisposPtr((Ptr)scope);
    }
    return status;
}

static OSErr stream_copy_descriptor(struct descriptor_stream* stream, const AEDesc* descriptor)
{
    if (!descriptor)
        return paramErr;
    if (!stream_can_insert(stream))
        return stream_wrong_state;
    AEDesc source = *descriptor;
    AEDesc copy = null_stream_descriptor();
    OSErr status =
        DESC_DATA(&source) ? C_AEDuplicateDesc(&source, &copy) : C_AECreateDesc(DESC_TYPE(&source), NULL, 0, &copy);
    if (!status)
        status = stream_insert(stream, &copy);
    dispose_stream_descriptor(&copy);
    return status;
}

static struct descriptor_stream* stream_adopt_event(AEDesc* event)
{
    if (!event || DESC_TYPE(event) != typeAppleEvent || !DESC_DATA(event))
        return NULL;
    struct descriptor_stream* stream = new_descriptor_stream();
    if (!stream)
        return NULL;
    stream->top = (struct stream_frame*)NewPtrClear(sizeof *stream->top);
    if (!stream->top) {
        dispose_descriptor_stream(stream);
        return NULL;
    }
    const OSErr status = C_AEGetAttributeDesc(event, stream_optional_key, typeAEList, &stream->optional_keys);
    if (status && status != errAEDescNotFound) {
        dispose_descriptor_stream(stream);
        return NULL;
    }
    stream->top->kind = stream_event;
    stream->top->result_type = typeAppleEvent;
    stream->top->descriptor = *event;
    *event = null_stream_descriptor();
    return stream;
}

static struct descriptor_stream* stream_create_event(const uint32* words)
{
    const int32 length = words[4];
    const void* bytes = words[3] && length > 0 ? ROMlib_carbon_try_guest_memory(words[3], length, 0) : NULL;
    if (length < 0 || (length && !bytes))
        return NULL;
    AEDesc target = null_stream_descriptor(), event = null_stream_descriptor();
    if (C_AECreateDesc(words[2], (Ptr)bytes, length, &target))
        return NULL;
    OSErr status = C_AECreateAppleEvent(words[0], words[1], &target, (int16)words[5], words[6], &event);
    dispose_stream_descriptor(&target);
    struct descriptor_stream* stream = status ? NULL : stream_adopt_event(&event);
    dispose_stream_descriptor(&event);
    return stream;
}

static OSErr stream_mark_optional(struct descriptor_stream* stream, AEKeyword key)
{
    struct stream_frame* root = stream->top;
    if (root)
        while (root->parent)
            root = root->parent;
    if (!root || root->kind != stream_event)
        return -1707;
    OSErr status = DESC_DATA(&stream->optional_keys) ? noErr : C_AECreateList(NULL, 0, FALSE, &stream->optional_keys);
    AEDesc descriptor = null_stream_descriptor();
    const uint32 encoded = CL(key);
    if (!status)
        status = C_AECreateDesc(stream_keyword_type, (Ptr)&encoded, sizeof encoded, &descriptor);
    if (!status)
        status = C_AEPutDesc(&stream->optional_keys, 0, &descriptor);
    dispose_stream_descriptor(&descriptor);
    return status;
}

static OSErr stream_close(struct descriptor_stream* stream, AEDesc* output)
{
    OSErr status = noErr;
    if (output) {
        *output = null_stream_descriptor();
        if (stream->top && stream->top->kind == stream_event && !stream->top->key_pending) {
            if (DESC_DATA(&stream->optional_keys))
                status = C_AEPutAttributeDesc(&stream->top->descriptor, stream_optional_key, &stream->optional_keys);
            if (!status)
                status = stream_pop(stream, stream_event);
        }
        if (!status && stream->top)
            status = stream_wrong_state;
        if (!status && stream->complete) {
            *output = stream->result;
            stream->result = null_stream_descriptor();
        }
    }
    dispose_descriptor_stream(stream);
    return status;
}

int ROMlib_carbon_ae_stream(enum carbon_ae_stream_operation operation, const char* name, ROMlib_carbon_ppc_frame* call)
{
    const uint32* words = call->words;
    struct descriptor_stream* stream;
    if (operation == ae_stream_open || operation == ae_stream_open_event || operation == ae_stream_create_event) {
        if (operation == ae_stream_open)
            stream = new_descriptor_stream();
        else if (operation == ae_stream_create_event)
            stream = stream_create_event(words);
        else {
            AEDesc* event = words[0] ? ROMlib_carbon_try_guest_memory(words[0], sizeof *event, 1) : NULL;
            stream = stream_adopt_event(event);
        }
        return descriptor_result(call, stream ? US_TO_SYN68K(stream) : 0);
    }
    stream = words[0] ? ROMlib_carbon_try_guest_memory(words[0], sizeof *stream, 1) : NULL;
    if (!stream || stream->signature != stream_signature) {
        ROMlib_carbon_reference reference;
        if (words[0] && ROMlib_carbon_probe_reference("CarbonLib", name, &reference))
            return ROMlib_carbon_invoke_frame(reference.function, call);
        return descriptor_result(call, paramErr);
    }
    OSErr status;
    switch (operation) {
    case ae_stream_close: {
        AEDesc* output = words[1] ? ROMlib_carbon_try_guest_memory(words[1], sizeof *output, 1) : NULL;
        status = words[1] && !output ? paramErr : stream_close(stream, output);
        break;
    }
    case ae_stream_open_desc:
        status = stream_push(stream, stream_scalar, words[1]);
        break;
    case ae_stream_close_desc:
        status = stream_pop(stream, stream_scalar);
        break;
    case ae_stream_write_data:
        status = stream_write_data(stream->top, words[1], words[2]);
        break;
    case ae_stream_write_desc:
        status = stream_write_descriptor(stream, words[1], words[2], words[3]);
        break;
    case ae_stream_write_aedesc:
        status = stream_copy_descriptor(stream,
                                        words[1] ? ROMlib_carbon_try_guest_memory(words[1], sizeof(AEDesc), 0) : NULL);
        break;
    case ae_stream_open_list:
        status = stream_push(stream, stream_list, typeAEList);
        break;
    case ae_stream_close_list:
        status = stream_pop(stream, stream_list);
        break;
    case ae_stream_open_record:
        status = stream_push(stream, stream_record, words[1]);
        break;
    case ae_stream_close_record:
        status = stream_pop(stream, stream_record);
        break;
    case ae_stream_record_type:
        status = words[1] == typeAEList ? errAEWrongDataType : stream_wrong_state;
        if (words[1] != typeAEList && stream->top && stream->top->kind == stream_record && !stream->top->key_pending) {
            stream->top->result_type = words[1];
            status = noErr;
        }
        break;
    case ae_stream_write_key:
        status = stream_write_key(stream, words[1]);
        break;
    case ae_stream_open_key_desc:
    case ae_stream_write_key_desc:
        status = stream_write_key(stream, words[1]);
        if (!status) {
            status = operation == ae_stream_open_key_desc
                         ? stream_push(stream, stream_scalar, words[2])
                         : stream_write_descriptor(stream, words[2], words[3], words[4]);
            if (status)
                stream->top->key_pending = FALSE;
        }
        break;
    case ae_stream_optional_param:
        status = stream_mark_optional(stream, words[1]);
        break;
    default:
        status = paramErr;
        break;
    }
    return descriptor_result(call, status);
}

/* Classic CFM va_list references consecutive big-endian parameter words. */
struct descriptor_builder {
    const char* start;
    const char* cursor;
    const ROMlib_carbon_ppc_frame* call;
    uint32 va_address;
    unsigned argument;
    unsigned syntax;
    Boolean indirect;
    Boolean fallback;
};

enum {
    ae_build_syntax = -1740,
    ae_build_short = 0x73686f72,
    ae_build_long = 0x6c6f6e67,
    ae_build_bool = 0x626f6f6c,
    ae_build_float = 0x73696e67,
    ae_build_double = 0x646f7562,
    ae_build_extended = 0x65787465,
    ae_build_text = 0x54455854,
    ae_build_enum = 0x656e756d,
};

static const char* builder_guest_string(uint32 address, Size* size)
{
    if (address) {
        for (uint32 length = 0; length < INT32_MAX && length <= UINT32_MAX - address; ++length) {
            const Byte* byte = ROMlib_carbon_try_guest_memory(address + length, 1, 0);
            if (!byte)
                break;
            if (!*byte) {
                *size = length;
                return ROMlib_carbon_try_guest_memory(address, (size_t)length + 1, 0);
            }
        }
    }
    return NULL;
}

static void builder_space(struct descriptor_builder* builder)
{
    while (isspace((Byte)*builder->cursor))
        ++builder->cursor;
}

static OSErr builder_error(struct descriptor_builder* builder, unsigned code)
{
    builder->syntax = code;
    return ae_build_syntax;
}

static int builder_word(struct descriptor_builder* builder, uint32* value)
{
    if (!builder->indirect)
        return ROMlib_carbon_frame_word(builder->call, builder->argument++, value);
    const uint32 address = builder->va_address;
    const Byte* word = address && address <= UINT32_MAX - 4 ? ROMlib_carbon_try_guest_memory(address, 4, 0) : NULL;
    if (!word)
        return 0;
    *value = descriptor_word(word);
    builder->va_address += 4;
    return 1;
}

static int builder_identifier(int c)
{
    return isalpha((Byte)c) || c == '\'';
}

static int builder_delimiter(Byte c)
{
    return !c || isspace(c) || strchr("()[]{},:\"'$@~", c) || c == 0xc7 || c == 0xc8;
}

static OSErr builder_code(struct descriptor_builder* builder, DescType* code)
{
    const Boolean quoted = *builder->cursor == '\'';
    builder->cursor += quoted;
    const char* first = builder->cursor;
    while (*builder->cursor && (quoted ? *builder->cursor != '\'' : !builder_delimiter(*builder->cursor)))
        ++builder->cursor;
    if (quoted && !*builder->cursor)
        return builder_error(builder, 5);
    *code = 0;
    const size_t length = builder->cursor - first;
    for (unsigned i = 0; i < 4; ++i)
        *code = ((uint32)*code << 8) | (i < length ? (Byte)first[i] : ' ');
    builder->cursor += quoted;
    return noErr;
}

static OSErr builder_number(struct descriptor_builder* builder, AEDesc* output, DescType type, int32 number)
{
    if (!type)
        type = number >= INT16_MIN && number <= INT16_MAX ? ae_build_short : ae_build_long;
    if (type == ae_build_short) {
        const uint16 value = CW(number);
        return number < INT16_MIN || number > INT16_MAX ? errAECoercionFail
                                                        : C_AECreateDesc(type, (Ptr)&value, 2, output);
    }
    if (type == ae_build_long) {
        const uint32 value = CL(number);
        return C_AECreateDesc(type, (Ptr)&value, 4, output);
    }
    if (type == ae_build_bool) {
        const Byte value = number != 0;
        return C_AECreateDesc(type, (Ptr)&value, 1, output);
    }
    builder->fallback = TRUE;
    return errAECoercionFail;
}

static OSErr builder_substitution(struct descriptor_builder* builder, DescType type, AEDesc* output)
{
    const Boolean from_handle = *++builder->cursor == '@';
    builder->cursor += from_handle;
    if (!type && from_handle)
        return builder_error(builder, 19);
    uint32 word;
    if (!builder_word(builder, &word))
        return paramErr;
    if (!type) {
        const AEDesc* source = word ? ROMlib_carbon_try_guest_memory(word, sizeof *source, 0) : NULL;
        if (!source)
            return paramErr;
        AEDesc value = *source;
        return DESC_DATA(&value) ? C_AEDuplicateDesc(&value, output)
                                 : C_AECreateDesc(DESC_TYPE(&value), NULL, 0, output);
    }
    if (from_handle) {
        const Handle handle = word ? ROMlib_carbon_try_guest_memory(word, sizeof(HIDDEN_Ptr), 0) : NULL;
        if (!handle || !STARH(handle))
            return paramErr;
        const Size size = GetHandleSize(handle);
        const SignedByte state = HGetState(handle);
        HLock(handle);
        const OSErr status = size < 0 ? paramErr : C_AECreateDesc(type, STARH(handle), size, output);
        HSetState(handle, state);
        return status;
    }
    if (type == ae_build_long || type == ae_build_short || type == ae_build_bool)
        return builder_number(builder, output, type, type == ae_build_long ? (int32)word : (int16)word);
    if (type == ae_build_float || type == ae_build_double || type == ae_build_extended) {
        builder->fallback = TRUE;
        return errAECoercionFail;
    }
    if (type == ae_build_text) {
        Size size;
        const char* string = builder_guest_string(word, &size);
        return string ? C_AECreateDesc(type, (Ptr)string, size, output) : paramErr;
    }
    uint32 address;
    if (word > INT32_MAX || !builder_word(builder, &address))
        return paramErr;
    const void* bytes = word && address ? ROMlib_carbon_try_guest_memory(address, word, 0) : NULL;
    return word && !bytes ? paramErr : C_AECreateDesc(type, (Ptr)bytes, word, output);
}

static int builder_nibble(Byte c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static OSErr builder_hex(struct descriptor_builder* builder, DescType type, AEDesc* output)
{
    if (!type)
        return builder_error(builder, 9);
    const Byte close = (Byte)*builder->cursor++ == '$' ? '$' : 0xc8;
    const char* first = builder->cursor;
    size_t digits = 0;
    while (*builder->cursor && (Byte)*builder->cursor != close) {
        const Byte c = *builder->cursor++;
        if (isspace(c))
            continue;
        if (builder_nibble(c) < 0)
            return builder_error(builder, 6);
        ++digits;
    }
    if (!*builder->cursor)
        return builder_error(builder, 8);
    if (digits & 1)
        return builder_error(builder, 7);
    Byte* bytes = digits ? malloc(digits / 2) : NULL;
    if (digits && !bytes)
        return memFullErr;
    size_t index = 0;
    while (first < builder->cursor) {
        const int nibble = builder_nibble(*first++);
        if (nibble < 0)
            continue;
        if (index & 1)
            bytes[index / 2] |= nibble;
        else
            bytes[index / 2] = nibble << 4;
        ++index;
    }
    ++builder->cursor;
    const OSErr status = C_AECreateDesc(type, (Ptr)bytes, digits / 2, output);
    free(bytes);
    return status;
}

static OSErr builder_value(struct descriptor_builder* builder, AEDesc* output, unsigned depth);

static OSErr builder_members(struct descriptor_builder* builder, AEDesc* descriptor, struct descriptor_stream* event,
                             int closing, unsigned depth)
{
    const Boolean keyed = event || closing == '}';
    builder_space(builder);
    while ((Byte)*builder->cursor != closing) {
        const Boolean optional = event && *builder->cursor == '~';
        builder->cursor += optional;
        if (optional)
            builder_space(builder);
        DescType key = 0;
        if (keyed) {
            if (!builder_identifier(*builder->cursor))
                return builder_error(builder, 16);
            OSErr status = builder_code(builder, &key);
            if (status)
                return status;
            builder_space(builder);
            if (*builder->cursor != ':')
                return builder_error(builder, 17);
            ++builder->cursor;
        }
        AEDesc item = null_stream_descriptor();
        OSErr status = builder_value(builder, &item, depth + 1);
        if (!status)
            status = keyed ? C_AEPutKeyDesc(descriptor, key, &item) : C_AEPutDesc(descriptor, 0, &item);
        dispose_stream_descriptor(&item);
        if (!status && optional)
            status = stream_mark_optional(event, key);
        if (status)
            return status;
        builder_space(builder);
        if ((Byte)*builder->cursor == closing)
            break;
        if (*builder->cursor != ',')
            return builder_error(builder, keyed ? 15 : 14);
        ++builder->cursor;
        builder_space(builder);
    }
    builder->cursor += closing != 0;
    return noErr;
}

static OSErr builder_data(struct descriptor_builder* builder, DescType type, AEDesc* output)
{
    const Byte next = *builder->cursor;
    if (next == '@')
        return builder_substitution(builder, type, output);
    if (next == '$' || next == 0xc7)
        return builder_hex(builder, type, output);
    if (next == '"') {
        const char* first = ++builder->cursor;
        while (*builder->cursor && *builder->cursor != '"')
            ++builder->cursor;
        if (!*builder->cursor)
            return builder_error(builder, 10);
        const Size size = builder->cursor++ - first;
        return C_AECreateDesc(type ? type : ae_build_text, (Ptr)first, size, output);
    }
    if (isdigit(next) || next == '-') {
        const Boolean negative = next == '-';
        builder->cursor += negative;
        if (!isdigit((Byte)*builder->cursor))
            return builder_error(builder, 4);
        uint64 magnitude = 0;
        while (isdigit((Byte)*builder->cursor)) {
            magnitude = magnitude * 10 + (*builder->cursor++ - '0');
            if (magnitude > (uint64)INT32_MAX + negative) {
                builder->fallback = TRUE;
                return errAECoercionFail;
            }
        }
        return builder_number(builder, output, type, negative ? -(int64)magnitude : (int64)magnitude);
    }
    if (builder_identifier(next)) {
        DescType code;
        OSErr status = builder_code(builder, &code);
        if (status)
            return status;
        const uint32 encoded = CL(code);
        return C_AECreateDesc(type ? type : ae_build_enum, (Ptr)&encoded, 4, output);
    }
    return builder_error(builder, next ? 1 : 2);
}

static OSErr builder_value(struct descriptor_builder* builder, AEDesc* output, unsigned depth)
{
    if (depth >= 128)
        return builder_error(builder, 11);
    builder_space(builder);
    DescType type = 0;
    if (builder_identifier(*builder->cursor)) {
        OSErr status = builder_code(builder, &type);
        if (status)
            return status;
        builder_space(builder);
        if (*builder->cursor != '(' && *builder->cursor != '[' && *builder->cursor != '{') {
            const uint32 encoded = CL(type);
            return C_AECreateDesc(ae_build_enum, (Ptr)&encoded, 4, output);
        }
    }
    const int opening = *builder->cursor;
    if (opening == '[' || opening == '{') {
        if (opening == '[' && type)
            return builder_error(builder, 18);
        ++builder->cursor;
        OSErr status = C_AECreateList(NULL, 0, opening == '{', output);
        if (!status)
            status = builder_members(builder, output, NULL, opening == '[' ? ']' : '}', depth);
        if (!status && type)
            DESC_TYPE_X(output) = CL(type);
        return status;
    }
    if (opening != '(')
        return builder_data(builder, 0, output);
    ++builder->cursor;
    builder_space(builder);
    OSErr status = *builder->cursor == ')' ? C_AECreateDesc(type ? type : typeNull, NULL, 0, output)
                                           : builder_data(builder, type, output);
    if (status)
        return status;
    builder_space(builder);
    if (*builder->cursor != ')')
        return builder_error(builder, 13);
    ++builder->cursor;
    return noErr;
}

static OSErr builder_event(struct descriptor_builder* builder, AEDesc* output, AEDesc* result, int create)
{
    struct descriptor_stream* stream;
    if (create)
        stream = stream_create_event(builder->call->words);
    else {
        if (DESC_TYPE(output) != typeAppleEvent || !DESC_DATA(output))
            return -1707;
        AEDesc source = *output, copy = null_stream_descriptor();
        const OSErr status = C_AEDuplicateDesc(&source, &copy);
        if (status)
            return status;
        stream = stream_adopt_event(&copy);
        dispose_stream_descriptor(&copy);
    }
    if (!stream)
        return create ? paramErr : memFullErr;
    const OSErr status = builder_members(builder, &stream->top->descriptor, stream, 0, 0);
    if (!status)
        return stream_close(stream, result);
    dispose_descriptor_stream(stream);
    return status;
}

int ROMlib_carbon_ae_build(const char* name, int event_kind, Boolean indirect, ROMlib_carbon_ppc_frame* call)
{
    const unsigned first = event_kind == 2 ? 7 : 0;
    uint32 output_address, error_address, format_address, arguments = 0;
    if (!ROMlib_carbon_frame_word(call, first, &output_address) ||
        !ROMlib_carbon_frame_word(call, first + 1, &error_address) ||
        !ROMlib_carbon_frame_word(call, first + 2, &format_address) ||
        (indirect && !ROMlib_carbon_frame_word(call, first + 3, &arguments)))
        return descriptor_result(call, paramErr);
    AEDesc* output = output_address ? ROMlib_carbon_try_guest_memory(output_address, sizeof *output, 1) : NULL;
    void* error = error_address ? ROMlib_carbon_try_guest_memory(error_address, 8, 1) : NULL;
    Size size;
    const char* format = !format_address && event_kind ? "" : builder_guest_string(format_address, &size);
    if (!output || (error_address && !error) || !format)
        return descriptor_result(call, paramErr);
    if (error)
        memset(error, 0, 8);
    struct descriptor_builder builder = {format, format, call, arguments, first + 3, 0, indirect, FALSE};
    AEDesc result = null_stream_descriptor();
    OSErr status;
    if (event_kind)
        status = builder_event(&builder, output, &result, event_kind == 2);
    else {
        status = builder_value(&builder, &result, 0);
        builder_space(&builder);
        if (!status && *builder.cursor)
            status = builder_error(&builder, 3);
    }
    if (status) {
        dispose_stream_descriptor(&result);
        if (builder.fallback) {
            ROMlib_carbon_reference reference;
            if (ROMlib_carbon_probe_reference("CarbonLib", name, &reference))
                return ROMlib_carbon_invoke_frame(reference.function, call);
        }
        if (event_kind != 1)
            *output = null_stream_descriptor();
        if (error && builder.syntax) {
            const uint32 words[] = {CL(builder.syntax), CL(builder.cursor - builder.start)};
            memcpy(error, words, sizeof words);
        }
    }
    else {
        if (event_kind == 1)
            dispose_stream_descriptor(output);
        *output = result;
    }
    return descriptor_result(call, status);
}

struct descriptor_printer {
    char* bytes;
    size_t length;
    size_t capacity;
    Boolean allocation_failed;
};

static int printer_append(struct descriptor_printer* output, const void* bytes, size_t length)
{
    if (length > INT32_MAX - output->length) {
        output->allocation_failed = TRUE;
        return 0;
    }
    const size_t required = output->length + length;
    if (required > output->capacity) {
        size_t grown = output->capacity + output->capacity / 2 + 64;
        if (grown < required || grown > INT32_MAX)
            grown = required;
        char* replacement = realloc(output->bytes, grown);
        if (!replacement) {
            output->allocation_failed = TRUE;
            return 0;
        }
        output->bytes = replacement;
        output->capacity = grown;
    }
    if (length)
        memcpy(output->bytes + output->length, bytes, length);
    output->length = required;
    return 1;
}

static int printer_literal(struct descriptor_printer* output, const char* text)
{
    return printer_append(output, text, strlen(text));
}

static int printer_code(struct descriptor_printer* output, uint32 code)
{
    char quoted[6] = {'\'', 0, 0, 0, 0, '\''};
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned byte = (code >> (24 - i * 8)) & 255;
        if (byte < 32 || byte >= 127 || byte == '\'')
            return 0;
        quoted[i + 1] = byte;
    }
    return printer_append(output, quoted, sizeof quoted);
}

static int printer_value(struct descriptor_printer* output, uint32 type, const Byte* data, size_t length,
                         unsigned depth);

static int printer_items(struct descriptor_printer* output, const Byte* data, size_t length, uint32 count,
                         const char* prefix, Boolean separated, unsigned depth)
{
    const size_t header = prefix ? sizeof(inline_key_desc_t) : sizeof(inline_desc_t);
    while (count--) {
        if (length < header)
            return 0;
        const size_t item_size = descriptor_word(data + header - 4);
        if (item_size > length - header)
            return 0;
        if (separated && !printer_literal(output, ", "))
            return 0;
        if (prefix && (!printer_literal(output, prefix) || !printer_code(output, descriptor_word(data)) ||
                       !printer_literal(output, ":")))
            return 0;
        if (!printer_value(output, descriptor_word(data + header - 8), data + header, item_size, depth + 1))
            return 0;
        data += header + item_size;
        length -= header + item_size;
        separated = TRUE;
    }
    return 1;
}

static int printer_event(struct descriptor_printer* output, const Byte* data, size_t length, unsigned depth)
{
    if (length < sizeof(ae_header_t))
        return 0;
    const size_t target_size = descriptor_word(data + offsetof(ae_header_t, target.size));
    const size_t target_start = offsetof(ae_header_t, target.data);
    if (target_size > length - target_start)
        return 0;
    const uint32 parameters = descriptor_word(data + offsetof(ae_header_t, param_count));
    const uint32 attributes = descriptor_word(data + offsetof(ae_header_t, attribute_count));
    const size_t param_start = descriptor_word(data + offsetof(ae_header_t, param_offset));
    if (param_start > length + 2 || (parameters && param_start > length) || param_start < target_start + target_size)
        return 0;
    if (!printer_code(output, descriptor_word(data + offsetof(ae_header_t, event_class))) ||
        !printer_literal(output, "\\") ||
        !printer_code(output, descriptor_word(data + offsetof(ae_header_t, event_id))) ||
        !printer_literal(output, "{") ||
        (parameters &&
         !printer_items(output, data + param_start, length - param_start, parameters, "", FALSE, depth)) ||
        (parameters && !printer_literal(output, ", ")) || !printer_literal(output, "&addr:") ||
        !printer_value(output, descriptor_word(data + offsetof(ae_header_t, target.type)), data + target_start,
                       target_size, depth + 1))
        return 0;
    if (attributes) {
        /* The classic Attribute Manager locates its first inline key with this
           field, including when the address descriptor contains a process ID. */
        const size_t attribute_header = offsetof(ae_header_t, target) + sizeof(inline_key_desc_t);
        if (param_start > length || length < attribute_header + 8)
            return 0;
        const uint64 start = (uint64)attribute_header + 8 + descriptor_word(data + attribute_header - 4);
        if (start > param_start ||
            !printer_items(output, data + start, param_start - start, attributes, "&", TRUE, depth))
            return 0;
    }
    return printer_literal(output, "}");
}

static int printer_raw(struct descriptor_printer* output, uint32 type, const Byte* data, size_t length)
{
    if (!printer_code(output, type) || !printer_literal(output, length ? "($" : "()"))
        return 0;
    if (!length)
        return 1;
    const char hex[] = "0123456789ABCDEF";
    while (length) {
        char encoded[128];
        const size_t chunk = length < sizeof encoded / 2 ? length : sizeof encoded / 2;
        for (size_t i = 0; i < chunk; ++i) {
            encoded[2 * i] = hex[data[i] >> 4];
            encoded[2 * i + 1] = hex[data[i] & 15];
        }
        if (!printer_append(output, encoded, chunk * 2))
            return 0;
        data += chunk;
        length -= chunk;
    }
    return printer_literal(output, "$)");
}

static int printer_value(struct descriptor_printer* output, uint32 type, const Byte* data, size_t length,
                         unsigned depth)
{
    if (depth >= 128)
        return 0;
    if (type == typeAppleEvent)
        return printer_event(output, data, length, depth);
    if (length >= sizeof(list_header_t) &&
        descriptor_word(data + offsetof(list_header_t, param_offset)) == sizeof(list_header_t)) {
        const uint32 marker = descriptor_word(data + offsetof(list_header_t, attribute_count));
        if (marker == typeAERecord || (marker == typeAEList && type == typeAEList)) {
            const Boolean record = marker == typeAERecord;
            if ((record && type != typeAERecord && !printer_code(output, type)) ||
                !printer_literal(output, record ? "{" : "["))
                return 0;
            const uint32 items = descriptor_word(data + offsetof(list_header_t, param_count));
            return printer_items(output, data + sizeof(list_header_t), length - sizeof(list_header_t), items,
                                 record ? "" : NULL, FALSE, depth) &&
                   printer_literal(output, record ? "}" : "]");
        }
    }
    if (type == typeAEList || type == typeAERecord)
        return 0;
    if (type == ae_build_text && (!length || (!memchr(data, '"', length) && !memchr(data, 0, length))))
        return printer_literal(output, "\"") && printer_append(output, data, length) && printer_literal(output, "\"");
    if ((type == ae_build_short && length == 2) || (type == ae_build_long && length == 4)) {
        char decimal[24];
        const int32 number = length == 2 ? (int16)(((uint16)data[0] << 8) | data[1]) : (int32)descriptor_word(data);
        const int n = snprintf(decimal, sizeof decimal, length == 2 ? "%d" : "long(%d)", number);
        return n > 0 && printer_append(output, decimal, n);
    }
    return printer_raw(output, type, data, length);
}

int ROMlib_carbon_AEPrintDescToHandle(ROMlib_carbon_ppc_frame* call)
{
    const uint32 input = call->words[0], destination = call->words[1];
    void* result = destination ? ROMlib_carbon_try_guest_memory(destination, sizeof(uint32), 1) : NULL;
    if (!result)
        return descriptor_result(call, paramErr);
    memset(result, 0, sizeof(uint32));
    const AEDesc* descriptor = input ? ROMlib_carbon_try_guest_memory(input, sizeof *descriptor, 0) : NULL;
    if (!descriptor)
        return descriptor_result(call, paramErr);

    const Handle handle = DESC_DATA(descriptor);
    const Size length = handle ? GetHandleSize(handle) : 0;
    const Byte* data = handle ? (const Byte*)STARH(handle) : NULL;
    struct descriptor_printer output = {0};
    const int printed =
        length >= 0 && (!length || data) && printer_value(&output, DESC_TYPE(descriptor), data, length, 0);
    if (!printed) {
        free(output.bytes);
        ROMlib_carbon_reference reference;
        if (!output.allocation_failed && ROMlib_carbon_probe_reference("CarbonLib", "AEPrintDescToHandle", &reference))
            return ROMlib_carbon_invoke_frame(reference.function, call);
        return descriptor_result(call, output.allocation_failed ? memFullErr : errAEWrongDataType);
    }
    const Handle text = NewHandle(output.length);
    if (text && output.length)
        memcpy(STARH(text), output.bytes, output.length);
    free(output.bytes);
    const uint32 encoded = text ? CL(US_TO_SYN68K(text)) : 0;
    memcpy(result, &encoded, sizeof encoded);
    return descriptor_result(call, text ? noErr : memFullErr);
}

/* ROMAW lists store full descriptors with optional keyword fields. Read and
   write that representation directly so arrays also work with the live list APIs. */
struct apple_event_array {
    const Byte* cursor;
    size_t remaining;
    uint32 count;
    Boolean keyed;
};

struct apple_event_array_item {
    uint32 keyword;
    DescType type;
    uint32 size;
    const Byte* bytes;
    Handle handle;
};

static int apple_event_array_view(const AEDescList* list, struct apple_event_array* array)
{
    const Handle handle = DESC_DATA(list);
    if (!handle || !STARH(handle))
        return 0;
    const Size size = GetHandleSize(handle);
    if (size < (Size)sizeof(list_header_t))
        return 0;
    const Byte* bytes = (const Byte*)STARH(handle);
    if (descriptor_word(bytes + offsetof(list_header_t, param_offset)) != sizeof(list_header_t) ||
        descriptor_word(bytes + offsetof(list_header_t, attribute_count)) != (uint32)DESC_TYPE(list))
        return 0;
    array->keyed = DESC_TYPE(list) == typeAERecord;
    array->count = descriptor_word(bytes + offsetof(list_header_t, param_count));
    array->cursor = bytes + sizeof(list_header_t);
    array->remaining = size - sizeof(list_header_t);
    return 1;
}

static int apple_event_array_next(struct apple_event_array* array, struct apple_event_array_item* item)
{
    const size_t header = array->keyed ? sizeof(inline_key_desc_t) : sizeof(inline_desc_t);
    if (array->remaining < header)
        return 0;
    item->size = descriptor_word(array->cursor + header - 4);
    if (item->size > array->remaining - header)
        return 0;
    item->keyword = array->keyed ? descriptor_word(array->cursor) : typeWildCard;
    item->type = descriptor_word(array->cursor + header - 8);
    item->bytes = array->cursor + header;
    array->cursor += header + item->size;
    array->remaining -= header + item->size;
    return 1;
}

static size_t apple_event_array_stride(AEArrayType representation, uint32 item_size)
{
    switch (representation) {
    case kAEDataArray:
        return ((size_t)item_size + 1) & ~(size_t)1;
    case kAEPackedArray:
        return item_size;
    case kAEHandleArray:
        return sizeof(HIDDEN_Handle);
    case kAEDescArray:
        return sizeof(AEDesc);
    case kAEKeyDescArray:
        return sizeof(AEKeyDesc);
    default:
        return 0;
    }
}

static void apple_event_array_word(void* output, uint32 value)
{
    value = CL(value);
    memcpy(output, &value, sizeof value);
}

static int apple_event_array_fallback(const char* name, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_probe_reference("CarbonLib", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return descriptor_result(call, errAEWrongDataType);
}

int ROMlib_carbon_AEGetArray(ROMlib_carbon_ppc_frame* call)
{
    uint32 count_address;
    if (!ROMlib_carbon_frame_word(call, 6, &count_address))
        return 0;
    const uint32* words = call->words;
    const AEArrayType representation = words[1];
    const int32 capacity = words[3];
    const AEDescList* list = words[0] ? ROMlib_carbon_try_guest_memory(words[0], sizeof *list, 0) : NULL;
    if (!list || capacity < 0 || representation > kAEKeyDescArray)
        return descriptor_result(call, paramErr);
    if (DESC_TYPE(list) != typeAEList && DESC_TYPE(list) != typeAERecord)
        return descriptor_result(call, errAEWrongDataType);
    struct apple_event_array array;
    if (!apple_event_array_view(list, &array))
        return apple_event_array_fallback("AEGetArray", call);
    if (representation == kAEKeyDescArray && !array.keyed)
        return descriptor_result(call, errAEWrongDataType);
    const Boolean raw = representation <= kAEPackedArray;
    const Boolean uniform_type = representation <= kAEHandleArray;
    struct apple_event_array scan = array;
    struct apple_event_array_item first = {0};
    for (uint32 i = 0; i < array.count; ++i) {
        struct apple_event_array_item item;
        if (!apple_event_array_next(&scan, &item))
            return descriptor_result(call, errAEWrongDataType);
        if (!i)
            first = item;
        if ((uniform_type && item.type != first.type) || (raw && item.size != first.size))
            return descriptor_result(call, errAEWrongDataType);
    }
    if (scan.remaining)
        return descriptor_result(call, errAEWrongDataType);
    const size_t stride = apple_event_array_stride(representation, first.size);
    const uint32 count = stride && (size_t)capacity / stride < array.count ? (size_t)capacity / stride : array.count;
    const size_t extent = count * stride;
    void* type_out = uniform_type && words[4] ? ROMlib_carbon_try_guest_memory(words[4], 4, 1) : NULL;
    void* size_out = raw && words[5] ? ROMlib_carbon_try_guest_memory(words[5], 4, 1) : NULL;
    void* count_out = count_address ? ROMlib_carbon_try_guest_memory(count_address, 4, 1) : NULL;
    Byte* data = extent && words[2] ? ROMlib_carbon_try_guest_memory(words[2], extent, 1) : NULL;
    if ((uniform_type && words[4] && !type_out) || (raw && words[5] && !size_out) || (count_address && !count_out) ||
        (extent && !data))
        return descriptor_result(call, paramErr);

    const Handle source = DESC_DATA(list);
    const SignedByte state = HGetState(source);
    HLock(source);
    uint32 i = 0;
    OSErr status = noErr;
    for (; i < count; ++i) {
        struct apple_event_array_item item;
        if (!apple_event_array_next(&array, &item)) {
            status = errAEWrongDataType;
            break;
        }
        if (raw) {
            if (stride) {
                memmove(data + i * stride, item.bytes, item.size);
                if (stride > item.size)
                    data[i * stride + item.size] = 0;
            }
            continue;
        }
        AEDesc copy = null_stream_descriptor();
        status = C_AECreateDesc(item.type, (Ptr)item.bytes, item.size, &copy);
        if (status)
            break;
        Byte* slot = data + i * stride;
        if (representation == kAEHandleArray)
            memcpy(slot, &DESC_DATA_X(&copy), sizeof(HIDDEN_Handle));
        else {
            if (representation == kAEKeyDescArray) {
                apple_event_array_word(slot, item.keyword);
                slot += sizeof(AEKeyword);
            }
            memcpy(slot, &copy, sizeof copy);
        }
    }
    HSetState(source, state);
    if (status && !raw) {
        while (i) {
            Byte* slot = data + --i * stride;
            HIDDEN_Handle handle;
            memcpy(&handle, slot + stride - sizeof handle, sizeof handle);
            DisposHandle(HIDDEN_NATIVE(handle));
            memset(slot, 0, stride);
        }
    }
    if (status)
        return descriptor_result(call, status);
    if (type_out)
        apple_event_array_word(type_out, array.count ? first.type : typeNull);
    if (size_out)
        apple_event_array_word(size_out, first.size);
    if (count_out)
        apple_event_array_word(count_out, count);
    return descriptor_result(call, noErr);
}

static OSErr apple_event_put_item(const Byte* input, AEArrayType representation, DescType type, uint32 size,
                                  struct apple_event_array_item* item)
{
    item->keyword = 0;
    item->type = type;
    item->handle = NULL;
    item->bytes = input;
    item->size = size;
    if (representation <= kAEPackedArray)
        return noErr;
    if (representation >= kAEDescArray) {
        if (representation == kAEKeyDescArray) {
            item->keyword = descriptor_word(input);
            input += sizeof(AEKeyword);
        }
        item->type = descriptor_word(input);
        input += sizeof(DescType);
    }
    const uint32 handle = descriptor_word(input);
    item->size = 0;
    if (!handle)
        return noErr;
    item->handle = ROMlib_carbon_try_guest_memory(handle, sizeof(HIDDEN_Ptr), 0);
    if (!item->handle || !STARH(item->handle))
        return paramErr;
    const Size length = GetHandleSize(item->handle);
    if (length < 0)
        return paramErr;
    item->size = length;
    return noErr;
}

int ROMlib_carbon_AEPutArray(ROMlib_carbon_ppc_frame* call)
{
    const uint32* words = call->words;
    AEDescList* list = words[0] ? ROMlib_carbon_try_guest_memory(words[0], sizeof *list, 1) : NULL;
    const AEArrayType representation = words[1];
    const int32 count = words[5];
    const int32 size = words[4];
    if (!list || count < 0 || representation > kAEKeyDescArray || (representation <= kAEPackedArray && size < 0))
        return descriptor_result(call, paramErr);
    if (DESC_TYPE(list) != typeAEList && DESC_TYPE(list) != typeAERecord)
        return descriptor_result(call, errAEWrongDataType);
    struct apple_event_array existing;
    if (!apple_event_array_view(list, &existing))
        return apple_event_array_fallback("AEPutArray", call);
    if (representation == kAEKeyDescArray && !existing.keyed)
        return descriptor_result(call, errAEWrongDataType);

    const size_t stride = apple_event_array_stride(representation, size);
    const uint64 extent = (uint64)stride * count;
    if (extent > INT32_MAX)
        return descriptor_result(call, paramErr);
    const Byte* data = extent && words[2] ? ROMlib_carbon_try_guest_memory(words[2], extent, 0) : NULL;
    if (extent && !data)
        return descriptor_result(call, paramErr);
    const size_t header = existing.keyed ? sizeof(inline_key_desc_t) : sizeof(inline_desc_t);
    uint64 total = sizeof(list_header_t) + (uint64)count * header;
    if (total > INT32_MAX)
        return descriptor_result(call, memFullErr);
    for (int32 i = 0; i < count; ++i) {
        struct apple_event_array_item item;
        const OSErr status =
            apple_event_put_item(data ? data + i * stride : NULL, representation, words[3], size, &item);
        if (status)
            return descriptor_result(call, status);
        total += item.size;
        if (total > INT32_MAX)
            return descriptor_result(call, memFullErr);
    }
    const Handle replacement = NewHandleClear(total);
    if (!replacement)
        return descriptor_result(call, memFullErr);
    Byte* cursor = (Byte*)STARH(replacement);
    apple_event_array_word(cursor + offsetof(list_header_t, param_offset), sizeof(list_header_t));
    apple_event_array_word(cursor + offsetof(list_header_t, attribute_count), DESC_TYPE(list));
    apple_event_array_word(cursor + offsetof(list_header_t, param_count), count);
    cursor += sizeof(list_header_t);
    for (int32 i = 0; i < count; ++i) {
        struct apple_event_array_item item;
        const OSErr status =
            apple_event_put_item(data ? data + i * stride : NULL, representation, words[3], size, &item);
        if (status) {
            DisposHandle(replacement);
            return descriptor_result(call, status);
        }
        if (existing.keyed)
            apple_event_array_word(cursor, item.keyword);
        apple_event_array_word(cursor + header - 8, item.type);
        apple_event_array_word(cursor + header - 4, item.size);
        if (item.size)
            memcpy(cursor + header, item.handle ? (const Byte*)STARH(item.handle) : item.bytes, item.size);
        cursor += header + item.size;
    }
    DisposHandle(DESC_DATA(list));
    FIELD_ASSIGN_RAW(DESC_DATA_X(list), RM(replacement));
    return descriptor_result(call, noErr);
}
