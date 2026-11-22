/*+===================================================================
  File:      dib_decode_test.c

  Summary:   Implements dib decode test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#define USE_WINDOWS_NOT_MAC_TYPEDEFS_AND_DEFINES
#include "rsys/common.h"
#include <windows.h>
#include <assert.h>
#include "SDL/SDL.h"
static int fail_after = -1, codec_blocks, fail_surface, fail_blit;

static void* codec_realloc(void* old, size_t size)
{
    void* result;
    if (!fail_after)
        return NULL;
    if (fail_after > 0)
        --fail_after;
    result = realloc(old, size);
    assert(result);
    if (!old)
        ++codec_blocks;
    return result;
}

static void* codec_malloc(size_t size)
{
    return codec_realloc(NULL, size);
}

static void codec_free(void* p)
{
    if (p)
        --codec_blocks;
    free(p);
}

static SDL_Surface* codec_surface(Uint32 flags, int w, int h, int depth, Uint32 r, Uint32 g, Uint32 b, Uint32 alpha)
{
    return fail_surface ? NULL : SDL_CreateRGBSurface(flags, w, h, depth, r, g, b, alpha);
}

static int codec_blit(SDL_Surface* src, SDL_Rect* sr, SDL_Surface* dst, SDL_Rect* dr)
{
    return fail_blit ? -1 : SDL_UpperBlit(src, sr, dst, dr);
}

#undef SDL_AllocSurface
#define SDL_AllocSurface codec_surface
#undef SDL_BlitSurface
#define SDL_BlitSurface codec_blit
#define malloc codec_malloc
#define realloc codec_realloc
#define free codec_free
#undef MAX
#include "../../frontend-sdl/SDL_bmp.c"
#undef malloc
#undef realloc
#undef free
#undef main

static void put16(unsigned char* p, unsigned value)
{
    p[0] = value;
    p[1] = value >> 8;
}

static void put32(unsigned char* p, unsigned value)
{
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
}

static size_t fixture(unsigned char* p, unsigned depth, int top_down, int core, int masks)
{
    unsigned offset = core ? 12 : 40;
    memset(p, 0, 128);
    put32(p, offset);
    if (core) {
        put16(p + 4, 1);
        put16(p + 6, 2);
        put16(p + 8, 1);
        put16(p + 10, depth);
    }
    else {
        put32(p + 4, 1);
        put32(p + 8, top_down ? -2 : 2);
        put16(p + 12, 1);
        put16(p + 14, depth);
        if (depth <= 8)
            put32(p + 32, 2);
        if (masks) {
            put32(p + 16, 3);
            put32(p + 40, 0xF800);
            put32(p + 44, 0x07E0);
            put32(p + 48, 0x001F);
            offset += 12;
        }
    }
    if (depth <= 8) {
        unsigned colors = core ? 1U << depth : 2;
        p[offset + 2] = 255;
        p[offset + (core ? 3 : 4)] = 255;
        offset += colors * (core ? 3 : 4);
    }
    if (depth <= 8)
        p[offset + (top_down ? 4 : 0)] = depth == 1 ? 0x80 : depth == 4 ? 0x10 : 1;
    else if (depth == 16) {
        put16(p + offset, top_down ? (masks ? 0xF800 : 0x7C00) : 0x001F);
        put16(p + offset + 4, top_down ? 0x001F : (masks ? 0xF800 : 0x7C00));
    }
    else {
        p[offset + (top_down ? 2 : 0)] = 255;
        p[offset + 4 + (top_down ? 0 : 2)] = 255;
    }
    return offset + 8;
}

static void colors(SDL_Surface* surface)
{
    int row;
    assert(surface && surface->w == 1 && surface->h == 2);
    for (row = 0; row < 2; ++row) {
        Uint32 pixel = 0;
        Uint8 r, g, b;
        memcpy(&pixel, (char*)surface->pixels + row * surface->pitch, surface->format->BytesPerPixel);
        SDL_GetRGB(pixel, surface->format, &r, &g, &b);
        assert(row ? (b == 255 && r == 0 && g == 0) : (r == 255 && b == 0 && g == 0));
    }
}

int main(void)
{
    unsigned char bytes[256];
    unsigned depths[] = {1, 4, 8, 16, 24, 32};
    SYSTEM_INFO info;
    unsigned char* pages;
    DWORD old;
    int i, top, n;
    size_t length, cut;
    SDL_Surface* surface;
    GetSystemInfo(&info);
    pages = VirtualAlloc(NULL, 2 * info.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(pages && VirtualProtect(pages + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old));
    for (i = 0; i < 6; ++i)
        for (top = 0; top < 2; ++top) {
            length = fixture(bytes, depths[i], top, 0, 0);
            {
                unsigned char* guarded = pages + info.dwPageSize - length;
                memcpy(guarded, bytes, length);
                surface = SDL_LoadCF_DIB(guarded, length);
                colors(surface);
                SDL_FreeSurface(surface);
            }
            for (cut = 0; cut < length; ++cut) {
                unsigned char* guarded = pages + info.dwPageSize - cut;
                memcpy(guarded, bytes, cut);
                assert(!SDL_LoadCF_DIB(guarded, cut));
            }
        }
    length = fixture(bytes, 16, 0, 0, 1);
    surface = SDL_LoadCF_DIB(bytes, length);
    colors(surface);
    SDL_FreeSurface(surface);
    length = fixture(bytes, 24, 0, 1, 0);
    surface = SDL_LoadCF_DIB(bytes, length);
    colors(surface);
    SDL_FreeSurface(surface);
    {
        unsigned headers[] = {52, 56, 108, 124};
        for (n = 0; n < 4; ++n) {
            unsigned header = headers[n];
            unsigned char original[128];
            length = fixture(original, 24, 0, 0, 0);
            memset(bytes, 0, sizeof bytes);
            memcpy(bytes, original, 40);
            put32(bytes, header);
            memcpy(bytes + header, original + 40, length - 40);
            length += header - 40;
            surface = SDL_LoadCF_DIB(bytes, length);
            colors(surface);
            SDL_FreeSurface(surface);
            assert(!codec_blocks);
        }
    }
    for (n = 0; n < 10; ++n) {
        length = fixture(bytes, 8, 0, 0, 0);
        switch (n) {
        case 0:
            put32(bytes, 124);
            break;
        case 1:
            put32(bytes + 4, 0);
            break;
        case 2:
            put32(bytes + 8, 0x80000000);
            break;
        case 3:
            put16(bytes + 12, 2);
            break;
        case 4:
            put16(bytes + 14, 2);
            break;
        case 5:
            put32(bytes + 16, 1);
            break;
        case 6:
            put32(bytes + 32, 257);
            break;
        case 7:
            put32(bytes + 4, 0x7FFFFFFF);
            break;
        case 8:
            put32(bytes + 8, 0);
            break;
        case 9:
            put16(bytes + 14, 32);
            put32(bytes + 4, 16384);
            break;
        }
        assert(!SDL_LoadCF_DIB(bytes, length));
    }
    length = fixture(bytes, 16, 0, 0, 1);
    put32(bytes + 44, 0xF800);
    assert(!SDL_LoadCF_DIB(bytes, length));
    length = fixture(bytes, 8, 0, 0, 0);
    bytes[48] = 2;
    assert(!SDL_LoadCF_DIB(bytes, length));
    length = fixture(bytes, 24, 0, 0, 0);
    fail_surface = 1;
    assert(!SDL_LoadCF_DIB(bytes, length));
    fail_surface = 0;
    assert(!SDL_LoadCF_DIB(NULL, 128));
    {
        SDL_Surface* input = SDL_CreateRGBSurface(0, 64, 64, 32, 0xFF0000, 0xFF00, 0xFF, 0);
        char* encoded;
        size_t used;
        int attempt, success = 0;
        assert(input);
        memset(input->pixels, 0xCC, input->h * input->pitch);
        for (attempt = 0; attempt < 10; ++attempt) {
            fail_after = attempt;
            encoded = (char*)1;
            used = 999;
            if (SDL_SaveCF_DIB(input, &encoded, &used) == 0) {
                assert(encoded && used && codec_blocks == 1);
                surface = SDL_LoadCF_DIB(encoded, used);
                assert(surface);
                SDL_FreeSurface(surface);
                codec_free(encoded);
                success = 1;
            }
            else
                assert(!encoded && !used);
            assert(!codec_blocks);
            if (success)
                break;
        }
        assert(success && attempt >= 2);
        fail_after = -1;
        fail_surface = 1;
        assert(SDL_SaveCF_DIB(input, &encoded, &used) < 0 && !encoded && !used && !codec_blocks);
        fail_surface = 0;
        fail_blit = 1;
        assert(SDL_SaveCF_DIB(input, &encoded, &used) < 0 && !encoded && !used && !codec_blocks);
        fail_blit = 0;
        SDL_FreeSurface(input);
        input = SDL_CreateRGBSurface(0, 1, 2, 24, 0xFF0000, 0xFF00, 0xFF, 0);
        assert(input);
        memset(input->pixels, 0xCC, input->h * input->pitch);
        assert(SDL_SaveCF_DIB(input, &encoded, &used) == 0 && used == 48);
        assert((unsigned char)encoded[43] == 0 && (unsigned char)encoded[47] == 0);
        codec_free(encoded);
        SDL_FreeSurface(input);
        assert(SDL_SaveCF_DIB(NULL, &encoded, &used) < 0 && !encoded && !used && !codec_blocks);
    }
    {
        char directory[MAX_PATH], filename[MAX_PATH];
        DWORD size = GetTempPathA(sizeof directory, directory);
        assert(size && size < sizeof directory);
        assert(GetTempFileNameA(directory, "dib", 0, filename));
        length = fixture(bytes, 24, 0, 0, 0);
        surface = SDL_LoadCF_DIB(bytes, length);
        colors(surface);
        assert(SDL_SaveBMP_ctmhackage(surface, filename) == 0);
        SDL_FreeSurface(surface);
        surface = SDL_LoadBMP_ctmhackage(filename);
        colors(surface);
        SDL_FreeSurface(surface);
        assert(DeleteFileA(filename));
        assert(!codec_blocks);
    }
    VirtualFree(pages, 0, MEM_RELEASE);
    puts(
        "PASS: bounded DIB decode, guard pages, palettes, bitfields, top-down rows, encoder failures/padding and BMP round trip");
    return 0;
}
