// Poll -> evaluate -> close/minimize loop body for window-title rules. OS-independent.
#pragma once
#include <functional>
#include <map>
#include <vector>

#include "engine/ProcessEnforcer.h"   // SteadyTime
#include "engine/WindowMatcher.h"

class IWindowOps {
public:
    virtual ~IWindowOps() = default;
    virtual std::vector<WindowInfo> Enumerate() = 0;   // visible top-level windows with a title
    virtual bool Close(std::uint64_t id) = 0;          // polite: WM_CLOSE (app may ask "save?")
    virtual bool Minimize(std::uint64_t id) = 0;
};

struct WindowEvent {
    WindowInfo         window;
    WindowTitleAction  action = WindowTitleAction::Close;
    int                attempt = 0;      // 1-based; Suppress counts every re-minimize
    bool               ok = false;
    bool               gaveUp = false;   // Close only: window ignored WM_CLOSE kMaxCloseAttempts times
};

class WindowEnforcer {
public:
    static constexpr int kMaxCloseAttempts = 20;
    static std::chrono::milliseconds CloseBackoff(int attemptsSoFar);   // 500 ms doubling, capped at 5 s

    WindowEnforcer(IWindowOps& ops, ExemptionPolicy exempt) : ops_(ops), exempt_(std::move(exempt)) {}

    void SetRules(const std::vector<BlockRule>& activeRules);   // keeps WindowTitle rules; empty = idle
    void SetEventCallback(std::function<void(const WindowEvent&)> cb) { onEvent_ = std::move(cb); }
    void Poll(SteadyTime now);

private:
    struct State { int attempts = 0; SteadyTime next{}; bool gaveUp = false; };

    IWindowOps& ops_;
    ExemptionPolicy exempt_;
    std::vector<BlockRule> rules_;
    std::map<std::uint64_t, State> states_;
    std::function<void(const WindowEvent&)> onEvent_;
};
