#include "ui/Controls/CarbonCheckbox.h"

#include <algorithm>
#include <cmath>

namespace {
// Keep the tail of a long path ("...\\Proteus 9\\BIN\\PROTEUS.EXE"): that is the part people recognise.
std::wstring FitTail(TextRenderer& text, std::wstring s, const carbon::TypeStyle& st, float maxW) {
    if (text.MeasureWidth(s, st) <= maxW) return s;
    while (s.size() > 4 && text.MeasureWidth(L"..." + s, st) > maxW) s.erase(0, 1);
    return L"..." + s;
}
std::wstring FitHead(TextRenderer& text, std::wstring s, const carbon::TypeStyle& st, float maxW) {
    if (text.MeasureWidth(s, st) <= maxW) return s;
    while (s.size() > 1 && text.MeasureWidth(s + L"...", st) > maxW) s.pop_back();
    return s + L"...";
}
}  // namespace

void CarbonCheckbox::Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) {
    const float lh = text.LineHeight(carbon::kBody01);
    const float box = std::max(18.f, std::floor(lh * 1.05f));   // scales with DPI / font size
    const float by = bounds_.y + (bounds_.h - box) * 0.5f;
    Rect boxR{bounds_.x, by, box, box};
    const float u = box / 18.f;
    if (checked_) {
        r.FillRect(boxR, theme.accent);
        r.FillRect({boxR.x + 3 * u, boxR.y + 9 * u, 5 * u, 2 * u}, theme.textOnColor);
        r.FillRect({boxR.x + 6 * u, boxR.y + 5 * u, 2 * u, 8 * u}, theme.textOnColor);
    } else {
        r.FillRect(boxR, theme.field);
        r.StrokeRect(boxR, theme.borderStrong, 1.f);
    }
    if (focused_) r.FocusRing(boxR, theme.focus);

    const float tx = bounds_.x + box + 12 * u;
    const float avail = std::max(20.f, bounds_.x + bounds_.w - tx);
    if (sub_.empty()) {
        text.Draw(r, FitHead(text, label_, carbon::kBody01, avail), tx, bounds_.y + (bounds_.h - lh) * 0.5f,
                  carbon::kBody01, theme.textPrimary);
    } else {
        const float sh = text.LineHeight(carbon::kHelper01);
        const float top = bounds_.y + (bounds_.h - (lh + sh + 2)) * 0.5f;
        text.Draw(r, FitHead(text, label_, carbon::kBody01, avail), tx, top, carbon::kBody01, theme.textPrimary);
        text.Draw(r, FitTail(text, sub_, carbon::kHelper01, avail), tx, top + lh + 2, carbon::kHelper01,
                  theme.textSecondary);
    }
}

void CarbonCheckbox::OnPointer(PointerState s, float x, float y) {
    ICarbonControl::OnPointer(s, x, y);
    if (s == PointerState::Up && bounds_.Contains(x, y)) {
        checked_ = !checked_;
        if (onChange_) onChange_(checked_);
    }
}
