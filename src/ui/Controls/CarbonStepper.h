#pragma once
#include "ui/Controls/ICarbonControl.h"
#include <functional>
#include <string>

// Simple − value + stepper for minutes / counts.
class CarbonStepper : public ICarbonControl {
public:
    void SetLabel(std::wstring label) { label_ = std::move(label); }
    void SetRange(int lo, int hi) { lo_ = lo; hi_ = hi; }
    void SetValue(int v);
    int Value() const { return value_; }
    void SetSuffix(std::wstring s) { suffix_ = std::move(s); }
    void SetOnChange(std::function<void(int)> cb) { onChange_ = std::move(cb); }

    void Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) override;
    void OnPointer(PointerState s, float x, float y) override;

private:
    std::wstring label_;
    std::wstring suffix_ = L"min";
    int value_ = 25, lo_ = 1, hi_ = 180;
    std::function<void(int)> onChange_;
    Rect minus_{}, plus_{};
    float mouseX_ = -1.f, mouseY_ = -1.f;   // last pointer position: lets each button highlight on its own
};
