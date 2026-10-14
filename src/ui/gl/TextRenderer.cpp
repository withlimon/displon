#include "ui/gl/TextRenderer.h"

#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

bool TextRenderer::Init(const char* ttfPath, float dpiScale) {
    dpiScale_ = dpiScale > 0 ? dpiScale : 1.f;
    std::ifstream in(ttfPath, std::ios::binary);
    if (!in) return false;
    fontData_.assign(std::istreambuf_iterator<char>(in), {});
    if (fontData_.empty()) return false;
    auto* info = new stbtt_fontinfo();
    if (!stbtt_InitFont(info, fontData_.data(), 0)) {
        delete info;
        return false;
    }
    fontInfo_ = info;
    return true;
}

void TextRenderer::Shutdown() {
    for (auto& [_, a] : atlases_) {
        if (a.tex) glDeleteTextures(1, &a.tex);
    }
    atlases_.clear();
    delete static_cast<stbtt_fontinfo*>(fontInfo_);
    fontInfo_ = nullptr;
    fontData_.clear();
}

void TextRenderer::SetDpiScale(float s) {
    // Snap to common DPI buckets so atlases stay integer-pixel.
    const float snapped = std::round(s * 4.f) / 4.f;  // 1.0, 1.25, 1.5, 1.75, 2.0…
    if (std::abs(snapped - dpiScale_) < 0.01f) return;
    dpiScale_ = snapped > 0 ? snapped : 1.f;
    for (auto& [_, a] : atlases_) {
        if (a.tex) glDeleteTextures(1, &a.tex);
    }
    atlases_.clear();
}

TextRenderer::Atlas& TextRenderer::EnsureAtlas(const carbon::TypeStyle& style) {
    // Key = integer physical pixel size (Carbon sizes * DPI, rounded).
    const int px = std::max(1, static_cast<int>(std::lround(style.sizePx * dpiScale_)));
    auto it = atlases_.find(px);
    if (it != atlases_.end()) return it->second;
    Atlas a;
    Bake(a, static_cast<float>(px));
    return atlases_[px] = std::move(a);
}

