#pragma once
#include <functional>

#include "engine/RuleWatcher.h"
#include "engine/WindowEnforcer.h"

// ~250 ms poll of visible windows against WindowTitle rules (always on, with or without an allow-list).
class WindowTitleWatcher final : public RuleWatcher {
public:
    WindowTitleWatcher(IWindowOps& ops, ExemptionPolicy exempt,
                       std::chrono::milliseconds pollInterval = std::chrono::milliseconds(250));
    ~WindowTitleWatcher() override { Stop(); }

    void SetEventCallback(std::function<void(const WindowEvent&)> cb);   // call before Start()

private:
    void ApplyRules(const std::vector<BlockRule>& rules) override { enforcer_.SetRules(rules); }
    void PollOnce(SteadyTime now) override { enforcer_.Poll(now); }

    WindowEnforcer enforcer_;
};
