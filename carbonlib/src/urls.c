/*+===================================================================
  File:      urls.c

  Summary:   CarbonLib 1.4 URL record access, components and path append control flow.

  Classes:   None.

  Functions: ROMlib_carbon_cf_url_absolute, ROMlib_carbon_cf_url_alloc, ROMlib_carbon_cf_url_append,
             ROMlib_carbon_cf_url_append_extension, ROMlib_carbon_cf_url_base, ROMlib_carbon_cf_url_create_bytes,
             ROMlib_carbon_cf_url_create_data, ROMlib_carbon_cf_url_create_file, ROMlib_carbon_cf_url_create_file_base,
             ROMlib_carbon_cf_url_create_string, ROMlib_carbon_cf_url_decomposable,
             ROMlib_carbon_cf_url_delete_extension, ROMlib_carbon_cf_url_delete_last, ROMlib_carbon_cf_url_directory,
             ROMlib_carbon_cf_url_extension, ROMlib_carbon_cf_url_filesystem_bytes,
             ROMlib_carbon_cf_url_filesystem_path, ROMlib_carbon_cf_url_fragment, ROMlib_carbon_cf_url_from_bytes,
             ROMlib_carbon_cf_url_from_bytes_base, ROMlib_carbon_cf_url_from_fsref, ROMlib_carbon_cf_url_get_fsref,
             ROMlib_carbon_cf_url_host, ROMlib_carbon_cf_url_initialize, ROMlib_carbon_cf_url_last_component,
             ROMlib_carbon_cf_url_net_location, ROMlib_carbon_cf_url_parameters, ROMlib_carbon_cf_url_password,
             ROMlib_carbon_cf_url_path, ROMlib_carbon_cf_url_port, ROMlib_carbon_cf_url_query,
             ROMlib_carbon_cf_url_reserved, ROMlib_carbon_cf_url_resource, ROMlib_carbon_cf_url_scheme,
             ROMlib_carbon_cf_url_set_reserved, ROMlib_carbon_cf_url_strict_path, ROMlib_carbon_cf_url_string,
             ROMlib_carbon_cf_url_type_id, ROMlib_carbon_cf_url_user

  Origin:    Existing repository source in packages/carbonlib/src; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* CarbonLib 1.4 URL record access, components and path append control flow. */
#include "internal.h"

static uint32 url_word(const void* memory, unsigned offset)
{
    uint32 word;
    memcpy(&word, (const Byte*)memory + offset, 4);
    return CL(word);
}

static int url_reference(const char* name, uint32 offset, const uint32* arguments, unsigned count,
                         ROMlib_carbon_reference* reference, uint32* result)
{
    if (!ROMlib_carbon_reference_function("CoreFoundationLib", name, reference))
        return 0;
    if (reference->code_offset == offset && reference->code_size == 0x5dee0 && !reference->toc_offset &&
        reference->toc && reference->toc_size == 0x72a8)
        return 1;
    uint32 answer;
    if (ROMlib_carbon_invoke_callback(reference->function, arguments, count, &answer))
        *result = answer;
    return 0;
}

static int url_call(const char* name, const uint32* arguments, unsigned count, uint32* result)
{
    ROMlib_carbon_reference reference;
    return ROMlib_carbon_reference_function("CoreFoundationLib", name, &reference) &&
           ROMlib_carbon_invoke_callback(reference.function, arguments, count, result);
}

static int url_private(const ROMlib_carbon_reference* reference, uint32 offset, const uint32* arguments, unsigned count,
                       uint32* result)
{
    _Alignas(16) uint32 vector[2] = {CL(url_word(reference->function, 0) - reference->code_offset + offset),
                                     CL(US_TO_SYN68K(reference->toc))};
    return ROMlib_carbon_invoke_callback(vector, arguments, count, result);
}

static unsigned url_style(const void* url)
{
    return (url_word(url, 8) >> 16) & 15;
}

static int url_parsed(const ROMlib_carbon_reference* reference, void* url)
{
    uint32 object = US_TO_SYN68K(url);
    return (url_word(url, 8) & 0x2000) || url_private(reference, 0x61e0, &object, 1, NULL);
}

/* The packed range array contains one entry for every present component bit,
   in ascending bit order. Range words remain guest-endian in the object. */
static void url_range(const void* url, uint32 mask, uint32* location, uint32* length)
{
    uint32 flags = url_word(url, 8);
    *location = UINT32_MAX;
    *length = 0;
    if (!(flags & mask))
        return;
    unsigned index = 0;
    for (uint32 lower = flags & (mask - 1); lower; lower &= lower - 1)
        ++index;
    const void* ranges = SYN68K_TO_US(url_word(url, 20));
    *location = url_word(ranges, index * 8);
    *length = url_word(ranges, index * 8 + 4);
}

static int url_substring(void* url, uint32 location, uint32 length, uint32* result)
{
    uint32 object = US_TO_SYN68K(url), allocator;
    if (!url_call("CFGetAllocator", &object, 1, &allocator))
        return 0;
    uint32 arguments[] = {allocator, url_word(url, 12), location, length};
    return url_call("CFStringCreateWithSubstring", arguments, 4, result);
}

static int url_component(const ROMlib_carbon_reference* reference, void* url, uint32 mask, uint32* result)
{
    if (!url_parsed(reference, url))
        return 0;
    uint32 location, length;
    url_range(url, mask, &location, &length);
    *result = 0;
    return location == UINT32_MAX || url_substring(url, location, length, result);
}

static int url_constant(const ROMlib_carbon_reference* reference, unsigned offset, uint32* result)
{
    uint32 text = url_word(reference->toc, 0x3f8) + offset;
    return url_call("__CFStringMakeConstantString", &text, 1, result);
}

static int url_retain_global(const ROMlib_carbon_reference* reference, unsigned offset, uint32* result)
{
    uint32 object = url_word(reference->toc, offset);
    if (!url_call("CFRetain", &object, 1, NULL))
        return 0;
    *result = url_word(reference->toc, offset);
    return 1;
}

uint32 ROMlib_carbon_cf_url_type_id(void)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (url_reference("CFURLGetTypeID", 0x61d8, NULL, 0, &reference, &result))
        result = url_word(reference.toc, 0x810);
    return result;
}

uint32 ROMlib_carbon_cf_url_decomposable(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCanBeDecomposed", 0x83e0, &object, 1, &reference, &result))
        return result & 255;
    if (!url)
        return 0;
    if (url_style(url) != 15)
        return 1;
    return url_parsed(&reference, url) && (url_word(url, 8) & 0x8000) != 0;
}

