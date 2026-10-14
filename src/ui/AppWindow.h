#pragma once
#include <windows.h>
#include <memory>
#include "ui/gl/GLContext.h"
#include "ui/gl/Renderer.h"
#include "ui/gl/TextRenderer.h"
#include "ui/WizardApp.h"
#include "ui/gl/IconAtlas.h"

class AppWindow {
public:
    bool Create(HINSTANCE inst, int nCmdShow);
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Render();
    void EnsureTextRenderer();

    HWND hwnd_ = nullptr;
    GLContext gl_;
    Renderer renderer_;
    TextRenderer text_;
    bool textReady_ = false;
    WizardApp wizard_;
    IconAtlas icons_;
    float dpiScale_ = 1.f;
    UINT_PTR countdownTimer_ = 0;
    int startX_ = CW_USEDEFAULT, startY_ = CW_USEDEFAULT;
};
