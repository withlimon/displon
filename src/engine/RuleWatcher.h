// Shared polling skeleton for the rule watchers (plan Section 3.1: one loop, subclasses
// override only the machine-specific parts). Owns the worker thread and the rule hand-off.
#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "engine/BlockRule.h"
#include "engine/ProcessEnforcer.h"   // SteadyTime

class RuleWatcher {
public:
    explicit RuleWatcher(std::chrono::milliseconds pollInterval) : interval_(pollInterval) {}
    virtual ~RuleWatcher();   // asserts the thread is already stopped (see below)
    RuleWatcher(const RuleWatcher&) = delete;
    RuleWatcher& operator=(const RuleWatcher&) = delete;

    void Start();
    void Stop();   // joins. Derived destructors MUST call Stop(): the thread uses their virtuals.
    // Thread-safe. Pass EngineCore::ActiveRules(); an empty vector means "no session".
    // Wakes the worker so new rules are enforced immediately, not on the next tick.
    void OnRuleChanged(const std::vector<BlockRule>& activeRules);

protected:
    // Both run on the worker thread only.
    virtual void ApplyRules(const std::vector<BlockRule>& activeRules) = 0;
    virtual void PollOnce(SteadyTime now) = 0;

private:
    void Run();

    std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<BlockRule> pending_;
    bool dirty_ = false;
    bool stop_ = false;
    std::thread thread_;
};
