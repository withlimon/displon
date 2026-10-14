// Displon design tokens — black + cream palette (cream = #FDFCE8).
// Adapted from IBM Carbon structure; blue accents replaced with black/cream.
#pragma once
#include <cstdint>

namespace carbon {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}
};

constexpr Color FromHex(std::uint32_t rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f,
            ((rgb >> 8) & 0xFF) / 255.0f,
            (rgb & 0xFF) / 255.0f, a};
}

struct Theme {
    Color background;
    Color layer;
    Color layerAccent;
    Color field;
    Color border;
    Color borderStrong;
    Color textPrimary;
    Color textSecondary;
    Color textOnColor;
    Color accent;
    Color accentHover;
    Color accentActive;
    Color danger;
    Color focus;
    Color success;
};

// Dark theme: near-black surfaces, cream text & accents.
inline constexpr Theme kG100{
    FromHex(0x0D0D0D),  // background
    FromHex(0x1A1A1A),  // layer
    FromHex(0x262626),  // layerAccent
    FromHex(0x2A2A2A),  // field
    FromHex(0x333333),  // border
    FromHex(0x6F6F6F),  // borderStrong
    FromHex(0xFDFCE8),  // textPrimary  (cream)
    FromHex(0xC8C6A8),  // textSecondary
    FromHex(0x0D0D0D),  // textOnColor  (on cream accent)
    FromHex(0xFDFCE8),  // accent       (cream)
    FromHex(0xE8E6C8),  // accentHover
    FromHex(0xD4D2B0),  // accentActive
    FromHex(0xDA1E28),  // danger
    FromHex(0xFDFCE8),  // focus
    FromHex(0x24A148),  // success
};

// Light theme: cream surfaces, black text & accents.
inline constexpr Theme kWhite{
    FromHex(0xFDFCE8),  // background  (cream)
    FromHex(0xF5F3D8),  // layer
    FromHex(0xEBE9C8),  // layerAccent
    FromHex(0xFDFCE8),  // field
    FromHex(0xD4D2B0),  // border
    FromHex(0x8A8868),  // borderStrong
    FromHex(0x0D0D0D),  // textPrimary  (black)
    FromHex(0x4A4A3A),  // textSecondary
    FromHex(0xFDFCE8),  // textOnColor  (on black accent)
    FromHex(0x0D0D0D),  // accent       (black)
    FromHex(0x2A2A2A),  // accentHover
    FromHex(0x000000),  // accentActive
    FromHex(0xDA1E28),  // danger
    FromHex(0x0D0D0D),  // focus
    FromHex(0x24A148),  // success
};

namespace space {
inline constexpr int mini = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32, xxxl = 48;
}

struct TypeStyle { int sizePx; float weight; const char* name; };
inline constexpr TypeStyle kDisplay01{64, 0.4f, "display-01"};   // countdown digits
inline constexpr TypeStyle kHeading03{28, 0.6f, "heading-03"};
inline constexpr TypeStyle kHeading05{20, 0.6f, "heading-05"};
inline constexpr TypeStyle kBody01{17, 0.4f, "body-01"};
inline constexpr TypeStyle kBody02{19, 0.4f, "body-02"};
inline constexpr TypeStyle kLabel01{15, 0.5f, "label-01"};
inline constexpr TypeStyle kHelper01{15, 0.3f, "helper-text-01"};
inline constexpr int kFast01Ms = 70;
inline constexpr float kRadiusSm = 4.f;   // slight corner radius for buttons (logical px, scaled by DPI)

// Legacy aliases
inline constexpr Color kG100Color = FromHex(0x0D0D0D);
inline constexpr Color kBlue60    = FromHex(0xFDFCE8);  // now cream (was Carbon blue)

}  // namespace carbon
