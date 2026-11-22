/*+===================================================================
  File:      drive_paths_test.c

  Summary:   Use mounted volume fixtures to exercise real Macintosh path parsing.

  Classes:   None.

  Functions: ROMlib_RelString, ROMlib_UNIX7_to_Mac, ROMlib_creator_and_type_from_filename, ROMlib_vcbbyvrn, Uclose,
             Umkdir, Uopen, Uopendir, Urmdir, Ustat, Uunlink, _gui_fatal, _warning, main, pstr_index_after,
             syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Use mounted volume fixtures to exercise real Macintosh path parsing. */
#define NDEBUG 1
#include "../../files/fileAccess.c"
#undef main
#undef NDEBUG
#include <assert.h>
#include <process.h>
#include <direct.h>
#if defined(TEST_HOST_NAMES)
#include "rsys/host_names.h"
#endif
void _warning(int err, const char* kind, const char* file, int line, const char* fn, const char* fmt, ...)
{
    abort();
}

int Uopen(const char* name, int flags, int mode)
{
    return open(name, flags, mode);
}

int Uclose(int fd)
{
    return close(fd);
}

int Umkdir(const char* path, int mode)
{
    return mkdir(path);
}

int Urmdir(const char* path)
{
    return rmdir(path);
}

int Uunlink(const char* path)
{
    return unlink(path);
}

boolean_t ROMlib_creator_and_type_from_filename(int length, const char* name, uint32* creator, uint32* type)
{
    return FALSE;
}

static unsigned host_stat_calls;

int Ustat(const char* name, struct stat* info)
{
    ++host_stat_calls;
    int result = stat(name, info);
#if defined(TEST_HOST_NAMES)
    if (result < 0 && (errno == ENOENT || errno == ENOTDIR))
        result = ROMlib_win_icon_stat(name, info);
#endif
    return result;
}

DIR* Uopendir(const char* name)
{
    return opendir(name);
}
#if defined(TEST_HOST_NAMES)
#include "../../files/fileInfo.c"
#include "rsys/host_names.h"
#include <wchar.h>
#else
INTEGER
ROMlib_UNIX7_to_Mac(char* name, INTEGER length)
{
    return 0;
}
#endif
LONGINT
ROMlib_RelString(unsigned char* a, unsigned char* b, BOOLEAN case_sensitive, BOOLEAN diac_sensitive, LONGINT lengths)
{
    (void)case_sensitive;
    (void)diac_sensitive;
    return memcmp(a, b, (unsigned long)lengths & 65535);
}

void _gui_fatal(const char* file, int line, const char* function, const char* format, ...)
{
    abort();
}
#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
LONGINT DefDirID;
#include "../../files/fileDouble.c"