bool TextRenderer::Bake(Atlas& a, float pixelHeight) {
    auto* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    if (!info) return false;
    // Integer pixel height only — prevents fractional glyph blurriness.
    const int iph = std::max(1, static_cast<int>(std::lround(pixelHeight)));
    a.pixelHeight = static_cast<float>(iph);
    a.w = 1024;
    a.h = 1024;
    std::vector<unsigned char> bitmap(static_cast<size_t>(a.w * a.h), 0);

    float scale = stbtt_ScaleForPixelHeight(info, a.pixelHeight);
    int x = 1, y = 1, rowH = 0;

    // Latin-1 plus the typographic punctuation the UI copy uses (en/em dash, minus, curly quotes, bullet, ellipsis).
    // Anything outside this set is still drawn as '?', but these used to be '?' as well: the stepper's "-" button
    // and every "-" / "--" in the help text showed up as a question mark.
    static const int kExtra[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2212};
    std::vector<int> codepoints;
    for (int cp = 32; cp < 256; ++cp) codepoints.push_back(cp);
    for (int cp : kExtra) codepoints.push_back(cp);

    for (int cp : codepoints) {
        if (cp >= 256 && !stbtt_FindGlyphIndex(info, cp)) continue;   // font lacks it: keep the '?' fallback
        int gw, gh, xoff, yoff;
        unsigned char* gbm = stbtt_GetCodepointBitmap(info, 0, scale, cp, &gw, &gh, &xoff, &yoff);
        if (!gbm) {
            // Blank glyphs (space, nbsp) have no bitmap but DO have an advance. Skipping them made every
            // space fall back to half an em, which is the "w i d e  s p a c e d" text in the screenshots.
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
            Glyph blank{};
            blank.advance = adv * scale;
            a.glyphs[static_cast<std::uint32_t>(cp)] = blank;
            continue;
        }
        if (x + gw + 1 >= a.w) {
            x = 1;
            y += rowH + 1;
            rowH = 0;
        }
        if (y + gh + 1 >= a.h) {
            free(gbm);
            break;
        }
        for (int row = 0; row < gh; ++row)
            memcpy(&bitmap[(y + row) * a.w + x], gbm + row * gw, static_cast<size_t>(gw));

        Glyph g;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
        g.advance = adv * scale;
        g.x0 = static_cast<float>(xoff);
        g.y0 = static_cast<float>(yoff);
        g.x1 = g.x0 + gw;
        g.y1 = g.y0 + gh;
        g.u0 = x / float(a.w);
        g.v0 = y / float(a.h);
        g.u1 = (x + gw) / float(a.w);
        g.v1 = (y + gh) / float(a.h);
        a.glyphs[static_cast<std::uint32_t>(cp)] = g;

        x += gw + 1;
        if (gh > rowH) rowH = gh;
        free(gbm);
    }

    glGenTextures(1, &a.tex);
    glBindTexture(GL_TEXTURE_2D, a.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, a.w, a.h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    // NEAREST = sharp Carbon UI text at integer DPI; no bilinear blur.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void TextRenderer::Draw(Renderer& r, std::wstring_view text, float x, float y,
                        const carbon::TypeStyle& style, const carbon::Color& color) {
    if (!fontInfo_) return;
    Atlas& a = EnsureAtlas(style);
    if (!a.tex) return;
    auto* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    float scale = stbtt_ScaleForPixelHeight(info, a.pixelHeight);
    int ascent = 0;
    stbtt_GetFontVMetrics(info, &ascent, nullptr, nullptr);
    // Snap baseline and pen to whole pixels — eliminates subpixel smearing.
    float baseline = std::floor(y + ascent * scale + 0.5f);
    float pen = std::floor(x + 0.5f);
    for (wchar_t wc : text) {
        std::uint32_t cp = static_cast<std::uint32_t>(wc);
        auto it = a.glyphs.find(cp);
        if (it == a.glyphs.end()) it = a.glyphs.find('?');   // not baked: draw a visible placeholder
        if (it == a.glyphs.end()) {
            pen += a.pixelHeight * 0.3f;
            continue;
        }
        const Glyph& g = it->second;
        const float gx = std::floor(pen + g.x0 + 0.5f);
        const float gy = std::floor(baseline + g.y0 + 0.5f);
        Rect qr{gx, gy, std::floor(g.x1 - g.x0 + 0.5f), std::floor(g.y1 - g.y0 + 0.5f)};
        r.TexturedQuad(qr, g.u0, g.v0, g.u1, g.v1, a.tex, color);
        pen += g.advance;
    }
}

void TextRenderer::DrawCentered(Renderer& r, std::wstring_view text, const Rect& box,
                                const carbon::TypeStyle& style, const carbon::Color& color) {
    if (!fontInfo_ || text.empty()) return;
    auto* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    const int px = std::max(1, static_cast<int>(std::lround(style.sizePx * dpiScale_)));
    const float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(px));
    int ascent = 0;
    stbtt_GetFontVMetrics(info, &ascent, nullptr, nullptr);

    // Vertical: put the middle of the capital letters on the middle of the box.
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    const float capH = stbtt_GetCodepointBox(info, 'H', &bx0, &by0, &bx1, &by1)
                           ? by1 * scale : static_cast<float>(px) * 0.7f;
    const float baseline = std::floor(box.y + (box.h + capH) * 0.5f + 0.5f);
    const float y = baseline - ascent * scale;

    // Horizontal: centre the visible ink (first glyph's left edge .. last glyph's right edge).
    size_t first = 0, last = text.size();
    while (first < last && text[first] == L' ') ++first;
    while (last > first && text[last - 1] == L' ') --last;
    float inkL = 0.f, inkR = MeasureWidth(text, style);
    if (first < last) {
        int a0, b0, a1, b1;
        if (stbtt_GetCodepointBox(info, static_cast<int>(text[first]), &a0, &b0, &a1, &b1))
            inkL = MeasureWidth(text.substr(0, first), style) + a0 * scale;
        else
            inkL = MeasureWidth(text.substr(0, first), style);
        const float before = MeasureWidth(text.substr(0, last - 1), style);
        if (stbtt_GetCodepointBox(info, static_cast<int>(text[last - 1]), &a0, &b0, &a1, &b1))
            inkR = before + a1 * scale;
        else
            inkR = MeasureWidth(text.substr(0, last), style);
    }
    const float x = box.x + (box.w - (inkR - inkL)) * 0.5f - inkL;
    Draw(r, text, x, y, style, color);
}

float TextRenderer::MeasureWidth(std::wstring_view text, const carbon::TypeStyle& style) const {
    if (!fontInfo_) return static_cast<float>(text.size() * style.sizePx * 0.5f * dpiScale_);
    auto* info = static_cast<const stbtt_fontinfo*>(fontInfo_);
    const int px = std::max(1, static_cast<int>(std::lround(style.sizePx * dpiScale_)));
    float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(px));
    float w = 0;
    for (wchar_t wc : text) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, static_cast<int>(wc), &adv, &lsb);
        w += adv * scale;
    }
    return w;
}

float TextRenderer::LineHeight(const carbon::TypeStyle& style) const {
    const int px = std::max(1, static_cast<int>(std::lround(style.sizePx * dpiScale_)));
    return static_cast<float>(px) * 1.4f;
}
