/*+===================================================================
  File:      configprefs.c

  Summary:   Implements configprefs support for Macintosh startup, traps and runtime services.

  Classes:   None.

  Functions: config_drive_directory, config_drive_directory_count, config_drive_label, config_drive_prefs_load,
             config_drive_prefs_mask, config_drive_prefs_save, config_drive_prefs_save_all, config_drive_rename,
             config_drive_sidecars, config_drive_format, config_drive_format_name, config_drive_detect_format,
             config_drive_prefs_save_formats, config_prefs_app_section, config_prefs_get, config_prefs_open,
             config_prefs_parameter_help, config_prefs_remove_startup, config_prefs_seed, config_prefs_set,
             config_prefs_set_many, config_prefs_visit_applications, config_prefs_store, config_prefs_store_ini, config_prefs_validate_line

  Origin:    Existing repository source in executor/src/runtime; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
/* Readable application settings and first-start defaults. */
#include "rsys/configprefs.h"
#include "rsys/prefspacing.h"
#include "rsys/mac_storage.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

#define PREFS_LIMIT (32U * 1024 * 1024)

static _Atomic uint32_t sidecar_drives;
static _Atomic int sidecar_formats[26];
static char* sidecar_apps;
static char* windows_desktop;
static char* drive_prefs_path;
static char* sidecar_directories[CONFIG_DRIVE_DIRECTORY_LIMIT];
static size_t sidecar_directory_count;

/* Canonicalize lexically so an Apps/../Other path cannot inherit Apps'
   compatibility. Preferences contain absolute host paths, never guest names. */
static char* drive_path_normalize(const char* path)
{
    if (!path || !*path || strlen(path) > 4095)
        return NULL;
    size_t length = strlen(path), prefix;
    char* result = malloc(length + 2);
    if (!result)
        return NULL;
    for (size_t i = 0; i <= length; ++i) {
        if (path[i] && (unsigned char)path[i] < 32)
            goto invalid;
        result[i] = path[i] == '\\' ? '/' : path[i];
    }
    if (isalpha((unsigned char)result[0]) && result[1] == ':' && result[2] == '/')
        prefix = 3;
    else if (result[0] == '/')
        prefix = result[1] == '/' ? 2 : 1;
    else
        goto invalid;
    size_t used = prefix, start = prefix;
    unsigned components = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && result[end] != '/')
            ++end;
        size_t n = end - start;
        if (n == 2 && result[start] == '.' && result[start + 1] == '.') {
            if (used <= prefix || (prefix == 2 && components <= 2))
                goto invalid;
            --components;
            while (used > prefix && result[used - 1] != '/')
                --used;
            if (used > prefix)
                --used;
        }
        else if (n && !(n == 1 && result[start] == '.')) {
            if (used > prefix)
                result[used++] = '/';
            memmove(result + used, result + start, n);
            used += n;
            ++components;
        }
        start = end + 1;
    }
    result[used] = 0;
    if (prefix == 2 && components < 2)
        goto invalid;
    return result;
invalid:
    free(result);
    return NULL;
}

static int drive_path_inside(const char* directory, const char* path)
{
    size_t i = 0;
    while (directory[i] && path[i] &&
           (directory[i] == '\\' ? '/' : tolower((unsigned char)directory[i])) ==
               (path[i] == '\\' ? '/' : tolower((unsigned char)path[i])))
        ++i;
    return !directory[i] && (!path[i] || path[i] == '/' || (i && directory[i - 1] == '/'));
}

const char* config_drive_format_name(int format)
{
    static const char* names[] = {"", "Windows", "OSX", "Netatalk", "BasiliskII", "Executor"};
    return format >= CONFIG_DRIVE_UNDECLARED && format <= CONFIG_DRIVE_ROMAW ? names[format] : NULL;
}

int config_drive_set_windows_desktop(const char* path)
{
    char* normalized = path ? drive_path_normalize(path) : NULL;
    if (path && !normalized)
        return 0;
    free(windows_desktop);
    windows_desktop = normalized;
    return 1;
}

int config_drive_format(const char* path)
{
    char* normalized = drive_path_normalize(path);
    if (!normalized)
        return CONFIG_DRIVE_UNDECLARED;
    int format = CONFIG_DRIVE_UNDECLARED;
    if (windows_desktop && drive_path_inside(windows_desktop, normalized))
        format = CONFIG_DRIVE_WINDOWS;
    else if (sidecar_apps && drive_path_inside(sidecar_apps, normalized))
        format = CONFIG_DRIVE_OSX;
    else if (isalpha((unsigned char)normalized[0]) && normalized[1] == ':')
        format = atomic_load(&sidecar_formats[toupper((unsigned char)normalized[0]) - 'A']);
    free(normalized);
    return format;
}

/* Detect only the supplied root, without traversing a whole drive or following
   directory links. ROMAW is intentionally the last recognized convention. */
int config_drive_detect_format(const char* directory)
{
    if (!directory || strlen(directory) > 4095)
        return CONFIG_DRIVE_WINDOWS;
    DIR* folder = opendir(directory);
    if (!folder)
        return CONFIG_DRIVE_WINDOWS;
    int osx = 0, netatalk = 0, basilisk = 0, romaw = 0;
    struct dirent* entry;
    while ((entry = readdir(folder))) {
        const char* name = entry->d_name;
        int standard = !strncmp(name, "._", 2);
        int percent = name[0] == '%';
        if (!standard && !percent && strcmp(name, ".DS_Store") && strcmp(name, ".AppleDouble") &&
            strcmp(name, ".finf") && strcmp(name, ".rsrc"))
            continue;
        char path[8192];
        snprintf(path, sizeof path, "%s/%s", directory, name);
        struct stat info;
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) || stat(path, &info))
#else
        if (lstat(path, &info))
