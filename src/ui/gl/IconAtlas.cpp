#include "ui/gl/IconAtlas.h"

#include <glad/gl.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

#define NANOSVG_IMPLEMENTATION
#include "third_party/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "third_party/nanosvgrast.h"

namespace {

const char* FilenameFor(IconId id) {
    switch (id) {
        case IconId::Gear: return "gear.svg";
        case IconId::Lock: return "lock-fill.svg";
        case IconId::ShieldCheck: return "shield-check.svg";
        case IconId::Clock: return "clock.svg";
        case IconId::Hourglass: return "hourglass-split.svg";
        case IconId::ListCheck: return "list-check.svg";
        case IconId::Play: return "play-fill.svg";
        case IconId::Pause: return "pause-fill.svg";
        case IconId::Check: return "check-lg.svg";
        case IconId::X: return "x-lg.svg";
        case IconId::Plus: return "plus-lg.svg";
        case IconId::Download: return "download.svg";
        case IconId::Upload: return "upload.svg";
        case IconId::FilePlus: return "file-earmark-plus.svg";
        case IconId::Globe: return "globe.svg";
        case IconId::Laptop: return "laptop.svg";
        case IconId::Window: return "window.svg";
        case IconId::Sliders: return "sliders.svg";
        case IconId::ArrowRight: return "arrow-right.svg";
        case IconId::ArrowLeft: return "arrow-left.svg";
        case IconId::App: return "displon-app.svg";  // full-color logo
        default: return nullptr;
    }
}

// Bootstrap SVGs use fill="currentColor"; nanosvg needs a real color.
std::string PrepSvg(std::string svg) {
    auto replace_all = [&](const char* from, const char* to) {
        size_t pos = 0;
        const size_t fl = std::strlen(from), tl = std::strlen(to);
        while ((pos = svg.find(from, pos)) != std::string::npos) {
            svg.replace(pos, fl, to);
            pos += tl;
        }
    };
    replace_all("currentColor", "#ffffff");
    replace_all("currentcolor", "#ffffff");
    return svg;
}

std::string WideToNarrow(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

}  // namespace

bool IconAtlas::Init(const std::wstring& iconsDir, int sizePx) {
    dir_ = iconsDir;
    sizePx_ = sizePx > 0 ? sizePx : 32;
    bool any = false;
    for (int i = 0; i < static_cast<int>(IconId::Count); ++i) {
        if (LoadOne(static_cast<IconId>(i), FilenameFor(static_cast<IconId>(i)))) any = true;
    }
    return any;
}

void IconAtlas::Shutdown() {
    for (auto& e : entries_) {
        if (e.tex) glDeleteTextures(1, &e.tex);
        e = {};
    }
}

bool IconAtlas::Loaded(IconId id) const {
    const int i = static_cast<int>(id);
    return i >= 0 && i < static_cast<int>(IconId::Count) && entries_[i].tex != 0;
}

bool IconAtlas::LoadOne(IconId id, const char* filename) {
    if (!filename) return false;
    const std::string path = WideToNarrow(dir_) + filename;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // try with backslash path already including separator
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string svg = PrepSvg(ss.str());

    NSVGimage* image = nsvgParse(svg.data(), "px", 96.0f);
    if (!image) return false;

    // The app logo is full color (not tinted) and drawn larger, so rasterize it bigger.
    const bool color = (id == IconId::App);
    const int px = color ? 128 : sizePx_;
    const float scale = px / (image->width > 0 ? image->width : 16.f);
    const int w = px;
    const int h = px;
    std::vector<unsigned char> rgba(static_cast<size_t>(w * h * 4), 0);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }
    nsvgRasterize(rast, image, 0, 0, scale, rgba.data(), w, h, w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    if (color) {
        GLuint ctex = 0;
        glGenTextures(1, &ctex);
        glBindTexture(GL_TEXTURE_2D, ctex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        Entry& ce = entries_[static_cast<int>(id)];
        ce.tex = ctex;
        ce.w = w;
        ce.h = h;
        ce.color = true;
        return true;
    }

    // Convert to single-channel alpha (white RGB was baked) for tinting like text.
    std::vector<unsigned char> alpha(static_cast<size_t>(w * h));
    for (int i = 0; i < w * h; ++i) {
        // Use max RGB or alpha from raster
        alpha[i] = rgba[i * 4 + 3];
        if (alpha[i] == 0) {
            // some fills put color in RGB with alpha 255
            const unsigned char mx = std::max({rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]});
            if (mx > 0) alpha[i] = mx;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, alpha.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    Entry& e = entries_[static_cast<int>(id)];
    e.tex = tex;
    e.w = w;
    e.h = h;
    return true;
}

void IconAtlas::Draw(Renderer& r, IconId id, const Rect& dest, const carbon::Color& tint) {
    if (!Loaded(id)) return;
    const Entry& e = entries_[static_cast<int>(id)];
    r.TexturedQuad(dest, 0, 0, 1, 1, e.tex, tint, e.color);
}
