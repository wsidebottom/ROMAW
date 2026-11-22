/*+===================================================================
  File:      sidecar_test.c

  Summary:   Windows regression test for the host-side fork formats. Build from src with the same include flags as
             fileDouble.c, adding -ffunction-sections -fdata-sections -Wl,--gc-sections and this test source. Run in a
             disposable working directory.

  Classes:   None.

  Functions: ROMlib_creator_and_type_from_filename, ROMlib_maperrno, main, syn68k_guest_to_host, syn68k_host_to_guest

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Windows regression test for the host-side fork formats.
 * Build from src with the same include flags as fileDouble.c, adding
 * -ffunction-sections -fdata-sections -Wl,--gc-sections and this test source.
 * Run in a disposable working directory.
 */
#include "rsys/common.h"
#include "rsys/file.h"
#include "rsys/filedouble.h"
#include "rsys/suffix_maps.h"
#include "rsys/osutil.h"
#include <assert.h>

#undef warning_unexpected
#define warning_unexpected(...) ((void)0)
#undef gui_assert
#define gui_assert(x) assert(x)
#undef fs_err_hook
#define fs_err_hook(x) ((void)0)

/* Use the Windows CRT for this isolated test; ROMAW's wrappers normalize
 * host paths in production. Forward slashes work in the CRT too. */
static unsigned stat_calls;

static int test_stat(const char* path, struct stat* info)
{
    ++stat_calls;
    return stat(path, info);
}

#define Ustat test_stat
#define Uopen open
#define Uclose close
#define Uopendir opendir
#define Uunlink unlink
#define Urename rename
#define Urmdir rmdir

static int test_mkdir(const char* path, int mode)
{
    return mkdir(path);
}

#define Umkdir test_mkdir

#undef ROMlib_fcblocks
static fcbrec test_fcblocks[NFCB];
#define ROMlib_fcblocks test_fcblocks

static unsigned fstat_calls;

static int test_fstat(int fd, struct stat* info)
{
    ++fstat_calls;
    return fstat(fd, info);
}

#undef fstat
#define fstat test_fstat
#include "../../files/fileDouble.c"
#undef fstat
#define fstat _fstat64
#undef main

LONGINT ROMlib_GMTcorrect;
#if __SIZEOF_POINTER__ == 8
uint64 ROMlib_offset;
#else
uint32 ROMlib_offset;
#endif
uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;

void* syn68k_guest_to_host(uint32 address)
{
    return SYN68K_TO_US_RAW(address);
}

uint32 syn68k_host_to_guest(ptr_sized_uint address)
{
    return US_TO_SYN68K_RAW(address);
}

OSErr ROMlib_maperrno(void)
{
    return ioErr;
}

boolean_t ROMlib_creator_and_type_from_filename(int len, const char* name, uint32* creator, uint32* type)
{
    return FALSE;
}

static void putfile(const char* name, const void* bytes, int length)
{
    int fd = open(name, O_BINARY | O_CREAT | O_TRUNC | O_RDWR, 0666);
    assert(fd >= 0);
    assert(write(fd, bytes, length) == length);
    close(fd);
}

static void expect_name(const char* data, const char* expected)
{
    const char* leaf = strrchr(data, '/') + 1;
    char* name = ROMlib_sidecar_name(data, leaf);
    if (!name || strcmp(name, expected))
        fprintf(stderr, "%s: expected %s, got %s\n", data, expected, name ? name : "(null)");
    assert(name && !strcmp(name, expected));
    free(name);
}

static OSErr newfork(const char* path, LONGINT* fd)
{
    char* name = strdup(path);
    OSErr err = ROMlib_newresfork(name, fd, FALSE);
    free(name);
    return err;
}

