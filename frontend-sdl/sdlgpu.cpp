/*+===================================================================
  File:      sdlgpu.cpp

  Summary:   Present packed Macintosh pixels with Direct3D palette lookup and exact integer scaling.

  Classes:   gpu_ref, gpu_library, gpu_renderer.

  Functions: sdl_gpu_present, sdl_gpu_repaint, sdl_gpu_reset

  Origin:    Executor SDL frontend; uses the Windows Direct3D 11 API.

  Copyright and Legal notices: Applicable repository license: COPYING.
===================================================================+*/
#include "sdlgpu.h"
#include "rsys/displayscale.h"

#if defined(_WIN32)
#include "SDL/SDL_syswm.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

// The DLLs are optional. Missing runtime components leave SDL software presentation available.
struct gpu_library {
    HMODULE module = nullptr;

    ~gpu_library()
    {
        if (module)
            FreeLibrary(module);
    }
};

template <class T> class gpu_ref {
    T* value = nullptr;

  public:
    gpu_ref() = default;
    gpu_ref(const gpu_ref&) = delete;
    gpu_ref& operator=(const gpu_ref&) = delete;

    ~gpu_ref()
    {
        if (value)
            value->Release();
    }

    T* get() const
    {
        return value;
    }

    T* operator->() const
    {
        return value;
    }

    T** put()
    {
        return &value;
    }
};

// Match ROMlib_scale_guest exactly, including fractional scales. Load avoids
// filtering palette indices and preserves the guest's packed pixel order.
static const char gpu_shader[] = R"(
Texture2D<uint> pixels : register(t0);
Texture2D<float4> palette : register(t1);
cbuffer Display : register(b0) { uint scale; uint bpp; uint unused0; uint unused1; };
float4 vertex(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 pixel(float4 p : SV_Position) : SV_Target {
    uint2 guest = uint2(p.xy) * 100 / scale;
    uint bit = guest.x * bpp;
    uint packed = pixels.Load(int3(bit >> 3, guest.y, 0));
    uint index = (packed >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
    return palette.Load(int3(index, 0, 0));
}
)";

class gpu_renderer {
    friend struct gpu_renderer_test;
    gpu_library runtime, compiler;
    gpu_ref<ID3D11Device> device;
    gpu_ref<ID3D11DeviceContext> context;
    gpu_ref<IDXGISwapChain> swap;
    gpu_ref<ID3D11RenderTargetView> target;
    gpu_ref<ID3D11VertexShader> vertex;
    gpu_ref<ID3D11PixelShader> pixel;
    gpu_ref<ID3D11Texture2D> pixels, palette;
    gpu_ref<ID3D11ShaderResourceView> pixel_view, palette_view;
    gpu_ref<ID3D11Buffer> constants;

  public:
    int width = 0, height = 0, bpp = 0, scale = 0;
    HWND window = nullptr;
    bool initialized = false;

    bool initialize(SDL_Surface* surface, int w, int h, int depth, int percent)
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWMInfo(&info) <= 0 || !info.window)
            return false;
        window = info.window;
        width = w;
        height = h;
        bpp = depth;
        scale = percent;
        // Resolve system DLLs only, never a DLL in a guest application's directory.
        runtime.module = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        compiler.module = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!runtime.module || !compiler.module)
            return false;
        decltype(&D3D11CreateDeviceAndSwapChain) create = nullptr;
        decltype(&D3DCompile) compile = nullptr;
        FARPROC address = GetProcAddress(runtime.module, "D3D11CreateDeviceAndSwapChain");
        static_assert(sizeof address == sizeof create && sizeof address == sizeof compile);
        std::memcpy(&create, &address, sizeof create);
        address = GetProcAddress(compiler.module, "D3DCompile");
        std::memcpy(&compile, &address, sizeof compile);
        if (!create || !compile)
            return false;
        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferDesc.Width = surface->w;
        desc.BufferDesc.Height = surface->h;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        if (FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &desc,
                          swap.put(), device.put(), nullptr, context.put())))
            return false;
        gpu_ref<IDXGIFactory> factory;
        if (SUCCEEDED(swap->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(factory.put()))))
            factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        gpu_ref<ID3D11Texture2D> buffer;
        if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(buffer.put()))) ||
            FAILED(device->CreateRenderTargetView(buffer.get(), nullptr, target.put())))
            return false;
        gpu_ref<ID3DBlob> vs, ps, errors;
        if (FAILED(compile(gpu_shader, sizeof gpu_shader - 1, "ROMAW display", nullptr, nullptr, "vertex", "vs_4_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vs.put(), errors.put())))
            return false;
        gpu_ref<ID3DBlob> pixel_errors;
        if (FAILED(compile(gpu_shader, sizeof gpu_shader - 1, "ROMAW display", nullptr, nullptr, "pixel", "ps_4_0",
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ps.put(), pixel_errors.put())) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vertex.put())) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, pixel.put())))
            return false;
        D3D11_TEXTURE2D_DESC texture = {};
        texture.Width = (w * depth + 7) / 8;
        texture.Height = h;
        texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R8_UINT;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&texture, nullptr, pixels.put())) ||
            FAILED(device->CreateShaderResourceView(pixels.get(), nullptr, pixel_view.put())))
            return false;
        texture.Width = 256;
        texture.Height = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (FAILED(device->CreateTexture2D(&texture, nullptr, palette.put())) ||
            FAILED(device->CreateShaderResourceView(palette.get(), nullptr, palette_view.put())))
            return false;
        unsigned values[] = {static_cast<unsigned>(percent), static_cast<unsigned>(depth), 0, 0};
        D3D11_BUFFER_DESC cb = {};
        cb.ByteWidth = sizeof values;
        cb.Usage = D3D11_USAGE_IMMUTABLE;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {values, 0, 0};
        return SUCCEEDED(device->CreateBuffer(&cb, &initial, constants.put()));
    }

    bool draw(SDL_Surface* surface, bool present = true)
    {
        unsigned char colors[256][4] = {};
        for (int i = 0; i < std::min(256, surface->format->palette->ncolors); ++i) {
            const SDL_Color& color = surface->format->palette->colors[i];
            colors[i][0] = color.r;
            colors[i][1] = color.g;
            colors[i][2] = color.b;
            colors[i][3] = 255;
        }
        context->UpdateSubresource(palette.get(), 0, nullptr, colors, sizeof colors, 0);
        ID3D11RenderTargetView* output = target.get();
        context->OMSetRenderTargets(1, &output, nullptr);
        D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(surface->w), static_cast<float>(surface->h), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.get(), nullptr, 0);
        context->PSSetShader(pixel.get(), nullptr, 0);
        ID3D11ShaderResourceView* views[] = {pixel_view.get(), palette_view.get()};
        context->PSSetShaderResources(0, 2, views);
        ID3D11Buffer* cb = constants.get();
        context->PSSetConstantBuffers(0, 1, &cb);
        context->Draw(3, 0);
        // Keep guest execution independent of the monitor refresh rate.
        return (!present || SUCCEEDED(swap->Present(0, 0))) && SUCCEEDED(device->GetDeviceRemovedReason());
    }

    bool update(SDL_Surface* surface, const unsigned char* source, int stride, int left, int top, int right, int bottom)
    {
        if (!initialized) {
            left = top = 0;
            right = width;
            bottom = height;
        }
        left = std::clamp(left, 0, width);
        right = std::clamp(right, 0, width);
        top = std::clamp(top, 0, height);
        bottom = std::clamp(bottom, 0, height);
        if (right > left && bottom > top) {
            D3D11_BOX box = {static_cast<UINT>(left * bpp / 8),        static_cast<UINT>(top),    0,
                             static_cast<UINT>((right * bpp + 7) / 8), static_cast<UINT>(bottom), 1};
            context->UpdateSubresource(pixels.get(), 0, &box, source + top * stride + box.left, stride, 0);
        }
        initialized = true;
        return draw(surface);
    }
};

