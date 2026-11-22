/*+===================================================================
  File:      rasterpdf.c

  Summary:   Native Unix print systems consume PDF; pages are already drawn by QuickDraw.

  Classes:   None.

  Functions: host_raster_begin, host_raster_end, host_raster_page

  Origin:    Existing repository source in executor/src/printing; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Native Unix print systems consume PDF; pages are already drawn by QuickDraw. */
#include "rsys/hostdialog.h"
#if !defined(_WIN32)
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static FILE* pdf;
static char* pdf_path;
static long* offsets;
static size_t capacity, pages;
static int paper_width, paper_height, failed;

static int object(size_t id)
{
    if (id >= capacity) {
        size_t count = id + 32;
        if (count < id || count > SIZE_MAX / sizeof *offsets)
            return 0;
        long* p = realloc(offsets, count * sizeof *p);
        if (!p)
            return 0;
        offsets = p;
        capacity = count;
    }
    offsets[id] = ftell(pdf);
    return offsets[id] >= 0 && fprintf(pdf, "%zu 0 obj\n", id) > 0;
}

int host_raster_begin(int width, int height)
{
    if (pdf || width <= 0 || height <= 0 || width > 32000 || height > 32000)
        return -1;
    pdf = host_print_spool(&pdf_path);
    if (!pdf)
        return -1;
    paper_width = width;
    paper_height = height;
    pages = 0;
    failed = fputs("%PDF-1.4\n", pdf) < 0;
    if (failed) {
        host_raster_end(1);
        return -1;
    }
    return failed ? -1 : 1;
}

int host_raster_page(const unsigned char* pixels, int width, int height, int stride)
{
    if (!pdf || failed || !pixels || width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
        stride < (width + 7) / 8 || pages > (SIZE_MAX - 6) / 3) {
        failed = 1;
        return -1;
    }
    size_t page = 3 + pages * 3, image = page + 1, content = page + 2;
    size_t row = ((size_t)width + 7) / 8;
    if ((size_t)height > SIZE_MAX / row || !object(page)) {
        failed = 1;
        return -1;
    }
    fprintf(pdf,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
            "/Resources << /XObject << /Im0 %zu 0 R >> >> "
            "/Contents %zu 0 R >>\nendobj\n",
            paper_width, paper_height, image, content);
    if (!object(image)) {
        failed = 1;
        return -1;
    }
    fprintf(pdf,
            "<< /Type /XObject /Subtype /Image /Width %d /Height %d "
            "/ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0] "
            "/Length %zu >>\nstream\n",
            width, height, row * height);
    for (int y = 0; y < height; ++y)
        if (fwrite(pixels + (size_t)y * stride, 1, row, pdf) != row)
            failed = 1;
    fputs("\nendstream\nendobj\n", pdf);
    char command[128];
    int length = snprintf(command, sizeof command, "q %d 0 0 %d 0 0 cm /Im0 Do Q\n", paper_width, paper_height);
    if (length <= 0 || length >= sizeof command || !object(content)) {
        failed = 1;
        return -1;
    }
    fprintf(pdf, "<< /Length %d >>\nstream\n", length);
    fputs(command, pdf);
    fputs("endstream\nendobj\n", pdf);
    ++pages;
    if (ferror(pdf))
        failed = 1;
    return failed ? -1 : 1;
}

int host_raster_end(int cancel)
{
    int result = cancel ? 1 : -1;
    if (!pdf)
        return result;
    if (!cancel && !failed && pages && object(1)) {
        fputs("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", pdf);
        if (!object(2))
            failed = 1;
        fprintf(pdf, "<< /Type /Pages /Count %zu /Kids [", pages);
        for (size_t i = 0; i < pages; ++i)
            fprintf(pdf, "%zu 0 R ", 3 + i * 3);
        fputs("] >>\nendobj\n", pdf);
        long cross_reference = ftell(pdf);
        if (cross_reference < 0)
            failed = 1;
        size_t count = 3 + pages * 3;
        fprintf(pdf, "xref\n0 %zu\n0000000000 65535 f \n", count);
        for (size_t i = 1; i < count; ++i)
            fprintf(pdf, "%010ld 00000 n \n", offsets[i]);
        fprintf(pdf, "trailer\n<< /Size %zu /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", count, cross_reference);
        if (!failed && !ferror(pdf))
            result = 1;
    }
    else if (!cancel)
        result = -1;
    if (fclose(pdf))
        result = -1;
    pdf = NULL;
    if (!cancel && result > 0)
        result = host_print_submit(pdf_path);
    unlink(pdf_path);
    free(pdf_path);
    pdf_path = NULL;
    free(offsets);
    offsets = NULL;
    capacity = pages = 0;
    failed = 0;
    return result;
}
#endif
