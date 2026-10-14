#include "ui/Autostart.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kTaskName[] = L"Displon Resume";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"DisplonUI";
constexpr DWORD kToolTimeoutMs = 8000;

std::wstring Env(const wchar_t* name) {
    wchar_t buf[512]{};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    return (n > 0 && n < sizeof(buf) / sizeof(buf[0])) ? std::wstring(buf, n) : std::wstring();
}

std::wstring OwnExe() {
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    return n ? std::wstring(buf, n) : std::wstring();
}

std::wstring SystemDir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring(L"C:\\Windows\\System32");
}

std::wstring XmlEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        switch (c) {
        case L'&': o += L"&amp;"; break;
        case L'<': o += L"&lt;"; break;
        case L'>': o += L"&gt;"; break;
        case L'"': o += L"&quot;"; break;
        case L'\'': o += L"&apos;"; break;
        default: o += c;
        }
    }
    return o;
}

// Runs schtasks.exe hidden and returns its exit code (-1 if it could not be started / timed out).
int RunSchtasks(const std::wstring& args) {
    std::wstring cmd = L"\"" + SystemDir() + L"\\schtasks.exe\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    int code = -1;
    if (WaitForSingleObject(pi.hProcess, kToolTimeoutMs) == WAIT_OBJECT_0) {
        DWORD ec = 1;
        GetExitCodeProcess(pi.hProcess, &ec);
        code = static_cast<int>(ec);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

// Task Scheduler XML. Importing XML (instead of `schtasks /Create /SC ONLOGON ...`) is what lets us turn off the
// "don't start on battery" / "stop on battery" defaults that would otherwise skip the task on a laptop.
std::wstring TaskXml(const std::wstring& exe) {
    std::wstring user = Env(L"USERNAME");
    const std::wstring domain = Env(L"USERDOMAIN");
    if (!domain.empty() && !user.empty()) user = domain + L"\\" + user;
    const std::wstring u = XmlEscape(user);
    std::wstring x;
    x += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n";
    x += L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n";
    x += L"  <RegistrationInfo><Description>Displon: puts an active lock back after sign-in.</Description></RegistrationInfo>\r\n";
    x += L"  <Triggers><LogonTrigger><Enabled>true</Enabled>";
    if (!u.empty()) x += L"<UserId>" + u + L"</UserId>";
    x += L"</LogonTrigger></Triggers>\r\n";
    x += L"  <Principals><Principal id=\"Author\">";
    if (!u.empty()) x += L"<UserId>" + u + L"</UserId>";
    x += L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n";
    x += L"  <Settings>\r\n";
    x += L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n";
    x += L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n";
    x += L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n";
    x += L"    <AllowHardTerminate>false</AllowHardTerminate>\r\n";
    x += L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n";
    x += L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n";
    x += L"    <Enabled>true</Enabled>\r\n";
    x += L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n";
    x += L"    <Priority>4</Priority>\r\n";
    x += L"  </Settings>\r\n";
    x += L"  <Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exe) + L"</Command><Arguments>--resume</Arguments></Exec></Actions>\r\n";
    x += L"</Task>\r\n";
    return x;
}

bool WriteUtf16File(const std::wstring& path, const std::wstring& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const WORD bom = 0xFEFF;
    DWORD w = 0;
    bool ok = WriteFile(h, &bom, sizeof(bom), &w, nullptr) != 0;
    ok = ok && WriteFile(h, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &w, nullptr) != 0;
    CloseHandle(h);
    return ok;
}

bool EnsureTask(const std::wstring& exe) {
    wchar_t tmpDir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tmpDir)) return false;
    const std::wstring xmlPath = std::wstring(tmpDir) + L"displon-resume-task.xml";
    if (!WriteUtf16File(xmlPath, TaskXml(exe))) return false;
    const int rc = RunSchtasks(L"/Create /TN \"" + std::wstring(kTaskName) + L"\" /XML \"" + xmlPath + L"\" /F");
    DeleteFileW(xmlPath.c_str());
    return rc == 0;
}

bool EnsureRunKeyFallback(const std::wstring& exe) {
    const std::wstring cmd = L"\"" + exe + L"\" --resume";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const LONG r = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                                  static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

void RemoveRunKeyFallback() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
    }
}

}  // namespace

namespace Autostart {

bool Ensure() {
    const std::wstring exe = OwnExe();
    if (exe.empty()) return false;
    if (EnsureTask(exe)) return true;
    return EnsureRunKeyFallback(exe);   // only works if the exe is not elevated, but costs nothing to try
}

void EnsureGuard() {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\DisplonWatchdog.Guard");
    if (h) {
        CloseHandle(h);
        return;
    }
    const std::wstring exe = OwnExe();
    const auto slash = exe.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    const std::wstring dog = exe.substr(0, slash + 1) + L"DisplonWatchdog.exe";
    if (GetFileAttributesW(dog.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring cmd = L"\"" + dog + L"\" --guard-ui \"" + exe + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr,
                       &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void Remove() {
    RunSchtasks(L"/Delete /TN \"" + std::wstring(kTaskName) + L"\" /F");
    RemoveRunKeyFallback();
}

}  // namespace Autostart

#else
namespace Autostart {
bool Ensure() { return false; }
void EnsureGuard() {}
void Remove() {}
}  // namespace Autostart
#endif
