// Module D start: durable lock-state file that survives process death / reboots.
// Format is deliberately simple and portable so unit tests run on any host.
// Integrity is a SHA-256 over the payload; v1 does NOT encrypt (see Section 10).
// Real DPAPI / SQLCipher encryption can replace the body later without changing callers.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "engine/LockState.h"

class FileLockStateStore final : public ILockStateStore {
public:
    // `mirrors` are extra copies kept in sync with `path` (best effort): deleting one file must not unlock.
    explicit FileLockStateStore(std::filesystem::path path, std::vector<std::filesystem::path> mirrors = {});

    bool Save(const LockState& state) override;
    std::optional<LockState> Load() override;
    void Clear() override;

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    std::vector<std::filesystem::path> mirrors_;
};
