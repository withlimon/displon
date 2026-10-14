#include "engine/ProcessEnforcer.h"

#include <algorithm>
#include <set>

std::chrono::milliseconds ProcessEnforcer::Backoff(int attemptsSoFar) {
    using namespace std::chrono;
    const int shift = std::clamp(attemptsSoFar - 1, 0, 4);
    return std::min(milliseconds(250) * (1 << shift), milliseconds(4000));
}

void ProcessEnforcer::SetPolicy(ProcessPolicy policy) {
    policy_ = std::move(policy);
    states_.clear();  // re-evaluate every running process under the new rules
}

void ProcessEnforcer::Poll(SteadyTime now) {
    if (policy_.Empty()) {          // idle: do not even enumerate
        states_.clear();
        return;
    }
    const std::vector<ProcessInfo> procs = ops_.Enumerate();

    std::set<std::pair<std::uint32_t, std::uint64_t>> live;
    for (const auto& p : procs) live.insert({p.pid, p.startTime});
    for (auto it = states_.begin(); it != states_.end();) {
        if (live.count({it->first.pid, it->first.start})) ++it;
        else it = states_.erase(it);                      // process exited
    }

    for (const auto& p : procs) {
        const Key key{p.pid, p.startTime};
        auto it = states_.find(key);
        if (it == states_.end()) {
            State st;
            st.verdict = EvaluateProcess(p, policy_, exempt_);
            it = states_.emplace(key, st).first;
        }
        State& st = it->second;
        if (st.verdict == Verdict::Allow || st.gaveUp || now < st.next) continue;

        ProcessEvent ev;
        ev.process = p;
        ev.verdict = st.verdict;
        if (minimizeOnly_) {
            ev.terminateOk = ops_.MinimizeWindows(p.pid);
            ev.attempt = ++st.attempts;
            st.next = now + std::chrono::milliseconds(500);   // user restored it? push it down again; never give up
            if (onEvent_) onEvent_(ev);
            continue;
        }
        ev.terminateOk = ops_.Terminate(p.pid);
        ev.attempt = ++st.attempts;
        st.next = now + Backoff(st.attempts);   // a relauncher/stubborn process gets retried, with backoff
        if (st.attempts >= kMaxAttempts) { st.gaveUp = true; ev.gaveUp = true; }
        if (onEvent_) onEvent_(ev);
    }
}
