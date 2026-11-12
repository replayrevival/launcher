#include "platform/UiBackend.h"

#include <RmlUi/Core/Log.h>
#include <SDL3/SDL.h>

#include <vector>

#include "RmlUi_Include_GL3.h"
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"

namespace {

class GL3Backend final : public UiBackend {
public:
    ~GL3Backend() override { shutdown(); }

    bool init(const char* title, int width, int height, std::string& err) override {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        window_ = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!window_) {
            err = std::string("Could not create the window: ") + SDL_GetError();
            return false;
        }

        glContext_ = SDL_GL_CreateContext(window_);
        if (!glContext_) {
            err = std::string("This computer's graphics driver does not support OpenGL 3.3: ") + SDL_GetError();
            return false;
        }
        SDL_GL_MakeCurrent(window_, glContext_);
        SDL_GL_SetSwapInterval(1);

        std::string glMsg;
        if (!RmlGL3::Initialize(&glMsg)) {
            err = "Could not load OpenGL functions: " + glMsg;
            return false;
        }
        glReady_ = true;

        system_ = std::make_unique<SystemInterface_SDL>(window_);
        render_ = std::make_unique<RenderInterface_GL3>();
        if (!*render_) {
            err = "Could not set up the OpenGL renderer";
            return false;
        }
        int pw = width, ph = height;
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        render_->SetViewport(pw, ph);
        return true;
    }

    void shutdown() override {
        render_.reset();
        system_.reset();
        if (glReady_) { RmlGL3::Shutdown(); glReady_ = false; }
        if (glContext_) { SDL_GL_DestroyContext(glContext_); glContext_ = nullptr; }
        if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    }

    SDL_Window* window() override { return window_; }
    Rml::SystemInterface* systemInterface() override { return system_.get(); }
    Rml::RenderInterface* renderInterface() override { return render_.get(); }

    std::string gpuInfo() const override {
        auto get = [](GLenum e) {
            const GLubyte* p = glGetString(e);
            return p ? std::string(reinterpret_cast<const char*>(p)) : std::string("unknown");
        };
        return get(GL_RENDERER) + " | driver " + get(GL_VERSION) + " (" + get(GL_VENDOR) + ")";
    }
    void setViewport(int w, int h) override { render_->SetViewport(w, h); }
    void beginFrame() override { render_->Clear(); render_->BeginFrame(); }
    void endFrame() override { render_->EndFrame(); }

    bool captureFrame(const std::string& pngPath) override {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        std::vector<unsigned char> px(size_t(w) * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        // OpenGL rows run bottom-up.
        std::vector<unsigned char> flipped(px.size());
        for (int y = 0; y < h; ++y)
            memcpy(&flipped[size_t(y) * w * 4], &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
        SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, flipped.data(), w * 4);
        if (!s) return false;
        bool ok = SDL_SavePNG(s, pngPath.c_str());
        SDL_DestroySurface(s);
        return ok;
    }

    void present() override { SDL_GL_SwapWindow(window_); }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool glReady_ = false;
    std::unique_ptr<SystemInterface_SDL> system_;
    std::unique_ptr<RenderInterface_GL3> render_;
};

}  // namespace

std::unique_ptr<UiBackend> UiBackend::createGL3() { return std::make_unique<GL3Backend>(); }
