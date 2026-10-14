// WGL bootstrap for an OpenGL 3.3 core context (plan Module G, GLContext).
#pragma once
#include <windows.h>
#include <string>

class GLContext {
public:
    GLContext() = default;
    ~GLContext();
    GLContext(const GLContext&) = delete;
    GLContext& operator=(const GLContext&) = delete;

    // dummy-window bootstrap -> wglCreateContextAttribsARB(3.3 core) on `hwnd`.
    // Returns false and fills LastError() on failure (e.g. no GL 3.3 driver).
    bool Create(HWND hwnd);
    void Destroy();

    bool MakeCurrent() const;
    void Resize(int widthPx, int heightPx);   // viewport update
    void SwapBuffers() const;

    const std::wstring& LastError() const { return error_; }
    bool IsValid() const { return rc_ != nullptr; }

private:
    bool Fail(std::wstring msg);

    HWND  hwnd_ = nullptr;
    HDC   dc_   = nullptr;
    HGLRC rc_   = nullptr;
    std::wstring error_;
};
