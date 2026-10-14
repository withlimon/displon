// Persisted lock state + the storage seam Module D (ServiceGuard) will implement.
#pragma once
#include <array>
#include <optional>
#include <string_view>

#include "common/Sha256.h"
#include "engine/BlockRule.h"

struct PasswordRecord {
    bool set = false;
    std::array<std::uint8_t, 16> salt{};
    Sha256Digest hash{};
};

// Empty password -> record with set == false (session can only end by timer).
// Stopgap KDF: salted SHA-256 iterated 10,000 times. Replace with PBKDF2/DPAPI in Module D.
PasswordRecord MakePasswordRecord(std::wstring_view password);
bool VerifyPassword(const PasswordRecord& record, std::wstring_view password);

struct LockState {
    Guid sessionId;
    SessionConfig config;  // config.absoluteUnlockTime is the authoritative end time
    PasswordRecord password;
};

class ILockStateStore {
public:
    virtual ~ILockStateStore() = default;
    virtual bool Save(const LockState& state) = 0;
    virtual std::optional<LockState> Load() = 0;
    virtual void Clear() = 0;
};

class InMemoryLockStateStore final : public ILockStateStore {
public:
    bool Save(const LockState& state) override { state_ = state; return true; }
    std::optional<LockState> Load() override { return state_; }
    void Clear() override { state_.reset(); }
private:
    std::optional<LockState> state_;
};