static void empty_fork_tests(void)
{
    const char* prefixes[] = {"._", "%", ".AppleDouble/", ".finf/"};
    for (unsigned i = 0; i < 4; ++i) {
        char folder[64], path[128], metadata[160];
        snprintf(folder, sizeof folder, "empty-%u", i);
        assert(!mkdir(folder));
        snprintf(path, sizeof path, "%s/Only", folder);
        snprintf(metadata, sizeof metadata, "%s/%sOnly", folder, prefixes[i]);
        double_dir_op(metadata, mkdir_op);
        LONGINT fd;
        assert(newfork(metadata, &fd) == noErr);
        close(fd);
        struct stat info;
        assert(stat(path, &info) < 0);
        assert(!ROMlib_ufs_stat(path, &info) && !info.st_size);
        ROMlib_orphan_scan* scan = ROMlib_orphan_scan_open(folder);
        const char* name = ROMlib_orphan_scan_next(scan, &info);
        assert(name && !strcmp(name, "Only") && !info.st_size);
        assert(!ROMlib_orphan_scan_next(scan, &info));
        ROMlib_orphan_scan_close(scan);

        fcbrec *first = &test_fcblocks[0], *second = &test_fcblocks[1];
        memset(test_fcblocks, 0, sizeof test_fcblocks);
        first->fdfnum = second->fdfnum = CLC(123);
        first->fcflags = fcwriteperm;
        assert(!ROMlib_open_empty_fork(first, path));
        assert(!ROMlib_open_empty_fork(second, path));
        second->fcleof = CLC(1);
        assert(ROMlib_seteof(second) == wrPermErr);
        second->fcleof = 0;
        assert(!ROMlib_geteofostype(first) && !first->fcleof);
        char byte;
        assert(!read(first->fcfd, &byte, 1));
        assert(lseek(second->fcfd, 3, SEEK_SET) == 3);
        assert(stat(path, &info) < 0);
        assert(ROMlib_empty_fork_busy(path));
        first->fcleof = CLC(4);
        assert(!ROMlib_seteof(first));
        assert(!ROMlib_empty_fork_p(first));
        assert(!ROMlib_empty_fork_p(second));
        assert(lseek(second->fcfd, 0, SEEK_CUR) == 3);
        assert(!fstat(first->fcfd, &info) && info.st_size == 4);
        assert(stat(metadata, &info) < 0); /* Empty catalog anchor retired. */
        assert(write(first->fcfd, "DATA", 4) == 4);
        assert(read(second->fcfd, &byte, 1) == 1 && byte == 'A');
        close(first->fcfd);
        close(second->fcfd);

        /* Opening an absent resource fork creates no named file. */
        char* resource = ROMlib_sidecar_resource_name(metadata);
        memset(first, 0, sizeof *first);
        first->fdfnum = CLC(124);
        first->fcflags = fcwriteperm | fcfisres;
        assert(!ROMlib_open_empty_fork(first, resource));
        assert(!ROMlib_seteof(first));
        assert(stat(resource, &info) < 0);
        first->fcleof = CLC(8);
        assert(!ROMlib_seteof(first));
        assert(!stat(resource, &info));
        assert(write(first->fcfd, "RESOURCE", 8) == 8);
        close(first->fcfd);
        free(resource);

        /* A zero Finder record alone need not acquire a sidecar. */
        char plain[128], companion[128];
        snprintf(plain, sizeof plain, "%s/Plain", folder);
        snprintf(companion, sizeof companion, "%s/%sPlain", folder, prefixes[i]);
        putfile(plain, "x", 1);
        FInfo finfo = {0};
        FXInfo fxinfo = {0};
        Single_dates dates = {0};
        assert(!ROMlib_hiddenbyname(Set, plain, companion, &dates, &finfo, &fxinfo, NULL, NULL));
        assert(stat(companion, &info) < 0);
        finfo.fdFlags = CWC(0x4000);
        assert(!ROMlib_hiddenbyname(Set, plain, companion, &dates, &finfo, &fxinfo, NULL, NULL));
        assert(!stat(companion, &info));
    }
    putfile("empty-0/._Invalid", "not AppleDouble", 15);
    struct stat invalid;
    assert(ROMlib_ufs_stat("empty-0/Invalid", &invalid) < 0);
    putfile("empty-0/._Zero", "", 0);
    assert(ROMlib_ufs_stat("empty-0/Zero", &invalid) < 0);
}