#endif
            continue;
        if (S_ISDIR(info.st_mode)) {
            netatalk |= !strcmp(name, ".AppleDouble");
            basilisk |= !strcmp(name, ".finf") || !strcmp(name, ".rsrc");
        }
        else if (S_ISREG(info.st_mode)) {
            osx |= !strcmp(name, ".DS_Store");
            if (standard || percent) {
                unsigned char header[512];
                FILE* input = fopen(path, "rb");
                size_t size = input ? fread(header, 1, sizeof header, input) : 0;
                if (input)
                    fclose(input);
                unsigned count;
                int valid = romaw_mac_storage_table(header, size, &count);
                for (unsigned i = 0; valid && i < count; ++i) {
                    ROMAWMacStorageEntry record;
                    valid = romaw_mac_storage_entry(header, size, info.st_size, i, &record);
                }
                osx |= standard && valid;
                romaw |= percent && valid;
            }
        }
    }
    closedir(folder);
    return osx        ? CONFIG_DRIVE_OSX
           : netatalk ? CONFIG_DRIVE_NETATALK
           : basilisk ? CONFIG_DRIVE_BASILISKII
           : romaw    ? CONFIG_DRIVE_ROMAW
                      : CONFIG_DRIVE_WINDOWS;
}

size_t config_drive_directory_count(void)
{
    return sidecar_directory_count;
}

const char* config_drive_directory(size_t index)
{
    return index < sidecar_directory_count ? sidecar_directories[index] : NULL;
}

static void drive_directories_clear(void)
{
    while (sidecar_directory_count)
        free(sidecar_directories[--sidecar_directory_count]);
}

int config_drive_sidecars(const char* path)
{
    if (!path || !*path)
        return 0;
    char* normalized = drive_path_normalize(path);
    if (!normalized)
        return 0;
    if (windows_desktop && drive_path_inside(windows_desktop, normalized)) {
        free(normalized);
        return 0;
    }
    int compatible = sidecar_apps && drive_path_inside(sidecar_apps, normalized);
    for (size_t i = 0; !compatible && i < sidecar_directory_count; ++i)
        compatible = drive_path_inside(sidecar_directories[i], normalized);
    free(normalized);
    if (compatible)
        return 1;
    unsigned char drive = toupper((unsigned char)path[0]);
    if (drive >= 'A' && drive <= 'Z' && path[1] == ':')
        return !!(sidecar_drives & (1u << (drive - 'A')));
    /* UNC host volumes have no configured drive letter. Native POSIX volumes
     retain their existing Macintosh metadata support. */
    return !(path[0] == '\\' || (path[0] == '/' && path[1] == '/'));
}

uint32_t config_drive_prefs_mask(void)
{
    return sidecar_drives;
}

int config_drive_prefs_load(const char* path, const char* apps)
{
    char value[256];
    uint32_t drives = 0;
    char* copy = apps ? drive_path_normalize(apps) : NULL;
    if (apps && !copy)
        return -1;
    char* prefs = strdup(path);
    if (!prefs) {
        free(copy);
        return -1;
    }
    free(drive_prefs_path);
    drive_prefs_path = prefs;
    free(sidecar_apps);
    sidecar_apps = copy;
    sidecar_drives = 0;
    for (unsigned i = 0; i < 26; ++i)
        atomic_store(&sidecar_formats[i], CONFIG_DRIVE_UNDECLARED);
    drive_directories_clear();
    config_prefs_get(path, "HostDrives", "MacDirectoryCount", value, sizeof value);
    unsigned count = 0;
    for (const unsigned char* p = (const unsigned char*)value; *p; ++p) {
        if (!isdigit(*p) || count > CONFIG_DRIVE_DIRECTORY_LIMIT / 10)
            return 0;
        count = count * 10 + *p - '0';
        if (count > CONFIG_DRIVE_DIRECTORY_LIMIT)
            return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        char key[32], directory[4096];
        snprintf(key, sizeof key, "MacDirectory%u", i + 1);
        size_t size = config_prefs_get(path, "HostDrives", key, directory, sizeof directory);
        char* normalized = size && size < sizeof directory - 1 ? drive_path_normalize(directory) : NULL;
        if (!normalized) {
            drive_directories_clear();
            return 0;
        }
        sidecar_directories[sidecar_directory_count++] = normalized;
    }
    int formats[26] = {0}, declared = 0;
    for (unsigned i = 0; i < 26; ++i) {
        char key[] = "FormatA", name[64];
        key[6] += i;
        if (!config_prefs_get(path, "HostDrives", key, name, sizeof name))
            continue;
        /* Preserve profiles written during the application-brand rename. The
           percent-sidecar storage convention itself is named Executor. */
        if (!strcasecmp(name, "ROMAW"))
            strcpy(name, "Executor");
        int format;
        for (format = CONFIG_DRIVE_WINDOWS; format <= CONFIG_DRIVE_ROMAW; ++format)
            if (!strcasecmp(name, config_drive_format_name(format)))
                break;
        if (format > CONFIG_DRIVE_ROMAW)
            return 0;
        formats[i] = format;
        declared = 1;
    }
    size_t size = config_prefs_get(path, "HostDrives", "SidecarDrives", value, sizeof value);
    if ((!size && !declared) || size == sizeof value - 1)
        return 0;
    if (!strcasecmp(value, "none"))
        value[0] = 0;
    const unsigned char* p = (const unsigned char*)value;
    while (*p) {
        unsigned char letter = toupper(*p++);
        if (letter < 'A' || letter > 'Z')
            return 0;
        if (drives & (1u << (letter - 'A')))
            return 0;
        drives |= 1u << (letter - 'A');
        if (*p == ':')
            ++p;
        if (!*p)
            break;
        if (*p++ != ',' || !*p)
            return 0;
        while (*p == ' ')
            ++p;
    }
    for (unsigned i = 0; i < 26; ++i) {
        if (formats[i] == CONFIG_DRIVE_WINDOWS)
            drives &= ~(1u << i);
        else if (formats[i] > CONFIG_DRIVE_WINDOWS)
            drives |= 1u << i;
        atomic_store(&sidecar_formats[i], formats[i]);
    }
    sidecar_drives = drives;
    return 1;
}

