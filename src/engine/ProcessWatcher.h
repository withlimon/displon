// Process blocking watcher (plan Module B). Phase 2 backend = polling a process snapshot; an ETW
// process-start consumer can replace the polling later without touching the enforcer.
#pragma once
#include <functional>

#include "engine/ProcessEnforcer.h"
#include "engine/RuleWatcher.h"

class ProcessWatcher final : public RuleWatcher {
public:
    ProcessWatcher(IProcessOps& ops, ExemptionPolicy exempt,
                   std::chrono::milliseconds pollInterval = std::chrono::milliseconds(250));
    ~ProcessWatcher() override { Stop(); }

    void SetEventCallback(std::function<void(const ProcessEvent&)> cb);   // call before Start()
    void SetMinimizeOnly(bool on) { enforcer_.SetMinimizeOnly(on); }       // thread-safe

private:
    void ApplyRules(const std::vector<BlockRule>& rules) override;
    void PollOnce(SteadyTime now) override { enforcer_.Poll(now); }

    ProcessEnforcer enforcer_;
};
