#include "engine/BlockRule.h"

#include <algorithm>
#include <cassert>

BlockRule MakeRule(RuleType type, std::wstring target, WindowTitleAction action) {
    assert(!target.empty() && "BlockRule target must not be empty");
    BlockRule r;
    r.id = Guid::Generate();
    r.type = type;
    r.target = std::move(target);
    r.titleAction = action;
    return r;
}

bool IsValidRule(const BlockRule& rule) { return !rule.target.empty(); }

std::chrono::minutes ClampDuration(std::chrono::minutes d) {
    return std::clamp(d, kMinSessionDuration, kMaxSessionDuration);
}

bool IsAllowListEngaged(const SessionConfig& c) {
    return !c.allowListExePaths.empty() || !c.allowListFolders.empty();
}

bool IsEnforcementPaused(const SessionConfig& c, TimePoint sessionStart, TimePoint now) {
    using namespace std::chrono;
    if (c.breakMode == SessionBreakMode::None) return false;
    if (now < sessionStart) return false;

    const auto elapsed = duration_cast<minutes>(now - sessionStart);

    if (c.breakMode == SessionBreakMode::Allowance) {
        // First allowanceMin minutes are free.
        return elapsed.count() < c.allowanceMin;
    }

    if (c.breakMode == SessionBreakMode::Pomodoro) {
        const int cycle = c.pomodoroWorkMin + c.pomodoroBreakMin;
        if (cycle <= 0) return false;
        const int mod = static_cast<int>(elapsed.count() % cycle);
        // After work portion comes the break.
        return mod >= c.pomodoroWorkMin;
    }
    return false;
}
