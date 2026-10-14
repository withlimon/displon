#include "engine/ServiceGuard.h"

#include <cstdlib>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#endif

namespace ServiceGuard {

std::filesystem::path DefaultLockStatePath() {
#ifdef _WIN32
    // %LOCALAPPDATA%\Displon\lock.state
    // GetEnvironmentVariableW instead of _wdupenv_s: the latter is missing from llvm-mingw's static UCRT import lib.
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) {
        std::filesystem::path p(std::wstring(buf, n));
        p /= L"Displon";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        return p / L"lock.state";
    }
    return std::filesystem::temp_directory_path() / "displon-lock.state";
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home) / ".local/share/displon"
                                      : std::filesystem::temp_directory_path() / "displon";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base / "lock.state";
#endif
}

std::vector<std::filesystem::path> DefaultLockMirrorPaths() {
    std::vector<std::filesystem::path> out;
#ifdef _WIN32
    for (const wchar_t* var : {L"PROGRAMDATA", L"APPDATA"}) {
        wchar_t buf[MAX_PATH * 2]{};
        const DWORD n = GetEnvironmentVariableW(var, buf, static_cast<DWORD>(std::size(buf)));
        if (n == 0 || n >= std::size(buf)) continue;
        out.push_back(std::filesystem::path(std::wstring(buf, n)) / L"Displon" / L"lock.state");
    }
#endif
    return out;
}

FileLockStateStore MakeDefaultLockStore() {
    return FileLockStateStore(DefaultLockStatePath(), DefaultLockMirrorPaths());
}

#ifdef _WIN32
bool HardenServiceACL(void* scHandle) {
    if (!scHandle) return false;
    SC_HANDLE svc = static_cast<SC_HANDLE>(scHandle);

    // Build an SD that grants SERVICE_ALL_ACCESS only to SYSTEM and
    // Administrators; everyone else gets SERVICE_QUERY_STATUS only.
    // Using SDDL is the least error-prone path.
    PSECURITY_DESCRIPTOR sd = nullptr;
    // D: DACL
    //  (A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)  SYSTEM full
    //  (A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)  Administrators full
    //  (A;;RPLCLORC;;;IU)                   Interactive users: query + enumerate
    //  (A;;RPLCLORC;;;SU)                   Service accounts: query
    const wchar_t* sddl =
        L"D:"
        L"(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;SY)"
        L"(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"
        L"(A;;RPLCLORC;;;IU)"
        L"(A;;RPLCLORC;;;SU)";

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
        return false;
    }
    const BOOL ok = SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    return ok != 0;
}

bool EnsureRunKey(const std::wstring& exePath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LONG r = RegSetValueExW(key, L"DisplonWatchdog", 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(exePath.c_str()),
                                  static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool EnsureScheduledTask(const std::wstring& /*exePath*/) {
    // Full task registration needs the Task Scheduler COM API. Stub returns
    // true for now; the real implementation lands when the installer is built.
    return true;
}
#endif

}  // namespace ServiceGuard
