// Rasterizes Bootstrap Icons SVGs (nanosvg) into GL textures; tint at draw time.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ui/Theme.h"
#include "ui/gl/Renderer.h"

enum class IconId {
    Gear,
    Lock,
    ShieldCheck,
    Clock,
    Hourglass,
    ListCheck,
    Play,
    Pause,
    Check,
    X,
    Plus,
    Download,
    Upload,
    FilePlus,
    Globe,
    Laptop,
    Window,
    Sliders,
    ArrowRight,
    ArrowLeft,
    App,
    Count
};

class IconAtlas {
public:
    bool Init(const std::wstring& iconsDir, int sizePx = 32);
    void Shutdown();

    // Draw icon at physical-pixel rect with tint (Carbon text/accent colors).
    // IconId::App is the full-color logo: the tint is ignored (only its alpha fades it).
    void Draw(Renderer& r, IconId id, const Rect& dest, const carbon::Color& tint);

    bool Loaded(IconId id) const;

private:
    bool LoadOne(IconId id, const char* filename);
    std::wstring dir_;
    int sizePx_ = 32;
    struct Entry {
        std::uint32_t tex = 0;
        int w = 0, h = 0;
        bool color = false;  // true: RGBA texture drawn untinted (app logo)
    };
    Entry entries_[static_cast<int>(IconId::Count)]{};
};
