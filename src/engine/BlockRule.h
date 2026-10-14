// Core engine data types (plan Module A). Portable: no Windows headers.
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/Guid.h"

using TimePoint = std::chrono::system_clock::time_point;  // wall clock: survives reboots

enum class RuleType { ProcessExe, WindowTitle, Website, AllowListExe, AllowListFolder };
enum class WindowTitleAction { Close, Suppress };

enum class SessionDurationMode {
    Timer,          // absoluteUnlockTime = now + duration (1 min – 24 h)
    UntilRestart,   // unlock only after reboot
    UntilDateTime,  // unlock at explicit wall-clock stop time (picker; may be >24h / "infinity")
};

enum class SessionBreakMode {
    None,
    Pomodoro,   // work minutes then break minutes, repeating until unlock
    Allowance,  // free minutes at start, then continuous block
};

struct BlockRule {
    Guid id;
    RuleType type = RuleType::ProcessExe;
    std::wstring target;
    WindowTitleAction titleAction = WindowTitleAction::Close;
};

BlockRule MakeRule(RuleType type, std::wstring target,
                   WindowTitleAction action = WindowTitleAction::Close);
bool IsValidRule(const BlockRule& rule);

inline constexpr std::chrono::minutes kMinSessionDuration{1};
inline constexpr std::chrono::minutes kMaxSessionDuration{24 * 60};  // 1 day
std::chrono::minutes ClampDuration(std::chrono::minutes d);

struct SessionConfig {
    std::vector<std::wstring> allowListExePaths;
    std::vector<std::wstring> allowListFolders;
    std::vector<BlockRule>    processBlockRules;
    std::vector<BlockRule>    windowTitleRules;
    std::vector<std::wstring> websiteBlockRules;
    bool                      blockTaskManager = false;
    bool                      blockWindowsSettings = false;
    // What happens to a process the lock rejects: Close = terminate it, Suppress = only keep its windows
    // minimized (the process keeps running). Window-title rules carry their own per-rule action.
    WindowTitleAction         processAction = WindowTitleAction::Close;
    SessionDurationMode       durationMode = SessionDurationMode::Timer;
    std::chrono::minutes      duration = kMinSessionDuration;
    TimePoint                 absoluteUnlockTime{};  // set by EngineCore
    TimePoint                 sessionStartTime{};    // set by EngineCore at StartSession
    // Boot identity (CurrentBootEpochSecs) captured at StartSession. Only meaningful for UntilRestart: a lock
    // that is resumed on a DIFFERENT boot has been "restarted" and must end.
    std::int64_t              bootEpochSecs = 0;

    SessionBreakMode breakMode = SessionBreakMode::None;
    int pomodoroWorkMin = 25;
    int pomodoroBreakMin = 5;
    int allowanceMin = 5;

    // True while in a Pomodoro break window (all apps allowed). Computed by EngineHost::Tick.
    bool inBreakWindow = false;
};

bool IsAllowListEngaged(const SessionConfig& c);
// Enforcement should pause (allow everything) during Pomodoro break or pre-allowance.
bool IsEnforcementPaused(const SessionConfig& c, TimePoint sessionStart, TimePoint now);
