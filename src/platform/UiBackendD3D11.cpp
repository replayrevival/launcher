#include "platform/UiBackendD3D11.h"

#include <RmlUi/Core.h>

#include <chrono>
#include <cstdio>

#include "RmlUi_Renderer_DX11.h"
#include "platform/png_decode.h"

namespace {

// RmlUi's DX11 renderer reads TGA only; the button pictures are PNG, so decode those here (see platform/png_decode).
class Renderer final : public RenderInterface_DX11 {
public:
    explicit Renderer(ID3D11Device* d) : RenderInterface_DX11(d) {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& source) override {
        if (source.size() > 4 && source.compare(source.size() - 4, 4, ".png") == 0) {
            int w = 0, h = 0;
            std::vector<uint8_t> px;
            std::string err;
            if (!loadPngPremultiplied(source, w, h, px, err)) {
                Rml::Log::Message(Rml::Log::LT_WARNING, "Picture %s: %s", source.c_str(), err.c_str());
                return {};
            }
            dims = {w, h};
            return GenerateTexture(Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(px.data()), px.size()), dims);
        }
        return RenderInterface_DX11::LoadTexture(dims, source);
    }
};

// Minimal system interface: a clock and the log. No window, no clipboard.
class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        using namespace std::chrono;
        return duration<double>(steady_clock::now() - start_).count();
    }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type <= Rml::Log::LT_WARNING) std::fprintf(stderr, "[RmlUi] %s\n", message.c_str());
        return true;
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace

struct UiBackendD3D11::Impl {
    System system;
    std::unique_ptr<Renderer> renderer;
    ID3D11DeviceContext* context = nullptr;      // immediate context (referenced)
    ID3D11RenderTargetView* savedRtv = nullptr;  // the host's render target while we draw (RmlUi does not restore it)
    ID3D11DepthStencilView* savedDsv = nullptr;
};

UiBackendD3D11::UiBackendD3D11() : impl_(new Impl) {}
UiBackendD3D11::~UiBackendD3D11() { shutdown(); }

bool UiBackendD3D11::init(ID3D11Device* device, std::string& err) {
    if (!device) { err = "no Direct3D device"; return false; }
    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0) { err = "the Direct3D device is older than feature level 10"; return false; }
    ID3D11Device1* d1 = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&d1))) || !d1) {
        err = "the Direct3D device does not support ID3D11Device1 (Windows 8 or newer is needed)";
        return false;
    }
    d1->Release();
    impl_->renderer = std::make_unique<Renderer>(device);
    device->GetImmediateContext(&impl_->context);
    return true;
}

void UiBackendD3D11::shutdown() {
    impl_->renderer.reset();
    if (impl_->context) { impl_->context->Release(); impl_->context = nullptr; }
}

Rml::SystemInterface* UiBackendD3D11::systemInterface() { return &impl_->system; }
Rml::RenderInterface* UiBackendD3D11::renderInterface() { return impl_->renderer.get(); }
void UiBackendD3D11::setViewport(int w, int h) { if (impl_->renderer) impl_->renderer->SetViewport(w, h); }
void UiBackendD3D11::beginFrame() {
    if (!impl_->renderer) return;
    impl_->context->OMGetRenderTargets(1, &impl_->savedRtv, &impl_->savedDsv);
    impl_->renderer->BeginFrame();
}
void UiBackendD3D11::endFrame(ID3D11RenderTargetView* target) {
    if (!impl_->renderer) return;
    impl_->renderer->EndFrame(target);
    impl_->context->OMSetRenderTargets(1, &impl_->savedRtv, impl_->savedDsv);  // give the host its targets back
    if (impl_->savedRtv) { impl_->savedRtv->Release(); impl_->savedRtv = nullptr; }
    if (impl_->savedDsv) { impl_->savedDsv->Release(); impl_->savedDsv = nullptr; }
}
