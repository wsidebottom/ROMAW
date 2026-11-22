/*+===================================================================
  File:      raster_pdf_test.c

  Summary:   A real PDF spool with original synthetic pages; no printer is contacted.

  Classes:   None.

  Functions: host_print_spool, host_print_submit, main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* A real PDF spool with original synthetic pages; no printer is contacted. */
#include "rsys/hostdialog.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int submitted;

FILE* host_print_spool(char** path)
{
    *path = strdup("/tmp/romaw-raster-pdf-XXXXXX");
    int fd = mkstemp(*path);
    return fd < 0 ? NULL : fdopen(fd, "wb");
}

int host_print_submit(const char* path)
{
    FILE *in = fopen(path, "rb"), *out = fopen("build/raster-print-test.pdf", "wb");
    assert(in && out);
    unsigned char bytes[4096];
    size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, in)))
        assert(fwrite(bytes, 1, n, out) == n);
    assert(!ferror(in) && !fclose(in) && !fclose(out));
    ++submitted;
    return 1;
}

int main(void)
{
    const unsigned char page[] = {0x80, 0, 0xaa, 0, 0x80, 0xbb};
    assert(host_raster_begin(612, 792) == 1);
    assert(host_raster_page(page, 9, 2, 3) == 1);
    assert(host_raster_page(page, 9, 2, 3) == 1);
    assert(host_raster_end(0) == 1 && submitted == 1);
    assert(host_raster_begin(612, 792) == 1);
    assert(host_raster_page(page, 9, 2, 1) < 0);
    assert(host_raster_end(0) < 0 && submitted == 1);
    assert(host_raster_begin(612, 792) == 1);
    assert(host_raster_page(page, 9, 2, 3) == 1);
    assert(host_raster_end(1) == 1 && submitted == 1);
    puts("Raster PDF: two pages, row padding, rejected short stride and cancellation passed");
    return 0;
}
