#include "ui/Controls/CarbonButton.h"

void CarbonButton::Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) {
    carbon::Color bg, fg, border;
    if (kind_ == Kind::Primary) {
        bg = pressed_ ? theme.accentActive : (hovered_ ? theme.accentHover : theme.accent);
        fg = theme.textOnColor;
        border = bg;
    } else if (kind_ == Kind::Secondary) {
        bg = pressed_ ? theme.layerAccent : (hovered_ ? theme.layerAccent : theme.layer);
        fg = theme.textPrimary;
        border = theme.borderStrong;
    } else {
        bg = hovered_ ? theme.layer : carbon::Color{0, 0, 0, 0};
        fg = theme.accent;
        border = carbon::Color{0, 0, 0, 0};
    }
    if (!enabled_) {
        bg = theme.layer;
        fg = theme.textSecondary;
        border = theme.border;
    }
    const float rad = carbon::kRadiusSm * text.DpiScale();   // slight rounding
    r.FillRoundRect(bounds_, rad, bg);
    if (kind_ == Kind::Secondary) r.StrokeRoundRect(bounds_, rad, border, 1.f);
    if (focused_) {
        r.StrokeRoundRect({bounds_.x - 2, bounds_.y - 2, bounds_.w + 4, bounds_.h + 4}, rad + 2.f, theme.focus, 2.f);
    }

    // Label centred on both axes (ink-based horizontally, cap-height based vertically).
    text.DrawCentered(r, label_, bounds_, carbon::kBody01, fg);
}

void CarbonButton::OnPointer(PointerState s, float x, float y) {
    ICarbonControl::OnPointer(s, x, y);
    if (s == PointerState::Up && enabled_ && bounds_.Contains(x, y) && onClick_) {
        onClick_();
    }
}
