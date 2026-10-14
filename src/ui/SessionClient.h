// UI → service session start/stop. Tries named pipe; falls back to in-process note.
#pragma once
#include <optional>
#include <string>
#include "engine/BlockRule.h"
#include "ipc/Protocol.h"

struct SessionClientResult {
    bool ok = false;
    std::string detail;
    std::optional<std::int64_t> unlockEpochSecs;
};

class SessionClient {
public:
    // Attempt StartSession over \\.\pipe\Displon. Returns false if pipe unavailable.
    static SessionClientResult StartRemote(const SessionConfig& cfg, const std::wstring& password);
    static SessionClientResult StatusRemote();
};
