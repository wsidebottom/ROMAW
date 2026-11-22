/*+===================================================================
  File:      ico_windows_test.c

  Summary:   Independent Windows decoding of every embedded ICO image; no UI shown.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Independent Windows decoding of every embedded ICO image; no UI shown. */
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned read16(const unsigned char* p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t read32(const unsigned char* p)
{
    return read16(p) | (uint32_t)read16(p + 2) << 16;
}

int main(int argc, char** argv)
{
    int arg, images = 0;
    assert(argc > 1);
    for (arg = 1; arg < argc; ++arg) {
        FILE* fp = fopen(argv[arg], "rb");
        unsigned char* data;
        long size;
        unsigned i, count;
        assert(fp);
        assert(fseek(fp, 0, SEEK_END) == 0);
        size = ftell(fp);
        assert(size >= 6 && fseek(fp, 0, SEEK_SET) == 0);
        data = malloc((size_t)size);
        assert(data && fread(data, 1, (size_t)size, fp) == (size_t)size);
        fclose(fp);
        count = read16(data + 4);
        assert(6 + count * 16 <= (unsigned long)size);
        for (i = 0; i < count; ++i) {
            const unsigned char* entry = data + 6 + i * 16;
            unsigned width = entry[0] ? entry[0] : 256;
            unsigned height = entry[1] ? entry[1] : 256;
            uint32_t length = read32(entry + 8), offset = read32(entry + 12);
            HICON icon;
            ICONINFO info;
            BITMAP bitmap;
            assert(offset <= (unsigned long)size && length <= size - offset);
            icon = CreateIconFromResourceEx(data + offset, length, TRUE, 0x00030000, width, height, 0);
            assert(icon && GetIconInfo(icon, &info) && info.fIcon);
            if (info.hbmColor) {
                assert(GetObject(info.hbmColor, sizeof bitmap, &bitmap));
                assert(bitmap.bmWidth == (LONG)width && bitmap.bmHeight == (LONG)height);
            }
            assert(GetObject(info.hbmMask, sizeof bitmap, &bitmap));
            assert(bitmap.bmWidth == (LONG)width);
            assert(bitmap.bmHeight == (LONG)height * (info.hbmColor ? 1 : 2));
            if (info.hbmColor)
                DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask);
            assert(DestroyIcon(icon));
            ++images;
        }
        free(data);
    }
    printf("Windows decoded %d ICO images at their native dimensions\n", images);
    return 0;
}
