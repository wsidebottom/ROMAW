/*+===================================================================
  File:      filetypemap.c

  Summary:   HFVExplorer-compatible file type mappings; bounded, host-independent parser.

  Classes:   None.

  Functions: filetype_map_application, filetype_map_copy_name, filetype_map_extension, filetype_map_extensions,
             filetype_map_format, filetype_map_lookup, filetype_map_open_mac, filetype_map_parse, filetype_map_record

  Origin:    Existing repository source in executor/src/files; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* HFVExplorer-compatible file type mappings; bounded, host-independent parser. */
#include "rsys/filetypemap.h"
#include "rsys/archive_types.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int decode(const char* line, char* out, size_t capacity)
{
    size_t used = 0;
    while (*line && *line != '\r' && *line != '\n') {
        unsigned char c = *line++;
        if (c == '\\') {
            unsigned value = 0;
            for (unsigned i = 0; i < 3; ++i) {
                if (*line < '0' || *line > '9')
                    return 0;
                value = value * 10 + *line++ - '0';
            }
            if (!value || value > 255)
                return 0;
            c = value;
        }
        if (used + 1 >= capacity || c == '\r' || c == '\n')
            return 0;
        out[used++] = c;
    }
    out[used] = 0;
    return 1;
}

static int flag(char** cursor, int* value)
{
    char* p = *cursor;
    *value = *p == 'x';
    if (*value)
        ++p;
    if (*p++ != ',')
        return 0;
    *cursor = p;
    return 1;
}

static int code(char** cursor, uint32_t* value)
{
    char* p = *cursor;
    size_t available = strlen(p), length;
    if (available >= 5 && p[4] == ',')
        length = 4;
    else {
        char* end = strchr(p, ',');
        if (!end)
            return 0;
        length = end - p;
    }
    if (!length || length > 4)
        return 0;
    uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i)
        result = (result << 8) | (i < length ? (unsigned char)p[i] : ' ');
    *value = result;
    *cursor = p + length + 1;
    return 1;
}

static int csv_text(char** cursor, char* out, size_t capacity)
{
    char* p = *cursor;
    size_t used = 0;
    if (*p != '"')
        return 0;
    ++p;
    while (*p) {
        if (*p == '"') {
            if (p[1] != '"') {
                ++p;
                out[used] = 0;
                *cursor = p;
                return 1;
            }
            ++p;
        }
        if (used + 1 >= capacity)
            return 0;
        out[used++] = *p++;
    }
    return 0;
}

int filetype_map_parse(const char* line, FileTypeMapping* mapping)
{
    char text[FILETYPE_MAP_LINE_CAP];
    FileTypeMapping result = {0};
    if (!line || !*line || *line == '#' || *line == ';' || !strncmp(line, "//", 2) || !decode(line, text, sizeof text))
        return 0;
    char *p = text, *end = strchr(p, ',');
    if (!end)
        return 0;
    *end = 0;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p == '.')
        ++p;
    size_t n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
        --n;
    if (n > 63)
        return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = p[i];
        if (c <= 32 || c >= 127 || strchr("/\\:\"", c))
            return 0;
        result.extension[i] = toupper(c);
    }
    p = end + 1;
    if (!flag(&p, &result.strip) || !code(&p, &result.type) || !code(&p, &result.creator) ||
        !flag(&p, &result.to_host) || !flag(&p, &result.to_mac))
        return 0;
    if (*p == '"') {
        if (!csv_text(&p, result.description, sizeof result.description))
            return 0;
        if (*p) {
            if (!strncmp(p, ",mac", 4) && (!p[4] || p[4] == ',')) {
                result.open_mac = 1;
                p += 4;
            }
            else if (!strncmp(p, ",host", 5) && (!p[5] || p[5] == ','))
                p += 5;
            else
                return 0;
            if (*p) {
                if (*p++ != ',' || !csv_text(&p, result.application, sizeof result.application))
                    return 0;
            }
            if (*p)
                return 0;
        }
    }
    else {
        n = strlen(p);
        if (n >= 4 && !strcmp(p + n - 4, ",mac")) {
            result.open_mac = 1;
            n -= 4;
        }
        else if (n >= 5 && !strcmp(p + n - 5, ",host"))
            n -= 5;
        if (n >= sizeof result.description)
            return 0;
        memcpy(result.description, p, n);
    }
    if (!result.extension[0] && !result.application[0])
        return 0;
    *mapping = result;
    return 1;
}

static int append(char* out, size_t capacity, size_t* used, unsigned char c)
{
    char escape[5];
    size_t n = 1;
    escape[0] = c;
    if (c < 32 || c > 127 || c == '\\') {
        snprintf(escape, sizeof escape, "\\%03u", c);
        n = 4;
    }
    if (*used + n >= capacity)
        return 0;
    memcpy(out + *used, escape, n);
    *used += n;
    out[*used] = 0;
    return 1;
}

