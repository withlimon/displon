#pragma once
#include <string>

// Modal message box (rarely wanted - blocks the caller). No-op on non-Windows.
void NotifyUser(const std::wstring& title, const std::wstring& body);

// Non-blocking Windows toast/balloon from a tray icon owned by `hwnd`. Call from the UI thread only.
// (Windows can swallow these - Focus Assist, "Do not disturb", per-app settings - so anything the user MUST see,
// like "this app is blocked by the lock", uses ShowToast below instead.)
void NotifyInit(void* hwnd, unsigned iconResourceId);
void NotifyBalloon(const std::wstring& title, const std::wstring& body);
void NotifyShutdown();

// Displon's own always-on-top popup, bottom-right of the screen the mouse is on. It never takes keyboard
// focus, fades in/out, closes itself after ~5 s (or on click) and ignores Windows notification settings.
// Calling it again while one is showing replaces the text and restarts the timer. UI thread only.
struct ToastInfo {
    std::wstring title;       // "App blocked"
    std::wstring message;     // "chrome.exe is blocked by the lock."
    std::wstring detail;      // small grey line: reason, time left
    std::wstring imagePath;   // optional exe whose icon is shown; empty / unreadable -> a "blocked" glyph
    bool dark = true;         // follow the app theme
    bool danger = true;       // red accent (blocked); false -> cream/black accent (information)
};
void ShowToast(const ToastInfo& info);
void ToastShutdown();
