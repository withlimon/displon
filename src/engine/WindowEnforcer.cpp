#include "engine/WindowEnforcer.h"

#include <algorithm>
#include <set>

std::chrono::milliseconds WindowEnforcer::CloseBackoff(int attemptsSoFar) {
    using namespace std::chrono;
    const int shift = std::clamp(attemptsSoFar - 1, 0, 4);
    return std::min(milliseconds(500) * (1 << shift), milliseconds(5000));
}

void WindowEnforcer::SetRules(const std::vector<BlockRule>& activeRules) {
    rules_.clear();
    for (const auto& r : activeRules)
        if (r.type == RuleType::WindowTitle) rules_.push_back(r);
    states_.clear();
}

void WindowEnforcer::Poll(SteadyTime now) {
    if (rules_.empty()) { states_.clear(); return; }   // idle: do not enumerate
    const std::vector<WindowInfo> windows = ops_.Enumerate();

    std::set<std::uint64_t> live;
    for (const auto& w : windows) live.insert(w.id);
    for (auto it = states_.begin(); it != states_.end();)
        it = live.count(it->first) ? std::next(it) : states_.erase(it);   // window gone

    for (const auto& w : windows) {
        // Titles change (browser tabs!), so re-evaluate every poll instead of caching a verdict.
        const auto action = EvaluateWindow(w, rules_, exempt_);
        if (!action) { states_.erase(w.id); continue; }
        State& st = states_[w.id];

        WindowEvent ev;
        ev.window = w;
        ev.action = *action;

        if (*action == WindowTitleAction::Suppress) {
            if (w.minimized) continue;                // already suppressed; re-minimize if it comes back
            ev.ok = ops_.Minimize(w.id);
            ev.attempt = ++st.attempts;
            if (onEvent_) onEvent_(ev);
        } else {
            if (st.gaveUp || now < st.next) continue;
            ev.ok = ops_.Close(w.id);
            ev.attempt = ++st.attempts;
            st.next = now + CloseBackoff(st.attempts);
            if (st.attempts >= kMaxCloseAttempts) { st.gaveUp = true; ev.gaveUp = true; }
            if (onEvent_) onEvent_(ev);
        }
    }
}
