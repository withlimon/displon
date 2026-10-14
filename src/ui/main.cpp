#include <windows.h>

#include <cwchar>

#include "ui/AppWindow.h"
#include "ui/Autostart.h"
#include "ui/LocalEngine.h"
#include "ui/SplashScreen.h"

namespace {
constexpr wchar_t kSingleInstanceMutex[] = L"Local\\DisplonUI.SingleInstance";
constexpr wchar_t kMainWindowClass[] = L"DisplonMainWindow";   // keep in sync with AppWindow.cpp
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int nCmdShow) {
    // `--resume` = started by the sign-in task to put a surviving lock back (see ui/Autostart.h).
    const bool resume = std::wcsstr(GetCommandLineW(), L"--resume") != nullptr;

    // One instance only: two engines would both enforce and both pop notifications.
    HANDLE once = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!resume) {   // the user launched it again: bring the running window forward
            if (HWND w = FindWindowW(kMainWindowClass, nullptr)) {
                ShowWindow(w, SW_RESTORE);
                SetForegroundWindow(w);
            }
        }
        CloseHandle(once);
        return 0;
    }

    if (resume) {
        // Boots the engine, which reloads the persisted lock (with all its rules) and starts enforcing at once.
        if (!LocalEngine::Instance().GetStatus().locked) {
            Autostart::Remove();                 // nothing to resume (expired / "until restart" reached)
            LocalEngine::Instance().Shutdown();
            if (once) CloseHandle(once);
            return 0;
        }
        nCmdShow = SW_SHOWMINNOACTIVE;           // enforce quietly; do not steal focus at sign-in
    }

    // Splash only for a normal launch while NO block is running (the user is about to configure a new one).
    // Not for --resume (silent) and not when a lock is already active (go straight to the countdown).
    SplashScreen splash;
    int showCmd = nCmdShow;
    if (!resume && !LocalEngine::Instance().GetStatus().locked && splash.Show(inst))
        nCmdShow = SW_HIDE;   // build the real window hidden behind the splash; reveal it when the splash ends

    AppWindow window;
    if (!window.Create(inst, nCmdShow)) return 1;

    if (nCmdShow == SW_HIDE && showCmd != SW_HIDE) {
        splash.WaitUntilElapsed();   // total splash time = SplashScreen::kDefaultMs, init time included
        ShowWindow(window.Handle(), showCmd);
        SetForegroundWindow(window.Handle());
        splash.Close();
    }

    // Blocking message loop: the UI only wakes when there is something to do.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (once) CloseHandle(once);
    return static_cast<int>(msg.wParam);
}
