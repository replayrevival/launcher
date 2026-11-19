// ste_overlay.dll: the overlay host (ste_overlay_host.h) behind the plain C interface of include/ste_overlay_c.h.
// A game written in C loads it at run time (LoadLibrary), so the game program needs no C++ runtime of its own.
#include <windows.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ste_overlay_c.h"
#include "ste_overlay_host.h"

struct ste_overlay_handle {
    std::unique_ptr<ste_overlay::Host> host;
};

namespace {

void copyErr(char* err, size_t cap, const std::string& s) {
    if (!err || !cap) return;
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(err, s.data(), n);
    err[n] = 0;
}

ste_overlay_handle* api_create(const ste_overlay_init* in, char* err, size_t errcap) {
    if (!in || in->struct_size != sizeof(ste_overlay_init)) { copyErr(err, errcap, "overlay: wrong init structure size"); return nullptr; }
    if (!in->device || !in->resource_dir || !in->data_dir) { copyErr(err, errcap, "overlay: device, resource_dir and data_dir are required"); return nullptr; }
    ste_overlay::InitInfo info;
    info.resource_dir = in->resource_dir;
    info.data_dir = in->data_dir;
    info.device = static_cast<ID3D11Device*>(in->device);
    info.width = in->width;
    info.height = in->height;
    info.content_scale = in->content_scale;
    const ste_overlay_callbacks cb = in->callbacks;  // copied: the caller's struct need not outlive this call
    if (cb.apply_setting)
        info.callbacks.apply_setting = [cb](const std::string& k, const std::string& v) {
            switch (cb.apply_setting(cb.user, k.c_str(), v.c_str())) {
                case STE_APPLIED_NOW: return ste_overlay::Applied::Now;
                case STE_APPLIED_NEXT_MATCH: return ste_overlay::Applied::NextMatch;
                case STE_APPLIED_REFUSED: return ste_overlay::Applied::Refused;
                default: return ste_overlay::Applied::AfterRestart;
            }
        };
    if (cb.on_show) info.callbacks.on_show = [cb] { cb.on_show(cb.user); };
    if (cb.on_hide) info.callbacks.on_hide = [cb] { cb.on_hide(cb.user); };
    if (cb.return_to_launcher) info.callbacks.return_to_launcher = [cb] { cb.return_to_launcher(cb.user); };
    if (cb.quit_game) info.callbacks.quit_game = [cb] { cb.quit_game(cb.user); };
    if (cb.rumble) info.callbacks.rumble = [cb](unsigned id, float s, unsigned ms) { return cb.rumble(cb.user, id, s, ms) != 0; };
    try {
        std::string e;
        auto host = ste_overlay::Host::create(info, e);
        if (!host) { copyErr(err, errcap, e.empty() ? "overlay: start-up failed" : e); return nullptr; }
        auto* o = new ste_overlay_handle;
        o->host = std::move(host);
        return o;
    } catch (const std::exception& ex) {
        copyErr(err, errcap, std::string("overlay: start-up failed: ") + ex.what());
    } catch (...) {
        copyErr(err, errcap, "overlay: start-up failed (unknown error)");
    }
    return nullptr;
}

void api_destroy(ste_overlay_handle* o) { delete o; }
void api_show(ste_overlay_handle* o) { if (o) o->host->show(); }
void api_hide(ste_overlay_handle* o) { if (o) o->host->hide(); }
int api_visible(const ste_overlay_handle* o) { return o && o->host->visible(); }

int api_feed(ste_overlay_handle* o, const ste_overlay_input* in) {
    if (!o || !in) return 0;
    ste_overlay::Input i;
    switch (in->type) {
        case STE_INPUT_NAV:
            i.type = ste_overlay::Input::Type::Nav;
            i.nav = static_cast<ste_overlay::Nav>(in->nav < 0 || in->nav > STE_NAV_NEXT_TAB ? 0 : in->nav);
            break;
        case STE_INPUT_NAV_RELEASE:
            i.type = ste_overlay::Input::Type::NavRelease;
            i.nav = static_cast<ste_overlay::Nav>(in->nav < 0 || in->nav > STE_NAV_NEXT_TAB ? 0 : in->nav);
            break;
        case STE_INPUT_MOUSE_MOVE: i.type = ste_overlay::Input::Type::MouseMove; i.x = in->x; i.y = in->y; break;
        case STE_INPUT_MOUSE_BUTTON: i.type = ste_overlay::Input::Type::MouseButton; i.x = in->x; i.y = in->y; i.button = in->button; i.down = in->down != 0; break;
        case STE_INPUT_MOUSE_WHEEL: i.type = ste_overlay::Input::Type::MouseWheel; i.wheel = in->wheel; break;
        case STE_INPUT_SOURCE: i.type = ste_overlay::Input::Type::Source; i.source = in->source ? in->source : ""; break;
        default: return 0;
    }
    return o->host->feed(i) ? 1 : 0;
}

ste_overlay::DeviceKind kindOf(int k) {
    switch (k) {
        case STE_DEVICE_KEYBOARD: return ste_overlay::DeviceKind::Keyboard;
        case STE_DEVICE_XBOX: return ste_overlay::DeviceKind::Xbox;
        case STE_DEVICE_PLAYSTATION: return ste_overlay::DeviceKind::PlayStation;
        case STE_DEVICE_SWITCH: return ste_overlay::DeviceKind::Switch;
        default: return ste_overlay::DeviceKind::Generic;
    }
}

void api_set_input_device(ste_overlay_handle* o, int kind) { if (o) o->host->set_input_device(kindOf(kind)); }

void api_set_pads(ste_overlay_handle* o, const ste_overlay_pad* pads, int count) {
    if (!o) return;
    std::vector<ste_overlay::Pad> v;
    for (int i = 0; pads && i < count; ++i) {
        ste_overlay::Pad p;
        p.guid = pads[i].guid ? pads[i].guid : "";
        p.name = pads[i].name ? pads[i].name : "";
        p.kind = kindOf(pads[i].kind);
        p.id = pads[i].id;
        v.push_back(std::move(p));
    }
    o->host->set_pads(v);
}

int api_listening(const ste_overlay_handle* o) { return o && o->host->listening(); }
void api_update(ste_overlay_handle* o, double s) { if (o) o->host->update(s); }
void api_render(ste_overlay_handle* o, void* rtv) { if (o) o->host->render(static_cast<ID3D11RenderTargetView*>(rtv)); }
void api_resize(ste_overlay_handle* o, int w, int h, float scale) { if (o) o->host->resize(w, h, scale); }
const char* api_build_id() { return "ste_overlay.dll " __DATE__; }

}  // namespace

extern "C" {

__declspec(dllexport) int ste_overlay_abi(void) { return STE_OVERLAY_ABI; }

__declspec(dllexport) int ste_overlay_fill_api(ste_overlay_api* out, size_t out_size) {
    if (!out || out_size != sizeof(ste_overlay_api)) return 0;
    out->abi = STE_OVERLAY_ABI;
    out->create = api_create;
    out->destroy = api_destroy;
    out->show = api_show;
    out->hide = api_hide;
    out->visible = api_visible;
    out->feed = api_feed;
    out->set_input_device = api_set_input_device;
    out->set_pads = api_set_pads;
    out->listening = api_listening;
    out->update = api_update;
    out->render = api_render;
    out->resize = api_resize;
    out->build_id = api_build_id;
    return 1;
}

}  // extern "C"