void* syn68k_guest_to_host(uint32 address)
{
    return (void*)(uintptr_t)(ROMlib_offset + address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return address - ROMlib_offset;
}

HVCB* ROMlib_vcbbyvrn(short vref)
{
    return NULL;
}

char* pstr_index_after(StringPtr p, char c, int i)
{
    int n;
    for (n = i; n < p[0]; ++n)
        if (p[n + 1] == c)
            return (char*)p + n + 1;
    return NULL;
}

int main(void)
{
    apple_double_quote_char = '%';
    apple_double_fork_prefix = "._";
    apple_double_fork_prefix_length = 2;
    unsigned char* memory = calloc(1, 65536);
    VCB* drive;
    ParamBlockRec pb;
    StringPtr name;
    char* rest;
    LONGINT dir;
    BOOLEAN full;
    unsigned i;

    static const struct {
        const char *path, *rest;
        int full;
    } cases[] = {{"D:", "", 1},          {"d:", "", 1},          {"D:Apps:ResEdit", "Apps/ResEdit", 1},
                 {"D/:Apps", "Apps", 1}, {"d/:Apps", "Apps", 1}, {":Apps:ResEdit", "Apps/ResEdit", 0},
                 {"D", "D", 0},          {"D:Apps", "Apps", 1}};

    assert(memory);
    ROMlib_offset = (uintptr_t)memory;
    drive = (VCB*)(memory + 0x1000);
    name = memory + 0x2000;
    drive->vcbVN[0] = 1;
    drive->vcbVN[1] = 'D';
    ((VCBExtra*)drive)->unixname = "D:/";
    PP_ASSIGN(VCBQHdr.qHead, (QElemPtr)drive);
    HIDDEN_RAW_SET(DefVCBPtr_H, RM(drive));
    DefDirID = CLC(2);
    memset(&pb, 0, sizeof pb);
    PP_ASSIGN(pb.ioParam.ioNamePtr, name);
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        VCB* found;
        name[0] = strlen(cases[i].path);
        memcpy(name + 1, cases[i].path, name[0]);
        found = ROMlib_breakoutioname(&pb, &dir, &rest, &full, TRUE);
        assert(found == drive && full == cases[i].full);
        assert(cases[i].rest ? rest && !strcmp(rest, cases[i].rest) : !rest);
        free(rest);
    }
    assert(!vlookupbyname("DD", "DD" + 2));
    assert(!vlookupbyname("D/foo", "D/foo" + 5));
#if defined(TEST_HOST_NAMES)
    extern unsigned long __stdcall GetFileAttributesA(const char*);
    extern int __stdcall SetFileAttributesA(const char*, unsigned long);
#define FILE_ATTRIBUTE_HIDDEN 2
#define FILE_ATTRIBUTE_ARCHIVE 32
    {
        char root[1024], raw[1200], encoded[1200], child[1300], alternate[1200];
        char cwd[900];
        assert(_getcwd(cwd, sizeof cwd));
        for (char* p = cwd; *p; ++p)
            if (*p == '\\')
                *p = '/';
        snprintf(root, sizeof root, "%s/build/host-names.%u", cwd, (unsigned)_getpid());
        assert(!_mkdir(root));
        /* Runtime uses Netatalk fork compatibility with Windows-safe percent
       quoting. Exercise real creation and decoding, not just lookup recovery. */
        netatalk_conventions_p = 1;
        apple_double_quote_char = '%';
        for (unsigned byte = 1; byte <= 255; ++byte) {
            if (byte == ':')
                continue; /* Macintosh path separator, not a filename character. */
            char mac[] = {'N', (char)byte, 'X', 0};
            char* host = ROMlib_newunixfrommac(mac, 3);
            assert(host && !strpbrk(host, ":/\\*?\"<>|"));
            for (const unsigned char* p = (unsigned char*)host; *p; ++p)
                assert(*p >= 32 && *p < 127);
            snprintf(raw, sizeof raw, "%s/%s", root, host);
            assert(!_mkdir(raw));
            char decoded[16];
            size_t length = strlen(host);
            assert(length < sizeof decoded);
            memcpy(decoded, host, length + 1);
            int removed = ROMlib_UNIX7_to_Mac(decoded, length);
            assert(length - removed == 3 && !memcmp(decoded, mac, 3));
            assert(!_rmdir(raw));
            free(host);
        }
        char holiday[] = "Holiday Lemmings 1994 \304";
        char* holiday_host = ROMlib_newunixfrommac(holiday, sizeof holiday - 1);
        assert(holiday_host && !strcmp(holiday_host, "Holiday Lemmings 1994 %c4"));
        free(holiday_host);
        char legacy[] = "Holiday Lemmings 1994 %C4";
        int removed = ROMlib_UNIX7_to_Mac(legacy, sizeof legacy - 1);
        assert(removed == 2 && !memcmp(legacy, holiday, sizeof holiday - 1));
        const char* terminal_names[] = {"SimCity", "SimCity ", "SimCity.", "SimCity . ", " ",   ".",
                                        "..",      " . . ",    " Leading", "A.B C",      "...X"};
        for (unsigned i = 0; i < sizeof terminal_names / sizeof *terminal_names; ++i) {
            const char* mac = terminal_names[i];
            size_t mac_length = strlen(mac);
            char* host = ROMlib_newunixfrommac((char*)mac, mac_length);
            assert(host);
            size_t host_length = strlen(host);
            assert(host_length && host[host_length - 1] != ' ' && host[host_length - 1] != '.');
            char decoded[64];
            assert(host_length < sizeof decoded);
            memcpy(decoded, host, host_length + 1);
            int shrink = ROMlib_UNIX7_to_Mac(decoded, host_length);
            assert(host_length - shrink == mac_length && !memcmp(decoded, mac, mac_length));
            snprintf(raw, sizeof raw, "%s/%s", root, host);
            assert(!_mkdir(raw));
            free(host);
        }
        /* Distinct Macintosh names must coexist rather than normalizing to the
       already-existing plain SimCity folder. */
        for (unsigned i = 0; i < sizeof terminal_names / sizeof *terminal_names; ++i) {
            char* host = ROMlib_newunixfrommac((char*)terminal_names[i], strlen(terminal_names[i]));
            snprintf(raw, sizeof raw, "%s/%s", root, host);
            assert(!_rmdir(raw));
            free(host);
        }
        char* nested = ROMlib_newunixfrommac("Outer :Inner.", 13);
        assert(nested && !strcmp(nested, "Outer%20/Inner%2e"));
        free(nested);
        assert(!ROMlib_newunixfrommac(NULL, 1));
        assert(!ROMlib_newunixfrommac("", -1));
        char* empty = ROMlib_newunixfrommac(NULL, 0);
        assert(empty && !*empty);
        free(empty);
        /* Expanded length can exceed INTEGER even though the input cannot. */
        char* long_tail = malloc(32767);
        assert(long_tail);
        for (unsigned i = 0; i < 32767; ++i)
            long_tail[i] = i & 1 ? '.' : ' ';
        char* expanded = ROMlib_newunixfrommac(long_tail, 32767);
        assert(expanded && strlen(expanded) == 3 * 32767);
        for (unsigned i = 0; i < 32767; ++i)
            assert(!memcmp(expanded + i * 3, i & 1 ? "%2e" : "%20", 3));
        free(expanded);
        free(long_tail);
        netatalk_conventions_p = 0;
        snprintf(raw, sizeof raw, "%s/Desktop", root);
        int desktop = open(raw, O_CREAT | O_EXCL | O_RDWR | O_BINARY, 0600);
        assert(desktop >= 0);
        ROMlib_win_hide_desktop(desktop, raw);
        assert(GetFileAttributesA(raw) & FILE_ATTRIBUTE_HIDDEN);
        assert(!close(desktop));
        assert(SetFileAttributesA(raw, FILE_ATTRIBUTE_ARCHIVE));
        desktop = open(raw, O_RDONLY | O_BINARY);
        assert(desktop >= 0);
        ROMlib_win_hide_desktop(desktop, raw);
        assert((GetFileAttributesA(raw) & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE)) ==
               (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE));
        assert(!close(desktop) && !remove(raw));
        wchar_t wide_icon[1200];
        assert(mbstowcs(wide_icon, root, 1100) != (size_t)-1);
        wcscat(wide_icon, L"/Icon\xf00d");
        FILE* icon_file = _wfopen(wide_icon, L"wb");
        assert(icon_file && fwrite("ICO", 1, 3, icon_file) == 3);
        assert(!fclose(icon_file));
        snprintf(encoded, sizeof encoded, "%s/Icon%%0D", root);
        struct stat alias_info;
        assert(!ROMlib_win_icon_stat(encoded, &alias_info));
        assert(alias_info.st_size == 3);
        int alias = ROMlib_win_icon_open(encoded, O_RDONLY | O_BINARY, 0);
        char payload[3];
        assert(alias >= 0 && read(alias, payload, 3) == 3);
        assert(!memcmp(payload, "ICO", 3));
        assert(!close(alias) && !_wremove(wide_icon));
        snprintf(raw, sizeof raw, "%s/AppleScript\x99 Setup", root);
        assert(!_mkdir(raw));
        snprintf(child, sizeof child, "%s/Nested\xe9", raw);
        assert(!_mkdir(child));
        snprintf(encoded, sizeof encoded, "%s/AppleScript%%AA Setup/Nested%%8E", root);
        struct stat info;
        assert(!weasel(encoded, &info));
        assert(!strcmp(encoded, child));
        assert(!_rmdir(child));
        snprintf(alternate, sizeof alternate, "%s/AppleScript\xe2\x84\xa2 Setup", root);
        assert(!_mkdir(alternate));
        snprintf(encoded, sizeof encoded, "%s/AppleScript%%AA Setup", root);
        assert(weasel(encoded, &info));
        assert(!_mkdir(encoded));
        assert(!weasel(encoded, &info));
        assert(!_rmdir(encoded));
        assert(!_rmdir(alternate));
        assert(!_rmdir(raw));
        assert(!_rmdir(root));
        char name[] = "AppleScript\x99 Setup";
        assert(!ROMlib_UNIX7_to_Mac(name, sizeof name - 1));
        assert((unsigned char)name[11] == 0xaa);
        char escaped[] = "AppleScript%AA Setup";
        assert(ROMlib_UNIX7_to_Mac(escaped, sizeof escaped - 1) == 2);
        assert(!memcmp(escaped, name, sizeof name - 1));
        char utf8[] = "AppleScript\xe2\x84\xa2 Setup";
        assert(ROMlib_UNIX7_to_Mac(utf8, sizeof utf8 - 1) == 2);
        assert(!memcmp(utf8, name, sizeof name - 1));
        char output[4] = {1, 2, 3, 4};
        assert(ROMlib_host_name_to_mac("\xf0\x9f\x98\x80", 4, output) < 0);
        assert(output[0] == 1 && output[3] == 4);
        const char* requested = getenv("ROMAW_TEST_HOST_PATH");
        if (requested && *requested) {
            char* resolved = strdup(requested);
            assert(resolved && !weasel(resolved, &info));
            printf("PASS: resolved supplied host path (%lu bytes)\n", (unsigned long)strlen(resolved));
            free(resolved);
        }
    }
