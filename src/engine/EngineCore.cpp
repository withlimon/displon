#include "engine/EngineCore.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace {

void KeepValid(std::vector<BlockRule>& rules) {
    rules.erase(std::remove_if(rules.begin(), rules.end(), [](const BlockRule& r) { return !IsValidRule(r); }),
                rules.end());
}
void KeepNonEmpty(std::vector<std::wstring>& v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](const std::wstring& s) { return s.empty(); }), v.end());
}

std::vector<BlockRule> Flatten(const SessionConfig& c) {
    std::vector<BlockRule> out;
    out.insert(out.end(), c.processBlockRules.begin(), c.processBlockRules.end());
    out.insert(out.end(), c.windowTitleRules.begin(), c.windowTitleRules.end());
    for (const auto& w : c.websiteBlockRules) out.push_back(MakeRule(RuleType::Website, w));
    for (const auto& p : c.allowListExePaths) out.push_back(MakeRule(RuleType::AllowListExe, p));
    for (const auto& f : c.allowListFolders) out.push_back(MakeRule(RuleType::AllowListFolder, f));
    if (c.blockTaskManager) {
        const bool present = std::any_of(out.begin(), out.end(), [](const BlockRule& r) {
            return r.type == RuleType::ProcessExe && r.target == L"taskmgr.exe";
        });
        if (!present) out.push_back(MakeRule(RuleType::ProcessExe, L"taskmgr.exe"));
    }
    return out;
}

}  // namespace

EngineCore::EngineCore(ILockStateStore& store, NowFn now, BootFn boot)
    : store_(store), now_(std::move(now)), boot_(std::move(boot)) {
    assert(now_ && "EngineCore needs a clock");
    assert(boot_ && "EngineCore needs a boot-identity source");
}

StartResult EngineCore::StartSession(const SessionConfig& config, std::wstring_view password) {
    Tick();
    if (session_) return StartResult::AlreadyLocked;

    LockState state;
    state.sessionId = Guid::Generate();
    state.config = config;
    KeepValid(state.config.processBlockRules);
    KeepValid(state.config.windowTitleRules);
    KeepNonEmpty(state.config.websiteBlockRules);
    KeepNonEmpty(state.config.allowListExePaths);
    KeepNonEmpty(state.config.allowListFolders);
    state.config.duration = ClampDuration(config.duration);
    state.config.durationMode = config.durationMode;
    state.config.breakMode = config.breakMode;
    state.config.pomodoroWorkMin = config.pomodoroWorkMin > 0 ? config.pomodoroWorkMin : 25;
    state.config.pomodoroBreakMin = config.pomodoroBreakMin > 0 ? config.pomodoroBreakMin : 5;
    state.config.allowanceMin = config.allowanceMin > 0 ? config.allowanceMin : 5;
    state.config.blockWindowsSettings = config.blockWindowsSettings;
    if (config.durationMode == SessionDurationMode::UntilRestart) {
        state.config.absoluteUnlockTime = now_() + std::chrono::hours(24 * 365);
        state.config.duration = std::chrono::minutes(24 * 60);
    } else if (config.durationMode == SessionDurationMode::UntilDateTime) {
        // Caller supplies absoluteUnlockTime (date/time picker). Must be in the future.
        auto unlock = config.absoluteUnlockTime;
        if (unlock <= now_()) unlock = now_() + std::chrono::minutes(1);
        state.config.absoluteUnlockTime = unlock;
        state.config.duration = std::chrono::duration_cast<std::chrono::minutes>(unlock - now_());
        if (state.config.duration.count() < 1) state.config.duration = std::chrono::minutes(1);
    } else {
        state.config.absoluteUnlockTime = now_() + state.config.duration;
    }
    state.config.sessionStartTime = now_();
    state.config.bootEpochSecs = boot_();
    state.password = MakePasswordRecord(password);

    if (!store_.Save(state)) return StartResult::PersistFailed;
    Arm(std::move(state));
    return StartResult::Started;
}

bool EngineCore::StopSession(const Guid& id, std::wstring_view password) {
    Tick();
    if (!session_ || !(session_->Id() == id)) return false;
    if (!session_->TryUnlock(password)) return false;
    EndSession();
    return true;
}

void EngineCore::Tick() {
    if (session_ && !session_->IsLocked(now_())) EndSession();
}

bool EngineCore::ResumeFromPersistedState() {
    assert(!session_ && "Resume is for service boot, with no active session");
    std::optional<LockState> saved = store_.Load();
    if (!saved) return false;
    if (saved->config.absoluteUnlockTime <= now_()) {  // expired while the machine was off
        store_.Clear();
        return false;
    }
    if (saved->config.durationMode == SessionDurationMode::UntilRestart) {
        const std::int64_t was = saved->config.bootEpochSecs, cur = boot_();
        // Unknown identity on either side keeps the lock (fail closed); a different boot ends it.
        if (was != 0 && cur != 0 && std::llabs(cur - was) > kSameBootToleranceSecs) {
            store_.Clear();
            return false;
        }
    }
    Arm(std::move(*saved));  // original timestamp, not a fresh timer
    return true;
}

std::optional<Guid> EngineCore::ActiveSessionId() const {
    if (!session_) return std::nullopt;
    return session_->Id();
}
std::optional<TimePoint> EngineCore::UnlockTime() const {
    if (!session_) return std::nullopt;
    return session_->UnlockTime();
}
const SessionConfig* EngineCore::ActiveConfig() const { return session_ ? &session_->Config() : nullptr; }

void EngineCore::Arm(LockState state) {
    activeRules_ = Flatten(state.config);
    session_ = std::make_unique<BlockSession>(std::move(state));
}

void EngineCore::EndSession() {
    session_.reset();
    activeRules_.clear();
    store_.Clear();
}
