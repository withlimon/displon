// Batched 2D renderer in physical pixels (plan Module G).
// Solid rects, 1–2 px borders, textured/tinted quads. Rects are flat; buttons use the slightly rounded
// FillRoundRect/StrokeRoundRect (anti-aliased in the fragment shader).
#pragma once
#include <cstdint>
#include <vector>

#include "ui/Theme.h"

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

class Renderer {
public:
    bool Init();
    void Shutdown();
    void BeginFrame(int fbWidth, int fbHeight, const carbon::Color& clear);
    void EndFrame();  // flushes batches + swap is caller's job via GLContext

    void FillRect(const Rect& r, const carbon::Color& c);
    void StrokeRect(const Rect& r, const carbon::Color& c, float thickness = 1.f);
    void FocusRing(const Rect& r, const carbon::Color& c);  // 2px Carbon focus outline

    // Rounded variants (radius in physical pixels, clamped to half the short side).
    void FillRoundRect(const Rect& r, float radius, const carbon::Color& c);
    void StrokeRoundRect(const Rect& r, float radius, const carbon::Color& c, float thickness = 1.f);

    // Textured quad (glyph / icon atlas). UV in [0,1].
    // colorTexture=false: single-channel alpha atlas tinted by `tint`.
    // colorTexture=true : RGBA texture drawn as-is (tint.a only fades it) - used by the app logo.
    void TexturedQuad(const Rect& r, float u0, float v0, float u1, float v1,
                      std::uint32_t texture, const carbon::Color& tint, bool colorTexture = false);

    void SetScissor(const Rect& r);  // physical pixels
    void ClearScissor();

private:
    struct Vertex {
        float x, y, u, v;
        float r, g, b, a;
        float hw = 0, hh = 0, rad = 0, bw = 0;  // rounded rect: half size, radius, border (hw=0: plain)
    };
    struct Batch {
        std::uint32_t texture = 0;  // 0 = solid
        bool color = false;         // true = RGBA texture, false = R8 alpha atlas
        std::vector<Vertex> verts;
    };

    void PushRound(const Rect& r, float radius, const carbon::Color& c, float border);
    void Flush();
    Batch& BatchFor(std::uint32_t tex, bool color = false);

    std::uint32_t program_ = 0;
    std::uint32_t vao_ = 0;
    std::uint32_t vbo_ = 0;
    int uProj_ = -1;
    int uTex_ = -1;
    int uUseTex_ = -1;
    int fbW_ = 0, fbH_ = 0;
    std::vector<Batch> batches_;
    bool scissorOn_ = false;
};