static int drive_preferences_save(const char* path, uint32_t drives, const int* formats, const char* const* directories,
                                  size_t count)
{
    if (count > CONFIG_DRIVE_DIRECTORY_LIMIT || (count && !directories))
        return 0;
    char* copies[CONFIG_DRIVE_DIRECTORY_LIMIT] = {0};
    const char* keys[CONFIG_DRIVE_DIRECTORY_LIMIT + 28];
    const char* values[CONFIG_DRIVE_DIRECTORY_LIMIT + 28];
    char names[CONFIG_DRIVE_DIRECTORY_LIMIT][32], number[16], mask[52];
    size_t used = 0;
    drives &= (1u << 26) - 1;
    for (unsigned i = 0; i < 26; ++i)
        if (drives & (1u << i)) {
            if (used)
                mask[used++] = ',';
            mask[used++] = 'A' + i;
        }
    mask[used] = 0;
    snprintf(number, sizeof number, "%u", (unsigned)count);
    keys[0] = "SidecarDrives";
    values[0] = used ? mask : "none";
    keys[1] = "MacDirectoryCount";
    values[1] = number;
    int ok = 0;
    for (unsigned i = 0; i < CONFIG_DRIVE_DIRECTORY_LIMIT; ++i) {
        snprintf(names[i], sizeof names[i], "MacDirectory%u", i + 1);
        keys[i + 2] = names[i];
        copies[i] = i < count ? drive_path_normalize(directories[i]) : NULL;
        if (i < count && !copies[i])
            goto done;
        values[i + 2] = copies[i];
    }
    char format_keys[26][8];
    size_t entries = CONFIG_DRIVE_DIRECTORY_LIMIT + 2;
    if (formats)
        for (unsigned i = 0; i < 26; ++i) {
            snprintf(format_keys[i], sizeof format_keys[i], "Format%c", 'A' + i);
            keys[entries] = format_keys[i];
            values[entries++] = formats[i] ? config_drive_format_name(formats[i]) : NULL;
        }
    if (config_prefs_set_many(path, "HostDrives", keys, values, entries)) {
        drive_directories_clear();
        sidecar_drives = drives;
        if (formats)
            for (unsigned i = 0; i < 26; ++i)
                atomic_store(&sidecar_formats[i], formats[i]);
        for (size_t i = 0; i < count; ++i) {
            sidecar_directories[sidecar_directory_count++] = copies[i];
            copies[i] = NULL;
        }
        ok = 1;
    }
done:
    for (unsigned i = 0; i < CONFIG_DRIVE_DIRECTORY_LIMIT; ++i)
        free(copies[i]);
    return ok;
}

int config_drive_prefs_save_all(const char* path, uint32_t drives, const char* const* directories, size_t count)
{
    return drive_preferences_save(path, drives, NULL, directories, count);
}

int config_drive_prefs_save_formats(const char* path, const int formats[26], const char* const* directories,
                                    size_t count)
{
    if (!formats)
        return 0;
    uint32_t drives = 0;
    for (unsigned i = 0; i < 26; ++i) {
        if (!config_drive_format_name(formats[i]))
            return 0;
        if (formats[i] > CONFIG_DRIVE_WINDOWS)
            drives |= 1u << i;
    }
    return drive_preferences_save(path, drives, formats, directories, count);
}

static char* drive_label_key(const char* root)
{
    char* normalized = drive_path_normalize(root);
    if (!normalized)
        return NULL;
    size_t length = strlen(normalized), used = 0;
    char* key = malloc(length * 3 + 1);
    if (key)
        for (size_t i = 0; i <= length; ++i) {
            unsigned char c = tolower((unsigned char)normalized[i]);
            if (c == '%' || c == '=' || c == ' ' || c == '[' || c == ']') {
                static const char hex[] = "0123456789abcdef";
                key[used++] = '%';
                key[used++] = hex[c >> 4];
                key[used++] = hex[c & 15];
            }
            else
                key[used++] = c;
        }
    free(normalized);
    return key;
}

size_t config_drive_label(const char* root, char* label, size_t capacity)
{
    if (capacity)
        label[0] = 0;
    char* key = drive_label_key(root);
    size_t size =
        key && drive_prefs_path ? config_prefs_get(drive_prefs_path, "HostVolumeNames", key, label, capacity) : 0;
    free(key);
    return size;
}

int config_drive_rename(const char* root, const char* label)
{
    if (!drive_prefs_path || !label || !*label || strlen(label) > 127 || strpbrk(label, ":\r\n") || *label == ' ' ||
        label[strlen(label) - 1] == ' ')
        return 0;
    for (const unsigned char* p = (const unsigned char*)label; *p; ++p)
        if (*p < 32 || *p == 127)
            return 0;
    char* key = drive_label_key(root);
    int ok = key && config_prefs_set(drive_prefs_path, "HostVolumeNames", key, label);
    free(key);
    return ok;
}

int config_drive_prefs_save(const char* path, uint32_t drives)
{
    char value[52];
    size_t used = 0;
    drives &= (1u << 26) - 1;
    for (unsigned i = 0; i < 26; ++i)
        if (drives & (1u << i)) {
            if (used)
                value[used++] = ',';
            value[used++] = 'A' + i;
        }
    value[used] = 0;
    if (!config_prefs_set(path, "HostDrives", "SidecarDrives", used ? value : "none"))
        return 0;
    sidecar_drives = drives;
    return 1;
}

