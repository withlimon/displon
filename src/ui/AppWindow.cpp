#include "ui/AppWindow.h"
#include "ui/LocalEngine.h"
#include "ui/Notify.h"
#include <algorithm>
#include <memory>
#include <string>

#include <glad/gl.h>
#include <dwmapi.h>

#include "ui/Theme.h"
#include "ui/SystemTheme.h"

namespace {
constexpr UINT WM_APP_BLOCKED = WM_APP + 1;
constexpr wchar_t kClassName[] = L"DisplonMainWindow";
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr int kDefaultWidthDip = 960;
constexpr int kDefaultHeightDip = 640;
constexpr UINT kCountdownTimerId = 1;
}  // namespace

bool AppWindow::Create(HINSTANCE inst, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    {   // open centred on the screen the mouse is on, instead of wherever Windows cascades it
        POINT cur{};
        GetCursorPos(&cur);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY), &mi);
        startX_ = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - kDefaultWidthDip) / 2;
        startY_ = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - kDefaultHeightDip) / 2;
    }
    hwnd_ = CreateWindowExW(0, kClassName, L"Displon", WS_OVERLAPPEDWINDOW,
                            startX_, startY_, kDefaultWidthDip, kDefaultHeightDip,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    const UINT dpi = GetDpiForWindow(hwnd_);
    dpiScale_ = dpi / 96.f;
    RECT r{0, 0, MulDiv(kDefaultWidthDip, dpi, 96), MulDiv(kDefaultHeightDip, dpi, 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    {   // size for the real DPI, keeping the window centred on that monitor
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        const int ww = r.right - r.left, wh = r.bottom - r.top;
        const int x = mi.rcWork.left + std::max(0, (int)(mi.rcWork.right - mi.rcWork.left - ww) / 2);
        const int y = mi.rcWork.top + std::max(0, (int)(mi.rcWork.bottom - mi.rcWork.top - wh) / 2);
        SetWindowPos(hwnd_, nullptr, x, y, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));

    if (!gl_.Create(hwnd_)) {
        MessageBoxW(hwnd_, gl_.LastError().c_str(), L"Displon - OpenGL error", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    if (!renderer_.Init()) {
        MessageBoxW(hwnd_, L"Renderer failed to init", L"Displon", MB_OK | MB_ICONERROR);
        return false;
    }
    EnsureTextRenderer();
    {
        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        std::wstring dir(modulePath);
        const auto slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash + 1);
        icons_.Init(dir + L"assets\\icons\\", 32);
        if (!icons_.Loaded(IconId::Gear))
            icons_.Init(L"assets/icons/", 32);
    }
    wizard_.Init(text_);
    wizard_.SetOwnerHwnd(hwnd_);
    wizard_.SetTheme(DetectSystemCarbonTheme());
    wizard_.SetIcons(&icons_);
    wizard_.SetDpiScale(dpiScale_);
    NotifyInit(hwnd_, 1);
    LocalEngine::Instance().SetBlockedCallback([h = hwnd_](const LocalEngine::BlockedApp& app) {
        // Worker thread -> UI thread. Ownership of the heap copy passes to the message.
        PostMessageW(h, WM_APP_BLOCKED, 0, reinterpret_cast<LPARAM>(new LocalEngine::BlockedApp(app)));
    });
    if (wizard_.WantsCountdownTimer() && !countdownTimer_)
        countdownTimer_ = SetTimer(hwnd_, kCountdownTimerId, 1000, nullptr);

    RECT client;
    GetClientRect(hwnd_, &client);
    gl_.Resize(client.right, client.bottom);
    wizard_.Resize(client.right, client.bottom);

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

void AppWindow::EnsureTextRenderer() {
    if (textReady_) return;
    // IBM Plex Sans (Carbon typeface). Integer-DPI atlases + NEAREST filter = sharp text.
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring dir(modulePath);
    const auto slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash + 1);

    auto tryPath = [&](const std::wstring& w) -> bool {
        std::string narrow;
        narrow.reserve(w.size());
        for (wchar_t c : w) narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
        return text_.Init(narrow.c_str(), dpiScale_);
    };

    const std::wstring candidates[] = {
        dir + L"assets\\fonts\\IBMPlexSans-Regular.ttf",
        dir + L"assets/fonts/IBMPlexSans-Regular.ttf",
        L"assets\\fonts\\IBMPlexSans-Regular.ttf",
        L"assets/fonts/IBMPlexSans-Regular.ttf",
        L"C:\\Windows\\Fonts\\segoeui.ttf",
        L"C:\\Windows\\Fonts\\arial.ttf",
    };
    for (const auto& path : candidates) {
        if (tryPath(path)) {
            textReady_ = true;
            return;
        }
    }
    textReady_ = true;
}

LRESULT CALLBACK AppWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->HandleMessage(msg, wp, lp);
}

LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        Render();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE: {
        const int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0) {
            gl_.Resize(w, h);
            wizard_.Resize(w, h);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        dpiScale_ = HIWORD(wp) / 96.f;
        text_.SetDpiScale(dpiScale_);
        wizard_.SetDpiScale(dpiScale_);
        RECT* suggested = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        wizard_.OnPointer(PointerState::Hover, static_cast<float>(LOWORD(lp)), static_cast<float>(HIWORD(lp)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        wizard_.OnPointer(PointerState::Leave, -1, -1);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        wizard_.OnPointer(PointerState::Down, static_cast<float>(LOWORD(lp)), static_cast<float>(HIWORD(lp)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        wizard_.OnPointer(PointerState::Up, static_cast<float>(LOWORD(lp)), static_cast<float>(HIWORD(lp)));
        // Start 1 Hz timer when entering countdown
        if (wizard_.WantsCountdownTimer() && !countdownTimer_) {
            countdownTimer_ = SetTimer(hwnd_, kCountdownTimerId, 1000, nullptr);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_APP_BLOCKED: {
        std::unique_ptr<LocalEngine::BlockedApp> app(reinterpret_cast<LocalEngine::BlockedApp*>(lp));
        // Own always-on-top popup (not a Windows toast): it still shows with Focus Assist on and while the
        // main window is minimized.
        if (app && wizard_.OnBlockedApp(app->name))
            ShowToast(wizard_.BuildBlockedToast(app->name, app->imagePath, app->onBlockList));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_GETMINMAXINFO: {   // the layouts are designed for >= 880x600 dip; below that they collide
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const UINT dpi = GetDpiForWindow(hwnd_);
        RECT r{0, 0, MulDiv(880, dpi, 96), MulDiv(600, dpi, 96)};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_MOUSEWHEEL:
        wizard_.OnWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        // Once a session is locked the window cannot be closed (Alt+F4 / X): minimize instead.
        if (wizard_.IsLocked()) {
            ShowWindow(hwnd_, SW_MINIMIZE);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == kCountdownTimerId) {
            wizard_.Tick();   // notices the end of the block (toast, window becomes closable again)
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_SETTINGCHANGE:
        if (lp && (std::wcscmp(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet") == 0 ||
                   std::wcscmp(reinterpret_cast<LPCWSTR>(lp), L"WindowsThemeElement") == 0)) {
            wizard_.SetTheme(DetectSystemCarbonTheme());
            const BOOL dark = SystemPrefersDark() ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        if (countdownTimer_) KillTimer(hwnd_, countdownTimer_);
        LocalEngine::Instance().SetBlockedCallback({});
        NotifyShutdown();
        LocalEngine::Instance().Shutdown();   // stops watchers; a persisted lock resumes on next launch
        renderer_.Shutdown();
        text_.Shutdown();
        icons_.Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void AppWindow::Render() {
    if (!gl_.MakeCurrent()) return;
    wizard_.Draw(renderer_, text_);
    gl_.SwapBuffers();
}