void* ROMlib_carbon_cf_url_string(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLGetString", 0x8448, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url)
        return NULL;
    if (url_style(url) != 15 && (!url_word(url, 16) || !(url_word(url, 8) & 0x100000)) &&
        !url_private(&reference, 0x98c8, &object, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(url_word(url, 12));
}

void* ROMlib_carbon_cf_url_base(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (url_reference("CFURLGetBaseURL", 0x84a8, &object, 1, &reference, &result))
        result = url ? url_word(url, 16) : 0;
    return SYN68K_TO_US_CHECK0(result);
}

uint32 ROMlib_carbon_cf_url_directory(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLHasDirectoryPath", 0x89d4, &object, 1, &reference, &result))
        return result & 255;
    while (url) {
        if (url_style(url) == 15) {
            if (!url_parsed(&reference, url))
                return 0;
            uint32 flags = url_word(url, 8), base = url_word(url, 16);
            if (base && !(flags & 0x3e)) {
                url = SYN68K_TO_US(base);
                continue;
            }
        }
        return (url_word(url, 8) & 0x1000) != 0;
    }
    return 0;
}

void* ROMlib_carbon_cf_url_scheme(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyScheme", 0x85c0, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    while (url) {
        if (url_style(url) != 15)
            return url_retain_global(&reference, 0x808, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
        if (!url_component(&reference, url, 1, &result))
            return NULL;
        if (result)
            return SYN68K_TO_US(result);
        url = SYN68K_TO_US_CHECK0(url_word(url, 16));
    }
    return NULL;
}

void* ROMlib_carbon_cf_url_net_location(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyNetLocation", 0x863c, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    while (url) {
        if (url_style(url) != 15)
            return url_retain_global(&reference, 0x80c, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
        if (!url_parsed(&reference, url))
            return NULL;
        if (!(url_word(url, 8) & 0x1e)) {
            url = SYN68K_TO_US_CHECK0(url_word(url, 16));
            continue;
        }
        uint32 location = url_word(reference.toc, 0x226c), length = url_word(reference.toc, 0x2270);
        for (uint32 bit = 2; bit <= 16; bit <<= 1) {
            uint32 at, size;
            url_range(url, bit, &at, &size);
            if (at != UINT32_MAX) {
                if (location == UINT32_MAX) {
                    location = at;
                    length = size;
                }
                else
                    length = at + size - location;
            }
        }
        return url_substring(url, location, length, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
    }
    return NULL;
}

static int url_copy_path(const ROMlib_carbon_reference* reference, void* url, uint32* result)
{
    uint32 style = url_style(url), object = US_TO_SYN68K(url);
    if (!style && (url_word(url, 8) & 0x100000)) {
        uint32 text = url_word(url, 12);
        if (!url_call("CFRetain", &text, 1, NULL))
            return 0;
        *result = url_word(url, 12);
        return 1;
    }
    if (style != 15 && !url_private(reference, 0x98c8, &object, 1, NULL))
        return 0;
    return url_component(reference, url, 32, result);
}

void* ROMlib_carbon_cf_url_path(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (url_reference("CFURLCopyPath", 0x8848, &object, 1, &reference, &result) && url)
        if (!url_copy_path(&reference, url, &result))
            result = 0;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_strict_path(void* url, Byte* absolute)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), US_TO_SYN68K(absolute)}, result = 0;
    if (!url_reference("CFURLCopyStrictPath", 0x88b4, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 path = 0, length = 0;
    if (!url || !url_copy_path(&reference, url, &path))
        return NULL;
    if (path && !url_call("CFStringGetLength", &path, 1, &length))
        return NULL;
    if (!path || !length) {
        if (path && !url_call("CFRelease", &path, 1, NULL))
            return NULL;
        if (absolute)
            *absolute = 0;
        return NULL;
    }
    uint32 character_arguments[] = {path, 0};
    if (!url_call("CFStringGetCharacterAtIndex", character_arguments, 2, &result))
        return NULL;
    if ((result & 65535) != '/') {
        if (absolute)
            *absolute = 0;
        return SYN68K_TO_US(path);
    }
    if (absolute)
        *absolute = 1;
    uint32 allocator;
    if (!url_call("CFStringGetLength", &path, 1, &length) || !url_call("CFGetAllocator", &path, 1, &allocator))
        return NULL;
    uint32 substring[] = {allocator, path, 1, length - 1};
    if (!url_call("CFStringCreateWithSubstring", substring, 4, &result) || !url_call("CFRelease", &path, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_resource(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyResourceSpecifier", 0x8a6c, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url || url_style(url) != 15 || !url_parsed(&reference, url))
        return NULL;
    uint32 flags = url_word(url, 8), location, length;
    if (!(flags & 0x8000)) {
        url_range(url, 1, &location, &length);
        location += length + 1;
    }
    else {
        uint32 selected = 0;
        for (uint32 bit = 0x100; bit != 0x20; bit >>= 1)
            if (flags & bit)
                selected = bit;
        if (!selected)
            return NULL;
        url_range(url, selected, &location, &length);
        --location;
    }
    uint32 text = url_word(url, 12);
    if (!url_call("CFStringGetLength", &text, 1, &length))
        return NULL;
    return url_substring(url, location, length - location, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
}

static unsigned url_separator(unsigned style)
{
    return style == 1 ? ':' : style == 2 ? '\\' : '/';
}

static unsigned url_separator_constant(unsigned style)
{
    return style == 1 ? 0x188 : style == 2 ? 0x1a0 : 0x1aa;
}

/* Native 0xa9c0: trim one final separator, then search the remaining path
   backwards. A root separator is retained as a one-character component. */
static int url_last_range(const ROMlib_carbon_reference* reference, void* url, uint32* location, uint32* length)
{
    unsigned style = url_style(url);
    uint32 text = url_word(url, 12);
    if (style == 15) {
        if (!url_parsed(reference, url))
            return 0;
        url_range(url, 32, location, length);
    }
    else {
        *location = 0;
        if (!url_call("CFStringGetLength", &text, 1, length))
            return 0;
    }
    if (*location == UINT32_MAX || !*length)
        return 1;
    text = url_word(url, 12);
    uint32 get[] = {text, *location + *length - 1}, character;
    if (!url_call("CFStringGetCharacterAtIndex", get, 2, &character))
        return 0;
    if ((character & 65535) == url_separator(style)) {
        --*length;
        if (!*length) {
            *length = 1;
            return 1;
        }
    }
    uint32 separator;
    if (!url_constant(reference, url_separator_constant(style), &separator))
        return 0;
    _Alignas(8) uint32 match[2];
    uint32 find[] = {url_word(url, 12), separator, *location, *length, 4, US_TO_SYN68K(match)}, found;
    if (!url_call("CFStringFindWithOptions", find, 6, &found))
        return 0;
    if (found & 255) {
        uint32 next = CL(match[0]) + 1;
        *length = *location + *length - next;
        *location = next;
    }
    return 1;
}

void* ROMlib_carbon_cf_url_last_component(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyLastPathComponent", 0xaba8, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url)
        return NULL;
    uint32 location, length;
    if (!url_last_range(&reference, url, &location, &length))
        return NULL;
    if (location == UINT32_MAX || !length) {
        if (!url_constant(&reference, 0x19f, &result) || !url_call("CFRetain", &result, 1, &result))
            return NULL;
        return SYN68K_TO_US_CHECK0(result);
    }
    if (length == 1) {
        uint32 get[] = {url_word(url, 12), location}, character;
        if (!url_call("CFStringGetCharacterAtIndex", get, 2, &character))
            return NULL;
        if ((character & 65535) == url_separator(url_style(url))) {
            if (!url_constant(&reference, 0x1aa, &result) || !url_call("CFRetain", &result, 1, &result))
                return NULL;
            return SYN68K_TO_US_CHECK0(result);
        }
    }
    if (!url_substring(url, location, length, &result))
        return NULL;
    if (url_style(url) == 15 && !(url_word(url, 8) & 0x100000)) {
        uint32 substring = result, allocator, empty;
        if (!url_constant(&reference, 0x19f, &empty) || !url_call("CFGetAllocator", &object, 1, &allocator))
            return NULL;
        uint32 decode[] = {allocator, substring, empty};
        if (!url_call("CFURLCreateStringByReplacingPercentEscapes", decode, 3, &result) ||
            !url_call("CFRelease", &substring, 1, NULL))
            return NULL;
    }
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_append(uint32 allocator, void* url, const void* component, uint32 directory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(url), US_TO_SYN68K(component), directory}, result = 0;
    if (!url_reference("CFURLCreateCopyAppendingPathComponent", 0xae24, arguments, 4, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url || !component)
        return NULL;
    unsigned style = url_style(url);
    if (style != 15) {
        uint32 text = US_TO_SYN68K(component), length, separator;
        if (!url_call("CFStringGetLength", &text, 1, &length) ||
            !url_constant(&reference, url_separator_constant(style), &separator))
            return NULL;
        uint32 find[] = {text, separator, 0, length, 0, 0};
        if (!url_call("CFStringFindWithOptions", find, 6, &result))
            return NULL;
        if (result & 255) {
            uint32 object = US_TO_SYN68K(url);
            style = 15;
            if (!url_private(&reference, 0x98c8, &object, 1, NULL))
                return NULL;
        }
    }
    uint32 path;
    if (style == 15) {
        if (!url_parsed(&reference, url) || !(url_word(url, 8) & 32))
            return NULL;
        uint32 copy[] = {allocator, 0, url_word(url, 12)};
        if (!url_call("CFStringCreateMutableCopy", copy, 3, &path) || !path)
            return NULL;
        uint32 force;
        if (!url_constant(&reference, 0x1a6, &force))
            return NULL;
        uint32 escape[] = {allocator, US_TO_SYN68K(component), 0, force, 0x08000100}, escaped;
        if (!url_call("CFURLCreateStringByAddingPercentEscapes", escape, 5, &escaped))
            return NULL;
        if (!escaped) {
            url_call("CFRelease", &path, 1, NULL);
            return NULL;
        }
        uint32 location, length;
        url_range(url, 32, &location, &length);
        uint32 slash;
        if (length) {
            uint32 get[] = {url_word(url, 12), location + length - 1};
            if (!url_call("CFStringGetCharacterAtIndex", get, 2, &result))
                return NULL;
        }
        if (!length || (result & 65535) != '/') {
            if (!url_constant(&reference, 0x1aa, &slash))
                return NULL;
            uint32 insert[] = {path, location + length, slash};
            if (!url_call("CFStringInsert", insert, 3, NULL))
                return NULL;
            ++length;
        }
        uint32 insert[] = {path, location + length, escaped};
        if (!url_call("CFStringInsert", insert, 3, NULL))
            return NULL;
        if (directory & 255) {
            uint32 escaped_length;
            if (!url_constant(&reference, 0x1aa, &slash) ||
                !url_call("CFStringGetLength", &escaped, 1, &escaped_length))
                return NULL;
            insert[1] = location + length + escaped_length;
            insert[2] = slash;
            if (!url_call("CFStringInsert", insert, 3, NULL))
                return NULL;
        }
        if (!url_call("CFRelease", &escaped, 1, NULL))
            return NULL;
        uint32 create[] = {allocator, path, url_word(url, 16)};
        if (!url_call("CFURLCreateWithString", create, 3, &result) || !url_call("CFRelease", &path, 1, NULL))
            return NULL;
    }
    else {
        uint32 text = url_word(url, 12), length;
        if (!url_call("CFStringGetLength", &text, 1, &length))
            return NULL;
        uint32 get[] = {url_word(url, 12), length - 1};
        if (!url_call("CFStringGetCharacterAtIndex", get, 2, &result))
            return NULL;
        uint32 separator = url_separator(style), format;
        int trailing = (result & 65535) == separator, folder = (directory & 255) != 0;
        unsigned offset = trailing ? (folder ? 0x1e1 : 0x1e8) : (folder ? 0x1d1 : 0x1da);
        if (!url_constant(&reference, offset, &format))
            return NULL;
        uint32 words[7] = {allocator, 0, format, url_word(url, 12)};
        unsigned n = 4;
        if (!trailing)
            words[n++] = separator;
        words[n++] = US_TO_SYN68K(component);
        if (folder)
            words[n++] = separator;
        if (!url_call("CFStringCreateWithFormat", words, n, &path) || !path)
            return NULL;
        uint32 create[] = {allocator, path, style, directory, url_word(url, 16)};
        if (!url_call("CFURLCreateWithFileSystemPathRelativeToBase", create, 5, &result) ||
            !url_call("CFRelease", &path, 1, NULL))
            return NULL;
    }
    return SYN68K_TO_US_CHECK0(result);
}

/* The component decoders own the temporary substring even if decoding returns NULL. */
static int url_decode(void* url, uint32 text, uint32 leave_escaped, uint32* result)
{
    uint32 object = US_TO_SYN68K(url), allocator;
    if (!url_call("CFGetAllocator", &object, 1, &allocator))
        return 0;
    uint32 arguments[] = {allocator, text, leave_escaped};
    return url_call("CFURLCreateStringByReplacingPercentEscapes", arguments, 3, result) &&
           url_call("CFRelease", &text, 1, NULL);
}

static void* url_authority(const char* name, uint32 offset, void* url, uint32 mask)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference(name, offset, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    while (url) {
        if (url_style(url) != 15)
            return mask == 8 && url_retain_global(&reference, 0x80c, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
        if (!url_component(&reference, url, mask, &result))
            return NULL;
        if (result) {
            uint32 text = result, empty;
            if (!url_constant(&reference, 0x19f, &empty) || !url_decode(url, text, empty, &result))
                return NULL;
            return SYN68K_TO_US_CHECK0(result);
        }
        /* An explicit scheme or any authority component prevents inheritance. */
        if (url_word(url, 8) & 0x1f)
            break;
        url = SYN68K_TO_US_CHECK0(url_word(url, 16));
    }
    return NULL;
}

void* ROMlib_carbon_cf_url_host(void* url)
{
    return url_authority("CFURLCopyHostName", 0x8be0, url, 8);
}

void* ROMlib_carbon_cf_url_user(void* url)
{
    return url_authority("CFURLCopyUserName", 0x8de8, url, 2);
}

void* ROMlib_carbon_cf_url_password(void* url)
{
    return url_authority("CFURLCopyPassword", 0x8ebc, url, 4);
}

static void* url_tail_component(const char* name, uint32 offset, void* url, const void* leave_escaped, uint32 mask)
{
    void* owner = url;
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), US_TO_SYN68K(leave_escaped)}, result = 0;
    if (!url_reference(name, offset, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    while (url && url_style(url) == 15) {
        if (!url_component(&reference, url, mask, &result))
            return NULL;
        if (result) {
            if (!url_decode(owner, result, arguments[1], &result))
                return NULL;
            return SYN68K_TO_US_CHECK0(result);
        }
        if (mask == 256 || !(url_word(url, 8) & 0x8000) || (url_word(url, 8) & (mask - 1)))
            break;
        url = SYN68K_TO_US_CHECK0(url_word(url, 16));
    }
    return NULL;
}

void* ROMlib_carbon_cf_url_parameters(void* url, const void* leave_escaped)
{
    return url_tail_component("CFURLCopyParameterString", 0x901c, url, leave_escaped, 64);
}

void* ROMlib_carbon_cf_url_query(void* url, const void* leave_escaped)
{
    return url_tail_component("CFURLCopyQueryString", 0x9124, url, leave_escaped, 128);
}

void* ROMlib_carbon_cf_url_fragment(void* url, const void* leave_escaped)
{
    return url_tail_component("CFURLCopyFragment", 0x91dc, url, leave_escaped, 256);
}

/* The original decimal scanner accepts this historical whitespace set, allows
   whitespace after a sign, consumes every digit and saturates signed 32-bit values. */
static int url_number_space(uint32 character)
{
    return character <= 0x20 || (character >= 0x7f && character <= 0xa0) ||
           (character >= 0x2000 && character <= 0x200b) || character == 0x3000;
}

static int url_port_number(uint32 text, uint32* result)
{
    uint32 length, character = 0, position = 0, magnitude = 0;
    if (!url_call("CFStringGetLength", &text, 1, &length))
        return 0;
    unsigned negative = 0, digits = 0, stage = 0;
    while (position < length) {
        uint32 arguments[] = {text, position};
        if (!url_call("CFStringGetCharacterAtIndex", arguments, 2, &character))
            return 0;
        character &= 65535;
        if (stage < 2 && url_number_space(character)) {
            ++position;
            continue;
        }
        if (!stage && (character == '+' || character == '-')) {
            negative = character == '-';
            stage = 1;
            ++position;
            continue;
        }
        stage = 2;
        if (character < '0' || character > '9')
            break;
        ++digits;
        uint32 limit = negative ? UINT32_C(0x80000000) : UINT32_C(0x7fffffff), digit = character - '0';
        magnitude = magnitude > (limit - digit) / 10 ? limit : magnitude * 10 + digit;
        ++position;
    }
    *result = digits && position == length ? (negative ? 0u - magnitude : magnitude) : UINT32_MAX;
    return 1;
}

uint32 ROMlib_carbon_cf_url_port(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = UINT32_MAX;
    if (!url_reference("CFURLGetPortNumber", 0x8cc0, &object, 1, &reference, &result))
        return result;
    while (url && url_style(url) == 15) {
        uint32 text;
        if (!url_component(&reference, url, 16, &text))
            return UINT32_MAX;
        if (text) {
            if (!url_port_number(text, &result) || !url_call("CFRelease", &text, 1, NULL))
                return UINT32_MAX;
            return result;
        }
        if (url_word(url, 8) & 0x1f)
            break;
        url = SYN68K_TO_US_CHECK0(url_word(url, 16));
    }
    return UINT32_MAX;
}

void* ROMlib_carbon_cf_url_reserved(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (url_reference("__CFURLReservedPtr", 0x9258, &object, 1, &reference, &result))
        result = url_word(url, 24);
    return SYN68K_TO_US_CHECK0(result);
}

void ROMlib_carbon_cf_url_set_reserved(void* url, void* value)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), US_TO_SYN68K(value)}, result = 0;
    if (url_reference("__CFURLSetReservedPtr", 0x9260, arguments, 2, &reference, &result)) {
        uint32 word = CL(arguments[1]);
        memcpy((Byte*)url + 24, &word, 4);
    }
}

void* ROMlib_carbon_cf_url_extension(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyPathExtension", 0xad14, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 component = US_TO_SYN68K(ROMlib_carbon_cf_url_last_component(url));
    if (!component)
        return NULL;
    uint32 dot, length;
    if (!url_constant(&reference, 0x1cf, &dot) || !url_call("CFStringGetLength", &component, 1, &length))
        return NULL;
    _Alignas(8) uint32 match[2];
    uint32 find[] = {component, dot, 0, length, 4, US_TO_SYN68K(match)};
    if (!url_call("CFStringFindWithOptions", find, 6, &result))
        return NULL;
    if (result & 255) {
        uint32 location = CL(match[0]) + 1;
        if ((int32)(length - location) > 0) {
            uint32 allocator;
            if (!url_call("CFGetAllocator", &object, 1, &allocator))
                return NULL;
            uint32 substring[] = {allocator, component, location, length - location};
            if (!url_call("CFStringCreateWithSubstring", substring, 4, &result))
                return NULL;
        }
        else if (!url_constant(&reference, 0x19f, &result) || !url_call("CFRetain", &result, 1, &result))
            return NULL;
    }
    else
        result = 0;
    if (!url_call("CFRelease", &component, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static void* url_from_bytes(const char* name, uint32 offset, uint32 allocator, const Byte* bytes, uint32 length,
                            uint32 directory, void* base, unsigned relative)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(bytes), length, directory, US_TO_SYN68K(base)}, result = 0;
    if (!url_reference(name, offset, arguments, relative ? 5 : 4, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 encoding;
    if (!url_call("CFStringGetSystemEncoding", NULL, 0, &encoding))
        return NULL;
    uint32 string_arguments[] = {allocator, arguments[1], length, encoding, 0}, text;
    if (!url_call("CFStringCreateWithBytes", string_arguments, 5, &text) || !text)
        return NULL;
    uint32 create[] = {allocator, text, 1, directory, arguments[4]};
    int ok = url_call(relative ? "CFURLCreateWithFileSystemPathRelativeToBase" : "CFURLCreateWithFileSystemPath",
                      create, relative ? 5 : 4, &result) &&
             url_call("CFRelease", &text, 1, NULL);
    return ok ? SYN68K_TO_US_CHECK0(result) : NULL;
}

void* ROMlib_carbon_cf_url_from_bytes(uint32 allocator, const Byte* bytes, uint32 length, uint32 directory)
{
    return url_from_bytes("CFURLCreateFromFileSystemRepresentation", 0xa880, allocator, bytes, length, directory, NULL,
                          0);
}

void* ROMlib_carbon_cf_url_from_bytes_base(uint32 allocator, const Byte* bytes, uint32 length, uint32 directory,
                                           void* base)
{
    return url_from_bytes("CFURLCreateFromFileSystemRepresentationRelativeToBase", 0xa928, allocator, bytes, length,
                          directory, base, 1);
}

void* ROMlib_carbon_cf_url_from_fsref(uint32 allocator, const void* fsref)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(fsref)}, result = 0;
    if (!url_reference("CFURLCreateFromFSRef", 0xc58c, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 supported;
    if (!url_private(&reference, 0x4af8, NULL, 0, &supported) || !(supported & 255))
        return NULL;
    _Alignas(256) Byte catalog[144];
    uint32 info[] = {arguments[1], 2, US_TO_SYN68K(catalog), 0, 0, 0};
    if (!url_private(&reference, 0x4d70, info, 6, &result) || (int16)result)
        return NULL;
    uint16 flags;
    memcpy(&flags, catalog, 2);
    uint32 text_arguments[] = {allocator, arguments[1]}, text;
    if (!url_private(&reference, 0x1832c, text_arguments, 2, &text) || !text)
        return NULL;
    uint32 create[] = {allocator, text, 1, CW(flags) & 16};
    int ok = url_call("CFURLCreateWithFileSystemPath", create, 4, &result) && url_call("CFRelease", &text, 1, NULL);
    return ok ? SYN68K_TO_US_CHECK0(result) : NULL;
}

uint32 ROMlib_carbon_cf_url_get_fsref(void* url, void* fsref)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), US_TO_SYN68K(fsref)}, result = 0, supported;
    if (!url_reference("CFURLGetFSRef", 0xc614, arguments, 2, &reference, &result))
        return result & 255;
    /* The original public and inner worker both consult the capability cache. */
    if (!url_private(&reference, 0x4af8, NULL, 0, &supported) || !(supported & 255))
        return 0;
    uint32 allocator;
    if (!url_call("CFGetAllocator", arguments, 1, &allocator) ||
        !url_private(&reference, 0x4af8, NULL, 0, &supported) || !(supported & 255))
        return 0;
    uint32 absolute;
    if (!url_call("CFURLCopyAbsoluteURL", arguments, 1, &absolute))
        return 0;
    uint32 path = 0;
    if (absolute) {
        /* 1.4 calls CopyFileSystemPath on the original URL, not the absolute copy. */
        uint32 copy[] = {arguments[0], 1};
        if (!url_call("CFURLCopyFileSystemPath", copy, 2, &path))
            return 0;
    }
    result = 0;
    if (path) {
        uint32 make[] = {allocator, path, arguments[1]};
        if (!url_private(&reference, 0xc178, make, 3, &result))
            return 0;
    }
    if (absolute && !url_call("CFRelease", &absolute, 1, NULL))
        return 0;
    if (path && !url_call("CFRelease", &path, 1, NULL))
        return 0;
    return result & 255;
}

static void url_store(void* object, unsigned offset, uint32 value)
{
    value = CL(value);
    memcpy((Byte*)object + offset, &value, 4);
}

static int url_character(uint32 text, uint32 index, uint32* character)
{
    uint32 arguments[] = {text, index};
    if (!url_call("CFStringGetCharacterAtIndex", arguments, 2, character))
        return 0;
    *character &= 65535;
    return 1;
}

static int url_find(uint32 text, uint32 pattern, uint32 location, uint32 length, uint32 options, uint32* found,
                    uint32* position)
{
    _Alignas(8) uint32 range[2];
    uint32 arguments[] = {text, pattern, location, length, options, US_TO_SYN68K(range)};
    if (!url_call("CFStringFindWithOptions", arguments, 6, found))
        return 0;
    *found &= 255;
    if (*found && position)
        *position = CL(range[0]);
    return 1;
}

static int url_allocate(const ROMlib_carbon_reference* reference, uint32 allocator, uint32* result)
{
    uint32 arguments[] = {allocator, url_word(reference->toc, 0x810), 20, 0};
    if (!url_call("_CFRuntimeCreateInstance", arguments, 4, result))
        return 0;
    if (*result)
        memset((Byte*)SYN68K_TO_US(*result) + 8, 0, 20);
    return 1;
}

void* ROMlib_carbon_cf_url_alloc(uint32 allocator)
{
    ROMlib_carbon_reference reference;
    uint32 result = 0;
    if (url_reference("_CFURLAlloc", 0x6ac8, &allocator, 1, &reference, &result) &&
        !url_allocate(&reference, allocator, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

/* Native 0x6b18: preserve flags, copy the string using the object's allocator,
   and own an absolute copy of the supplied base. */
static int url_initialize_file(void* url, uint32 text, uint32 style, uint32 base)
{
    uint32 object = US_TO_SYN68K(url), allocator, copy;
    if (!url_call("CFGetAllocator", &object, 1, &allocator))
        return 0;
    uint32 arguments[] = {allocator, text};
    if (!url_call("CFStringCreateCopy", arguments, 2, &copy))
        return 0;
    url_store(url, 12, copy);
    url_store(url, 8, url_word(url, 8) | (style << 16));
    if (base && !url_call("CFURLCopyAbsoluteURL", &base, 1, &base))
        return 0;
    url_store(url, 16, base);
    return 1;
}

static int url_initialize_string(const ROMlib_carbon_reference* reference, void* url, uint32 text, uint32 base)
{
    uint32 colon, hash, length, found, colon_position, hash_position;
    if (!url_constant(reference, 0x188, &colon) || !url_call("CFStringGetLength", &text, 1, &length) ||
        !url_find(text, colon, 0, length, 0, &found, &colon_position))
        return 0;
    unsigned absolute = 0;
    if (found) {
        if (!url_constant(reference, 0x18a, &hash) || !url_find(text, hash, 0, length, 4, &found, &hash_position))
            return 0;
        absolute = !found || (int32)hash_position > (int32)colon_position;
    }
    if (!url_initialize_file(url, text, 15, absolute ? 0 : base))
        return 0;
    if (absolute)
        url_store(url, 8, url_word(url, 8) | 0x4000);
    return 1;
}

void ROMlib_carbon_cf_url_initialize(void* url, const void* text, void* base)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), US_TO_SYN68K(text), US_TO_SYN68K(base)}, result = 0;
    if (url_reference("_CFURLInitWithString", 0x7084, arguments, 3, &reference, &result))
        url_initialize_string(&reference, url, arguments[1], arguments[2]);
}

/* Native 0x6bac. The reference increments its cursor before rejecting an
   ordinary character or repeated '#': rejection at the final unit succeeds.
   Malformed percent triplets explicitly set a failure cursor and still fail. */
static int url_validate_string(uint32 text, uint32* valid)
{
    uint32 length;
    *valid = 0;
    if (!text)
        return 1;
    if (!url_call("CFStringGetLength", &text, 1, &length))
        return 0;
    unsigned fragment = 0;
    for (uint32 index = 0; index < length;) {
        uint32 character;
        if (!url_character(text, index++, &character))
            return 0;
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            (character && character < 128 && strchr("_-.!~'()*/:;?@&=$+,", character)))
            continue;
        if (character == '#' && !fragment) {
            fragment = 1;
            continue;
        }
        if (character != '%') {
            *valid = index >= length;
            return 1;
        }
        if (length - index < 2)
            return 1;
        for (unsigned digit = 0; digit < 2; ++digit) {
            if (!url_character(text, index++, &character))
                return 0;
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F')))
                return 1;
        }
    }
    *valid = 1;
    return 1;
}

static int url_create_validated(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 text, uint32 base,
                                uint32* result)
{
    uint32 valid;
    *result = 0;
    if (!url_validate_string(text, &valid))
        return 0;
    if (!(valid & 255))
        return 1;
    return url_allocate(reference, allocator, result) &&
           (!*result || url_initialize_string(reference, SYN68K_TO_US(*result), text, base));
}

static int url_create_string(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 text, uint32 base,
                             uint32* result)
{
    uint32 length;
    *result = 0;
    if (!text)
        return 1;
    if (!url_call("CFStringGetLength", &text, 1, &length))
        return 0;
    return !length || url_create_validated(reference, allocator, text, base, result);
}

void* ROMlib_carbon_cf_url_create_string(uint32 allocator, const void* text, void* base)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(text), US_TO_SYN68K(base)}, result = 0;
    if (url_reference("CFURLCreateWithString", 0x7ac0, arguments, 3, &reference, &result) &&
        !url_create_string(&reference, allocator, arguments[1], arguments[2], &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_create_bytes(uint32 allocator, const Byte* bytes, uint32 length, uint32 encoding, void* base)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(bytes), length, encoding, US_TO_SYN68K(base)}, result = 0;
    if (!url_reference("CFURLCreateWithBytes", 0x78bc, arguments, 5, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 string_arguments[] = {allocator, arguments[1], length, encoding, 0}, text, size;
    if (!url_call("CFStringCreateWithBytes", string_arguments, 5, &text))
        return NULL;
    if (text && !url_call("CFStringGetLength", &text, 1, &size))
        return NULL;
    if (!text || !size) {
        if (text)
            url_call("CFRelease", &text, 1, NULL);
        return NULL;
    }
    if (encoding != 0x08000100) {
        uint32 convert[] = {text, encoding, 0x08000100, 0, 0, 0, 0}, converted;
        if (!url_private(&reference, 0x755c, convert, 7, &converted) || !url_call("CFRelease", &text, 1, NULL))
            return NULL;
        text = converted;
        if (!text)
            return NULL;
    }
    if (!url_create_validated(&reference, allocator, text, arguments[4], &result) ||
        !url_call("CFRelease", &text, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_create_data(uint32 allocator, void* url, uint32 encoding, uint32 escape_whitespace)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(url), encoding, escape_whitespace}, result = 0;
    if (!url_reference("CFURLCreateData", 0x79e4, arguments, 4, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 text = US_TO_SYN68K(ROMlib_carbon_cf_url_string(url));
    if (!text)
        return NULL;
    if (encoding == 0x08000100) {
        if (!url_call("CFRetain", &text, 1, NULL))
            return NULL;
    }
    else {
        uint32 escaped = (escape_whitespace & 255) != 0;
        uint32 convert[] = {
            text, 0x08000100, encoding, 1, 0, escaped ? US_TO_SYN68K(reference.toc) + 0x2264 : 0, escaped ? 4 : 0};
        if (!url_private(&reference, 0x755c, convert, 7, &text))
            return NULL;
    }
    uint32 create[] = {allocator, text, encoding, 0};
    if (!url_call("CFStringCreateExternalRepresentation", create, 4, &result) || !url_call("CFRelease", &text, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int url_path_absolute(uint32 text, uint32 length, uint32 style, unsigned* absolute)
{
    uint32 character;
    *absolute = 1;
    switch (style) {
    case 0:
    case 1:
        *absolute = 0;
        if ((int32)length > 0) {
            if (!url_character(text, 0, &character))
                return 0;
            *absolute = style ? character != ':' : character == '/';
        }
        break;
    case 2:
        /* The installed 1.4 code tests an apostrophe at index two. */
        *absolute = 0;
        if ((int32)length > 3) {
            if (!url_character(text, 1, &character))
                return 0;
            if (character == ':') {
                if (!url_character(text, 2, &character))
                    return 0;
                *absolute = character == 39;
            }
        }
        break;
    }
    return 1;
}

static int url_safe_path_character(uint32 character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           (character && character < 128 && strchr("_-.!~'()*/:@&=$+,", character));
}

static int url_create_file(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 text, uint32 style,
                           uint32 directory, uint32 base, uint32* result)
{
    uint32 length, character, modified = 0;
    unsigned absolute;
    *result = 0;
    if (!url_call("CFStringGetLength", &text, 1, &length) || !url_path_absolute(text, length, style, &absolute))
        return 0;
    if (absolute)
        base = 0;
    uint32 separator = style == 0 ? '/' : style == 1 ? ':' : style == 2 ? 92 : 0;
    if ((int32)length > 0) {
        if (!url_character(text, length - 1, &character))
            return 0;
        if ((directory & 255) && character != separator) {
            uint32 format;
            if (!url_constant(reference, 0x1ca, &format))
                return 0;
            uint32 words[] = {allocator, 0, format, text, separator};
            if (!url_call("CFStringCreateWithFormat", words, 5, &text))
                return 0;
            modified = 1;
        }
        else if (!(directory & 255) && character == separator) {
            if (length == 1)
                directory = 1;
            else {
                if (!url_character(text, length - 2, &character))
                    return 0;
                if (character == separator)
                    directory = 1;
                else {
                    uint32 substring[] = {allocator, text, 0, length - 1};
                    if (!url_call("CFStringCreateWithSubstring", substring, 4, &text))
                        return 0;
                    modified = 1;
                }
            }
        }
    }
    if (text && !url_call("CFStringGetLength", &text, 1, &length))
        return 0;
    if (!text || !length) {
        if (modified && text && !url_call("CFRelease", &text, 1, NULL))
            return 0;
        return 1;
    }
    if (!url_allocate(reference, allocator, result))
        return 0;
    if (!*result) {
        if (modified && !url_call("CFRelease", &text, 1, NULL))
            return 0;
        return 1;
    }
    void* url = SYN68K_TO_US(*result);
    if (!url_initialize_file(url, text, style, base) || (modified && !url_call("CFRelease", &text, 1, NULL)))
        return 0;
    if (directory & 255)
        url_store(url, 8, url_word(url, 8) | 0x1000);
    if (!style) {
        text = url_word(url, 12);
        if (!url_call("CFStringGetLength", &text, 1, &length))
            return 0;
        uint32 index;
        for (index = 0; index < length; ++index) {
            if (!url_character(text, index, &character))
                return 0;
            if (!url_safe_path_character(character))
                break;
        }
        if (index == length)
            url_store(url, 8, url_word(url, 8) | 0x100000);
    }
    return 1;
}

void* ROMlib_carbon_cf_url_create_file_base(uint32 allocator, const void* text, uint32 style, uint32 directory,
                                            void* base)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(text), style, directory, US_TO_SYN68K(base)}, result = 0;
    if (url_reference("CFURLCreateWithFileSystemPathRelativeToBase", 0x9f90, arguments, 5, &reference, &result) &&
        !url_create_file(&reference, allocator, arguments[1], style, directory, arguments[4], &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int url_create_file_current(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 text,
                                   uint32 style, uint32 directory, uint32* result)
{
    uint32 length, base = 0;
    unsigned absolute;
    if (!url_call("CFStringGetLength", &text, 1, &length) || !url_path_absolute(text, length, style, &absolute))
        return 0;
    if (!absolute && !url_private(reference, 0x9d84, &allocator, 1, &base))
        return 0;
    return url_create_file(reference, allocator, text, style, directory, base, result) &&
           (!base || url_call("CFRelease", &base, 1, NULL));
}

void* ROMlib_carbon_cf_url_create_file(uint32 allocator, const void* text, uint32 style, uint32 directory)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(text), style, directory}, result = 0;
    if (url_reference("CFURLCreateWithFileSystemPath", 0x9e10, arguments, 4, &reference, &result) &&
        !url_create_file_current(&reference, allocator, arguments[1], style, directory, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int url_mutable_copy(uint32 allocator, void* url, uint32* text)
{
    uint32 arguments[] = {allocator, 0, url_word(url, 12)};
    return url_call("CFStringCreateMutableCopy", arguments, 3, text);
}

static int url_mutation_result(const ROMlib_carbon_reference* reference, uint32 allocator, void* url, uint32 text,
                               unsigned style, uint32 directory, uint32* result)
{
    int ok = style == 15 ? url_create_string(reference, allocator, text, url_word(url, 16), result)
                         : url_create_file(reference, allocator, text, style, directory, url_word(url, 16), result);
    return ok && url_call("CFRelease", &text, 1, NULL);
}

static int url_insert(uint32 text, uint32 position, uint32 addition)
{
    uint32 arguments[] = {text, position, addition};
    return url_call("CFStringInsert", arguments, 3, NULL);
}

static int url_delete(uint32 text, uint32 location, uint32 length)
{
    uint32 arguments[] = {text, location, length};
    return url_call("CFStringDelete", arguments, 3, NULL);
}

void* ROMlib_carbon_cf_url_append_extension(uint32 allocator, void* url, const void* extension)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(url), US_TO_SYN68K(extension)}, result = 0;
    if (!url_reference("CFURLCreateCopyAppendingPathExtension", 0xb704, arguments, 3, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 location, length;
    if (!url_last_range(&reference, url, &location, &length) || (int32)location < 0)
        return NULL;
    unsigned style = url_style(url);
    if (style != 15) {
        uint32 size, separator, found;
        if (!url_call("CFStringGetLength", &arguments[2], 1, &size) ||
            !url_constant(&reference, url_separator_constant(style), &separator) ||
            !url_find(arguments[2], separator, 0, size, 0, &found, NULL))
            return NULL;
        if (found) {
            uint32 object = arguments[1];
            if (!url_private(&reference, 0x98c8, &object, 1, NULL) ||
                !url_last_range(&reference, url, &location, &length))
                return NULL;
            style = 15;
        }
    }
    uint32 text, dot;
    if (!url_mutable_copy(allocator, url, &text) || !text || !url_constant(&reference, 0x1cf, &dot) ||
        !url_insert(text, location + length, dot))
        return NULL;
    uint32 addition = arguments[2];
    if (style == 15) {
        uint32 force;
        if (!url_constant(&reference, 0x1a6, &force))
            return NULL;
        uint32 escape[] = {allocator, addition, 0, force, 0x08000100};
        if (!url_call("CFURLCreateStringByAddingPercentEscapes", escape, 5, &addition))
            return NULL;
    }
    if (!url_insert(text, location + length + 1, addition) ||
        (style == 15 && !url_call("CFRelease", &addition, 1, NULL)) ||
        !url_mutation_result(&reference, allocator, url, text, style, (url_word(url, 8) & 0x1000) != 0, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_delete_extension(uint32 allocator, void* url)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(url)}, result = 0;
    if (!url_reference("CFURLCreateCopyDeletingPathExtension", 0xb928, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    uint32 location, length;
    if (!url_last_range(&reference, url, &location, &length) || (int32)location < 0)
        return NULL;
    uint32 dot, found = 0, position = 0;
    if (length && (!url_constant(&reference, 0x1cf, &dot) ||
                   !url_find(url_word(url, 12), dot, location, length, 4, &found, &position)))
        return NULL;
    if (!found) {
        if (!url_call("CFRetain", &arguments[1], 1, &result))
            return NULL;
    }
    else {
        uint32 text;
        if (!url_mutable_copy(allocator, url, &text) || !text ||
            !url_delete(text, position, location + length - position) ||
            !url_mutation_result(&reference, allocator, url, text, url_style(url), (url_word(url, 8) & 0x1000) != 0,
                                 &result))
            return NULL;
    }
    return SYN68K_TO_US_CHECK0(result);
}

void* ROMlib_carbon_cf_url_delete_last(uint32 allocator, void* url)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {allocator, US_TO_SYN68K(url)}, result = 0;
    if (!url_reference("CFURLCreateCopyDeletingLastPathComponent", 0xb1e0, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    unsigned style = url_style(url);
    uint32 location = 0, length, last, last_length;
    if (style == 15) {
        if (!url_parsed(&reference, url) || !(url_word(url, 8) & 32))
            return NULL;
        url_range(url, 32, &location, &length);
    }
    else {
        uint32 text = url_word(url, 12);
        if (!url_call("CFStringGetLength", &text, 1, &length))
            return NULL;
    }
    if (!url_last_range(&reference, url, &last, &last_length))
        return NULL;
    uint32 character, separator = url_separator(style);
    unsigned append_parent = !last_length;
    if (last_length == 1) {
        if (!url_character(url_word(url, 12), last, &character))
            return NULL;
        append_parent = character == '.' || character == separator;
    }
    else if (last_length == 2) {
        if (!url_character(url_word(url, 12), last, &character))
            return NULL;
        if (character == '.') {
            if (!url_character(url_word(url, 12), last + 1, &character))
                return NULL;
            append_parent = character == '.';
        }
    }
    uint32 text;
    if (!url_mutable_copy(allocator, url, &text) || !text)
        return NULL;
    if (append_parent) {
        uint32 extra = 0, constant;
        if ((int32)length > 0) {
            if (!url_character(url_word(url, 12), location + length - 1, &character))
                return NULL;
            if (character != separator) {
                if (!url_constant(&reference, url_separator_constant(style), &constant) ||
                    !url_insert(text, location + length, constant))
                    return NULL;
                extra = 1;
            }
        }
        if (!url_constant(&reference, 0x1ed, &constant) || !url_insert(text, location + length + extra, constant) ||
            !url_constant(&reference, url_separator_constant(style), &constant) ||
            !url_insert(text, location + length + extra + 2, constant))
            return NULL;
        uint32 size = length + extra + 3;
        if ((int32)size > 4) {
            if (!url_character(text, location + length + extra - 2, &character))
                return NULL;
            if (character == '.') {
                unsigned remove_dot = 0;
                if ((int32)size > 7) {
                    if (!url_character(text, location + length + extra - 3, &character))
                        return NULL;
                    remove_dot = character == separator;
                }
                if (remove_dot) {
                    if (!url_delete(text, location + length + extra - 3, 2))
                        return NULL;
                }
                else if (size == 5 && !url_delete(text, location + length + extra - 2, 2))
                    return NULL;
            }
        }
    }
    else if (last == location) {
        uint32 dot;
        if (!url_constant(&reference, 0x1cf, &dot))
            return NULL;
        uint32 replace[] = {text, location, length, dot};
        if (!url_call("CFStringReplace", replace, 4, NULL) ||
            !url_constant(&reference, url_separator_constant(style), &dot) || !url_insert(text, 1, dot))
            return NULL;
    }
    else if (!url_delete(text, last, location + length - last))
        return NULL;
    if (!url_mutation_result(&reference, allocator, url, text, style, 1, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

static int url_append_text(uint32 destination, uint32 text)
{
    uint32 arguments[] = {destination, text};
    return url_call("CFStringAppend", arguments, 2, NULL);
}

static int url_append_formatted(const ROMlib_carbon_reference* reference, uint32 destination, unsigned offset,
                                uint32 text)
{
    uint32 format;
    if (!url_constant(reference, offset, &format))
        return 0;
    uint32 arguments[] = {destination, 0, format, text};
    return url_call("CFStringAppendFormat", arguments, 4, NULL);
}

static int url_append_owned(uint32 destination, uint32 component)
{
    return !component || (url_append_text(destination, component) && url_call("CFRelease", &component, 1, NULL));
}

static int url_append_owned_formatted(const ROMlib_carbon_reference* reference, uint32 destination, unsigned offset,
                                      uint32 component)
{
    return !component || (url_append_formatted(reference, destination, offset, component) &&
                          url_call("CFRelease", &component, 1, NULL));
}

static int url_append_required_formatted(const ROMlib_carbon_reference* reference, uint32 destination, unsigned offset,
                                         uint32 component)
{
    return url_append_formatted(reference, destination, offset, component) &&
           url_call("CFRelease", &component, 1, NULL);
}

void* ROMlib_carbon_cf_url_absolute(void* url)
{
    ROMlib_carbon_reference reference;
    uint32 object = US_TO_SYN68K(url), result = 0;
    if (!url_reference("CFURLCopyAbsoluteURL", 0x7f6c, &object, 1, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url)
        return NULL;
    uint32 allocator;
    if (!url_call("CFGetAllocator", &object, 1, &allocator))
        return NULL;
    void* base = SYN68K_TO_US_CHECK0(url_word(url, 16));
    if (!base)
        return url_call("CFRetain", &object, 1, &result) ? SYN68K_TO_US_CHECK0(result) : NULL;
    unsigned style = url_style(url);
    if (style != 15 && style == url_style(base)) {
        /* Native 0x9b20: the helper performs its own allocator lookup. */
        if (!url_call("CFGetAllocator", &object, 1, &allocator))
            return NULL;
        uint32 merge[] = {url_word(url, 12), url_word(base, 12), (url_word(base, 8) & 0x1000) != 0, style, allocator},
               path;
        if (!url_private(&reference, 0x9be8, merge, 5, &path) ||
            !url_create_file_current(&reference, allocator, path, style, (url_word(url, 8) & 0x1000) != 0, &result) ||
            !url_call("CFRelease", &path, 1, NULL))
            return NULL;
        return SYN68K_TO_US_CHECK0(result);
    }
    if (style != 15 && !url_private(&reference, 0x98c8, &object, 1, NULL))
        return NULL;
    if (!url_parsed(&reference, url))
        return NULL;
    uint32 flags = url_word(url, 8);
    if ((flags & 0x100000) && !(flags & 0x1de) && !url_style(base)) {
        uint32 path, merged;
        if (!url_copy_path(&reference, url, &path))
            return NULL;
        uint32 merge[] = {path, url_word(base, 12), ROMlib_carbon_cf_url_directory(base), 0, allocator};
        if (!url_private(&reference, 0x9be8, merge, 5, &merged) ||
            !url_create_file_current(&reference, allocator, merged, 0, ROMlib_carbon_cf_url_directory(url), &result) ||
            !url_call("CFRelease", &path, 1, NULL) || !url_call("CFRelease", &merged, 1, NULL))
            return NULL;
        return SYN68K_TO_US_CHECK0(result);
    }
    uint32 create[] = {allocator, 0}, text;
    if (!url_call("CFStringCreateMutable", create, 2, &text) || !text ||
        !url_append_owned_formatted(&reference, text, 0x18c, US_TO_SYN68K(ROMlib_carbon_cf_url_scheme(base))))
        return NULL;
    if (url_word(url, 8) & 0x1e) {
        if (!url_append_text(text, url_word(url, 12)))
            return NULL;
    }
    else {
        uint32 slash;
        if (!url_constant(&reference, 0x190, &slash) || !url_append_text(text, slash) ||
            !url_append_owned(text, US_TO_SYN68K(ROMlib_carbon_cf_url_net_location(base))))
            return NULL;
        if (url_word(url, 8) & 32) {
            uint32 resolved;
            if (!url_private(&reference, 0x7d68, &object, 1, &resolved))
                return NULL;
            if (!resolved) {
                url_call("CFRelease", &text, 1, NULL);
                return NULL;
            }
            if (!url_append_owned(text, resolved) ||
                !url_append_owned(text, US_TO_SYN68K(ROMlib_carbon_cf_url_resource(url))))
                return NULL;
        }
        else {
            uint32 path;
            if (!url_copy_path(&reference, base, &path) || !url_append_owned(text, path))
                return NULL;
            flags = url_word(url, 8);
            if (!(flags & 0x1c0)) {
                if (!url_append_owned(text, US_TO_SYN68K(ROMlib_carbon_cf_url_resource(base))))
                    return NULL;
            }
            else if (flags & 64) {
                uint32 resource = US_TO_SYN68K(ROMlib_carbon_cf_url_resource(url));
                if (!resource) {
                    url_call("CFRelease", &text, 1, NULL);
                    return NULL;
                }
                if (!url_append_owned(text, resource))
                    return NULL;
            }
            else {
                if (!url_append_owned_formatted(&reference, text, 0x193,
                                                US_TO_SYN68K(ROMlib_carbon_cf_url_parameters(base, NULL))))
                    return NULL;
                if (url_word(url, 8) & 128) {
                    if (!url_append_required_formatted(&reference, text, 0x197,
                                                       US_TO_SYN68K(ROMlib_carbon_cf_url_query(url, NULL))))
                        return NULL;
                }
                else if (!url_append_owned_formatted(&reference, text, 0x197,
                                                     US_TO_SYN68K(ROMlib_carbon_cf_url_query(base, NULL))))
                    return NULL;
                if ((url_word(url, 8) & 256) &&
                    !url_append_required_formatted(&reference, text, 0x19b,
                                                   US_TO_SYN68K(ROMlib_carbon_cf_url_fragment(url, NULL))))
                    return NULL;
            }
        }
    }
    if (!url_create_string(&reference, allocator, text, 0, &result) || !url_call("CFRelease", &text, 1, NULL))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

/* Native 0x95c4: percent-decode a POSIX path, trimming one trailing slash. */
static int url_posix_path(const ROMlib_carbon_reference* reference, uint32 allocator, uint32 path, uint32* result)
{
    uint32 empty;
    if (!url_constant(reference, 0x19f, &empty))
        return 0;
    uint32 decode[] = {allocator, path, empty};
    if (!url_call("CFURLCreateStringByReplacingPercentEscapes", decode, 3, result))
        return 0;
    uint32 length, character;
    if (!*result)
        return 1;
    if (!url_call("CFStringGetLength", result, 1, &length))
        return 0;
    if ((int32)length > 1) {
        if (!url_character(*result, length - 1, &character))
            return 0;
        if (character == '/') {
            uint32 old = *result, substring[] = {allocator, old, 0, length - 1};
            if (!url_call("CFStringCreateWithSubstring", substring, 4, result) || !url_call("CFRelease", &old, 1, NULL))
                return 0;
        }
    }
    return 1;
}

static int url_filesystem_path(const ROMlib_carbon_reference* reference, uint32 allocator, void* url, uint32 style,
                               unsigned resolve, uint32* result)
{
    void* base = resolve ? SYN68K_TO_US_CHECK0(url_word(url, 16)) : NULL;
    uint32 base_path = 0;
    if (base && !url_filesystem_path(reference, allocator, base, style, 0, &base_path))
        return 0;
    uint32 path = 0;
    if (url_style(url) == style) {
        uint32 text = url_word(url, 12);
        if (!url_call("CFRetain", &text, 1, &path))
            return 0;
    }
    else if (!style && url_style(url) == 15) {
        if (!url_parsed(reference, url))
            return 0;
        if ((url_word(url, 8) & 0x100000) && !url_copy_path(reference, url, &path))
            return 0;
    }
    if (!path) {
        uint32 raw;
        if (!url_copy_path(reference, url, &raw))
            return 0;
        if (raw) {
            if (style < 3) {
                uint32 convert[] = {raw, allocator};
                int ok = style == 0 ? url_posix_path(reference, allocator, raw, &path)
                                    : url_private(reference, style == 1 ? 0xbf64 : 0x96ac, convert, 2, &path);
                if (!ok)
                    return 0;
            }
            if (!url_call("CFRelease", &raw, 1, NULL))
                return 0;
        }
    }
    if (path && ROMlib_carbon_cf_url_directory(url)) {
        uint32 length, character;
        if (!url_call("CFStringGetLength", &path, 1, &length))
            return 0;
        if ((int32)length > 1) {
            if (!url_character(path, length - 1, &character))
                return 0;
            if (character == url_separator(style)) {
                uint32 old = path, substring[] = {allocator, old, 0, length - 1};
                if (!url_call("CFStringCreateWithSubstring", substring, 4, &path) ||
                    !url_call("CFRelease", &old, 1, NULL))
                    return 0;
            }
        }
    }
    if (base && path) {
        uint32 merge[] = {path, base_path, ROMlib_carbon_cf_url_directory(base), style, allocator};
        if (!url_private(reference, 0x9be8, merge, 5, result) || !url_call("CFRelease", &base_path, 1, NULL) ||
            !url_call("CFRelease", &path, 1, NULL))
            return 0;
    }
    else
        *result = path;
    return 1;
}

void* ROMlib_carbon_cf_url_filesystem_path(void* url, uint32 style)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), style}, result = 0, allocator;
    if (!url_reference("CFURLCopyFileSystemPath", 0xa4bc, arguments, 2, &reference, &result))
        return SYN68K_TO_US_CHECK0(result);
    if (!url)
        return NULL;
    if (!url_call("CFGetAllocator", arguments, 1, &allocator) ||
        !url_filesystem_path(&reference, allocator, url, style, 0, &result))
        return NULL;
    return SYN68K_TO_US_CHECK0(result);
}

uint32 ROMlib_carbon_cf_url_filesystem_bytes(void* url, uint32 resolve, Byte* buffer, uint32 capacity)
{
    ROMlib_carbon_reference reference;
    uint32 arguments[] = {US_TO_SYN68K(url), resolve, US_TO_SYN68K(buffer), capacity}, result = 0, allocator;
    if (!url_reference("CFURLGetFileSystemRepresentation", 0xa78c, arguments, 4, &reference, &result))
        return result & 255;
    if (!url || !buffer || (int32)capacity <= 0)
        return 0;
    if (!url_call("CFGetAllocator", arguments, 1, &allocator))
        return 0;
    uint32 path, length, encoding;
    if (!url_filesystem_path(&reference, allocator, url, 1, (resolve & 255) != 0, &path) || !path)
        return 0;
    if (!url_call("CFStringGetLength", &path, 1, &length) || !url_call("CFStringGetSystemEncoding", NULL, 0, &encoding))
        return 0;
    /* A zero encoder capacity is a sizing query in the reference worker.
       The original wrapper then writes the reported terminator past capacity.
       A nonempty path cannot fit in the one byte reserved for its terminator. */
    if (capacity == 1 && length) {
        url_call("CFRelease", &path, 1, NULL);
        return 0;
    }
    _Alignas(8) uint32 used = 0;
    uint32 convert[] = {path, 0, length, encoding, 0, 1, US_TO_SYN68K(buffer), capacity - 1, US_TO_SYN68K(&used)};
    if (!url_call("CFStringGetBytes", convert, 9, &result) || !url_call("CFRelease", &path, 1, NULL))
        return 0;
    if (result != length || CL(used) >= capacity)
        return 0;
    buffer[CL(used)] = 0;
    return 1;
}
