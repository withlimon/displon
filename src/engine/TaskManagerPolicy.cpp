#include "engine/TaskManagerPolicy.h"

#include <windows.h>

#include "common/UniqueHandle.h"

namespace {

constexpr wchar_t kKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
constexpr wchar_t kPolicyValue[] = L"DisableTaskMgr";
constexpr wchar_t kMarkerValue[] = L"DisplonDisabledTaskMgr";   // 1 = no prior value, 2 = prior value was 0

bool ReadDword(HKEY key, const wchar_t* name, DWORD& out) {
    DWORD type = 0, size = sizeof(DWORD), v = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&v), &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
        return false;
    out = v;
    return true;
}
bool WriteDword(HKEY key, const wchar_t* name, DWORD v) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) == ERROR_SUCCESS;
}
bool DeleteValue(HKEY key, const wchar_t* name) {
    const LONG rc = RegDeleteValueW(key, name);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}
bool OpenKey(UniqueRegKey& key) {
    return RegCreateKeyExW(HKEY_CURRENT_USER, kKeyPath, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
                           key.Put(), nullptr) == ERROR_SUCCESS;
}

}  // namespace

bool IsTaskManagerPolicyOurs() {
    UniqueRegKey key;
    DWORD marker = 0;
    return OpenKey(key) && ReadDword(key.Get(), kMarkerValue, marker);
}

bool SetTaskManagerPolicy(bool disabled) {
    UniqueRegKey key;
    if (!OpenKey(key)) return false;

    DWORD marker = 0, current = 0;
    const bool haveMarker = ReadDword(key.Get(), kMarkerValue, marker);
    const bool haveValue = ReadDword(key.Get(), kPolicyValue, current);

    if (disabled) {
        if (haveMarker) return WriteDword(key.Get(), kPolicyValue, 1);   // already ours; make sure it is on
        if (haveValue && current != 0) return true;                      // an admin already disabled it: not ours
        if (!WriteDword(key.Get(), kMarkerValue, haveValue ? 2 : 1)) return false;  // marker FIRST (crash-safe)
        return WriteDword(key.Get(), kPolicyValue, 1);
    }
    if (!haveMarker) return true;                                        // nothing of ours to undo
    const bool restored = marker == 2 ? WriteDword(key.Get(), kPolicyValue, 0) : DeleteValue(key.Get(), kPolicyValue);
    return restored && DeleteValue(key.Get(), kMarkerValue);
}
