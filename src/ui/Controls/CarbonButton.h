#pragma once
#include "ui/Controls/ICarbonControl.h"
#include <functional>
#include <string>

class CarbonButton : public ICarbonControl {
public:
    enum class Kind { Primary, Secondary, Ghost };

    void SetLabel(std::wstring label) { label_ = std::move(label); }
    void SetKind(Kind k) { kind_ = k; }
    void SetOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
    void SetEnabled(bool e) { enabled_ = e; }

    void Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) override;
    void OnPointer(PointerState s, float x, float y) override;

private:
    std::wstring label_;
    Kind kind_ = Kind::Primary;
    std::function<void()> onClick_;
    bool enabled_ = true;
};
