#pragma once
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <memory>
#include <string>

struct SDL_Window;

// Thin seam between the UI and a graphics API. Only an OpenGL 3.3 implementation
// exists; an SDL_GPU / Metal / Vulkan one can be added without touching the UI code.
class UiBackend {
public:
    virtual ~UiBackend() = default;

    // Creates the window and graphics context. SDL must already be initialised (video).
    virtual bool init(const char* title, int width, int height, std::string& err) = 0;
    virtual void shutdown() = 0;

    virtual SDL_Window* window() = 0;
    // "<graphics card> | <driver / API version>" as the graphics API reports them (for support details).
    virtual std::string gpuInfo() const = 0;
    virtual Rml::SystemInterface* systemInterface() = 0;
    virtual Rml::RenderInterface* renderInterface() = 0;

    virtual void setViewport(int width, int height) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;  // flush UI draw calls; the frame can be captured after this
    virtual bool captureFrame(const std::string& pngPath) = 0;
    virtual void present() = 0;

    static std::unique_ptr<UiBackend> createGL3();
};
