// Overlay test host: a Win32 + Direct3D 11 window that draws a test picture (tests/overlay/test_scene.png, our own art) and the
// in-game overlay on top of it through include/ste_overlay_host.h, exactly as the game will. Lets the D3D11 path be tested without
// the game. Developer tool, not shipped.
//
//   overlay_test_host [--data <dir>] [--size WxH] [--device keyboard|xbox|playstation|switch|generic]
//                     [--script f1,up,down,left,right,confirm,back,wait] [--shot out.png]
// Keys: F1 opens / closes the overlay, arrows + Enter + Esc navigate, F2 cycles the hint device, mouse works.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "miniz.h"
#include "platform/png_decode.h"
#include "ste_overlay_host.h"

using Microsoft::WRL::ComPtr;

namespace {

ste_overlay::Host* g_host = nullptr;
bool g_quit = false;
int g_w = 1280, g_h = 720;
bool g_resize = false;
ste_overlay::DeviceKind g_device = ste_overlay::DeviceKind::Keyboard;

void feedNav(ste_overlay::Nav n) {
    if (!g_host || !g_host->visible()) return;
    ste_overlay::Input in;
    in.type = ste_overlay::Input::Type::Nav;
    in.nav = n;
    g_host->feed(in);
}

void toggle() {
    if (!g_host) return;
    if (g_host->visible()) g_host->hide(); else g_host->show();
}

void cycleDevice() {
    using ste_overlay::DeviceKind;
    g_device = g_device == DeviceKind::Keyboard ? DeviceKind::Xbox : g_device == DeviceKind::Xbox ? DeviceKind::PlayStation : DeviceKind::Keyboard;
    if (g_host) g_host->set_input_device(g_device);
}

void mouse(ste_overlay::Input::Type t, LPARAM lp, int button = 0, bool down = false, float wheel = 0) {
    if (!g_host || !g_host->visible()) return;
    ste_overlay::Input in;
    in.type = t;
    in.x = float(GET_X_LPARAM(lp));
    in.y = float(GET_Y_LPARAM(lp));
    in.button = button;
    in.down = down;
    in.wheel = wheel;
    g_host->feed(in);
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    using ste_overlay::Nav;
    switch (m) {
        case WM_CLOSE: g_quit = true; return 0;
        case WM_SIZE:
            if (w != SIZE_MINIMIZED) { g_w = LOWORD(l); g_h = HIWORD(l); g_resize = true; }
            return 0;
        case WM_KEYDOWN:
            switch (w) {
                case VK_F1: if (!(l & (1 << 30))) toggle(); return 0;
                case VK_F2: if (!(l & (1 << 30))) cycleDevice(); return 0;
                case VK_UP: feedNav(Nav::Up); return 0;
                case VK_DOWN: feedNav(Nav::Down); return 0;
                case VK_LEFT: feedNav(Nav::Left); return 0;
                case VK_RIGHT: feedNav(Nav::Right); return 0;
                case VK_RETURN: feedNav(Nav::Confirm); return 0;
                case VK_ESCAPE: if (g_host && g_host->visible()) feedNav(Nav::Back); else g_quit = true; return 0;
            }
            break;
        case WM_MOUSEMOVE: mouse(ste_overlay::Input::Type::MouseMove, l); return 0;
        case WM_LBUTTONDOWN: SetCapture(h); mouse(ste_overlay::Input::Type::MouseButton, l, 0, true); return 0;
        case WM_LBUTTONUP: ReleaseCapture(); mouse(ste_overlay::Input::Type::MouseButton, l, 0, false); return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            mouse(ste_overlay::Input::Type::MouseWheel, MAKELPARAM(p.x, p.y), 0, false, float(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

const char* kPictureShader = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET { return tex.Sample(smp, i.uv); }
)";

std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s = buf;
    return s.substr(0, s.find_last_of("\\/"));
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir = exeDir() + "\\data_overlay_test", shot, device;
    std::vector<std::string> script;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &g_w, &g_h);
        else if (a == "--device" && i + 1 < argc) device = argv[++i];
        else if (a == "--script" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string t;
            while (std::getline(ss, t, ',')) script.push_back(t);
        }
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OverlayTestHost";
    RegisterClassW(&wc);
    RECT r{0, 0, g_w, g_h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Overlay test host (F1 = overlay, F2 = hint device)", WS_OVERLAPPEDWINDOW, 100, 100,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    GetClientRect(hwnd, &r);
    g_w = r.right;
    g_h = r.bottom;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = g_w;
    sd.BufferDesc.Height = g_h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lv, 3, D3D11_SDK_VERSION, &sd, &swap, &dev, nullptr, &ctx))) {
        std::fprintf(stderr, "Could not create a Direct3D 11 device\n");
        return 1;
    }

