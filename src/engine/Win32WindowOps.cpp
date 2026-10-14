#include "engine/Win32WindowOps.h"

#include <windows.h>
#include <dwmapi.h>

#include <unordered_map>

#include "common/PathUtil.h"
#include "engine/Win32ProcessOps.h"

namespace {

struct EnumContext {
    std::vector<WindowInfo>* out;
    std::unordered_map<DWORD, std::wstring> ownerNames;   // per-call cache: pid -> exe name
};

BOOL CALLBACK OnWindow(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumContext*>(lp);
    if (!IsWindowVisible(hwnd)) return TRUE;

    DWORD cloaked = 0;   // DWMWA_CLOAKED: suspended UWP "ghost" windows are visible but not shown
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, 14, &cloaked, sizeof(cloaked))) && cloaked != 0) return TRUE;

    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    std::wstring title(static_cast<std::size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, title.data(), len + 1);
    if (got <= 0) return TRUE;
    title.resize(static_cast<std::size_t>(got));

    WindowInfo w;
    w.id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd));
    w.title = std::move(title);
    w.minimized = IsIconic(hwnd) != FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    w.pid = pid;
    auto it = ctx->ownerNames.find(pid);
    if (it == ctx->ownerNames.end())
        it = ctx->ownerNames.emplace(pid, FileNameOf(QueryImagePathByPid(pid))).first;
    w.ownerName = it->second;
    ctx->out->push_back(std::move(w));
    return TRUE;
}

HWND ToHwnd(std::uint64_t id) { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id)); }

}  // namespace

std::vector<WindowInfo> Win32WindowOps::Enumerate() {
    std::vector<WindowInfo> out;
    EnumContext ctx{&out, {}};
    EnumWindows(OnWindow, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

bool Win32WindowOps::Close(std::uint64_t id) {
    HWND h = ToHwnd(id);
    return IsWindow(h) && PostMessageW(h, WM_CLOSE, 0, 0) != 0;
}

bool Win32WindowOps::Minimize(std::uint64_t id) {
    HWND h = ToHwnd(id);
    return IsWindow(h) && ShowWindowAsync(h, SW_MINIMIZE) != 0;   // async: never hangs on a frozen app
}
