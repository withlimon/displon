#include "ipc/Protocol.h"

#include <sstream>
#include <cstring>

#include "common/Utf.h"
#include "engine/BlockRule.h"

namespace ipc {
namespace {

std::string Escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '%' || c == '\n' || c == '\r' || c == '=') {
            static constexpr char kHex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
            out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string Unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = nibble(s[i + 1]), lo = nibble(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

const char* CmdName(Command c) {
    switch (c) {
        case Command::GetStatus: return "GetStatus";
        case Command::StartSession: return "StartSession";
        case Command::StopSession: return "StopSession";
        case Command::AddRule: return "AddRule";
        case Command::DeleteRule: return "DeleteRule";
        case Command::AddSchedule: return "AddSchedule";
        case Command::RemoveSchedule: return "RemoveSchedule";
        case Command::SetScheduleEnabled: return "SetScheduleEnabled";
        case Command::Ping: return "Ping";
    }
    return "Unknown";
}

std::optional<Command> ParseCmd(std::string_view s) {
    if (s == "GetStatus") return Command::GetStatus;
    if (s == "StartSession") return Command::StartSession;
    if (s == "StopSession") return Command::StopSession;
    if (s == "AddRule") return Command::AddRule;
    if (s == "DeleteRule") return Command::DeleteRule;
    if (s == "AddSchedule") return Command::AddSchedule;
    if (s == "RemoveSchedule") return Command::RemoveSchedule;
    if (s == "SetScheduleEnabled") return Command::SetScheduleEnabled;
    if (s == "Ping") return Command::Ping;
    return std::nullopt;
}

void AppendList(std::ostringstream& out, const char* key, const std::vector<std::wstring>& items) {
    for (const auto& w : items) {
        out << key << "=" << Escape(WideToUtf8(w)) << "\n";
    }
}

}  // namespace

std::string EncodeMessage(const Message& m) {
    std::ostringstream out;
    out << "cmd=" << CmdName(m.cmd) << "\n";
    if (m.id) out << "id=" << m.id->ToString() << "\n";
    if (m.password) out << "password=" << Escape(WideToUtf8(*m.password)) << "\n";
    if (m.duration) out << "duration=" << m.duration->count() << "\n";
    if (m.blockTaskManager) out << "taskmgr=" << (*m.blockTaskManager ? 1 : 0) << "\n";
    if (m.enabled) out << "enabled=" << (*m.enabled ? 1 : 0) << "\n";
    AppendList(out, "process", m.processBlocks);
    AppendList(out, "title", m.windowTitles);
    AppendList(out, "site", m.websites);
    AppendList(out, "allow_exe", m.allowExes);
    AppendList(out, "allow_folder", m.allowFolders);
    if (m.blockWindowsSettings) out << "winsettings=" << (*m.blockWindowsSettings ? 1 : 0) << "\n";
    if (m.untilRestart) out << "until_restart=" << (*m.untilRestart ? 1 : 0) << "\n";
    if (m.breakMode) out << "break_mode=" << *m.breakMode << "\n";
    if (m.pomodoroWork) out << "pomodoro_work=" << *m.pomodoroWork << "\n";
    if (m.pomodoroBreak) out << "pomodoro_break=" << *m.pomodoroBreak << "\n";
    if (m.allowance) out << "allowance=" << *m.allowance << "\n";
    out << "\n";
    return out.str();
}

std::optional<Message> DecodeMessage(std::string_view body) {
    Message m;
    bool sawCmd = false;
    std::string text(body);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) break;
        if (line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const std::string key = line.substr(0, eq);
        const std::string raw = line.substr(eq + 1);
        const std::string val = Unescape(raw);

        if (key == "cmd") {
            auto c = ParseCmd(val);
            if (!c) return std::nullopt;
            m.cmd = *c;
            sawCmd = true;
        } else if (key == "id") {
            auto g = Guid::FromString(val);
            if (!g) return std::nullopt;
            m.id = *g;
        } else if (key == "password") {
            m.password = Utf8ToWide(val);
        } else if (key == "duration") {
            try {
                m.duration = std::chrono::minutes(std::stoi(val));
            } catch (...) {
                return std::nullopt;
            }
        } else if (key == "taskmgr") {
            m.blockTaskManager = (val == "1");
        } else if (key == "enabled") {
            m.enabled = (val == "1");
        } else if (key == "process") {
            m.processBlocks.push_back(Utf8ToWide(val));
        } else if (key == "title") {
            m.windowTitles.push_back(Utf8ToWide(val));
        } else if (key == "site") {
            m.websites.push_back(Utf8ToWide(val));
        } else if (key == "allow_exe") {
            m.allowExes.push_back(Utf8ToWide(val));
        } else if (key == "allow_folder") {
            m.allowFolders.push_back(Utf8ToWide(val));
        } else if (key == "winsettings") {
            m.blockWindowsSettings = (val == "1");
        } else if (key == "until_restart") {
            m.untilRestart = (val == "1");
        } else if (key == "break_mode") {
            try { m.breakMode = std::stoi(val); } catch (...) { return std::nullopt; }
        } else if (key == "pomodoro_work") {
            try { m.pomodoroWork = std::stoi(val); } catch (...) { return std::nullopt; }
        } else if (key == "pomodoro_break") {
            try { m.pomodoroBreak = std::stoi(val); } catch (...) { return std::nullopt; }
        } else if (key == "allowance") {
            try { m.allowance = std::stoi(val); } catch (...) { return std::nullopt; }
        }
        // unknown keys ignored
    }
    if (!sawCmd) return std::nullopt;
    return m;
}

std::string EncodeReply(const Reply& r) {
    std::ostringstream out;
    out << "code=" << static_cast<int>(r.code) << "\n";
    if (!r.detail.empty()) out << "detail=" << Escape(r.detail) << "\n";
    if (!r.state.empty()) out << "state=" << Escape(r.state) << "\n";
    if (r.unlockEpochSecs) out << "unlock=" << *r.unlockEpochSecs << "\n";
    out << "fails=" << r.failedAttempts << "\n";
    out << "\n";
    return out.str();
}

std::optional<Reply> DecodeReply(std::string_view body) {
    Reply r;
    bool sawCode = false;
    std::string text(body);
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) break;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const std::string key = line.substr(0, eq);
        const std::string val = Unescape(line.substr(eq + 1));
        if (key == "code") {
            try {
                int c = std::stoi(val);
                if (c < 0 || c > 3) return std::nullopt;
                r.code = static_cast<ReplyCode>(c);
                sawCode = true;
            } catch (...) {
                return std::nullopt;
            }
        } else if (key == "detail") {
            r.detail = val;
        } else if (key == "state") {
            r.state = val;
        } else if (key == "unlock") {
            try {
                r.unlockEpochSecs = std::stoll(val);
            } catch (...) {
                return std::nullopt;
            }
        } else if (key == "fails") {
            try {
                r.failedAttempts = std::stoi(val);
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    if (!sawCode) return std::nullopt;
    return r;
}

bool IsValidPassword(std::wstring_view pw) {
    if (pw.size() > 128) return false;
    for (wchar_t c : pw) {
        if (c < 0x20 && c != L'\t') return false;  // reject most control chars
    }
    return true;
}

bool IsValidDuration(std::chrono::minutes d) {
    return d >= kMinSessionDuration && d <= kMaxSessionDuration;
}


std::string Frame(std::string_view body) {
    const std::uint32_t n = static_cast<std::uint32_t>(body.size());
    std::string out;
    out.resize(4 + body.size());
    out[0] = static_cast<char>(n & 0xFF);
    out[1] = static_cast<char>((n >> 8) & 0xFF);
    out[2] = static_cast<char>((n >> 16) & 0xFF);
    out[3] = static_cast<char>((n >> 24) & 0xFF);
    std::memcpy(out.data() + 4, body.data(), body.size());
    return out;
}

std::optional<std::string> TryDeframe(std::string& buffer) {
    if (buffer.size() < 4) return std::nullopt;
    const std::uint32_t n =
        (std::uint8_t)buffer[0] | ((std::uint8_t)buffer[1] << 8) |
        ((std::uint8_t)buffer[2] << 16) | ((std::uint8_t)buffer[3] << 24);
    if (n > 8 * 1024 * 1024) {  // 8 MiB sanity
        buffer.clear();
        return std::nullopt;
    }
    if (buffer.size() < 4 + n) return std::nullopt;
    std::string body = buffer.substr(4, n);
    buffer.erase(0, 4 + n);
    return body;
}

}  // namespace ipc