    // The test picture and the shader that draws it.
    int pw = 0, ph = 0;
    std::vector<uint8_t> pic;
    std::string err;
    if (!loadPngPremultiplied(std::string(OVERLAY_TEST_SCENE), pw, ph, pic, err)) {
        std::fprintf(stderr, "Test picture: %s\n", err.c_str());
        return 1;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = pw; td.Height = ph; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr{pic.data(), UINT(pw * 4), 0};
    ComPtr<ID3D11Texture2D> picTex;
    ComPtr<ID3D11ShaderResourceView> picSrv;
    dev->CreateTexture2D(&td, &sr, &picTex);
    dev->CreateShaderResourceView(picTex.Get(), nullptr, &picSrv);
    ComPtr<ID3DBlob> vsb, psb, eb;
    D3DCompile(kPictureShader, std::strlen(kPictureShader), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsb, &eb);
    D3DCompile(kPictureShader, std::strlen(kPictureShader), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psb, &eb);
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    D3D11_SAMPLER_DESC smd{};
    smd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ComPtr<ID3D11SamplerState> sampler;
    dev->CreateSamplerState(&smd, &sampler);

    ComPtr<ID3D11RenderTargetView> rtv;
    auto makeTarget = [&] {
        ComPtr<ID3D11Texture2D> bb;
        swap->GetBuffer(0, IID_PPV_ARGS(&bb));
        dev->CreateRenderTargetView(bb.Get(), nullptr, &rtv);
    };
    makeTarget();

    ste_overlay::InitInfo info;
    info.resource_dir = exeDir();
    info.data_dir = dataDir;
    info.device = dev.Get();
    info.width = g_w;
    info.height = g_h;
    info.callbacks.apply_setting = [](const std::string& k, const std::string& v) {
        std::fprintf(stderr, "HOST apply_setting %s=%s\n", k.c_str(), v.c_str());
        return ste_overlay::Applied::Now;
    };
    info.callbacks.on_show = [] { std::fprintf(stderr, "HOST on_show\n"); };
    info.callbacks.on_hide = [] { std::fprintf(stderr, "HOST on_hide\n"); };
    info.callbacks.return_to_launcher = [] { std::fprintf(stderr, "HOST return_to_launcher\n"); g_quit = true; };
    info.callbacks.quit_game = [] { std::fprintf(stderr, "HOST quit_game\n"); g_quit = true; };
    auto host = ste_overlay::Host::create(info, err);
    if (!host) {
        std::fprintf(stderr, "Overlay: %s\n", err.c_str());
        return 1;
    }
    g_host = host.get();
    if (device == "xbox") g_device = ste_overlay::DeviceKind::Xbox;
    else if (device == "playstation") g_device = ste_overlay::DeviceKind::PlayStation;
    else if (device == "switch") g_device = ste_overlay::DeviceKind::Switch;
    else if (device == "generic") g_device = ste_overlay::DeviceKind::Generic;
    g_host->set_input_device(g_device);

    size_t pos = 0;
    int frame = 0, next = 10, waitFrames = 0;
    bool shotDone = false;
    auto last = std::chrono::steady_clock::now();
    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_resize) {
            g_resize = false;
            rtv.Reset();
            swap->ResizeBuffers(0, g_w, g_h, DXGI_FORMAT_UNKNOWN, 0);
            makeTarget();
            g_host->resize(g_w, g_h, 1.f);
        }
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        if (pos < script.size() && frame >= next) {
            const std::string t = script[pos++];
            using ste_overlay::Nav;
            if (t == "f1") toggle();
            else if (t == "f2") cycleDevice();
            else if (t == "up") feedNav(Nav::Up);
            else if (t == "down") feedNav(Nav::Down);
            else if (t == "left") feedNav(Nav::Left);
            else if (t == "right") feedNav(Nav::Right);
            else if (t == "confirm") feedNav(Nav::Confirm);
            else if (t == "back") feedNav(Nav::Back);
            next = frame + (t == "wait" ? 30 : 6);
        }

        g_host->update(dt);

        // The "game frame": the test picture.
        D3D11_VIEWPORT vp{0, 0, float(g_w), float(g_h), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, picSrv.GetAddressOf());
        ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
        ctx->Draw(3, 0);

        g_host->render(rtv.Get());

        if (!shot.empty() && !shotDone && pos >= script.size() && frame >= next + 20) {
            ComPtr<ID3D11Texture2D> bb, stage;
            swap->GetBuffer(0, IID_PPV_ARGS(&bb));
            D3D11_TEXTURE2D_DESC d;
            bb->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
            dev->CreateTexture2D(&d, nullptr, &stage);
            ctx->CopyResource(stage.Get(), bb.Get());
            D3D11_MAPPED_SUBRESOURCE map;
            if (SUCCEEDED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map))) {
                std::vector<uint8_t> out(size_t(d.Width) * d.Height * 4);
                for (UINT y = 0; y < d.Height; ++y) std::memcpy(&out[size_t(y) * d.Width * 4], static_cast<uint8_t*>(map.pData) + size_t(y) * map.RowPitch, size_t(d.Width) * 4);
                ctx->Unmap(stage.Get(), 0);
                size_t len = 0;
                void* png = tdefl_write_image_to_png_file_in_memory(out.data(), int(d.Width), int(d.Height), 4, &len);
                if (png) {
                    FILE* f = std::fopen(shot.c_str(), "wb");
                    if (f) { std::fwrite(png, 1, len, f); std::fclose(f); std::fprintf(stderr, "Saved %s\n", shot.c_str()); }
                    mz_free(png);
                }
            }
            shotDone = true;
            g_quit = true;
        }
        swap->Present(1, 0);
        ++frame;
        (void)waitFrames;
    }
    g_host = nullptr;
    host.reset();
    return 0;
}
