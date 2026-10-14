// Module H: portable IPC message codec.
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/Guid.h"
#include "engine/BlockRule.h"

namespace ipc {

enum class Command : std::uint8_t {
    GetStatus = 1,
    StartSession = 2,
    StopSession = 3,
    AddRule = 4,
    DeleteRule = 5,
    AddSchedule = 6,
    RemoveSchedule = 7,
    SetScheduleEnabled = 8,
    Ping = 9,
};

enum class ReplyCode : std::uint8_t {
    Ok = 0,
    Error = 1,
    Unauthorized = 2,
    Invalid = 3,
};

struct Message {
    Command cmd = Command::Ping;
    std::optional<Guid> id;
    std::optional<std::wstring> password;
    std::optional<std::chrono::minutes> duration;
    std::optional<bool> blockTaskManager;
    std::optional<bool> blockWindowsSettings;
    std::optional<bool> untilRestart;
    std::optional<int> breakMode;       // 0 none, 1 pomodoro, 2 allowance
    std::optional<int> pomodoroWork;
    std::optional<int> pomodoroBreak;
    std::optional<int> allowance;
    std::vector<std::wstring> processBlocks;
    std::vector<std::wstring> windowTitles;
    std::vector<std::wstring> websites;
    std::vector<std::wstring> allowExes;
    std::vector<std::wstring> allowFolders;
    std::optional<bool> enabled;
};

struct Reply {
    ReplyCode code = ReplyCode::Ok;
    std::string detail;
    std::string state;
    std::optional<std::int64_t> unlockEpochSecs;
    int failedAttempts = 0;
};

std::string EncodeMessage(const Message& m);
std::optional<Message> DecodeMessage(std::string_view body);
std::string EncodeReply(const Reply& r);
std::optional<Reply> DecodeReply(std::string_view body);

bool IsValidPassword(std::wstring_view pw);
bool IsValidDuration(std::chrono::minutes d);

// Length-prefixed framing (4-byte little-endian size + body). Portable helpers.
std::string Frame(std::string_view body);
std::optional<std::string> TryDeframe(std::string& buffer);  // consumes one frame from buffer

}  // namespace ipc
