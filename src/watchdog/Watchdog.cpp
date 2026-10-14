// DisplonWatchdog.exe — lightweight mutual-watch process.
// Watches the Service; the Service (via EngineHost health) watches the UI/tray.
// If the Service disappears, the watchdog attempts to restart it via SCM.
//
// Portable skeleton: the restart logic is Win32-only; the main loop structure
// is written so unit tests can drive a fake "is service alive?" check later.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#endif

namespace {

std::atomic<bool> gStop{false};

#ifdef _WIN32
bool IsServiceRunning(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    SERVICE_STATUS_PROCESS ssp{};
    DWORD needed = 0;
    bool running = false;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
        running = (ssp.dwCurrentState == SERVICE_RUNNING ||
                   ssp.dwCurrentState == SERVICE_START_PENDING);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return running;
}

bool StartServiceByName(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }
    const BOOL ok = StartServiceW(svc, 0, nullptr);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok != 0 || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}
#endif

#ifdef _WIN32
// --guard-ui <DisplonUI.exe>: while a lock is persisted on disk, make sure the UI (which hosts the in-process
// engine) is running; relaunch it with --resume if someone killed it. Exits once no lock file is left. The UI starts
// this process while locked and restarts it if it dies, so each guards the other.
constexpr wchar_t kUiMutex[] = L"Local\\DisplonUI.SingleInstance";
constexpr wchar_t kGuardMutex[] = L"Local\\DisplonWatchdog.Guard";

std::wstring Env(const wchar_t* n) {
    wchar_t b[1024]{};
    const DWORD len = GetEnvironmentVariableW(n, b, 1024);
    return (len > 0 && len < 1024) ? std::wstring(b, len) : std::wstring();
}

bool LockFileExists() {
    for (const wchar_t* var : {L"LOCALAPPDATA", L"PROGRAMDATA", L"APPDATA"}) {
        const std::wstring base = Env(var);
        if (base.empty()) continue;
        if (GetFileAttributesW((base + L"\\Displon\\lock.state").c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}

bool UiRunning() {
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, kUiMutex);
    if (!h) return false;
    CloseHandle(h);
    return true;
}

int GuardUi(const std::wstring& uiExe) {
    HANDLE once = CreateMutexW(nullptr, FALSE, kGuardMutex);
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;      // one guard is enough
    int absentChecks = 0;
    for (;;) {
        if (!LockFileExists()) {
            if (++absentChecks >= 5) break;                              // lock is over (or never was)
        } else {
            absentChecks = 0;
            if (!UiRunning()) {
                std::wstring cmd = L"\"" + uiExe + L"\" --resume";
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
                std::this_thread::sleep_for(std::chrono::seconds(8));   // give it time to take the mutex
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (once) CloseHandle(once);
    return 0;
}
#endif

void WatchLoop() {
    using namespace std::chrono_literals;
    int consecutiveMisses = 0;
    while (!gStop) {
#ifdef _WIN32
        if (!IsServiceRunning(L"Displon")) {
            ++consecutiveMisses;
            if (consecutiveMisses >= 3) {  // ~6 s of absence
                StartServiceByName(L"Displon");
                consecutiveMisses = 0;
            }
        } else {
            consecutiveMisses = 0;
        }
#endif
        std::this_thread::sleep_for(2s);
    }
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && std::wstring(argv[1]) == L"--guard-ui") return GuardUi(argv[2]);
    // Hide console window when launched from Run key / scheduled task.
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    WatchLoop();
    return 0;
}
#else
int main() {
    // On non-Windows hosts just exercise the loop for a few ticks then exit
    // (so CI / host tests can link the file).
    gStop = true;
    WatchLoop();
    return 0;
}
#endif
