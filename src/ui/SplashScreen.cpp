// NOTE: the current Displon splash screen (white tile with the black "D" logo, the name, "Acronym of Multitasking",
// the GitHub URL and the GPL v3 notice) is better than the previous one: it is cleaner, has far more contrast, shows
// the licence and project link, and matches the new logo instead of the old gradient-and-badge design.
#include "ui/SplashScreen.h"

#include <objidl.h>
#include <ocidl.h>
#include <olectl.h>

#include <algorithm>

namespace {
constexpr wchar_t kSplashClass[] = L"DisplonSplashWindow";
constexpr int     kSplashResourceId = 2;   // RCDATA, see DisplonUI.rc
constexpr int     kSplashWidthDip = 640;   // height follows the image's aspect ratio (1466 x 720)

// Decodes the embedded JPEG with OLE's IPicture (no extra library beyond oleaut32/ole32).
IPicture* LoadEmbeddedPicture(HINSTANCE inst) {
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(kSplashResourceId), RT_RCDATA);
    if (!res) return nullptr;
    const DWORD size = SizeofResource(inst, res);
    HGLOBAL loaded = LoadResource(inst, res);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || size == 0) return nullptr;

    // OleLoadPicture needs a stream over memory it may keep: copy into a movable HGLOBAL.
    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) return nullptr;
    void* dst = GlobalLock(copy);
    if (!dst) { GlobalFree(copy); return nullptr; }
    memcpy(dst, bytes, size);
    GlobalUnlock(copy);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(copy, TRUE /* stream frees the memory */, &stream)) || !stream) {
        GlobalFree(copy);
        return nullptr;
    }
    IPicture* pic = nullptr;
    const HRESULT hr = OleLoadPicture(stream, static_cast<LONG>(size), FALSE, IID_IPicture,
                                      reinterpret_cast<void**>(&pic));
    stream->Release();
    return SUCCEEDED(hr) ? pic : nullptr;
}
}  // namespace

LRESULT CALLBACK SplashScreen::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;   // the splash never takes clicks or focus
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT: {
        auto* self = reinterpret_cast<SplashScreen*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (self && self->picture_) {
            auto* pic = static_cast<IPicture*>(self->picture_);
            OLE_XSIZE_HIMETRIC hmW = 0;
            OLE_YSIZE_HIMETRIC hmH = 0;
            pic->get_Width(&hmW);
            pic->get_Height(&hmH);
            RECT rc{0, 0, self->width_, self->height_};
            // Standard IPicture idiom: source rect is bottom-up in HIMETRIC.
            pic->Render(dc, 0, 0, self->width_, self->height_, 0, hmH, hmW, -hmH, &rc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool SplashScreen::Show(HINSTANCE inst) {
    if (hwnd_) return true;

    IPicture* pic = LoadEmbeddedPicture(inst);
    if (!pic) return false;

    OLE_XSIZE_HIMETRIC hmW = 0;
    OLE_YSIZE_HIMETRIC hmH = 0;
    pic->get_Width(&hmW);
    pic->get_Height(&hmH);
    if (hmW <= 0 || hmH <= 0) { pic->Release(); return false; }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSplashClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { pic->Release(); return false; }

    // Centre on the monitor the mouse is on (same rule the main window uses), sized for that monitor's DPI.
    POINT cur{};
    GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor")))
            fn(mon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY);
        FreeLibrary(shcore);
    }
    width_ = MulDiv(kSplashWidthDip, static_cast<int>(dpiX), 96);
    height_ = static_cast<int>(static_cast<double>(width_) * hmH / hmW + 0.5);
    const int x = mi.rcWork.left + std::max(0, (int)(mi.rcWork.right - mi.rcWork.left - width_) / 2);
    const int y = mi.rcWork.top + std::max(0, (int)(mi.rcWork.bottom - mi.rcWork.top - height_) / 2);

    picture_ = pic;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kSplashClass, L"Displon",
                            WS_POPUP, x, y, width_, height_, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        pic->Release();
        picture_ = nullptr;
        return false;
    }
    shownAt_ = GetTickCount();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);   // paint now, before any slow start-up work blocks the message loop
    return true;
}

void SplashScreen::WaitUntilElapsed(DWORD ms) {
    if (!hwnd_) return;
    for (;;) {
        const DWORD elapsed = GetTickCount() - shownAt_;   // unsigned wrap-safe
        if (elapsed >= ms) break;
        // Sleep until the deadline OR any message arrives, then drain messages so the window stays responsive.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, ms - elapsed, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); return; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

void SplashScreen::Close() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (picture_) { static_cast<IPicture*>(picture_)->Release(); picture_ = nullptr; }
}
