#pragma once
#include <string_view>

#include "engine/LockState.h"

class BlockSession {
public:
    explicit BlockSession(LockState state) : state_(std::move(state)) {}

    const Guid& Id() const { return state_.sessionId; }
    const SessionConfig& Config() const { return state_.config; }
    const LockState& State() const { return state_; }
    TimePoint UnlockTime() const { return state_.config.absoluteUnlockTime; }

    bool IsLocked(TimePoint now) const { return !released_ && now < UnlockTime(); }

    // Early unlock. Only succeeds when the session has a password and it matches.
    bool TryUnlock(std::wstring_view password);
    int FailedAttempts() const { return failedAttempts_; }

private:
    LockState state_;
    bool released_ = false;
    int failedAttempts_ = 0;
};
