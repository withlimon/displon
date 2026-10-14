#pragma once
#include "ui/Controls/ICarbonControl.h"
#include <functional>
#include <string>
#include <vector>

// Carbon-style segmented control (e.g. Timer | Restart, No Breaks | Pomodoro | Allowance).
class CarbonSegmented : public ICarbonControl {
public:
    void SetOptions(std::vector<std::wstring> opts) { options_ = std::move(opts); }
    void SetSelected(int i) { if (i >= 0 && i < (int)options_.size()) selected_ = i; }
    int Selected() const { return selected_; }
    void SetOnChange(std::function<void(int)> cb) { onChange_ = std::move(cb); }

    void Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) override;
    void OnPointer(PointerState s, float x, float y) override;

private:
    std::vector<std::wstring> options_;
    int selected_ = 0;
    std::function<void(int)> onChange_;
};
