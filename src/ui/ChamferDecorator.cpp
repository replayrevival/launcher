#include "ui/ChamferDecorator.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>

#include <array>
#include <cmath>

namespace {

using Rml::Vector2f;

void addPoly(Rml::Mesh& mesh, const std::array<Vector2f, 8>& pts, Rml::ColourbPremultiplied col) {
    const int base = int(mesh.vertices.size());
    Vector2f c{0, 0};
    for (auto& p : pts) c += p * (1.f / 8.f);
    mesh.vertices.push_back({c, col, {0, 0}});
    for (auto& p : pts) mesh.vertices.push_back({p, col, {0, 0}});
    for (int i = 0; i < 8; ++i) {
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1 + i);
        mesh.indices.push_back(base + 1 + (i + 1) % 8);
    }
}

// Quad strip between two octagons (outer and inner), one colour.
void addRing(Rml::Mesh& mesh, const std::array<Vector2f, 8>& outer, const std::array<Vector2f, 8>& inner,
             Rml::ColourbPremultiplied col) {
    const int base = int(mesh.vertices.size());
    for (auto& p : outer) mesh.vertices.push_back({p, col, {0, 0}});
    for (auto& p : inner) mesh.vertices.push_back({p, col, {0, 0}});
    for (int i = 0; i < 8; ++i) {
        const int j = (i + 1) % 8;
        const int o0 = base + i, o1 = base + j, i0 = base + 8 + i, i1 = base + 8 + j;
        for (int v : {o0, o1, i1, o0, i1, i0}) mesh.indices.push_back(v);
    }
}

// Octagon with 45 degree cut corners, inset by `inset` from the w*h rectangle.
std::array<Vector2f, 8> octagon(float w, float h, float cut, float inset) {
    // An inward offset of t moves a 45 degree edge so the cut measured from the inner rectangle
    // shrinks by t*(2 - sqrt(2)).
    const float c = std::fmax(0.f, cut - inset * (2.f - std::sqrt(2.f)));
    const float l = inset, t = inset, r = w - inset, b = h - inset;
    return {{{l + c, t}, {r - c, t}, {r, t + c}, {r, b - c}, {r - c, b}, {l + c, b}, {l, b - c}, {l, t + c}}};
}

class ChamferDecorator final : public Rml::Decorator {
public:
    ChamferDecorator(float cut, float line, Rml::Colourb fill, Rml::Colourb outline, float glow, Rml::Colourb glowCol)
        : cut_(cut), line_(line), fill_(fill), outline_(outline), glow_(glow), glowCol_(glowCol) {}

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* e, Rml::BoxArea) const override {
        const float dp = e->GetContext() ? e->GetContext()->GetDensityIndependentPixelRatio() : 1.f;
        const Vector2f size = e->GetBox().GetSize(Rml::BoxArea::Border).Round();
        const float cut = cut_ * dp, line = line_ * dp;
        const float opacity = e->GetComputedValues().opacity();

        Rml::Mesh mesh;
        if (glow_ > 0.f && glowCol_.alpha > 0) {
            // Soft glow: stacked rings growing outwards, fading with distance (no post-processing filter needed).
            const int steps = 8;
            const float step = glow_ * dp / steps;
            for (int i = 0; i < steps; ++i) {
                const float fall = 1.f - float(i) / steps;
                Rml::Colourb c = glowCol_;
                c.alpha = static_cast<Rml::byte>(glowCol_.alpha * fall * fall * 0.45f);
                const auto outer = octagon(size.x, size.y, cut, -step * (i + 1));
                const auto inner = octagon(size.x, size.y, cut, -step * i);
                addRing(mesh, outer, inner, c.ToPremultiplied(opacity));
            }
        }
        if (line > 0.f) {
            // Outline ring between the outer octagon and the one inset by the line width.
            const auto outer = octagon(size.x, size.y, cut, 0.f);
            const auto inner = octagon(size.x, size.y, cut, line);
            addRing(mesh, outer, inner, outline_.ToPremultiplied(opacity));
        }
        if (fill_.alpha > 0) addPoly(mesh, octagon(size.x, size.y, cut, line), fill_.ToPremultiplied(opacity));

        return reinterpret_cast<Rml::DecoratorDataHandle>(
            new Rml::Geometry(e->GetRenderManager()->MakeGeometry(std::move(mesh))));
    }
    void ReleaseElementData(Rml::DecoratorDataHandle h) const override { delete reinterpret_cast<Rml::Geometry*>(h); }
    void RenderElement(Rml::Element* e, Rml::DecoratorDataHandle h) const override {
        reinterpret_cast<Rml::Geometry*>(h)->Render(e->GetAbsoluteOffset(Rml::BoxArea::Border));
    }

