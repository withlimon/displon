#pragma once
#include "ui/Controls/ICarbonControl.h"
#include <functional>
#include <string>

class CarbonCheckbox : public ICarbonControl {
public:
    void SetLabel(std::wstring label) { label_ = std::move(label); }
    // Optional second line (e.g. the exe path), drawn smaller and truncated with "..." at the start.
    void SetSubLabel(std::wstring sub) { sub_ = std::move(sub); }
    void SetChecked(bool c) { checked_ = c; }
    bool Checked() const { return checked_; }
    void SetOnChange(std::function<void(bool)> cb) { onChange_ = std::move(cb); }

    void Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) override;
    void OnPointer(PointerState s, float x, float y) override;

private:
    std::wstring label_;
    std::wstring sub_;
    bool checked_ = false;
    std::function<void(bool)> onChange_;
};
