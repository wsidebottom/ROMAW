/*+===================================================================
  File:      sdl_gpu_test.cpp

  Summary:   Compare GPU output with CPU packed-pixel scaling and measure presentation throughput.

  Classes:   gpu_renderer_test.

  Functions: main

  Origin:    Executor graphics regression fixture; uses original generated pixel patterns.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "../../frontend-sdl/sdlgpu.cpp"
#undef main
#include "rsys/sdlpixels.h"
#include <cassert>
#include <vector>

struct gpu_renderer_test {
    static void compare(SDL_Surface* surface, const std::vector<unsigned char>& expected, int stride, int width,
                        int height, int bpp, int scale)
    {
        assert(renderer && renderer->draw(surface, false));
        gpu_ref<ID3D11Texture2D> back, readback;
        assert(
            SUCCEEDED(renderer->swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.put()))));
        D3D11_TEXTURE2D_DESC desc;
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        assert(SUCCEEDED(renderer->device->CreateTexture2D(&desc, nullptr, readback.put())));
        renderer->context->CopyResource(readback.get(), back.get());
        D3D11_MAPPED_SUBRESOURCE mapped;
        assert(SUCCEEDED(renderer->context->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped)));
        std::vector<unsigned char> row(surface->w);
        for (int y = 0; y < surface->h; ++y) {
            assert(y * 100 / scale < height);
            ROMlib_sdl_copy_scaled_row(row.data(), expected.data() + (y * 100 / scale) * stride, 0, surface->w, 0, bpp,
                                       scale);
            const auto* actual = static_cast<unsigned char*>(mapped.pData) + y * mapped.RowPitch;
            for (int x = 0; x < surface->w; ++x) {
                assert(x * 100 / scale < width);
                SDL_Color color = surface->format->palette->colors[row[x]];
                assert(actual[x * 4] == color.r && actual[x * 4 + 1] == color.g && actual[x * 4 + 2] == color.b &&
                       actual[x * 4 + 3] == 255);
            }
        }
        renderer->context->Unmap(readback.get(), 0);
    }
};

static SDL_Surface* mode(int width, int height, int scale)
{
    sdl_gpu_reset();
    SDL_Surface* surface =
        SDL_SetVideoMode(ROMlib_scale_edge(width, scale), ROMlib_scale_edge(height, scale), 8, SDL_SWSURFACE);
    assert(surface);
    SDL_Color palette[256];
    for (int i = 0; i < 256; ++i)
        palette[i] = SDL_Color{static_cast<Uint8>(i), static_cast<Uint8>(255 - i), static_cast<Uint8>(i ^ 0xa5), 0};
    assert(SDL_SetColors(surface, palette, 0, 256));
    return surface;
}

static void pixels(void)
{
    const int width = 17, height = 11, stride = 32;
    std::vector<unsigned char> source(stride * height), expected;
    for (int bpp : {1, 2, 4, 8})
        for (int scale : {125, 150, 200, 400}) {
            SDL_Surface* surface = mode(width, height, scale);
            for (size_t i = 0; i < source.size(); ++i)
                source[i] = static_cast<unsigned char>(i * 37 + i / 7);
            expected = source;
            // The initial upload must cover everything, even for a small first dirty rectangle.
            assert(sdl_gpu_present(surface, source.data(), stride, width, height, bpp, scale, 3, 2, 7, 8));
            gpu_renderer_test::compare(surface, expected, stride, width, height, bpp, scale);
            for (auto& byte : source)
                byte ^= 0xff;
            for (int y = 2; y < 8; ++y)
                for (int x = 3 * bpp / 8; x < (7 * bpp + 7) / 8; ++x)
                    expected[y * stride + x] = source[y * stride + x];
            assert(sdl_gpu_present(surface, source.data(), stride, width, height, bpp, scale, 3, 2, 7, 8));
            gpu_renderer_test::compare(surface, expected, stride, width, height, bpp, scale);
            SDL_Color changed = {29, 41, 53, 0};
            assert(SDL_SetColors(surface, &changed, 0, 1));
            assert(sdl_gpu_repaint(surface));
            gpu_renderer_test::compare(surface, expected, stride, width, height, bpp, scale);
            // Empty and clipped-out uploads preserve the retained frame.
            assert(sdl_gpu_present(surface, source.data(), stride, width, height, bpp, scale, -8, -4, -1, -1));
            gpu_renderer_test::compare(surface, expected, stride, width, height, bpp, scale);
        }
    sdl_gpu_reset();
    assert(!sdl_gpu_repaint(SDL_GetVideoSurface()));
    SDL_Surface* surface = mode(width, height, 100);
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 100, 0, 0, width, height));
    surface = mode(width, height, 200);
    _putenv("ROMAW_GPU=0");
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 200, 0, 0, width, height));
    _putenv("ROMAW_GPU=");
    assert(!sdl_gpu_present(surface, source.data(), 1, width, height, 8, 200, 0, 0, width, height));
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 3, 200, 0, 0, width, height));
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 150, 0, 0, width, height));
    unavailable = true;
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 200, 0, 0, width, height));
    assert(!renderer);
    sdl_gpu_reset();
    assert(!unavailable);
    surface->flags |= SDL_NOFRAME;
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 200, 0, 0, width, height));
    surface->flags &= ~SDL_NOFRAME;
    surface->flags |= SDL_FULLSCREEN;
    assert(!sdl_gpu_present(surface, source.data(), stride, width, height, 8, 200, 0, 0, width, height));
    surface->flags &= ~SDL_FULLSCREEN;
    fprintf(
        stderr,
        "GPU readback matches CPU pixels: depths, fractional scales, dirty bytes, palette changes and reset passed.\n");
}

static void benchmark(void)
{
    const int width = 640, height = 480, frames = 120;
    std::vector<unsigned char> source(width * height), output(width * height * 16);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<unsigned char>(i);
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    for (int scale : {100, 200, 400}) {
        SDL_Surface* surface = mode(width, height, scale);
        int w = surface->w, h = surface->h;
        QueryPerformanceCounter(&start);
        for (int frame = 0; frame < frames; ++frame)
            for (int y = 0; y < h; ++y) {
                const auto* row = source.data() + (y * 100 / scale) * width;
                if (scale == 100)
                    ROMlib_sdl_copy_cropped_row(output.data() + y * w, row, 0, width, 8);
                else
                    ROMlib_sdl_copy_scaled_row(output.data() + y * w, row, 0, w, 0, 8, scale);
            }
        QueryPerformanceCounter(&end);
        double cpu = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart / frames;
        if (scale == 100) {
            fprintf(stderr, "%d%% CPU conversion %.3f ms/frame (GPU bypassed)\n", scale, cpu);
            continue;
        }
        // Warm shaders/device creation before measuring steady-state upload and presentation.
        assert(sdl_gpu_present(surface, source.data(), width, width, height, 8, scale, 0, 0, width, height));
        QueryPerformanceCounter(&start);
        for (int frame = 0; frame < frames; ++frame)
            assert(sdl_gpu_present(surface, source.data(), width, width, height, 8, scale, 0, 0, width, height));
        QueryPerformanceCounter(&end);
        double gpu = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart / frames;
        fprintf(stderr, "%d%% CPU conversion %.3f ms/frame; GPU upload + presentation %.3f ms/frame\n", scale, cpu,
                gpu);
    }
}

int main(int argc, char** argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    pixels();
    if (argc > 1 && !std::strcmp(argv[1], "--benchmark"))
        benchmark();
    sdl_gpu_reset();
    SDL_Quit();
    return 0;
}