static void fourcc_text(uint32_t code, char text[11])
{
    for (unsigned i = 0; i < 4; ++i) {
        unsigned char c = code >> (24 - i * 8);
        if (c < 32 || c > 126 || c == '[' || c == ']') {
            snprintf(text, 11, "0x%08lx", (unsigned long)code);
            return;
        }
        text[i] = c;
    }
    text[4] = 0;
}

void config_prefs_app_section(uint32_t type, uint32_t creator, char section[CONFIG_PREFS_SECTION_SIZE])
{
    char t[11], c[11];
    fourcc_text(type, t);
    fourcc_text(creator, c);
    snprintf(section, CONFIG_PREFS_SECTION_SIZE, "%s.%s", t, c);
}

static char* read_contents(FILE* file, size_t* length)
{
    char* bytes = NULL;
    size_t used = 0, capacity = 0;
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (!c || used == PREFS_LIMIT) {
            errno = EINVAL;
            goto failed;
        }
        if (used + 1 >= capacity) {
            size_t next = capacity ? capacity * 2 : 4096;
            char* grown = realloc(bytes, next);
            if (!grown)
                goto failed;
            bytes = grown;
            capacity = next;
        }
        bytes[used++] = c;
    }
    if (ferror(file)) {
        errno = EIO;
        goto failed;
    }
    if (!bytes)
        bytes = malloc(1);
    if (!bytes)
        return NULL;
    bytes[used] = 0;
    *length = used;
    return bytes;
failed:
    free(bytes);
    return NULL;
}

static char* read_path(const char* path, size_t* length)
{
    FILE* file = fopen(path, "rb");
    if (!file)
        return NULL;
    char* bytes = read_contents(file, length);
    fclose(file);
    return bytes;
}

static int publish(const char* path, const char* bytes, size_t length, int seed)
{
    if (strlen(path) > SIZE_MAX - 64)
        return 0;
    char* temporary = malloc(strlen(path) + 64);
    int fd = -1, ok = 0;
    if (!temporary)
        return 0;
#ifdef _WIN32
    unsigned long pid = (unsigned long)_getpid();
    WritePrivateProfileStringA(NULL, NULL, NULL, path);
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    for (unsigned attempt = 0; attempt < 100 && fd < 0; ++attempt) {
        sprintf(temporary, "%s.config.%lu.%u", path, pid, attempt);
        fd = open(temporary,
                  O_WRONLY | O_CREAT | O_EXCL
#ifdef O_BINARY
                      | O_BINARY
#endif
                  ,
                  0600);
        if (fd < 0 && errno != EEXIST)
            break;
    }
    if (fd < 0)
        goto done;
    FILE* file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        goto remove_temp;
    }
    ok = fwrite(bytes, 1, length, file) == length;
    if (fclose(file))
        ok = 0;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path, MOVEFILE_WRITE_THROUGH | (seed ? 0 : MOVEFILE_REPLACE_EXISTING)) != 0;
        if (!ok && seed && GetLastError() == ERROR_ALREADY_EXISTS)
            ok = 1;
        WritePrivateProfileStringA(NULL, NULL, NULL, path);
#else
        if (seed)
            ok = link(temporary, path) == 0 || errno == EEXIST;
        else
            ok = rename(temporary, path) == 0;
#endif
    }
remove_temp:
    remove(temporary);
done:
    free(temporary);
    if (ok)
        prefs_space_sections(path);
    return ok;
}

int config_prefs_seed(const char* path, const char* master)
{
    FILE* existing = fopen(path, "rb");
    if (existing) {
        fclose(existing);
        config_prefs_remove_startup(path);
        return 1;
    }
    if (errno != ENOENT)
        return 0;
    size_t length;
    char* bytes = read_path(master, &length);
    if (!bytes) {
        if (errno != ENOENT)
            return 0;
        static const char fallback[] = "[ROMAW]\r\n";
        return publish(path, fallback, sizeof fallback - 1, 1);
    }
    int ok = publish(path, bytes, length, 1);
    free(bytes);
    if (ok)
        config_prefs_remove_startup(path);
    return ok;
}

/* Return a complete physical line without its line ending. */
static size_t next_line(const char* bytes, size_t length, size_t offset, size_t* end)
{
    size_t next = offset;
    while (next < length && bytes[next] != '\n')
        ++next;
    *end = next;
    if (*end > offset && bytes[*end - 1] == '\r')
        --*end;
    return next < length ? next + 1 : next;
}

static int section_code(const char* text, size_t length, uint32_t* value)
{
    uint32_t code = 0;
    if (length == 4) {
        for (size_t i = 0; i < 4; ++i) {
            unsigned char c = text[i];
            if (c < 32 || c > 126 || c == '[' || c == ']')
                return 0;
            code = code * 256 + c;
        }
    }
    else if (length == 10 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        for (size_t i = 2; i < 10; ++i) {
            unsigned char c = text[i];
            unsigned digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return 0;
            code = code * 16 + digit;
        }
    }
    else
        return 0;
    *value = code;
    return 1;
}

static int section_identity(const char* name, size_t length, uint32_t* type, uint32_t* creator)
{
    for (size_t n = 4; n <= 10; n += 6)
        if (length > n && name[n] == '.' && section_code(name, n, type) &&
            section_code(name + n + 1, length - n - 1, creator))
            return 1;
    return 0;
}

static int section_matches(const char* line, size_t length, const char* section)
{
    if (length < 2 || line[0] != '[' || line[length - 1] != ']')
        return 0;
    if (length - 2 == strlen(section) && !memcmp(line + 1, section, length - 2))
        return 1;
    uint32_t type, creator, wanted_type, wanted_creator;
    return section_identity(line + 1, length - 2, &type, &creator) &&
           section_identity(section, strlen(section), &wanted_type, &wanted_creator) && type == wanted_type &&
           creator == wanted_creator;
}