#endif
    {
        char directory[1024], path[1200], cwd[900];
        const char* files[] = {"README", "1234567890123456789012345678901", "12345678901234567890123456789012"};
        FILE* file;
        assert(_getcwd(cwd, sizeof cwd));
        for (char* p = cwd; *p; ++p)
            if (*p == '\\')
                *p = '/';
        snprintf(directory, sizeof directory, "%s/build/drive-count.%u", cwd, (unsigned)_getpid());
        assert(_mkdir(directory) == 0);
        netatalk_conventions_p = 1;
        for (i = 0; i < 3; ++i) {
            snprintf(path, sizeof path, "%s/%s", directory, files[i]);
            file = fopen(path, "wb");
            assert(file);
            assert(fclose(file) == 0);
        }
        snprintf(path, sizeof path, "%s/.AppleDouble", directory);
        assert(_mkdir(path) == 0);
        snprintf(path, sizeof path, "%s/.AppleDouble/ResourceOnly", directory);
        LONGINT resource_fd;
        assert(!ROMlib_newresfork(path, &resource_fd, FALSE));
        close(resource_fd);
        assert(ROMlib_ufs_directory_count(directory) == 3);
        struct stat orphan_info;
        uint32 volume_files, volume_directories;
        ROMlib_ufs_volume_counts(directory, &volume_files, &volume_directories);
        assert(volume_files == 3 && volume_directories == 0);
        const char* orphan_name = dirindex(directory, 3, FALSE, &orphan_info);
        assert(orphan_name && !strcmp(orphan_name, "ResourceOnly"));
        snprintf(path, sizeof path, "%s/ResourceOnly", directory);
        assert(!weasel(path, &orphan_info) && !orphan_info.st_size);
        snprintf(path, sizeof path, "%s/resourceonly", directory);
        assert(!weasel(path, &orphan_info));
        snprintf(path, sizeof path, "%s/.AppleDouble/ResourceOnly", directory);
        assert(!remove(path));
        ROMlib_rewinddir();
        snprintf(path, sizeof path, "%s/Folder", directory);
        assert(_mkdir(path) == 0);
        snprintf(path, sizeof path, "%s/Folder/Nested", directory);
        file = fopen(path, "wb");
        assert(file && !fclose(file));
        ROMlib_ufs_volume_counts(directory, &volume_files, &volume_directories);
        assert(volume_files == 3 && volume_directories == 1);
        assert(!remove(path));
        for (unsigned item = 0; item < 20; ++item) {
            snprintf(path, sizeof path, "%s/Folder/File%u", directory, item);
            file = fopen(path, "wb");
            assert(file && !fclose(file));
        }
        ROMlib_ufs_volume_counts(directory, &volume_files, &volume_directories);
        assert(volume_files == 22 && volume_directories == 1);
        for (unsigned item = 0; item < 20; ++item) {
            snprintf(path, sizeof path, "%s/Folder/File%u", directory, item);
            assert(!remove(path));
        }
        host_stat_calls = 0;
        assert(ROMlib_ufs_directory_count(directory) == 3);
#if defined(__MINGW32__)
        assert(!host_stat_calls);
#endif
        snprintf(path, sizeof path, "%s/._README", directory);
        file = fopen(path, "wb");
        const unsigned char magic[] = {0, 5, 0x16, 7};
        assert(file && fwrite(magic, 1, sizeof magic, file) == sizeof magic && !fclose(file));
        snprintf(path, sizeof path, "%s/._ordinary", directory);
        file = fopen(path, "wb");
        assert(file && fputs("plain text", file) >= 0 && !fclose(file));
        assert(ROMlib_ufs_directory_count(directory) == 3);
        snprintf(path, sizeof path, "%s/%%%%2F", directory);
        file = fopen(path, "wb");
        assert(file && fwrite(magic, 1, sizeof magic, file) == sizeof magic && !fclose(file));
        /* Root metadata is hidden even when ordinary percent forks live in
       netatalk subdirectories. It must not acquire its own ._ sidecar. */
        assert(ROMlib_isresourcefork(path));
        assert(ROMlib_ufs_directory_count(directory) == 3);
        assert(!remove(path));
        struct stat indexed_info;
        for (unsigned index = 1; index <= 3; ++index)
            assert(dirindex(directory, index, FALSE, &indexed_info));
        assert(!dirindex(directory, 4, FALSE, &indexed_info));
        snprintf(path, sizeof path, "%s/._README", directory);
        assert(!remove(path));
        snprintf(path, sizeof path, "%s/._ordinary", directory);
        assert(!remove(path));
        for (i = 0; i < 3; ++i) {
            snprintf(path, sizeof path, "%s/%s", directory, files[i]);
            assert(remove(path) == 0);
        }
        snprintf(path, sizeof path, "%s/.AppleDouble", directory);
        assert(_rmdir(path) == 0);
        snprintf(path, sizeof path, "%s/Folder", directory);
        assert(_rmdir(path) == 0);
        assert(ROMlib_ufs_directory_count(directory) == 0);
        assert(_rmdir(directory) == 0);
        assert(ROMlib_ufs_directory_count(directory) == 0);
    }
    free(memory);
    puts("PASS: single-letter drive paths, legacy aliases, relative paths and visible directory counts");
    return 0;
}
