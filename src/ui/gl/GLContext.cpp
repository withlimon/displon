#include "ui/gl/GLContext.h"

#include <glad/gl.h>

#include <cassert>

namespace {

// --- WGL extension constants/typedefs (declared locally; no wglext.h needed) ---
constexpr int WGL_DRAW_TO_WINDOW_ARB    = 0x2001;
constexpr int WGL_ACCELERATION_ARB      = 0x2003;
constexpr int WGL_SUPPORT_OPENGL_ARB    = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB     = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB        = 0x2013;
constexpr int WGL_COLOR_BITS_ARB        = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB        = 0x2022;
constexpr int WGL_STENCIL_BITS_ARB      = 0x2023;
constexpr int WGL_FULL_ACCELERATION_ARB = 0x2027;
constexpr int WGL_TYPE_RGBA_ARB         = 0x202B;
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB    = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB    = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB     = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

using PFNWGLCHOOSEPIXELFORMATARB = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using PFNWGLCREATECONTEXTATTRIBSARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLSWAPINTERVALEXT = BOOL(WINAPI*)(int);

constexpr wchar_t kDummyClass[] = L"DisplonDummyGL";

LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

template <typename Fn>
Fn LoadWgl(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(wglGetProcAddress(name)));
}

struct WglFunctions {
    PFNWGLCHOOSEPIXELFORMATARB    choosePixelFormat = nullptr;
    PFNWGLCREATECONTEXTATTRIBSARB createContextAttribs = nullptr;
    PFNWGLSWAPINTERVALEXT         swapInterval = nullptr;
};

// Creates a throw-away legacy context just to resolve the ARB entry points.
bool BootstrapWgl(WglFunctions& out, std::wstring& err) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DummyProc;
    wc.hInstance = inst;
    wc.lpszClassName = kDummyClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        err = L"RegisterClassEx failed for the dummy GL window";
        return false;
    }

    HWND hwnd = CreateWindowExW(0, kDummyClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) { err = L"CreateWindowEx failed for the dummy GL window"; return false; }
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    bool ok = false;
    int fmt = ChoosePixelFormat(dc, &pfd);
    if (fmt && SetPixelFormat(dc, fmt, &pfd)) {
        if (HGLRC rc = wglCreateContext(dc)) {
            if (wglMakeCurrent(dc, rc)) {
                out.choosePixelFormat = LoadWgl<PFNWGLCHOOSEPIXELFORMATARB>("wglChoosePixelFormatARB");
                out.createContextAttribs = LoadWgl<PFNWGLCREATECONTEXTATTRIBSARB>("wglCreateContextAttribsARB");
                out.swapInterval = LoadWgl<PFNWGLSWAPINTERVALEXT>("wglSwapIntervalEXT");
                ok = out.choosePixelFormat && out.createContextAttribs;
                if (!ok) err = L"Driver lacks WGL_ARB_pixel_format / WGL_ARB_create_context";
                wglMakeCurrent(nullptr, nullptr);
            } else {
                err = L"wglMakeCurrent failed on the dummy context";
            }
            wglDeleteContext(rc);
        } else {
            err = L"wglCreateContext failed (no OpenGL driver?)";
        }
    } else {
        err = L"Could not set a legacy pixel format on the dummy window";
    }

    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    UnregisterClassW(kDummyClass, inst);
    return ok;
}

}  // namespace

GLContext::~GLContext() { Destroy(); }

bool GLContext::Fail(std::wstring msg) {
    error_ = std::move(msg);
    Destroy();
    return false;
}

bool GLContext::Create(HWND hwnd) {
    assert(hwnd && "GLContext::Create needs a valid window");
    assert(!rc_ && "GLContext already created");
    hwnd_ = hwnd;

    WglFunctions wgl;
    std::wstring err;
    if (!BootstrapWgl(wgl, err)) return Fail(err);

    dc_ = GetDC(hwnd);
    if (!dc_) return Fail(L"GetDC failed on the app window");

    // No sRGB framebuffer attribute on purpose: Carbon hex tokens must reach the screen verbatim.
    const int pixelAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, 1,
        WGL_SUPPORT_OPENGL_ARB, 1,
        WGL_DOUBLE_BUFFER_ARB,  1,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     0,
        WGL_STENCIL_BITS_ARB,   0,
        0};
    int format = 0;
    UINT count = 0;
    if (!wgl.choosePixelFormat(dc_, pixelAttribs, nullptr, 1, &format, &count) || count == 0)
        return Fail(L"No suitable hardware-accelerated pixel format");

    PIXELFORMATDESCRIPTOR pfd{};
    DescribePixelFormat(dc_, format, sizeof(pfd), &pfd);
    if (!SetPixelFormat(dc_, format, &pfd)) return Fail(L"SetPixelFormat failed on the app window");

    const int ctxAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0};
    rc_ = wgl.createContextAttribs(dc_, nullptr, ctxAttribs);
    if (!rc_) return Fail(L"OpenGL 3.3 core context creation failed (driver too old, RDP session or VM?)");
    if (!wglMakeCurrent(dc_, rc_)) return Fail(L"wglMakeCurrent failed on the 3.3 core context");

    if (!gladLoaderLoadGL()) return Fail(L"glad could not load OpenGL 3.3 functions");
    if (wgl.swapInterval) wgl.swapInterval(1);  // vsync on
    return true;
}

void GLContext::Destroy() {
    if (rc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(rc_);
        rc_ = nullptr;
        gladLoaderUnloadGL();
    }
    if (dc_) {
        ReleaseDC(hwnd_, dc_);
        dc_ = nullptr;
    }
}

bool GLContext::MakeCurrent() const { return rc_ && wglMakeCurrent(dc_, rc_); }

void GLContext::Resize(int w, int h) {
    if (!rc_ || w <= 0 || h <= 0) return;
    MakeCurrent();
    glViewport(0, 0, w, h);
}

void GLContext::SwapBuffers() const {
    if (dc_) ::SwapBuffers(dc_);
}
