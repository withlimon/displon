// Short start-up splash (Windows only).
//
// Shown for ~1.2 s when DisplonUI.exe is opened by the user while NO block is running (i.e. the user is about
// to configure a new one). It is NOT shown for `--resume` (silent sign-in enforcement) or when a lock is already
// active (the countdown view opens straight away).
//
// The JPEG is embedded in the exe as RCDATA (see DisplonUI.rc), so there is no file to lose next to the binary.
#pragma once
#include <windows.h>

class SplashScreen {
public:
    static constexpr DWORD kDefaultMs = 1200;   // 1.0 - 1.5 s is the intended range

    ~SplashScreen() { Close(); }

    // Creates and paints the splash immediately. Returns false (and shows nothing) if the image cannot be loaded;
    // the caller must then simply carry on without a splash.
    bool Show(HINSTANCE inst);

    // Keeps the splash up until `ms` have passed since Show(), pumping messages meanwhile. Call this AFTER the
    // expensive start-up work so the splash is never shown longer than needed.
    void WaitUntilElapsed(DWORD ms = kDefaultMs);

    void Close();

private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);

    HWND   hwnd_ = nullptr;
    void*  picture_ = nullptr;   // IPicture*, kept opaque so this header needs no OLE includes
    DWORD  shownAt_ = 0;
    int    width_ = 0, height_ = 0;
};
