#pragma once
#include <chrono>
#include <string>
#include <vector>

#include "engine/BlockRule.h"

enum class WizardStep {
    Settings = 0,
    Whitelist,
    Duration,
    Breaks,
    Start,
    Countdown,
};

struct WhitelistEntry {
    std::wstring name;
    std::wstring path;
    bool checked = true;
};

struct UiSessionConfig {
    bool blockTaskManager = true;
    bool blockWindowsSettings = false;
    WindowTitleAction blockAction = WindowTitleAction::Close;   // Close = force close (strongest); Suppress = minimize only

    bool notifyOnAppBlocked = true;
    bool notifyOnBlockEnd = true;

    std::vector<WhitelistEntry> whitelist;
    std::vector<std::wstring> websiteBlocks;  // hostnames

    SessionDurationMode durationMode = SessionDurationMode::Timer;
    std::chrono::minutes timerMinutes{30};

    SessionBreakMode breakMode = SessionBreakMode::None;
    int pomodoroBlockMin = 25;
    int pomodoroBreakMin = 5;
    int allowanceMinutes = 5;

    std::wstring sessionPassword;
    std::chrono::system_clock::time_point unlockTime{};

    SessionConfig ToEngineConfig() const {
        SessionConfig c;
        c.blockTaskManager = blockTaskManager;
        c.blockWindowsSettings = blockWindowsSettings;
        c.processAction = blockAction;
        c.durationMode = durationMode;
        c.duration = timerMinutes;
        if (durationMode == SessionDurationMode::UntilDateTime)
            c.absoluteUnlockTime = unlockTime;
        c.breakMode = breakMode;
        c.pomodoroWorkMin = pomodoroBlockMin;
        c.pomodoroBreakMin = pomodoroBreakMin;
        c.allowanceMin = allowanceMinutes;
        c.websiteBlockRules = websiteBlocks;
        for (const auto& e : whitelist) {
            if (!e.checked) continue;
            if (!e.path.empty())
                c.allowListExePaths.push_back(e.path);
            else
                c.allowListExePaths.push_back(e.name);
        }
        return c;
    }
};

inline const wchar_t* StepTitle(WizardStep s) {
    switch (s) {
        case WizardStep::Settings: return L"Settings";
        case WizardStep::Whitelist: return L"Whitelist";
        case WizardStep::Duration: return L"Duration";
        case WizardStep::Breaks: return L"Breaks";
        case WizardStep::Start: return L"Start";
        case WizardStep::Countdown: return L"Active";
    }
    return L"";
}
