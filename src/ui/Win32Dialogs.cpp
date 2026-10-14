#include "ui/Win32Dialogs.h"

#include "common/Utf.h"

#include <fstream>
#include <set>
#include <cwctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>

namespace {

std::wstring PickFile(HWND owner, bool save, const COMDLG_FILTERSPEC* filters, UINT nFilters,
                      const wchar_t* defaultName) {
    std::wstring result;
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* dlg = nullptr;
    const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    const IID iid = save ? IID_IFileSaveDialog : IID_IFileOpenDialog;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid, reinterpret_cast<void**>(&dlg));
    if (FAILED(hr) || !dlg) {
        if (SUCCEEDED(hrCo)) CoUninitialize();
        return result;
    }
    dlg->SetFileTypes(nFilters, filters);
    dlg->SetFileTypeIndex(1);
    if (defaultName && save) dlg->SetFileName(defaultName);
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    hr = dlg->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (SUCCEEDED(hrCo)) CoUninitialize();
    return result;
}

}  // namespace

std::wstring PickExecutableFile(void* hwndOwner) {
    COMDLG_FILTERSPEC filters[] = {
        {L"Applications (*.exe)", L"*.exe"},
        {L"All files (*.*)", L"*.*"},
    };
    return PickFile(static_cast<HWND>(hwndOwner), false, filters, 2, nullptr);
}

std::wstring PickOpenTextFile(void* hwndOwner) {
    COMDLG_FILTERSPEC filters[] = {
        {L"Whitelist (*.txt;*.ftw)", L"*.txt;*.ftw"},
        {L"All files (*.*)", L"*.*"},
    };
    return PickFile(static_cast<HWND>(hwndOwner), false, filters, 2, nullptr);
}

std::wstring PickSaveTextFile(void* hwndOwner, const std::wstring& defaultName) {
    COMDLG_FILTERSPEC filters[] = {
        {L"Whitelist (*.txt)", L"*.txt"},
        {L"All files (*.*)", L"*.*"},
    };
    return PickFile(static_cast<HWND>(hwndOwner), true, filters, 2, defaultName.c_str());
}

namespace {

struct EnumCtx { std::set<DWORD> pids; };

BOOL CALLBACK CollectAppWindows(HWND h, LPARAM lp) {
    // Only what the user can actually SEE as an app window: visible, un-owned, titled, not a tool window,
    // and not a cloaked (hidden UWP / other-virtual-desktop) window.
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(h) == 0) return TRUE;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid) reinterpret_cast<EnumCtx*>(lp)->pids.insert(pid);
    return TRUE;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

}  // namespace

std::vector<RunningApp> EnumerateRunningApps(std::size_t maxCount) {
    std::vector<RunningApp> out;
    EnumCtx ctx;
    EnumWindows(CollectAppWindows, reinterpret_cast<LPARAM>(&ctx));

    wchar_t winDirBuf[MAX_PATH]{};
    const UINT wn = GetWindowsDirectoryW(winDirBuf, MAX_PATH);
    std::wstring winDir = Lower(std::wstring(winDirBuf, wn));
    if (!winDir.empty() && winDir.back() != L'\\') winDir += L'\\';

    wchar_t selfBuf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    const std::wstring self = Lower(selfBuf);

    std::set<std::wstring> seen;
    for (DWORD pid : ctx.pids) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2]{};
        DWORD n = static_cast<DWORD>(std::size(path));
        const bool ok = QueryFullProcessImageNameW(h, 0, path, &n) != FALSE;
        CloseHandle(h);
        if (!ok) continue;

        const std::wstring full(path, n);
        const std::wstring low = Lower(full);
        if (low == self) continue;                              // never offer Displon itself
        if (low.compare(0, winDir.size(), winDir) == 0) continue;  // Windows' own apps are always exempt
        if (!seen.insert(low).second) continue;                 // one row per exe

        RunningApp app;
        const auto slash = full.find_last_of(L"\\/");
        app.name = slash == std::wstring::npos ? full : full.substr(slash + 1);
        app.path = full;
        out.push_back(std::move(app));
        if (out.size() >= maxCount) break;
    }
    return out;
}
#endif  // _WIN32

bool ExportWhitelistFile(const std::wstring& path,
                         const std::vector<std::pair<std::wstring, std::wstring>>& namePathPairs) {
    const std::string narrow = WideToUtf8(path);
    std::ofstream out(narrow, std::ios::binary);
    if (!out) return false;
    out << "# Displon whitelist\n";
    for (const auto& [name, p] : namePathPairs) {
        const std::wstring& line = p.empty() ? name : p;
        out << WideToUtf8(line) << "\n";
    }
    return true;
}

bool ImportWhitelistFile(const std::wstring& path,
                         std::vector<std::pair<std::wstring, std::wstring>>& outNamePath) {
    const std::string narrow = WideToUtf8(path);
    std::ifstream in(narrow, std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::wstring w = Utf8ToWide(line);
        if (w.empty()) continue;
        // display name = filename portion
        std::wstring name = w;
        const auto slash = w.find_last_of(L"\\/");
        if (slash != std::wstring::npos) name = w.substr(slash + 1);
        outNamePath.emplace_back(name, w);
    }
    return true;
}
