#include "ui/SystemTheme.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool SystemPrefersDark() {
    // AppsUseLightTheme: 0 = dark, 1 = light (Win10 1809+).
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
        return true;  // default dark (black theme) if unknown
    DWORD value = 1, size = sizeof(value), type = 0;
    const LONG rc = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return true;
    return value == 0;
}
#else
bool SystemPrefersDark() { return true; }
#endif

carbon::Theme DetectSystemCarbonTheme() {
    return SystemPrefersDark() ? carbon::kG100 : carbon::kWhite;
}