static std::unique_ptr<gpu_renderer> renderer;
static bool unavailable;

void sdl_gpu_reset(void)
{
    renderer.reset();
    unavailable = false;
}

int sdl_gpu_repaint(SDL_Surface* surface)
{
    if (!renderer)
        return 0;
    if (renderer->draw(surface))
        return 1;
    renderer.reset();
    unavailable = true;
    return 0;
}

int sdl_gpu_present(SDL_Surface* surface, const unsigned char* source, int stride, int width, int height, int bpp,
                    int scale, int left, int top, int right, int bottom)
{
    const char* setting = std::getenv("ROMAW_GPU");
    if (unavailable || (setting && !std::strcmp(setting, "0")) || !surface || !source || !surface->format->palette ||
        width <= 0 || width > 32767 || height <= 0 || height > 32767 || scale <= 100 || scale > 1600 ||
        surface->w != ROMlib_scale_edge(width, scale) || surface->h != ROMlib_scale_edge(height, scale) ||
        (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || stride < (width * bpp + 7) / 8 ||
        (surface->flags & (SDL_NOFRAME | SDL_FULLSCREEN)))
        return 0;
    if (!renderer) {
        renderer.reset(new (std::nothrow) gpu_renderer);
        if (!renderer || !renderer->initialize(surface, width, height, bpp, scale)) {
            renderer.reset();
            unavailable = true;
            std::fprintf(stderr, "Display: GPU unavailable; using SDL software presentation.\n");
            return 0;
        }
        std::fprintf(stderr, "Display: Direct3D 11 palette conversion and scaling enabled (%d%%).\n", scale);
    }
    if (renderer->update(surface, source, stride, left, top, right, bottom))
        return 1;
    renderer.reset();
    unavailable = true;
    std::fprintf(stderr, "Display: GPU presentation failed; using SDL software presentation.\n");
    return 0;
}
#endif