int filetype_map_format(const FileTypeMapping* mapping, char* out, size_t capacity)
{
    size_t used = 0;
    if (!capacity)
        return 0;
    out[0] = 0;
    char prefix[80];
    snprintf(prefix, sizeof prefix, "%s,%s,", mapping->extension, mapping->strip ? "x" : "");
    const char* p;
    for (p = prefix; *p; ++p)
        if (!append(out, capacity, &used, *p))
            return 0;
    uint32_t codes[] = {mapping->type, mapping->creator};
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned j = 0; j < 4; ++j)
            if (!append(out, capacity, &used, codes[i] >> (24 - j * 8)))
                return 0;
        if (!append(out, capacity, &used, ','))
            return 0;
    }
    snprintf(prefix, sizeof prefix, "%s,%s,\"", mapping->to_host ? "x" : "", mapping->to_mac ? "x" : "");
    for (p = prefix; *p; ++p)
        if (!append(out, capacity, &used, *p))
            return 0;
    for (p = mapping->description; *p; ++p)
        if ((*p == '"' && !append(out, capacity, &used, '"')) || !append(out, capacity, &used, *p))
            return 0;
    if (!append(out, capacity, &used, '"'))
        return 0;
    for (p = mapping->open_mac ? ",mac" : ",host"; *p; ++p)
        if (!append(out, capacity, &used, *p))
            return 0;
    if (mapping->application[0]) {
        if (!append(out, capacity, &used, ',') || !append(out, capacity, &used, '"'))
            return 0;
        for (p = mapping->application; *p; ++p)
            if ((*p == '"' && !append(out, capacity, &used, '"')) || !append(out, capacity, &used, *p))
                return 0;
        if (!append(out, capacity, &used, '"'))
            return 0;
    }
    return 1;
}

int filetype_map_record(const char* line, char extension[65], uint32_t* type, uint32_t* creator)
{
    FileTypeMapping mapping;
    if (!filetype_map_parse(line, &mapping) || !mapping.to_mac || !mapping.extension[0])
        return 0;
    extension[0] = '.';
    strcpy(extension + 1, mapping.extension);
    *type = mapping.type;
    *creator = mapping.creator;
    return 1;
}

int filetype_map_lookup(const char* text, const char* extension, uint32_t type, uint32_t creator, int to_mac,
                        FileTypeMapping* mapping)
{
    int archive = romaw_archive_extension(extension);
    if (!archive)
        archive = romaw_archive_type(type);
    if (archive) {
        static const uint32_t types[] = {0, 0x5a495020, 0x53495421, 0x42696e48, 0x6d42494e};
        if (!mapping)
            return 0;
        memset(mapping, 0, sizeof *mapping);
        mapping->type = types[archive];
        mapping->creator = 0x4d414353;
        mapping->to_host = mapping->to_mac = 1;
        snprintf(mapping->description, sizeof mapping->description, "%s", romaw_archive_description(archive));
        return 1;
    }
    int found = 0;
    char normalized[65];
    if (extension) {
        while (*extension == ' ' || *extension == '\t')
            ++extension;
        if (*extension == '.')
            ++extension;
        size_t n = strlen(extension);
        while (n && (extension[n - 1] == ' ' || extension[n - 1] == '\t'))
            --n;
        if (n >= sizeof normalized)
            return 0;
        memcpy(normalized, extension, n);
        normalized[n] = 0;
        extension = normalized;
    }
    while (*text) {
        const char* end = strpbrk(text, "\r\n");
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char line[FILETYPE_MAP_LINE_CAP];
        FileTypeMapping current;
        if (n < sizeof line) {
            memcpy(line, text, n);
            line[n] = 0;
            if (filetype_map_parse(line, &current) &&
                (to_mac ? (to_mac == 2 || current.to_mac) && extension && !strcasecmp(current.extension, extension)
                        : current.to_host && current.type == type && current.creator == creator)) {
                *mapping = current;
                found = 1;
            }
        }
        text += n;
        while (*text == '\r' || *text == '\n')
            ++text;
    }
    return found;
}

int filetype_map_open_mac(const char* text, const char* extension)
{
    FileTypeMapping mapping;
    return text && filetype_map_lookup(text, extension, 0, 0, 2, &mapping) && mapping.open_mac;
}

int filetype_map_extension(const char* text, uint32_t type, uint32_t creator, char* extension, size_t capacity)
{
    FileTypeMapping mapping;
    if (!extension || !capacity)
        return 0;
    extension[0] = 0;
    if (!text || !creator)
        return 0;
    if (!type) {
        const char* cursor = text;
        while (*cursor) {
            const char* end = strpbrk(cursor, "\r\n");
            size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
            char line[FILETYPE_MAP_LINE_CAP];
            if (n < sizeof line) {
                memcpy(line, cursor, n);
                line[n] = 0;
                if (filetype_map_parse(line, &mapping) && mapping.to_host && mapping.extension[0] &&
                    mapping.creator == creator && mapping.type != 0x4150504c) /* APPL is not a document. */
                {
                    if (type && type != mapping.type)
                        return 0;
                    type = mapping.type;
                }
            }
            cursor += n;
            while (*cursor == '\r' || *cursor == '\n')
                ++cursor;
        }
    }
    if (!type || !filetype_map_lookup(text, NULL, type, creator, 0, &mapping) || !mapping.extension[0] ||
        strlen(mapping.extension) + 2 > capacity)
        return 0;
    extension[0] = '.';
    strcpy(extension + 1, mapping.extension);
    return 1;
}

