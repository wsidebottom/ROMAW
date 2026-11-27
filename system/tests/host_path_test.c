/*+===================================================================
  File:      host_path_test.c

  Summary:   Implements host path test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "rsys/common.h"
#include <assert.h>
#include <setjmp.h>
static int fail;
static jmp_buf allocation_error;
char ROMlib_startdir[1024] = "D:\\ROMAW";
uint32 ROMlib_skipdrives;

static void* path_malloc(size_t size)
{
    return fail ? NULL : malloc(size);
}

static void path_exit(int status)
{
    assert(status == 1);
    longjmp(allocation_error, 1);
}

#define malloc path_malloc
#define exit path_exit
#include "../../files/fileMisc.c"
#undef malloc
#undef exit
#undef main

int main(void)
{
    char *path, *large = malloc(1000001);
    assert(large);
    assert(!copystr(NULL));
    assert(!host_drive_skipped("C:/Apps"));
    ROMlib_skipdrives = (1U << ('C' - 'A')) | (1U << ('Z' - 'A'));
    assert(host_drive_skipped("C:/Apps"));
    assert(host_drive_skipped("c:\\Apps"));
    assert(host_drive_skipped("z:/"));
    assert(!host_drive_skipped("D:/Apps"));
    assert(!host_drive_skipped("/Apps"));
    assert(!host_drive_skipped("ROMAW:Apps"));
    assert(!host_drive_skipped(NULL));
    assert(!host_drive_skipped(""));
    assert(!host_drive_skipped("[ :/"));
    ROMlib_skipdrives = 0;
    strcpy(ROMlib_userdir, "C:/Users/Test/.romaw");
    path = copystr("@/Apps");
    assert(!strcmp(path, "C:\\Users\\Test\\.romaw\\Apps"));
    free(path);
    path = copystr("+/Apps");
    assert(path && !strcmp(path, "D:\\ROMAW\\Apps"));
    free(path);
    path = copystr("C:/Other/App");
    assert(path && !strcmp(path, "C:\\Other\\App"));
    free(path);
    path = copystr("");
    assert(path && !*path);
    free(path);
    memset(large, 'A', 1000000);
    large[1000000] = 0;
    path = copystr(large);
    assert(path && strlen(path) == 1000000 && !strcmp(path, large));
    free(path);
    fail = 1;
    assert(!combine_str("a", "b"));
    if (!setjmp(allocation_error)) {
        copystr("+/Apps");
        assert(!"Missing controlled allocation failure");
    }
    free(large);
    puts("PASS: host path expansion, long paths and controlled allocation failure");
    return 0;
}
