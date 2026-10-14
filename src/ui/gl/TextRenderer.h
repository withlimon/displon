// stb_truetype glyph atlas renderer (plan Module G).
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/Theme.h"
#include "ui/gl/Renderer.h"

class TextRenderer {
public:
    bool Init(const char* ttfPath, float dpiScale = 1.f);
    void Shutdown();
    void SetDpiScale(float s);

    // Draw UTF-16 text at top-left origin in physical pixels.
    void Draw(Renderer& r, std::wstring_view text, float x, float y,
              const carbon::TypeStyle& style, const carbon::Color& color);

    // Draw text centred in `box`: horizontally on the glyph ink (ignores side bearings), vertically on the
    // cap height (so it looks centred for mixed-case labels instead of sitting low in the line box).
    void DrawCentered(Renderer& r, std::wstring_view text, const Rect& box,
                      const carbon::TypeStyle& style, const carbon::Color& color);

    float DpiScale() const { return dpiScale_; }
    float MeasureWidth(std::wstring_view text, const carbon::TypeStyle& style) const;
    float LineHeight(const carbon::TypeStyle& style) const;

private:
    struct Glyph {
        float advance;
        float x0, y0, x1, y1;  // bitmap bounds relative to baseline
        float u0, v0, u1, v1;
    };
    struct Atlas {
        std::uint32_t tex = 0;
        int w = 0, h = 0;
        std::unordered_map<std::uint32_t /*codepoint*/, Glyph> glyphs;
        float pixelHeight = 0;
    };

    Atlas& EnsureAtlas(const carbon::TypeStyle& style);
    bool Bake(Atlas& a, float pixelHeight);

    std::vector<unsigned char> fontData_;
    void* fontInfo_ = nullptr;  // stbtt_fontinfo*
    float dpiScale_ = 1.f;
    std::unordered_map<int /*sizePx*/, Atlas> atlases_;
};
