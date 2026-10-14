// Poll -> evaluate -> terminate loop body (plan Module B), independent of any OS API.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <vector>

#include "engine/ProcessMatcher.h"

using SteadyTime = std::chrono::steady_clock::time_point;

class IProcessOps {
public:
    virtual ~IProcessOps() = default;
    virtual std::vector<ProcessInfo> Enumerate() = 0;
    virtual bool Terminate(std::uint32_t pid) = 0;   // true = termination was issued
    // Minimize every visible top-level window of the process; true if there was one. Default: unsupported.
    virtual bool MinimizeWindows(std::uint32_t /*pid*/) { return false; }
};

struct ProcessEvent {
    ProcessInfo process;
    Verdict     verdict = Verdict::Allow;
    int         attempt = 0;          // 1-based
    bool        terminateOk = false;
    bool        gaveUp = false;       // last attempt; process will be left alone
};

class ProcessEnforcer {
public:
    static constexpr int kMaxAttempts = 10;
    static std::chrono::milliseconds Backoff(int attemptsSoFar);   // 250ms doubling, capped at 4s

    ProcessEnforcer(IProcessOps& ops, ExemptionPolicy exempt) : ops_(ops), exempt_(std::move(exempt)) {}

    void SetPolicy(ProcessPolicy policy);                          // empty policy = idle
    void SetEventCallback(std::function<void(const ProcessEvent&)> cb) { onEvent_ = std::move(cb); }
    // true: rejected processes are only kept minimized (re-minimized every ~500 ms), never terminated.
    void SetMinimizeOnly(bool on) { minimizeOnly_ = on; }
    void Poll(SteadyTime now);

private:
    struct Key {
        std::uint32_t pid; std::uint64_t start;
        bool operator<(const Key& o) const { return pid != o.pid ? pid < o.pid : start < o.start; }
    };
    struct State {
        Verdict verdict = Verdict::Allow;
        int attempts = 0;
        SteadyTime next{};
        bool gaveUp = false;
    };

    IProcessOps& ops_;
    ExemptionPolicy exempt_;
    ProcessPolicy policy_;
    std::atomic<bool> minimizeOnly_{false};
    std::map<Key, State> states_;
    std::function<void(const ProcessEvent&)> onEvent_;
};
