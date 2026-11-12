// Implementation of include/ste_overlay_host.h: RmlUi on the game's Direct3D 11 device + the launcher's Overlay.
#include "ste_overlay_host.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <filesystem>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/config.h"
#include "core/identify.h"
#include "core/launcher_model.h"
#include "platform/UiBackendD3D11.h"
#include "ui/ChamferDecorator.h"
#include "ui/Overlay.h"

namespace fs = std::filesystem;

namespace {

// The monitors without SDL (the game owns the window): one entry per monitor with its current and listed modes, in physical pixels.
// The game process should be DPI aware, otherwise Windows reports scaled sizes.
DisplayList detectDisplaysWin32() {
    DisplayList out;
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr,
                        [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL { reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(m); return TRUE; },
                        reinterpret_cast<LPARAM>(&mons));
    for (size_t i = 0; i < mons.size(); ++i) {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoW(mons[i], &mi)) continue;
        DisplayInfo d;
        d.name = "Display " + std::to_string(out.displays.size() + 1);
        DEVMODEW dm{};
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) d.desktop = {int(dm.dmPelsWidth), int(dm.dmPelsHeight), float(dm.dmDisplayFrequency)};
        for (DWORD k = 0; EnumDisplaySettingsW(mi.szDevice, k, &dm); ++k) d.modes.push_back({int(dm.dmPelsWidth), int(dm.dmPelsHeight), float(dm.dmDisplayFrequency)});
        if (d.desktop.w <= 0) { d.desktop.w = mi.rcMonitor.right - mi.rcMonitor.left; d.desktop.h = mi.rcMonitor.bottom - mi.rcMonitor.top; }
        if (mi.dwFlags & MONITORINFOF_PRIMARY) out.windowDisplay = int(out.displays.size());
        out.displays.push_back(std::move(d));
    }
    if (out.displays.empty()) {  // should not happen; keep the pages from indexing an empty list
        DisplayInfo d;
        d.name = "Display 1";
        d.desktop = {GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 60.f};
        out.displays.push_back(d);
    }
    return out;
}

}  // namespace

namespace ste_overlay {

struct Host::Impl {
    std::unique_ptr<LauncherModel> model;
    UiBackendD3D11 backend;
    Overlay overlay;
    Callbacks cb;
    bool rmlStarted = false;
    ~Impl() {
        overlay.shutdown();  // contexts go before the library
        if (rmlStarted) Rml::Shutdown();
        backend.shutdown();
    }
};

Host::Host() : impl_(new Impl) {}
Host::~Host() = default;

std::unique_ptr<Host> Host::create(const InitInfo& info, std::string& err) {
    std::unique_ptr<Host> h(new Host);
    Impl& m = *h->impl_;
    m.cb = info.callbacks;
    const fs::path res = fs::u8path(info.resource_dir), data = fs::u8path(info.data_dir);

    Profile profile;
    if (!loadProfile(pathToUtf8(res / "config" / "profile.json"), profile, err)) return nullptr;
    m.model = std::make_unique<LauncherModel>(profile, LauncherPaths{res, data});
    if (!m.model->load(err)) return nullptr;
    m.model->refreshDisplays(detectDisplaysWin32);

    if (!m.backend.init(info.device, err)) return nullptr;
    m.backend.setViewport(info.width, info.height);
    Rml::SetSystemInterface(m.backend.systemInterface());
    Rml::SetRenderInterface(m.backend.renderInterface());
    if (!Rml::Initialise()) { err = "Could not start the UI library"; return nullptr; }
    m.rmlStarted = true;
    registerDecorators();
    const std::string fontDir = pathToUtf8(res / "assets" / "fonts") + "/";
    Rml::LoadFontFace(fontDir + "PixelifySans-Bold.ttf", false, Rml::Style::FontWeight::Bold);
    for (auto& f : {std::pair<const char*, int>{"ChakraPetch-Regular.ttf", 400}, {"ChakraPetch-Medium.ttf", 500},
                    {"ChakraPetch-SemiBold.ttf", 600}, {"ChakraPetch-Bold.ttf", 700}})
        Rml::LoadFontFace(fontDir + f.first, false, Rml::Style::FontWeight(f.second));

    OverlayInit oi;
    oi.resourceDir = res;
    oi.width = info.width;
    oi.height = info.height;
    oi.contentScale = info.content_scale;
    OverlayCallbacks cb;
    Impl* impl = &m;
    cb.applySetting = [impl](const std::string& k, const std::string& v) {
        if (!impl->cb.apply_setting) return ::Applied::AfterRestart;
        switch (impl->cb.apply_setting(k, v)) {
            case Applied::Now: return ::Applied::Now;
            case Applied::NextMatch: return ::Applied::NextMatch;
            case Applied::Refused: return ::Applied::Refused;
            default: return ::Applied::AfterRestart;
        }
    };
    cb.onShow = [impl] { if (impl->cb.on_show) impl->cb.on_show(); };
    cb.onHide = [impl] { if (impl->cb.on_hide) impl->cb.on_hide(); };
    cb.returnToLauncher = [impl] { if (impl->cb.return_to_launcher) impl->cb.return_to_launcher(); };
    cb.quitGame = [impl] { if (impl->cb.quit_game) impl->cb.quit_game(); };
    if (!m.overlay.init(*m.model, oi, cb, err)) return nullptr;
    return h;
}

void Host::show() { impl_->overlay.show(); }
void Host::hide() { impl_->overlay.hide(); }
bool Host::visible() const { return impl_->overlay.visible(); }

bool Host::feed(const Input& in) {
    if (!impl_->overlay.visible()) return false;
    SDL_Event e{};
    switch (in.type) {
        case Input::Type::Nav: {
            static const NavAction map[] = {NavAction::Up, NavAction::Down, NavAction::Left, NavAction::Right, NavAction::Confirm, NavAction::Back};
            impl_->overlay.navigation().push(map[int(in.nav)]);
            return true;
        }
        case Input::Type::MouseMove:
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = in.x;
            e.motion.y = in.y;
            break;
        case Input::Type::MouseButton:
            e.type = in.down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = in.button == 0 ? SDL_BUTTON_LEFT : in.button == 1 ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE;
            e.button.down = in.down;
            break;
        case Input::Type::MouseWheel:
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.y = in.wheel;
            break;
    }
    return impl_->overlay.feedEvent(e);
}

void Host::set_input_device(DeviceKind k) {
    static const ::DeviceKind map[] = {::DeviceKind::Keyboard, ::DeviceKind::Xbox, ::DeviceKind::PlayStation, ::DeviceKind::Switch, ::DeviceKind::Generic};
    impl_->overlay.navigation().forceDevice(map[int(k)]);
}

void Host::update(double dt) { impl_->overlay.update(dt); }

void Host::render(ID3D11RenderTargetView* rtv) {
    if (!impl_->overlay.visible()) return;
    impl_->backend.beginFrame();
    impl_->overlay.render();
    impl_->backend.endFrame(rtv);
}

void Host::resize(int w, int h, float scale) {
    impl_->backend.setViewport(w, h);
    impl_->overlay.resize(w, h, scale, 1.f);
}

}  // namespace ste_overlay