/* Enumerate canonical application identities, never unrelated INI sections. */
int config_prefs_visit_applications(const char* path, int (*visit)(const char*, void*), void* context)
{
    size_t length;
    char* bytes = read_path(path, &length);
    if (!bytes || !visit) {
        free(bytes);
        return 0;
    }
    int ok = 1;
    for (size_t off = 0; ok && off < length;) {
        size_t end = off;
        while (end < length && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        uint32_t type, creator;
        if (end - off > 2 && bytes[off] == '[' && bytes[end - 1] == ']' &&
            section_identity(bytes + off + 1, end - off - 2, &type, &creator)) {
            char section[CONFIG_PREFS_SECTION_SIZE];
            config_prefs_app_section(type, creator, section);
            ok = visit(section, context);
        }
        off = end;
        while (off < length && (bytes[off] == '\r' || bytes[off] == '\n'))
            ++off;
    }
    free(bytes);
    return ok;
}

static int matches_key(const char* line, size_t length, const char* key, size_t* equals)
{
    size_t e = 0;
    while (e < length && line[e] != '=')
        ++e;
    size_t k = e;
    while (k && isspace((unsigned char)line[k - 1]))
        --k;
    *equals = e;
    return e < length && k == strlen(key) && !strncasecmp(line, key, k);
}

size_t config_prefs_get(const char* path, const char* section, const char* key, char* value, size_t capacity)
{
    if (!capacity)
        return 0;
    value[0] = 0;
    size_t length, result = 0;
    char* bytes = read_path(path, &length);
    if (!bytes)
        return 0;
    int active = 0;
    for (size_t off = 0; off < length;) {
        size_t end, next = next_line(bytes, length, off, &end), equals;
        if (bytes[off] == '[')
            active = section_matches(bytes + off, end - off, section);
        else if (active && matches_key(bytes + off, end - off, key, &equals)) {
            size_t start = off + equals + 1;
            while (start < end && isspace((unsigned char)bytes[start]))
                ++start;
            while (end > start && isspace((unsigned char)bytes[end - 1]))
                --end;
            result = end - start < capacity ? end - start : capacity - 1;
            memcpy(value, bytes + start, result);
            value[result] = 0;
            break;
        }
        off = next;
    }
    free(bytes);
    return result;
}

static int set_values(const char* path, const char* section, const char* const* keys, const char* const* values,
                      size_t count)
{
    if (!*section || strpbrk(section, "[]\r\n"))
        return 0;
    int have_values = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!keys[i] || !*keys[i] || strpbrk(keys[i], "=\r\n") || (values[i] && strpbrk(values[i], "\r\n")))
            return 0;
        have_values |= values[i] != NULL;
    }
    size_t length = 0;
    char* bytes = read_path(path, &length);
    if (!bytes && errno != ENOENT)
        return 0;
    FILE* output = tmpfile();
    int active = 0, inserted = 0, ok = 0;
    if (!output)
        goto done;
    for (size_t off = 0; off < length;) {
        size_t end, next = next_line(bytes, length, off, &end), equals;
        int heading = bytes[off] == '[';
        if (heading)
            active = section_matches(bytes + off, end - off, section);
        int remove = active && !count;
        if (active && !heading)
            for (size_t i = 0; i < count; ++i)
                if (matches_key(bytes + off, end - off, keys[i], &equals))
                    remove = 1;
        if (!remove)
            fwrite(bytes + off, 1, next - off, output);
        if (heading && active && have_values && !inserted) {
            if (next == end)
                fputs("\r\n", output);
            for (size_t i = 0; i < count; ++i)
                if (values[i])
                    fprintf(output, "%s=%s\r\n", keys[i], values[i]);
            inserted = 1;
        }
        off = next;
    }
    if (!inserted && have_values) {
        fprintf(output, "\r\n[%s]\r\n", section);
        for (size_t i = 0; i < count; ++i)
            if (values[i])
                fprintf(output, "%s=%s\r\n", keys[i], values[i]);
    }
    if (!ferror(output) && !fflush(output) && !fseek(output, 0, SEEK_SET)) {
        size_t n;
        char* result = read_contents(output, &n);
        if (result) {
            ok = publish(path, result, n, 0);
            free(result);
        }
    }
    fclose(output);
done:
    free(bytes);
    return ok;
}

int config_prefs_set(const char* path, const char* section, const char* key, const char* value)
{
    if (value && strpbrk(value, "\r\n"))
        return 0;
    return set_values(path, section, &key, &value, key ? 1 : 0);
}

int config_prefs_set_many(const char* path, const char* section, const char* const* keys, const char* const* values,
                          size_t count)
{
    return keys && values && count && set_values(path, section, keys, values, count);
}

int config_prefs_remove_startup(const char* path)
{
    size_t length = 0;
    char* bytes = read_path(path, &length);
    if (!bytes)
        return errno == ENOENT;
    int active = 0, found = 0;
    for (size_t off = 0; off < length;) {
        size_t end, next = next_line(bytes, length, off, &end), equals;
        if (bytes[off] == '[')
            active = section_matches(bytes + off, end - off, "ROMAW");
        else if (active && (matches_key(bytes + off, end - off, "Startup", &equals) ||
                            matches_key(bytes + off, end - off, "Browser", &equals)))
            found = 1;
        off = next;
    }
    free(bytes);
    if (!found)
        return 1;
    const char* keys[] = {"Startup", "Browser"};
    const char* values[] = {NULL, NULL};
    return config_prefs_set_many(path, "ROMAW", keys, values, 2);
}

