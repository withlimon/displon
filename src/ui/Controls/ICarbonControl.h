#pragma once
#include <windows.h>
#include "ui/Theme.h"
#include "ui/gl/Renderer.h"
#include "ui/gl/TextRenderer.h"

enum class PointerState { Leave, Hover, Down, Up };

struct KeyEvent {
    int vk = 0;
    wchar_t ch = 0;
    bool shift = false;
    bool ctrl = false;
};

class ICarbonControl {
public:
    virtual ~ICarbonControl() = default;
    virtual void Layout(const Rect& bounds) { bounds_ = bounds; }
    virtual void Draw(Renderer& r, TextRenderer& text, const carbon::Theme& theme) = 0;
    virtual bool HitTest(float x, float y) const { return bounds_.Contains(x, y); }
    virtual void OnPointer(PointerState s, float x, float y) {
        (void)x; (void)y;
        if (s == PointerState::Hover) hovered_ = true;
        else if (s == PointerState::Leave) hovered_ = false;
        else if (s == PointerState::Down) pressed_ = true;
        else if (s == PointerState::Up) pressed_ = false;
    }
    virtual void OnKey(const KeyEvent&) {}
    virtual void SetFocus(bool f) { focused_ = f; }
    bool Focused() const { return focused_; }
    bool Hovered() const { return hovered_; }
    const Rect& Bounds() const { return bounds_; }

protected:
    Rect bounds_{};
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
};
