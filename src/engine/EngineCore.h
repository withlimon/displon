// Core Engine: single active session + lock state machine (plan Module A).
// Phase 1 states: Idle <-> Locked. Armed / Unlocking arrive with the Scheduler (Phase 5)
// and delayed-unlock (Phase 7).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "common/BootTime.h"
#include "engine/BlockSession.h"

enum class EngineState { Idle, Locked };
enum class StartResult { Started, AlreadyLocked, PersistFailed };

class EngineCore {
public:
    using NowFn = std::function<TimePoint()>;
    using BootFn = std::function<std::int64_t()>;   // identity of the current boot (see common/BootTime.h)

    explicit EngineCore(ILockStateStore& store,
                        NowFn now = [] { return std::chrono::system_clock::now(); },
                        BootFn boot = [] { return CurrentBootEpochSecs(); });

    // Clamps duration to 5-180 min, computes the absolute unlock time itself, persists the
    // lock state BEFORE arming (fail closed), and drops rules with empty targets.
    StartResult StartSession(const SessionConfig& config, std::wstring_view password = {});

    // Ends the session if `id` matches and the password is correct. Returns false if there is
    // nothing to stop (including a session that already expired) or the password is wrong.
    bool StopSession(const Guid& id, std::wstring_view password);

    void Tick();                       // call periodically; releases expired sessions
    // Boot: re-lock with the ORIGINAL unlock time and the FULL persisted rule set. An "until restart" session
    // that is found on a different boot has done its job (the machine was restarted) and is dropped.
    bool ResumeFromPersistedState();

    EngineState State() const { return session_ ? EngineState::Locked : EngineState::Idle; }
    std::optional<Guid> ActiveSessionId() const;
    std::optional<TimePoint> UnlockTime() const;
    const SessionConfig* ActiveConfig() const;
    int FailedUnlockAttempts() const { return session_ ? session_->FailedAttempts() : 0; }
    const std::vector<BlockRule>& ActiveRules() const { return activeRules_; }  // flattened, stable ids

private:
    void Arm(LockState state);
    void EndSession();

    ILockStateStore& store_;
    NowFn now_;
    BootFn boot_;
    std::unique_ptr<BlockSession> session_;
    std::vector<BlockRule> activeRules_;
};
