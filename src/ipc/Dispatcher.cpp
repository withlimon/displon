#include "ipc/Dispatcher.h"

#include "common/Utf.h"
#include "engine/BlockRule.h"

namespace ipc {

SessionConfig Dispatcher::BuildConfig(const Message& msg) const {
    SessionConfig c;
    if (msg.duration) c.duration = ClampDuration(*msg.duration);
    if (msg.blockTaskManager) c.blockTaskManager = *msg.blockTaskManager;
    for (const auto& p : msg.processBlocks)
        c.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, p));
    for (const auto& t : msg.windowTitles)
        c.windowTitleRules.push_back(MakeRule(RuleType::WindowTitle, t));
    c.websiteBlockRules = msg.websites;
    c.allowListExePaths = msg.allowExes;
    c.allowListFolders = msg.allowFolders;
    // Extended fields from Protocol (optional; default if absent)
    if (msg.untilRestart && *msg.untilRestart)
        c.durationMode = SessionDurationMode::UntilRestart;
    if (msg.breakMode) {
        if (*msg.breakMode == 1) c.breakMode = SessionBreakMode::Pomodoro;
        else if (*msg.breakMode == 2) c.breakMode = SessionBreakMode::Allowance;
        else c.breakMode = SessionBreakMode::None;
    }
    if (msg.pomodoroWork) c.pomodoroWorkMin = *msg.pomodoroWork;
    if (msg.pomodoroBreak) c.pomodoroBreakMin = *msg.pomodoroBreak;
    if (msg.allowance) c.allowanceMin = *msg.allowance;
    if (msg.blockWindowsSettings) c.blockWindowsSettings = *msg.blockWindowsSettings;
    return c;
}

Reply Dispatcher::StatusReply() const {
    Reply r;
    r.code = ReplyCode::Ok;
    const HostStatus st = host_.Status();
    r.state = (st.state == EngineState::Locked) ? "Locked" : "Idle";
    if (st.unlockTime) {
        r.unlockEpochSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                st.unlockTime->time_since_epoch())
                                .count();
    }
    r.failedAttempts = st.failedUnlockAttempts;
    return r;
}

Reply Dispatcher::Handle(const Message& msg) {
    Reply r;
    switch (msg.cmd) {
    case Command::Ping:
        r.code = ReplyCode::Ok;
        r.detail = "pong";
        return r;

    case Command::GetStatus:
        return StatusReply();

    case Command::StartSession: {
        if (msg.duration && !IsValidDuration(*msg.duration) &&
            !(msg.untilRestart && *msg.untilRestart)) {
            r.code = ReplyCode::Invalid;
            r.detail = "duration out of range";
            return r;
        }
        if (msg.password && !IsValidPassword(*msg.password)) {
            r.code = ReplyCode::Invalid;
            r.detail = "invalid password";
            return r;
        }
        SessionConfig cfg = BuildConfig(msg);
        const std::wstring_view pw = msg.password ? std::wstring_view(*msg.password) : L"";
        const StartResult sr = host_.StartSession(cfg, pw);
        if (sr == StartResult::Started) {
            r = StatusReply();
            r.detail = "started";
        } else if (sr == StartResult::AlreadyLocked) {
            r.code = ReplyCode::Error;
            r.detail = "already locked";
        } else {
            r.code = ReplyCode::Error;
            r.detail = "persist failed";
        }
        return r;
    }

    case Command::StopSession: {
        if (!msg.id) {
            r.code = ReplyCode::Invalid;
            r.detail = "missing session id";
            return r;
        }
        if (msg.password && !IsValidPassword(*msg.password)) {
            r.code = ReplyCode::Invalid;
            r.detail = "invalid password";
            return r;
        }
        const std::wstring_view pw = msg.password ? std::wstring_view(*msg.password) : L"";
        if (!host_.StopSession(*msg.id, pw)) {
            r.code = ReplyCode::Unauthorized;
            r.detail = "stop failed";
            return r;
        }
        r = StatusReply();
        r.detail = "stopped";
        return r;
    }

    case Command::AddRule:
    case Command::DeleteRule:
    case Command::AddSchedule:
    case Command::RemoveSchedule:
    case Command::SetScheduleEnabled:
        r.code = ReplyCode::Error;
        r.detail = "not implemented in dispatcher v1";
        return r;
    }
    r.code = ReplyCode::Invalid;
    r.detail = "unknown command";
    return r;
}

}  // namespace ipc