static const char* const scalar_keys[] = {"Comments",         "WindowName",   "DongleFamily", "PrinterName",
                                          "PrinterTypeLabel", "Win32Token",   "MacCDROM",     "SystemVersion",
                                          "PrVers",           "SysZone",      "ApplZone",     "BitsPerPixel",
                                          "RefreshNumber",    "Delay",        "AppleChar",    "RightButtonModifier",
                                          "CacheProblems",    "DisplayScale", "PreferM68K"};
static const char* const pair_keys[] = {"ScreenLocation", "ScreenSize", "MacSize"};
static const char* const flag_keys[] = {
    "Accelerated",      "BlitAtTrapEnd", "BlitInOSEvent",    "BlitOften",    "CloseIsQuit",    "Debug",
    "DirectDiskAccess", "DirtyVariant",  "DisposHandleHack", "FlushOften",   "NewLineToCR",    "NoClock",
    "NoLower",          "NoPrefPanel",   "NoSuspend",        "NoWarn32",     "PassPostscript", "PretendAlias",
    "PretendEdition",   "PretendHelp",   "PretendScript",    "PretendSound", "PrintingHack",   "RectScreen",
    "Refresh",          "SoundOff",      "SoundOn",          "StripAddress", "TextDisableHack"};

static const char* const boolean_keys[] = {"StickyMenus", "ForwardDelete", "AutoRefresh", "SubstituteFonts"};

static int key_in(const char* key, size_t n, const char* const* table, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (strlen(table[i]) == n && !strncasecmp(key, table[i], n))
            return 1;
    return 0;
}

#define KEY_IN(key, n, table) key_in(key, n, table, sizeof table / sizeof table[0])

/* 1 scalar, 2 point/size, 3 option flag, 4 conditional rule, 5 empty options,
   6 a direct Gestalt statement, 8 an independent boolean. */
static int natural_key(const char* line, size_t n, size_t* equals, size_t* key_length)
{
    size_t e = 0;
    if (n >= 15 && !strncasecmp(line, "Gestalt(", 8)) {
        size_t end = 0;
        if (line[8] == '\'' && line[13] == '\'' && line[14] == ')')
            end = 15;
        else if (n >= 19 && line[8] == '0' && line[9] == 'x' && line[18] == ')') {
            end = 19;
            for (size_t i = 10; i < 18; ++i)
                if (!isxdigit((unsigned char)line[i]))
                    end = 0;
        }
        if (end) {
            e = end;
            while (e < n && isspace((unsigned char)line[e]))
                ++e;
            if (e < n && line[e] == '=') {
                *equals = e;
                *key_length = end;
                return 6;
            }
        }
        return 0;
    }
    while (e < n && line[e] != '=')
        ++e;
    if (e == n)
        return 0;
    size_t k = e;
    while (k && isspace((unsigned char)line[k - 1]))
        --k;
    *equals = e;
    *key_length = k;
    if (KEY_IN(line, k, scalar_keys))
        return 1;
    if (KEY_IN(line, k, pair_keys))
        return 2;
    if (KEY_IN(line, k, boolean_keys))
        return 8;
    if (KEY_IN(line, k, flag_keys))
        return 3;
    if (k == 7 && !strncasecmp(line, "Options", k))
        return 5;
    if (k == 10 && !strncasecmp(line, "Rule", 4)) {
        for (size_t i = 4; i < k; ++i)
            if (!isdigit((unsigned char)line[i]))
                return 0;
        return 4;
    }
    return 0;
}

static int settings_number(const char* text, size_t length, int memory, long long* result)
{
    char number[128], *end;
    if (!length || length >= sizeof number)
        return 0;
    memcpy(number, text, length);
    number[length] = 0;
    errno = 0;
    long long value = strtoll(number, &end, number[0] == '0' && (number[1] == 'x' || number[1] == 'X') ? 16 : 10);
    if (end == number || errno || value < 0)
        return 0;
    long long scale = 1;
    if (memory && (*end == 'k' || *end == 'K' || *end == 'm' || *end == 'M')) {
        scale = *end == 'k' || *end == 'K' ? 1024 : 1048576;
        ++end;
    }
    if (*end || value > (memory ? 2147483647LL : 4294967295LL) / scale)
        return 0;
    *result = value * scale;
    return 1;
}

