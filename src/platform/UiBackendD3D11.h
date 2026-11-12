#pragma once
// RmlUi on a host-owned Direct3D 11 device (docs/OVERLAY.md, "Rendering backend"). Windows only.
// Owns no window and never presents: the host gives it its ID3D11Device and, per frame, the render target to draw on top of.
// RmlUi's own DX11 renderer saves and restores the pipeline state it touches around the draw.
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <memory>
#include <string>

struct ID3D11Device;
struct ID3D11RenderTargetView;

class UiBackendD3D11 {
public:
    UiBackendD3D11();
    ~UiBackendD3D11();

    // Creates the renderer on `device` (the immediate context is taken from it). False with a message when the shaders
    // cannot be compiled or the device is too old (feature level 10 or better is needed).
    bool init(ID3D11Device* device, std::string& err);
    void shutdown();

    // Install these with Rml::SetSystemInterface / SetRenderInterface before Rml::Initialise.
    Rml::SystemInterface* systemInterface();
    Rml::RenderInterface* renderInterface();

    void setViewport(int width, int height);
    void beginFrame();                              // call before Context::Render
    void endFrame(ID3D11RenderTargetView* target);  // call after it: composites the UI onto `target` and restores the pipeline

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
