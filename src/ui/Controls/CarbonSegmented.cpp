#include "ui/Controls/CarbonSegmented.h"

void CarbonSegmented::Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) {
    if (options_.empty()) return;
    const float segW = bounds_.w / options_.size();
    r.FillRect(bounds_, theme.layer);
    r.StrokeRect(bounds_, theme.border, 1.f);
    for (size_t i = 0; i < options_.size(); ++i) {
        Rect seg{bounds_.x + i * segW, bounds_.y, segW, bounds_.h};
        if ((int)i == selected_) {
            // underline indicator (Carbon style)
            r.FillRect({seg.x, seg.y + seg.h - 3, seg.w, 3}, theme.accent);
        }
        const float tw = text.MeasureWidth(options_[i], carbon::kBody01);
        const float th = text.LineHeight(carbon::kBody01);
        text.Draw(r, options_[i],
                  seg.x + (seg.w - tw) * 0.5f,
                  seg.y + (seg.h - th) * 0.5f,
                  carbon::kBody01,
                  (int)i == selected_ ? theme.textPrimary : theme.textSecondary);
    }
    if (focused_) r.FocusRing(bounds_, theme.focus);
}

void CarbonSegmented::OnPointer(PointerState s, float x, float y) {
    ICarbonControl::OnPointer(s, x, y);
    if (s == PointerState::Up && bounds_.Contains(x, y) && !options_.empty()) {
        const float segW = bounds_.w / options_.size();
        int idx = static_cast<int>((x - bounds_.x) / segW);
        if (idx < 0) idx = 0;
        if (idx >= (int)options_.size()) idx = (int)options_.size() - 1;
        if (idx != selected_) {
            selected_ = idx;
            if (onChange_) onChange_(selected_);
        }
    }
}