int config_prefs_validate_line(const char* line)
{
    size_t length = strlen(line), equals, key;
    int kind = natural_key(line, length, &equals, &key);
    if (!kind) {
        const char* e = strchr(line, '=');
        if (!e)
            return 0;
        key = e - line;
        while (key && isspace((unsigned char)line[key - 1]))
            --key;
        if (!((key == 4 && !strncasecmp(line, "Size", key)) || (key == 8 && !strncasecmp(line, "Location", key))))
            return 0;
        equals = e - line;
        kind = 7;
    }
    const char* value = line + equals + 1;
    while (isspace((unsigned char)*value))
        ++value;
    size_t n = strlen(value);
    while (n && isspace((unsigned char)value[n - 1]))
        --n;
    if (!n)
        return 0;
    if (key == 12 && !strncasecmp(line, "DisplayScale", key)) {
        long long percent;
        return settings_number(value, n, 0, &percent) && percent >= 25 && percent <= 1600;
    }
    if (kind == 3 || kind == 8 || (key == 10 && !strncasecmp(line, "PreferM68K", key)))
        return n == 1 && (*value == '0' || *value == '1');
    int quoted = 0, braces = 0, parentheses = 0;
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)value[i] < 32 && value[i] != '\t')
            return 0;
        if (value[i] == '\\' && quoted) {
            if (++i == n)
                return 0;
            continue;
        }
        if (value[i] == '"')
            quoted = !quoted;
        if (!quoted) {
            if (value[i] == '{')
                ++braces;
            if (value[i] == '}' && --braces < 0)
                return 0;
            if (value[i] == '(')
                ++parentheses;
            if (value[i] == ')' && --parentheses < 0)
                return 0;
        }
    }
    if (quoted || braces || parentheses)
        return 0;
    if (kind == 2 || kind == 7) {
        char pair[128], *end;
        if (n >= sizeof pair)
            return 0;
        memcpy(pair, value, n);
        pair[n] = 0;
        errno = 0;
        long a = strtol(pair, &end, 10);
        if (end == pair || errno)
            return 0;
        int location = (key == 8 && !strncasecmp(line, "Location", key)) ||
                       (key == 14 && !strncasecmp(line, "ScreenLocation", key));
        char separator = key == 4 && !strncasecmp(line, "Size", key) ? 'x' : ',';
        while (*end == ' ' || *end == '\t')
            ++end;
        if (*end++ != separator)
            return 0;
        char* second = end;
        errno = 0;
        long b = strtol(second, &end, 10);
        if (end == second || errno)
            return 0;
        while (*end == ' ' || *end == '\t')
            ++end;
        return !*end && a >= (location ? -32768 : 1) && a <= 32767 && b >= (location ? -32768 : 1) && b <= 32767;
    }
    if (kind == 6) {
        char* end;
        if (*value++ != '{')
            return 0;
        errno = 0;
        long long error = strtoll(value, &end, 0);
        if (end == value || errno || error < -2147483648LL || error > 2147483647LL)
            return 0;
        while (isspace((unsigned char)*end))
            ++end;
        if (*end++ != ',')
            return 0;
        value = end;
        errno = 0;
        long long result = strtoll(value, &end, 0);
        if (end == value || errno || result < -2147483648LL || result > 4294967295LL)
            return 0;
        while (isspace((unsigned char)*end))
            ++end;
        if (*end++ != '}')
            return 0;
        while (isspace((unsigned char)*end))
            ++end;
        if (*end == ';')
            ++end;
        while (isspace((unsigned char)*end))
            ++end;
        return !*end;
    }
    if (key == 13 && !strncasecmp(line, "SystemVersion", key)) {
        unsigned components = 0, component = 0, digits = 0;
        for (size_t i = 0; i <= n; ++i) {
            char c = i < n ? value[i] : 0;
            if (c >= '0' && c <= '9') {
                component = component * 10 + c - '0';
                if (component > 15)
                    return 0;
                ++digits;
            }
            else if (c == '.' || !c) {
                if (!digits || (!components && !component) || ++components > 3)
                    return 0;
                component = digits = 0;
            }
            else
                return 0;
        }
    }
    if (key == 6 && !strncasecmp(line, "PrVers", key)) {
        const char* dot = memchr(value, '.', n);
        long long major, minor = 0;
        if (!settings_number(value, dot ? (size_t)(dot - value) : n, 0, &major) || major > 3276)
            return 0;
        if (dot && (!settings_number(dot + 1, n - (dot - value) - 1, 0, &minor) || minor > 9))
            return 0;
        return major * 10 + minor <= 32767;
    }
    if (kind == 1) {
        static const char* const text_keys[] = {"Comments", "WindowName", "PrinterName", "PrinterTypeLabel",
                                                "Win32Token"};
        if (KEY_IN(line, key, text_keys))
            return n >= 2 && value[0] == '"' && value[n - 1] == '"';
        if (!(key == 13 && !strncasecmp(line, "SystemVersion", key)) &&
            !(key == 12 && !strncasecmp(line, "BitsPerPixel", key))) {
            long long number;
            return settings_number(value, n,
                                   (key == 7 && !strncasecmp(line, "SysZone", key)) ||
                                       (key == 8 && !strncasecmp(line, "ApplZone", key)),
                                   &number);
        }
    }
    if (key == 12 && !strncasecmp(line, "BitsPerPixel", key)) {
        return (n == 1 && strchr("01248", value[0])) ||
               (n == 2 && ((!strncmp(value, "16", 2)) || !strncmp(value, "32", 2)));
    }
    return 1;
}

const char* config_prefs_parameter_help(void)
{
    return "Text (quoted): Comments, WindowName, PrinterName, PrinterTypeLabel, Win32Token.\n"
           "Numbers: BitsPerPixel, RefreshNumber, Delay, AppleChar, CacheProblems, DongleFamily, MacCDROM, RightButtonModifier.\n"
           "Versions: SystemVersion, PrVers (for example 7.1.1). Memory: SysZone, ApplZone (bytes or k/m suffix).\n"
           "Coordinates: ScreenLocation=x,y; ScreenSize=w,h; MacSize=w,h. Host window: Size=WxH; Location=x,y. DisplayScale=100 (percent, 25..1600; 150 means 1.5x).\n"
           "Flags (0/1): Accelerated, BlitAtTrapEnd, BlitInOSEvent, BlitOften, CloseIsQuit, Debug, DirectDiskAccess, DirtyVariant, DisposHandleHack, FlushOften, NewLineToCR, NoClock, NoLower, NoPrefPanel, NoSuspend, NoWarn32, PassPostscript, PretendAlias, PretendEdition, PretendHelp, PretendScript, PrintingHack, RectScreen, Refresh, StripAddress, TextDisableHack.\n"
           "Advanced: Gestalt('fs  ')={0,1}; and Rule000000=Version(...){...};. Use the same syntax as ROMAW.prefs. Existing window geometry and other application sections are retained.";
}

static int config_key(const char* line, size_t length)
{
    size_t equals, key;
    int kind = natural_key(line, length, &equals, &key);
    /* Gestalt overrides have no dialog controls; saving the dialog must retain
     these hand-written statements. */
    return kind && kind != 6;
}

