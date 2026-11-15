/* Plain C program (no C++ runtime) that loads ste_overlay.dll the way a C game does and runs a few frames:
 * create, show, render, feed input, hide, destroy. Usage: overlay_dll_test <dll path> <resource dir> <data dir>
 * Exit code 0 = ok. Not shipped. */
#define COBJMACROS
#define STE_OVERLAY_WANT_LOADER
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <wchar.h>
#include "ste_overlay_c.h"

static int shown, hidden, applied, returned;
static void on_show(void* u) { (void)u; ++shown; }
static void on_hide(void* u) { (void)u; ++hidden; }
static int on_apply(void* u, const char* k, const char* v) { (void)u; ++applied; printf("apply %s=%s\n", k, v); return STE_APPLIED_NOW; }
static void on_return(void* u) { (void)u; ++returned; }

int main(int argc, char** argv) {
    wchar_t dll[MAX_PATH];
    ste_overlay_api api;
    ste_overlay_init info;
    ste_overlay_handle* ov;
    char err[512];
    ID3D11Device* dev = NULL;
    ID3D11DeviceContext* ctx = NULL;
    ID3D11Texture2D* tex = NULL;
    ID3D11RenderTargetView* rtv = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D_FEATURE_LEVEL fl;
    int i, fails = 0;
    ste_overlay_input in;

    if (argc < 4) { printf("usage: overlay_dll_test <dll> <resource dir> <data dir>\n"); return 2; }
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, dll, MAX_PATH);
    ZeroMemory(&api, sizeof api);
    if (!ste_overlay_load(&api, dll, err, sizeof err)) { printf("load failed: %s\n", err); return 1; }
    printf("loaded: abi %d, %s\n", api.abi, api.build_id());

    /* a missing device must be a plain error, not a crash */
    ZeroMemory(&info, sizeof info);
    info.struct_size = sizeof info;
    info.resource_dir = argv[2];
    info.data_dir = argv[3];
    err[0] = 0;
    if (api.create(&info, err, sizeof err)) { printf("FAIL: create without a device succeeded\n"); ++fails; }
    else printf("create without device: \"%s\"\n", err);

    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) { printf("no D3D11 device\n"); return 1; }
    ZeroMemory(&td, sizeof td);
    td.Width = 1280; td.Height = 720; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &tex)) || FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource*)tex, NULL, &rtv))) { printf("no render target\n"); return 1; }

    info.device = dev;
    info.width = 1280; info.height = 720; info.content_scale = 1.f;
    info.callbacks.on_show = on_show; info.callbacks.on_hide = on_hide; info.callbacks.apply_setting = on_apply; info.callbacks.return_to_launcher = on_return;
    ov = api.create(&info, err, sizeof err);
    if (!ov) { printf("FAIL create: %s\n", err); return 1; }
    api.set_input_device(ov, STE_DEVICE_XBOX);
    api.set_pads(ov, NULL, 0);
    api.show(ov);
    if (!api.visible(ov)) { printf("FAIL not visible after show\n"); ++fails; }
    for (i = 0; i < 20; ++i) { api.update(ov, 1.0 / 60); api.render(ov, rtv); }
    ZeroMemory(&in, sizeof in);
    in.type = STE_INPUT_NAV; in.nav = STE_NAV_DOWN;
    printf("feed(down) used: %d\n", api.feed(ov, &in));
    in.nav = STE_NAV_CONFIRM;
    api.feed(ov, &in);
    for (i = 0; i < 10; ++i) { api.update(ov, 1.0 / 60); api.render(ov, rtv); }
    api.resize(ov, 1920, 1080, 1.f);
    api.hide(ov);
    if (api.visible(ov)) { printf("FAIL still visible after hide\n"); ++fails; }
    api.destroy(ov);
    printf("callbacks: show %d hide %d apply %d return %d\n", shown, hidden, applied, returned);
    if (shown < 1 || hidden < 1) ++fails;
    ID3D11RenderTargetView_Release(rtv); ID3D11Texture2D_Release(tex); ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev);
    printf(fails ? "FAILED\n" : "all passed\n");
    return fails ? 1 : 0;
}