int filetype_map_extensions(const char* text, const uint32_t* types, size_t count, uint32_t creator, char* out,
                            size_t capacity)
{
    if (!out || !capacity)
        return 0;
    out[0] = 0;
    if (!text || (count && !types) || count > 256 || (!count && !creator))
        return 0;
    size_t used = 0;
    const char* rules = text;
    /* Keep the application's own formats and common text exports visible
     before a long assortment of source-code and specialty TEXT mappings. */
    for (int pass = 0; pass < 3; ++pass) {
        text = rules;
        while (*text) {
            const char* end = strpbrk(text, "\r\n");
            size_t length = end ? (size_t)(end - text) : strlen(text);
            char line[FILETYPE_MAP_LINE_CAP];
            FileTypeMapping mapping;
            if (length < sizeof line) {
                memcpy(line, text, length);
                line[length] = 0;
                if (filetype_map_parse(line, &mapping) && mapping.to_host && mapping.extension[0] && mapping.type &&
                    mapping.type != 0x4150504c) {
                    int match = !count && mapping.creator == creator;
                    for (size_t i = 0; i < count; ++i)
                        match |= mapping.type == types[i];
                    int common_text = mapping.type == 0x54455854 &&
                                      (!strcmp(mapping.extension, "TXT") || !strcmp(mapping.extension, "CSV") ||
                                       !strcmp(mapping.extension, "TSV"));
                    int priority = creator && mapping.creator == creator ? 0 : common_text ? 1 : 2;
                    if (match && priority == pass) {
                        char pattern[67];
                        snprintf(pattern, sizeof pattern, "*.%s", mapping.extension);
                        for (char* p = pattern; *p; ++p)
                            *p = tolower((unsigned char)*p);
                        size_t n = strlen(pattern);
                        int duplicate = 0;
                        for (const char* p = out; *p;) {
                            const char* comma = strstr(p, ", ");
                            size_t size = comma ? (size_t)(comma - p) : strlen(p);
                            duplicate |= size == n && !memcmp(p, pattern, n);
                            p = comma ? comma + 2 : p + size;
                        }
                        if (!duplicate) {
                            size_t separator = used ? 2 : 0;
                            if (capacity - used <= separator + n + 5) {
                                if (used && capacity - used >= 6)
                                    strcat(out, ", ...");
                                return used != 0;
                            }
                            if (used) {
                                memcpy(out + used, ", ", 2);
                                used += 2;
                            }
                            memcpy(out + used, pattern, n + 1);
                            used += n;
                        }
                    }
                }
            }
            text += length;
            while (*text == '\r' || *text == '\n')
                ++text;
        }
    }
    return used != 0;
}

int filetype_map_application(const char* text, uint32_t type, uint32_t creator, char* application, size_t capacity)
{
    int found = 0;
    if (!text || !application || !capacity)
        return 0;
    application[0] = 0;
    if (romaw_archive_type(type))
        return 0;
    while (*text) {
        const char* end = strpbrk(text, "\r\n");
        size_t length = end ? (size_t)(end - text) : strlen(text);
        char line[FILETYPE_MAP_LINE_CAP];
        FileTypeMapping mapping;
        if (length < sizeof line) {
            memcpy(line, text, length);
            line[length] = 0;
            if (filetype_map_parse(line, &mapping) && mapping.application[0] && mapping.type == type &&
                (!creator || mapping.creator == creator) && strlen(mapping.application) < capacity) {
                strcpy(application, mapping.application);
                found = 1;
            }
        }
        text += length;
        while (*text == '\r' || *text == '\n')
            ++text;
    }
    return found;
}

int filetype_map_copy_name(const char* text, char* name, size_t capacity, uint32_t* type, uint32_t* creator,
                           int source_hfs, int destination_hfs, int has_resource_fork)
{
    if (source_hfs == destination_hfs || has_resource_fork)
        return 1;
    FileTypeMapping mapping;
    char* dot = strrchr(name, '.');
    if (destination_hfs) {
        if (!dot || !filetype_map_lookup(text, dot, 0, 0, 1, &mapping))
            return 1;
        if ((*type || *creator) && (*type != mapping.type || *creator != mapping.creator))
            return 1;
        *type = mapping.type;
        *creator = mapping.creator;
        if (mapping.strip && dot != name)
            *dot = 0;
    }
    else if (filetype_map_lookup(text, NULL, *type, *creator, 0, &mapping)) {
        if (dot && !strcasecmp(dot + 1, mapping.extension))
            return 1;
        // HFVExplorer leaves an existing short extension intact.
        if (dot && strlen(dot) <= 6)
            return 1;
        size_t prefix = dot ? (size_t)(dot + 1 - name) : strlen(name) + 1;
        size_t extension = strlen(mapping.extension);
        if (prefix >= capacity || extension >= capacity - prefix)
            return 0;
        name[prefix - 1] = '.';
        memcpy(name + prefix, mapping.extension, extension + 1);
    }
    return 1;
}
