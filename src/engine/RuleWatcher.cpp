#include "engine/RuleWatcher.h"

#include <cassert>

RuleWatcher::~RuleWatcher() {
    assert(!thread_.joinable() && "derived watcher destructor must call Stop() first");
    Stop();  // release builds: still avoid std::terminate, though virtuals may already be gone
}

void RuleWatcher::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
}

void RuleWatcher::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void RuleWatcher::OnRuleChanged(const std::vector<BlockRule>& activeRules) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = activeRules;
        dirty_ = true;
    }
    cv_.notify_all();
}

void RuleWatcher::Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        if (dirty_) {
            std::vector<BlockRule> rules = std::move(pending_);
            pending_.clear();
            dirty_ = false;
            lock.unlock();
            ApplyRules(rules);
            lock.lock();
            if (stop_) break;
        }
        lock.unlock();                                   // never hold the lock across OS calls
        PollOnce(std::chrono::steady_clock::now());
        lock.lock();
        cv_.wait_for(lock, interval_, [this] { return stop_ || dirty_; });
    }
}