static int app_config_key(const char* line, size_t length, int fat)
{
    static const char* const fields[] = {
        "WindowName", "SystemVersion", "Size", "BitsPerPixel", "SysZone", "ApplZone"
    };
    size_t equals, key;
    if (natural_key(line, length, &equals, &key) == 6)
        return 1;
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i)
        if (matches_key(line, length, fields[i], &equals))
            return 1;
    return fat && matches_key(line, length, "PreferM68K", &equals);
}

FILE* config_prefs_open(const char* path, const char* section)
{
    size_t length;
    char* bytes = read_path(path, &length);
    FILE* script = NULL;
    int active = 0, format = 0, flags = 0;
    if (!bytes)
        return NULL;
    script = tmpfile();
    if (!script)
        goto done;
    for (size_t offset = 0; offset < length;) {
        size_t end, next = next_line(bytes, length, offset, &end);
        const char* line = bytes + offset;
        size_t n = end - offset;
        if (n && line[0] == '[')
            active = section_matches(line, n, section);
        else if (active) {
            size_t e, k;
            int kind = natural_key(line, n, &e, &k);
            if (kind) {
                const char* value = line + e + 1;
                size_t v = n - e - 1;
                while (v && isspace((unsigned char)*value))
                    ++value, --v;
                while (v && isspace((unsigned char)value[v - 1]))
                    --v;
                format = 1;
                if (kind == 3 && !flags++)
                    fputs("Options={};\n", script);
                if (kind == 6)
                    fprintf(script, "%.*s\n", (int)n, line);
                else if (kind == 4)
                    fprintf(script, "%.*s\n", (int)v, value);
                else if (kind == 5 && v == 1 && *value == '0')
                    fputs("Options={};\n", script);
                else
                    fprintf(script, "%.*s=%s%.*s%s;\n", (int)k, line, kind == 2 ? "{" : "", (int)v, value,
                            kind == 2 ? "}" : "");
            }
        }
        offset = next;
    }
    if (!format || ferror(script) || fflush(script) || fseek(script, 0, SEEK_SET))
        goto invalid;
    goto done;
invalid:
    fclose(script);
    script = NULL;
done:
    free(bytes);
    return script;
}

static const char* const default_zero_keys[] = {"SysZone", "ApplZone", "BitsPerPixel"};

/* Dialog snapshots already use the same INI keys as the master file. */
static int ini_key(const char* line, size_t n)
{
    size_t i = 0;
    while (i < n && (isalnum((unsigned char)line[i]) || line[i] == '_' || line[i] == '-' || line[i] == '.'))
        ++i;
    return i > 0 && i < n && line[i] == '=';
}

/* Mode 0 owns an INI section, 1 owns the legacy preferences panel keys,
   and 2/3 own Configure App keys (with CPU choice only in mode 3). */
static int write_settings(FILE* output, const char* settings, size_t length, int mode)
{
    for (size_t offset = 0; offset < length;) {
        size_t end, next = next_line(settings, length, offset, &end);
        const char* line = settings + offset;
        size_t n = end - offset, e, k;
        int kind = mode >= 2 ? app_config_key(line, n, mode == 3)
                 : mode ? natural_key(line, n, &e, &k) : ini_key(line, n);
        if (n && !kind && !(n >= 2 && line[0] == '/' && line[1] == '/'))
            return 0;
        if (mode == 1 && kind && n - e - 1 == 1 && line[e + 1] == '0' && (KEY_IN(line, k, default_zero_keys))) {
            offset = next;
            continue;
        }
        if (fprintf(output, "%.*s\r\n", (int)n, line) < 0)
            return 0;
        offset = next;
    }
    return !ferror(output);
}

static int store_section(const char* path, const char* section, FILE* script, int mode)
{
    if (!*section || strpbrk(section, "[]\r\n"))
        return 0;
    size_t length = 0, script_length;
    char* bytes = read_path(path, &length);
    if (!bytes && errno != ENOENT)
        return 0;
    if (fseek(script, 0, SEEK_SET)) {
        free(bytes);
        return 0;
    }
    char* settings = read_contents(script, &script_length);
    FILE* output = settings ? tmpfile() : NULL;
    int ok = 0, active = 0, written = 0;
    if (!output)
        goto done;
    for (size_t offset = 0; offset < length;) {
        size_t end, next = next_line(bytes, length, offset, &end);
        const char* line = bytes + offset;
        size_t n = end - offset;
        if (n && line[0] == '[')
            active = section_matches(line, n, section);
        int owned = mode >= 2 ? app_config_key(line, n, mode == 3)
                  : mode ? config_key(line, n) : ini_key(line, n);
        if (!(active && owned) &&
            fwrite(line, 1, next - offset, output) != next - offset)
            goto close_output;
        if (active && !written) {
            if (next == end && fputs("\r\n", output) == EOF)
                goto close_output;
            if (!write_settings(output, settings, script_length, mode))
                goto close_output;
            written = 1;
        }
        offset = next;
    }
    if (!written) {
        if (fprintf(output, "%s[%s]\r\n", !length ? "" : bytes[length - 1] == '\n' ? "\r\n" : "\r\n\r\n", section) < 0)
            goto close_output;
        if (!write_settings(output, settings, script_length, mode))
            goto close_output;
    }
    if (!fflush(output) && !fseek(output, 0, SEEK_SET)) {
        size_t n;
        char* result = read_contents(output, &n);
        if (result) {
            ok = publish(path, result, n, 0);
            free(result);
        }
    }
close_output:
    fclose(output);
done:
    free(settings);
    free(bytes);
    return ok;
}

int config_prefs_store(const char* path, const char* section, FILE* script)
{
    return store_section(path, section, script, 1);
}

int config_prefs_store_app(const char* path, const char* section, FILE* script, int fat)
{
    return store_section(path, section, script, fat ? 3 : 2);
}

int config_prefs_store_ini(const char* path, const char* section, FILE* script)
{
    return store_section(path, section, script, 0);
}
