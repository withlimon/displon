#include "ui/Notify.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>

namespace {
NOTIFYICONDATAW g_nid{};
bool g_added = false;
}  // namespace

void NotifyUser(const std::wstring& title, const std::wstring& body) {
    MessageBoxW(nullptr, body.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

void NotifyInit(void* hwnd, unsigned iconResourceId) {
    if (g_added) return;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = static_cast<HWND>(hwnd);
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(iconResourceId));
    wcsncpy(g_nid.szTip, L"Displon", ARRAYSIZE(g_nid.szTip) - 1);
    g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

void NotifyBalloon(const std::wstring& title, const std::wstring& body) {
    if (!g_added) return;
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_WARNING;
    wcsncpy(g_nid.szInfoTitle, title.c_str(), ARRAYSIZE(g_nid.szInfoTitle) - 1);
    wcsncpy(g_nid.szInfo, body.c_str(), ARRAYSIZE(g_nid.szInfo) - 1);
    g_nid.szInfoTitle[ARRAYSIZE(g_nid.szInfoTitle) - 1] = 0;
    g_nid.szInfo[ARRAYSIZE(g_nid.szInfo) - 1] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void NotifyShutdown() {
    ToastShutdown();
    if (!g_added) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_added = false;
}

// ---------------------------------------------------------------------------------------------------------
// Toast window (plain GDI: it must work even when the OpenGL window is minimized or hidden)
// ---------------------------------------------------------------------------------------------------------
namespace {

constexpr wchar_t kToastClass[] = L"DisplonToast";
constexpr UINT_PTR kToastTimer = 1;
constexpr DWORD kShowMs = 5500, kFadeInMs = 140, kFadeOutMs = 350;
constexpr int kWidthDip = 384, kHeightDip = 92;

// Black + cream tokens (aligned with ui/Theme.h). Cream = #FDFCE8.
struct Palette {
    COLORREF surface, border, text, textSecondary, accent, danger;
};
constexpr COLORREF Hex(unsigned rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
constexpr Palette kDark{Hex(0x1A1A1A), Hex(0x333333), Hex(0xFDFCE8), Hex(0xC8C6A8), Hex(0xFDFCE8), Hex(0xFA4D56)};
constexpr Palette kLight{Hex(0xFDFCE8), Hex(0xD4D2B0), Hex(0x0D0D0D), Hex(0x4A4A3A), Hex(0x0D0D0D), Hex(0xDA1E28)};

struct Toast {
    HWND hwnd = nullptr;
    ToastInfo info;
    HICON icon = nullptr;
    ULONGLONG t0 = 0;
    int dpi = 96;
    HFONT fontTitle = nullptr, fontBody = nullptr, fontSmall = nullptr;
} g_toast;

bool g_classRegistered = false;
bool g_fontsProbed = false;
bool g_havePlex = false;

int Px(int dip) { return MulDiv(dip, g_toast.dpi, 96); }

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(ARRAYSIZE(buf)));
    std::wstring p(buf, n);
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
}

// IBM Plex Sans is bundled next to the exe (same face as the main window). Private = not installed system-wide.
void ProbeFonts() {
    if (g_fontsProbed) return;
    g_fontsProbed = true;
    const std::wstring dir = ExeDir();
    int added = 0;
    for (const wchar_t* f : {L"assets\\fonts\\IBMPlexSans-Regular.ttf", L"assets\\fonts\\IBMPlexSans-Bold.ttf"})
        added += AddFontResourceExW((dir + f).c_str(), FR_PRIVATE, nullptr);
    g_havePlex = added >= 2;
}

HFONT MakeFont(int px, int weight) {
    return CreateFontW(-MulDiv(px, g_toast.dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       g_havePlex ? L"IBM Plex Sans" : L"Segoe UI");
}

void FreeFonts() {
    for (HFONT* f : {&g_toast.fontTitle, &g_toast.fontBody, &g_toast.fontSmall}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

void BuildFonts() {
    FreeFonts();
    g_toast.fontTitle = MakeFont(14, FW_BOLD);
    g_toast.fontBody = MakeFont(14, FW_NORMAL);
    g_toast.fontSmall = MakeFont(12, FW_NORMAL);
}

void Fill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    SetDCBrushColor(dc, c);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    PatBlt(dc, x, y, w, h, PATCOPY);
}

void Text(HDC dc, const std::wstring& s, HFONT font, COLORREF c, int x, int y, int w, int h) {
    SelectObject(dc, font);
    SetTextColor(dc, c);
    RECT r{x, y, x + w, y + h};
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()),
              &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void PaintToast(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetBkMode(mem, TRANSPARENT);

    const Palette& p = g_toast.info.dark ? kDark : kLight;
    const COLORREF accent = g_toast.info.danger ? p.danger : p.accent;

    Fill(mem, 0, 0, W, H, p.border);                             // 1 px border
    Fill(mem, 1, 1, W - 2, H - 2, p.surface);
    Fill(mem, 0, 0, Px(4), H, accent);                           // left status bar (notification style)

    const int pad = Px(16);
    const int iconSize = Px(32);
    const int ix = Px(4) + pad, iy = pad;
    if (g_toast.icon) {
        DrawIconEx(mem, ix, iy, g_toast.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    } else {   // "blocked" glyph: ring with a slash
        HPEN pen = CreatePen(PS_SOLID, std::max(2, Px(3)), accent);
        HGDIOBJ oldPen = SelectObject(mem, pen);
        HGDIOBJ oldBrush = SelectObject(mem, GetStockObject(NULL_BRUSH));
        const int m = std::max(2, Px(2));
        Ellipse(mem, ix + m, iy + m, ix + iconSize - m, iy + iconSize - m);
        MoveToEx(mem, ix + iconSize * 22 / 100, iy + iconSize * 78 / 100, nullptr);
        LineTo(mem, ix + iconSize * 78 / 100, iy + iconSize * 22 / 100);
        SelectObject(mem, oldBrush);
        SelectObject(mem, oldPen);
        DeleteObject(pen);
    }

    const int tx = ix + iconSize + Px(14);
    const int tw = W - tx - pad;
    int y = pad - Px(2);
    Text(mem, g_toast.info.title, g_toast.fontTitle, p.text, tx, y, tw, Px(20));
    y += Px(22);
    Text(mem, g_toast.info.message, g_toast.fontBody, p.text, tx, y, tw, Px(20));
    y += Px(22);
    Text(mem, g_toast.info.detail, g_toast.fontSmall, p.textSecondary, tx, y, tw, Px(18));

    // Remaining-lifetime sliver along the bottom edge.
    const ULONGLONG el = GetTickCount64() - g_toast.t0;
    const double left = 1.0 - std::min<double>(1.0, static_cast<double>(el) / kShowMs);
    Fill(mem, 1, H - Px(3), static_cast<int>((W - 2) * left), Px(3) - 1, accent);

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void Place(HWND hwnd) {
    POINT cur{};
    GetCursorPos(&cur);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int w = Px(kWidthDip), h = Px(kHeightDip), margin = Px(16);
    SetWindowPos(hwnd, HWND_TOPMOST, mi.rcWork.right - w - margin, mi.rcWork.bottom - h - margin, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintToast(hwnd);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        DestroyWindow(hwnd);
        return 0;
    case WM_TIMER: {
        const ULONGLONG el = GetTickCount64() - g_toast.t0;
        if (el >= kShowMs) {
            DestroyWindow(hwnd);
            return 0;
        }
        double a = 1.0;
        if (el < kFadeInMs) a = static_cast<double>(el) / kFadeInMs;
        else if (el > kShowMs - kFadeOutMs) a = static_cast<double>(kShowMs - el) / kFadeOutMs;
        SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(std::clamp(a, 0.0, 1.0) * 255.0), LWA_ALPHA);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kToastTimer);
        FreeFonts();
        if (g_toast.icon) DestroyIcon(g_toast.icon);
        g_toast.icon = nullptr;
        g_toast.hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HICON IconOf(const std::wstring& path) {
    if (path.empty()) return nullptr;
    HICON large = nullptr;
    if (ExtractIconExW(path.c_str(), 0, &large, nullptr, 1) < 1) return nullptr;
    return large;
}

}  // namespace

void ShowToast(const ToastInfo& info) {
    ProbeFonts();
    if (!g_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ToastProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = kToastClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        g_classRegistered = true;
    }

    if (g_toast.icon) DestroyIcon(g_toast.icon);
    g_toast.icon = IconOf(info.imagePath);
    g_toast.info = info;
    g_toast.t0 = GetTickCount64();

    if (!g_toast.hwnd) {
        POINT cur{};
        GetCursorPos(&cur);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY), &mi);
        // Created on the target monitor so it picks up THAT monitor's DPI; sized and shown by Place().
        g_toast.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                       kToastClass, L"Displon", WS_POPUP, mi.rcWork.left, mi.rcWork.top, 10, 10,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!g_toast.hwnd) return;
        const UINT dpi = GetDpiForWindow(g_toast.hwnd);
        g_toast.dpi = dpi ? static_cast<int>(dpi) : 96;
        const DWORD kDoNotRound = 1;   // DWMWCP_DONOTROUND: keep Carbon's square corners on Windows 11
        DwmSetWindowAttribute(g_toast.hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &kDoNotRound, sizeof(kDoNotRound));
        SetLayeredWindowAttributes(g_toast.hwnd, 0, 0, LWA_ALPHA);
        BuildFonts();
        SetTimer(g_toast.hwnd, kToastTimer, 16, nullptr);
    }
    Place(g_toast.hwnd);
    InvalidateRect(g_toast.hwnd, nullptr, FALSE);
}

void ToastShutdown() {
    if (g_toast.hwnd) DestroyWindow(g_toast.hwnd);
}

#else
void NotifyUser(const std::wstring&, const std::wstring&) {}
void NotifyInit(void*, unsigned) {}
void NotifyBalloon(const std::wstring&, const std::wstring&) {}
void NotifyShutdown() {}
void ShowToast(const ToastInfo&) {}
void ToastShutdown() {}
#endif