private:
    float cut_, line_;
    Rml::Colourb fill_, outline_;
    float glow_;
    Rml::Colourb glowCol_;
};

class ArrowDecorator final : public Rml::Decorator {
public:
    ArrowDecorator(Rml::Colourb col, bool left) : col_(col), left_(left) {}
    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* e, Rml::BoxArea) const override {
        const Vector2f s = e->GetBox().GetSize(Rml::BoxArea::Border).Round();
        const auto c = col_.ToPremultiplied(e->GetComputedValues().opacity());
        Rml::Mesh mesh;
        if (left_) mesh.vertices = {{{s.x, 0}, c, {0, 0}}, {{0, s.y * 0.5f}, c, {0, 0}}, {{s.x, s.y}, c, {0, 0}}};
        else mesh.vertices = {{{0, 0}, c, {0, 0}}, {{s.x, s.y * 0.5f}, c, {0, 0}}, {{0, s.y}, c, {0, 0}}};
        mesh.indices = {0, 1, 2};
        return reinterpret_cast<Rml::DecoratorDataHandle>(
            new Rml::Geometry(e->GetRenderManager()->MakeGeometry(std::move(mesh))));
    }
    void ReleaseElementData(Rml::DecoratorDataHandle h) const override { delete reinterpret_cast<Rml::Geometry*>(h); }
    void RenderElement(Rml::Element* e, Rml::DecoratorDataHandle h) const override {
        reinterpret_cast<Rml::Geometry*>(h)->Render(e->GetAbsoluteOffset(Rml::BoxArea::Border));
    }

private:
    Rml::Colourb col_;
    bool left_;
};

class ChamferInstancer final : public Rml::DecoratorInstancer {
public:
    ChamferInstancer() {
        ids_.cut = RegisterProperty("cut", "8").AddParser("number").GetId();
        ids_.line = RegisterProperty("outline", "2").AddParser("number").GetId();
        ids_.fill = RegisterProperty("fill", "transparent").AddParser("color").GetId();
        ids_.outline = RegisterProperty("outline-color", "#3FD4FF").AddParser("color").GetId();
        ids_.glow = RegisterProperty("glow", "0").AddParser("number").GetId();
        ids_.glowCol = RegisterProperty("glow-color", "#3FD4FF").AddParser("color").GetId();
        RegisterShorthand("decorator", "cut, outline, fill, outline-color, glow, glow-color", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& p,
                                                     const Rml::DecoratorInstancerInterface&) override {
        return Rml::MakeShared<ChamferDecorator>(p.GetProperty(ids_.cut)->Get<float>(), p.GetProperty(ids_.line)->Get<float>(),
                                                  p.GetProperty(ids_.fill)->Get<Rml::Colourb>(),
                                                  p.GetProperty(ids_.outline)->Get<Rml::Colourb>(), p.GetProperty(ids_.glow)->Get<float>(),
                                                  p.GetProperty(ids_.glowCol)->Get<Rml::Colourb>());
    }

private:
    struct { Rml::PropertyId cut, line, fill, outline, glow, glowCol; } ids_;
};

class ArrowInstancer final : public Rml::DecoratorInstancer {
public:
    ArrowInstancer() {
        id_ = RegisterProperty("color", "#FFD43B").AddParser("color").GetId();
        RegisterShorthand("decorator", "color", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& p,
                                                     const Rml::DecoratorInstancerInterface&) override {
        return Rml::MakeShared<ArrowDecorator>(p.GetProperty(id_)->Get<Rml::Colourb>(), name == "arrow-left");
    }

private:
    Rml::PropertyId id_;
};

}  // namespace

// Instancers register properties with RmlUi's parsers, so they must be built after Rml::Initialise().
void registerDecorators() {
    static ChamferInstancer chamfer;
    static ArrowInstancer arrow;
    Rml::Factory::RegisterDecoratorInstancer("chamfer", &chamfer);
    Rml::Factory::RegisterDecoratorInstancer("arrow", &arrow);
    Rml::Factory::RegisterDecoratorInstancer("arrow-left", &arrow);
}