int main(void)
{
    unsigned char compact[18] = "APPLTEST";
    unsigned char apple[98] = {0}, check[98];
    Single_header* head = (Single_header*)apple;
    Single_descriptor *entries = (Single_descriptor*)(apple + 26), d;
    Single_finfo info;
    Single_dates dates;
    FInfo finfo;
    FXInfo fxinfo;
    LONGINT data_length, resource_length, fd;
    ULONGINT capacity;
    fcbrec fcb;
    char* name;

    apple_double_fork_prefix = "._";
    apple_double_fork_prefix_length = strlen(apple_double_fork_prefix);
    netatalk_conventions_p = 1;
    assert(mkdir("basilisk") == 0);
    assert(mkdir("basilisk/.finf") == 0);
    assert(mkdir("basilisk/.rsrc") == 0);
    putfile("basilisk/App", "data", 4);
    putfile("basilisk/.finf/App", compact, 18);
    putfile("basilisk/.rsrc/App", "resource", 8);
    expect_name("basilisk/App", "basilisk/.finf/App");
    expect_name("basilisk/New", "basilisk/.finf/New");
    name = ROMlib_sidecar_resource_name("basilisk/.finf/App");
    assert(!strcmp(name, "basilisk/.rsrc/App"));
    free(name);
    assert(ROMlib_hiddenbyname(Get, "basilisk/App", "basilisk/.finf/App", &dates, &finfo, &fxinfo, &data_length,
                               &resource_length) == noErr);
    assert(!memcmp(&finfo, "APPLTEST", 8));
    assert(CL(data_length) == 4 && CL(resource_length) == 8);
    memset(&info, 0x77, sizeof(info));
    fd = open("basilisk/.finf/App", O_BINARY | O_RDWR);
    assert(getsetentry(Get, fd, Finder_Info_ID, &d, NULL));
    assert(getsetpiece(Set, fd, &d, (char*)&info, sizeof(info)));
    lseek(fd, 16, SEEK_SET);
    assert(read(fd, check, 2) == 2 && check[0] == 0 && check[1] == 0);
    close(fd);
    assert(newfork("basilisk/.finf/New", &fd) == noErr);
    assert(lseek(fd, 0, SEEK_END) == 32);
    close(fd);
    /* Real Basilisk metadata has a full FXInfo after FInfo. Guest updates
       must retain those bytes and leave the independent resource fork intact. */
    memset(&info, 0x53, sizeof info);
    memcpy(&info.finfo, "APPLTEST", 8);
    putfile("basilisk/.finf/Extended", &info, sizeof info);
    putfile("basilisk/Extended", "data", 4);
    putfile("basilisk/.rsrc/Extended", "resource", 8);
    assert(ROMlib_hiddenbyname(Get, "basilisk/Extended", "basilisk/.finf/Extended", &dates, &finfo, &fxinfo,
                               &data_length, &resource_length) == noErr);
    assert(!memcmp(&finfo, &info.finfo, sizeof finfo));
    assert(!memcmp(&fxinfo, &info.fxinfo, sizeof fxinfo));
    memset(&fxinfo, 0x6a, sizeof fxinfo);
    assert(ROMlib_hiddenbyname(Set, "basilisk/Extended", "basilisk/.finf/Extended", &dates, &finfo, &fxinfo, NULL,
                               NULL) == noErr);
    fd = open("basilisk/.finf/Extended", O_BINARY | O_RDONLY);
    assert(read(fd, check, 32) == 32);
    assert(!memcmp(check, &finfo, 16) && !memcmp(check + 16, &fxinfo, 16));
    close(fd);
    fd = open("basilisk/.rsrc/Extended", O_BINARY | O_RDONLY);
    assert(read(fd, check, 8) == 8 && !memcmp(check, "resource", 8));
    close(fd);
    assert(newfork("basilisk/.rsrc/New", &fd) == noErr);
    memset(&fcb, 0, sizeof(fcb));
    fcb.fcfd = fd;
    fcb.hiddenfd = -1;
    fcb.fcflags = fcfisres;
    assert(ROMlib_FORKOFFSET(&fcb) == 0);
    assert(lseek(fd, 0, SEEK_END) == 0);
    close(fd);
    putfile("basilisk/.rsrc/OnlyResource", "resource", 8);
    expect_name("basilisk/OnlyResource", "basilisk/.finf/OnlyResource");

    assert(mkdir("osx") == 0);
    putfile("osx/.DS_Store", "", 0);
    expect_name("osx/New", "osx/._New");
    assert(newfork("osx/._New", &fd) == noErr);
    assert(ROMlib_resource_fork_present(fd));
    Single_header native_header;
    Single_descriptor native_entries[2];
    lseek(fd, 0, SEEK_SET);
    assert(read(fd, &native_header, sizeof native_header) == sizeof native_header);
    assert(CW(native_header.nentries) == 2);
    assert(read(fd, native_entries, sizeof native_entries) == sizeof native_entries);
    assert(CL(native_entries[0].id) == Finder_Info_ID && CL(native_entries[0].offset) == 50 &&
           CL(native_entries[0].length) == 32);
    assert(CL(native_entries[1].id) == Resource_Fork_ID && CL(native_entries[1].offset) == 82 &&
           CL(native_entries[1].length) == 0);
    assert(lseek(fd, 0, SEEK_END) == 82);
    close(fd);
    /* The descriptor order deliberately differs from the payload order. */
    head->magic = CLC(DOUBLEMAGIC);
    head->version = CLC(SINGLEVERSION);
    head->nentries = CWC(2);
    entries[0].id = CLC(Resource_Fork_ID);
    entries[0].offset = CLC(50);
    entries[0].length = CLC(32);
    entries[1].id = CLC(Finder_Info_ID);
    entries[1].offset = CLC(82);
    entries[1].length = CLC(16);
    memcpy(apple + 82, "APPLTEST", 8);
    putfile("osx/._App", apple, sizeof(apple));
    expect_name("osx/App", "osx/._App");
    fd = open("osx/._App", O_BINARY | O_RDWR);
    assert(getsetentry(Get, fd, Resource_Fork_ID, &d, &capacity));
    assert(CL(d.offset) == 50 && capacity == 32);
    fcbrec* cached_fork = &test_fcblocks[0];
    memset(cached_fork, 0, sizeof *cached_fork);
    cached_fork->fcfd = cached_fork->hiddenfd = fd;
    cached_fork->fdfnum = CLC(123);
    cached_fork->fcflags = fcfisres;
    assert(lseek(fd, 7, SEEK_SET) == 7);
    unsigned metadata_before = fstat_calls;
    for (int repeat = 0; repeat < 1024; ++repeat)
        assert(ROMlib_FORKOFFSET(cached_fork) == 50);
    assert(fstat_calls == metadata_before + 1);
    assert(lseek(fd, 0, SEEK_CUR) == 7);
    ROMlib_forget_fork_offset(cached_fork); /* Closing/reusing the FCB clears it. */
    metadata_before = fstat_calls;
    assert(ROMlib_FORKOFFSET(cached_fork) == 50);
    assert(fstat_calls == metadata_before + 1);
    /* A rejected descriptor update leaves both the map and file mark intact. */
    Single_descriptor invalid_descriptor = d;
    invalid_descriptor.length = CLC(4096);
    assert(lseek(fd, 7, SEEK_SET) == 7);
    assert(!getsetentry(Set, fd, Resource_Fork_ID, &invalid_descriptor, NULL));
    assert(lseek(fd, 0, SEEK_CUR) == 7);
    assert(getsetentry(Get, fd, Resource_Fork_ID, &invalid_descriptor, NULL));
    assert(!memcmp(&invalid_descriptor, &d, sizeof d));

    assert(getsetentry(Get, fd, Finder_Info_ID, &d, NULL));
    assert(getsetpiece(Get, fd, &d, (char*)&info, sizeof(info)));
    assert(!memcmp(&info, "APPLTEST", 8));
    {
        FXInfo empty = {0};
        assert(!memcmp(&info.fxinfo, &empty, sizeof(empty)));
    }
    memset(&info, 0x77, sizeof(info));
    assert(getsetpiece(Set, fd, &d, (char*)&info, sizeof(info)));
    lseek(fd, 0, SEEK_SET);
    assert(read(fd, check, sizeof(check)) == sizeof(check));
    assert(!memcmp(check, apple, 82));
    assert(getsetentry(Get, fd, Resource_Fork_ID, &d, NULL));
    lseek(fd, CL(d.offset) + 3, SEEK_SET);
    assert(grow_resource_entry(fd, &d, 65536));
    assert(CL(d.offset) == sizeof(apple));
    metadata_before = fstat_calls;
    assert(ROMlib_FORKOFFSET(cached_fork) == sizeof(apple));
    assert(fstat_calls == metadata_before + 1); /* Relocation invalidated it. */
    cached_fork->fdfnum = CLC(456);
    metadata_before = fstat_calls;
    assert(ROMlib_FORKOFFSET(cached_fork) == sizeof(apple));
    assert(fstat_calls == metadata_before + 1); /* Identity changes never reuse it. */
    ROMlib_forget_fork_offset(cached_fork);

    assert(lseek(fd, 0, SEEK_CUR) == sizeof(apple) + 3);
    lseek(fd, 82, SEEK_SET);
    assert(read(fd, check, 16) == 16 && check[0] == 0x77);
    close(fd);
    assert(mkdir("unmarked") == 0);
    putfile("unmarked/._ordinary", "ordinary text", 13);
    expect_name("unmarked/New", "unmarked/._New");
    apple_double_fork_prefix = ".AppleDouble/";
    expect_name("unmarked/New", "unmarked/.AppleDouble/New");
    apple_double_fork_prefix = ".finf/";
    expect_name("unmarked/New", "unmarked/.finf/New");
    expect_name("osx/New", "osx/._New");
    apple_double_fork_prefix = "%";
    expect_name("unmarked/New", "unmarked/%New");
    expect_name("basilisk/New", "basilisk/.finf/New");
    apple_double_fork_prefix = "._";
    /* A header with no room for its declared table must be rejected. */
    head->nentries = CWC(20);
    putfile("osx/._Bad", apple, sizeof(apple));
    fd = open("osx/._Bad", O_BINARY | O_RDONLY);
    assert(!getsetentry(Get, fd, Resource_Fork_ID, &d, NULL));
    close(fd);
    assert(mkdir("netatalk") == 0);
    assert(mkdir("netatalk/.AppleDouble") == 0);
    expect_name("netatalk/New", "netatalk/.AppleDouble/New");
    assert(newfork("netatalk/.AppleDouble/New", &fd) == noErr);
    assert(ROMlib_resource_fork_present(fd));
    /* Browser copies allocate a resource fork, then set its EOF before
     writing it. Both calls must leave a physically valid descriptor. */
    memset(&fcb, 0, sizeof fcb);
    fcb.fcfd = fcb.hiddenfd = fd;
    fcb.fcflags = fcfisres;
    fcb.fcleof = CL(436697);
    test_fcblocks[0] = fcb;
    assert(lseek(fd, 17, SEEK_SET) == 17);
    assert(ROMlib_seteof(&fcb) == noErr);
    assert(lseek(fd, 0, SEEK_CUR) == 17);
    assert(getsetentry(Get, fd, Resource_Fork_ID, &d, NULL));
    assert(CL(d.offset) == 512 && CL(d.length) == 436697);
    assert(lseek(fd, 0, SEEK_END) == 512 + 436697);
    assert(ROMlib_seteof(&fcb) == noErr);
    assert(ROMlib_geteofostype(&fcb) == noErr);
    assert(CL(fcb.fcleof) == 436697);
    fcb.fcleof = CL(8);
    assert(ROMlib_seteof(&fcb) == noErr);
    assert(getsetentry(Get, fd, Resource_Fork_ID, &d, NULL));
    assert(CL(d.length) == 8);
    close(fd);
    /* Out-of-file payloads cannot masquerade as usable resource forks. */
    head->nentries = CWC(2);
    entries[0].offset = CLC(4096);
    putfile("osx/._OutOfBounds", apple, sizeof(apple));
    fd = open("osx/._OutOfBounds", O_BINARY | O_RDONLY);
    assert(!ROMlib_resource_fork_present(fd));
    close(fd);
    {
        const char* target = "netatalk/App";
        name = ROMlib_sidecar_rename_name("osx/._App", target, strrchr(target, '/') + 1);
    }
    assert(!strcmp(name, "netatalk/._App"));
    free(name);
    /* A move preserves both companions, even into a Netatalk directory. */
    {
        const char* target = "netatalk/Moved";
        char* source = strdup("basilisk/.finf/App");
        char* destination = ROMlib_sidecar_rename_name(source, target, strrchr(target, '/') + 1);
        assert(ROMlib_sidecar_move(source, destination) == 0);
        assert(!sidecar_exists(source, FALSE));
        assert(!sidecar_exists("basilisk/.rsrc/App", FALSE));
        assert(sidecar_exists(destination, FALSE));
        fd = open("netatalk/.rsrc/Moved", O_BINARY | O_RDONLY);
        assert(fd >= 0 && read(fd, check, 8) == 8);
        assert(!memcmp(check, "resource", 8));
        close(fd);
        expect_name(target, "netatalk/.finf/Moved");
        assert(ROMlib_sidecar_delete(destination) == 0);
        assert(!sidecar_exists(destination, FALSE));
        assert(!sidecar_exists("netatalk/.rsrc/Moved", FALSE));
        free(source);
        free(destination);
    }
    assert(mkdir("basilisk/Empty") == 0);
    assert(mkdir("basilisk/Empty/.finf") == 0);
    assert(mkdir("basilisk/Empty/.rsrc") == 0);
    assert(mkdir("basilisk/Empty/.AppleDouble") == 0);
    putfile("basilisk/Empty/.AppleDouble/.Parent", "metadata", 8);
    putfile("basilisk/Empty/._.Parent", "user file", 9);
    putfile("basilisk/Empty/.DS_Store", "cache", 5);
    name = strdup("basilisk/Empty");
    double_dir_op(name, rmdir_op);
    assert(!sidecar_exists("basilisk/Empty/.AppleDouble", TRUE));
    assert(sidecar_exists("basilisk/Empty/._.Parent", FALSE));
    assert(remove("basilisk/Empty/._.Parent") == 0);
    double_dir_op(name, rmdir_op);
    free(name);
    assert(rmdir("basilisk/Empty") == 0);
    /* Repeated catalog entries share absent-directory probes, but never cache
     the existence of individual companions or retain a snapshot after exit. */
    assert(mkdir("batch") == 0);
    putfile("batch/._One", apple, sizeof apple);
    putfile("batch/._Two", apple, sizeof apple);
    ROMlib_sidecar_catalog_batch(TRUE);
    expect_name("batch/One", "batch/._One");
    unsigned before = stat_calls;
    expect_name("batch/Two", "batch/._Two");
    assert(stat_calls - before == 1);
    ROMlib_sidecar_catalog_batch(TRUE); /* Nested opt-in preserves the batch. */
    before = stat_calls;
    expect_name("batch/One", "batch/._One");
    assert(stat_calls - before == 1);
    ROMlib_sidecar_catalog_batch(FALSE);
    before = stat_calls;
    expect_name("batch/One", "batch/._One");
    assert(stat_calls - before == 1); /* OS X wins without legacy probes. */
    ROMlib_sidecar_catalog_batch(TRUE);
    before = stat_calls;
    assert(!catalog_probe("batch/.AppleDouble", TRUE));
    assert(!catalog_probe("batch/.AppleDouble", TRUE));
    assert(stat_calls - before == 1);
    ROMlib_sidecar_catalog_batch(FALSE);
    assert(mkdir("batch/.AppleDouble") == 0);
    putfile("batch/.AppleDouble/One", apple, sizeof apple);
    ROMlib_sidecar_catalog_batch(TRUE);
    expect_name("batch/One", "batch/._One");
    expect_name("batch/Two", "batch/._Two");
    expect_name("batch/New", "batch/._New");
    putfile("batch/.AppleDouble/Legacy", apple, sizeof apple);
    assert(mkdir("batch/.finf") == 0);
    assert(mkdir("batch/.rsrc") == 0);
    putfile("batch/.finf/Legacy", compact, sizeof compact);
    putfile("batch/.finf/Basilisk", compact, sizeof compact);
    ROMlib_sidecar_catalog_batch(FALSE);
    ROMlib_sidecar_catalog_batch(TRUE);
    expect_name("batch/Legacy", "batch/.AppleDouble/Legacy");
    expect_name("batch/Basilisk", "batch/.finf/Basilisk");
    putfile("batch/._Legacy", "ordinary file", 13);
    expect_name("batch/Legacy", "batch/.AppleDouble/Legacy");
    ROMlib_sidecar_catalog_batch(FALSE);
    empty_fork_tests();
    puts("Sidecar regression tests passed");
    return 0;
}
