// In-process enforcement engine for the UI (Windows only).
//
// Why this exists: the UI used to talk ONLY to DisplonService over a named pipe. When the service was not
// installed/running (the normal case for a fresh build) the UI silently fell back to a fake countdown and
// nothing was ever blocked. LocalEngine owns a real EngineHost inside the UI process, so a lock always
// enforces. If a service IS reachable, WizardApp still prefers it.
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "engine/BlockRule.h"

#ifdef _WIN32
class LocalEngine {
public:
    struct Result {
        bool        ok = false;
        std::wstring detail;
        std::optional<TimePoint> unlockTime;
    };
    struct Status {
        bool locked = false;
        std::optional<TimePoint> unlockTime;
        std::optional<SessionConfig> config;   // the running (or just-resumed) session, for the countdown view
    };
    // One app the engine just closed because of the lock.
    struct BlockedApp {
        std::wstring name;        // exe file name, e.g. "chrome.exe"
        std::wstring imagePath;   // full path when Windows let us read it (for the toast icon), else empty
        bool onBlockList = false; // true: explicit deny rule; false: simply not on the whitelist
    };

    static LocalEngine& Instance();

    // Boots the engine once (resumes a persisted lock, e.g. after the UI was killed or after a reboot).
    void EnsureStarted();
    Result StartSession(const SessionConfig& cfg, const std::wstring& password);
    Status GetStatus();
    // Called when the process exits: stop watchers but leave the persisted lock for the next launch.
    void Shutdown();
    // Fired (on a worker thread!) when the engine closes an app the user tried to open.
    void SetBlockedCallback(std::function<void(const BlockedApp&)> cb);

private:
    LocalEngine();
    ~LocalEngine();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
