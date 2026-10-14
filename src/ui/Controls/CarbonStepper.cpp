#include "ui/Controls/CarbonStepper.h"
#include <algorithm>
#include <cstdio>

void CarbonStepper::SetValue(int v) {
    if (v < lo_) v = lo_;
    if (v > hi_) v = hi_;
    value_ = v;
}

void CarbonStepper::Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) {
    // DPI scale recovered from the text metrics (kBody01 = 17 px * 1.4 line height at 100%).
    const float u = std::max(1.f, text.LineHeight(carbon::kBody01) / 23.8f);
    text.Draw(r, label_, bounds_.x, bounds_.y, carbon::kLabel01, theme.textSecondary);
    const float y = bounds_.y + 18.f * u;
    const float btn = 28.f * u;
    minus_ = {bounds_.x, y, btn, btn};
    plus_ = {bounds_.x + bounds_.w - btn, y, btn, btn};
    Rect field{bounds_.x + btn + 4 * u, y, bounds_.w - 2 * btn - 8 * u, btn};

    const bool overMinus = hovered_ && minus_.Contains(mouseX_, mouseY_);
    const bool overPlus = hovered_ && plus_.Contains(mouseX_, mouseY_);
    r.FillRect(minus_, overMinus ? theme.layerAccent : theme.layer);
    r.StrokeRect(minus_, theme.borderStrong, 1.f);
    text.Draw(r, L"\u2212", minus_.x + 8 * u, minus_.y + 4 * u, carbon::kBody02, theme.textPrimary);

    r.FillRect(field, theme.field);
    r.StrokeRect(field, theme.border, 1.f);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d %ls", value_, suffix_.c_str());
    const float tw = text.MeasureWidth(buf, carbon::kBody01);
    text.Draw(r, buf, field.x + (field.w - tw) * 0.5f, field.y + 4 * u, carbon::kBody01, theme.textPrimary);

    r.FillRect(plus_, overPlus ? theme.layerAccent : theme.layer);
    r.StrokeRect(plus_, theme.borderStrong, 1.f);
    text.Draw(r, L"+", plus_.x + 8 * u, plus_.y + 4 * u, carbon::kBody02, theme.textPrimary);

    if (focused_) r.FocusRing(bounds_, theme.focus);
}

void CarbonStepper::OnPointer(PointerState s, float x, float y) {
    ICarbonControl::OnPointer(s, x, y);
    mouseX_ = x;
    mouseY_ = y;
    if (s != PointerState::Up) return;
    if (minus_.Contains(x, y)) {
        SetValue(value_ - 1);
        if (onChange_) onChange_(value_);
    } else if (plus_.Contains(x, y)) {
        SetValue(value_ + 1);
        if (onChange_) onChange_(value_);
    }
}
