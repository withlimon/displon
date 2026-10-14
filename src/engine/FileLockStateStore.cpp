#include "engine/FileLockStateStore.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "common/Guid.h"
#include "common/Sha256.h"
#include "common/Utf.h"
#include "engine/BlockRule.h"

namespace {

std::string BytesToHex(const std::uint8_t* data, std::size_t n) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(kHex[data[i] >> 4]);
        s.push_back(kHex[data[i] & 0x0F]);
    }
    return s;
}

std::optional<std::vector<std::uint8_t>> HexToBytes(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = nibble(hex[i]), lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

// Free-text values (paths, window titles, host names) are hex-encoded UTF-8 so no character in them can ever
// break the line format.
std::string EncodeText(std::wstring_view w) {
    const std::string u8 = WideToUtf8(w);
    return BytesToHex(reinterpret_cast<const std::uint8_t*>(u8.data()), u8.size());
}

std::optional<std::wstring> DecodeText(std::string_view hex) {
    auto bytes = HexToBytes(hex);
    if (!bytes) return std::nullopt;
    return Utf8ToWide(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

std::optional<long long> ParseInt(const std::string& v) {
    try {
        std::size_t used = 0;
        const long long x = std::stoll(v, &used);
        if (used != v.size()) return std::nullopt;
        return x;
    } catch (...) {
        return std::nullopt;
    }
}

constexpr std::size_t kMaxListEntries = 20000;   // a tampered file must not make resume allocate without bound

// Simple text serialization of LockState (one line per field).
// Version tag first so we can reject future formats cleanly.
//   v=1  unlock time + password only  (older builds - a resumed lock then enforced NOTHING)
//   v=2  the whole session: every rule list, duration/break mode, start time, boot identity
std::string Serialize(const LockState& s) {
    const SessionConfig& c = s.config;
    auto secs = [](TimePoint t) {
        return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
    };
    std::ostringstream out;
    out << "v=2\n";
    out << "id=" << s.sessionId.ToString() << "\n";
    out << "unlock=" << secs(c.absoluteUnlockTime) << "\n";
    out << "start=" << secs(c.sessionStartTime) << "\n";
    out << "boot=" << c.bootEpochSecs << "\n";
    out << "duration=" << c.duration.count() << "\n";
    out << "mode=" << static_cast<int>(c.durationMode) << "\n";
    out << "taskmgr=" << (c.blockTaskManager ? 1 : 0) << "\n";
    out << "winsettings=" << (c.blockWindowsSettings ? 1 : 0) << "\n";
    out << "paction=" << (c.processAction == WindowTitleAction::Close ? 0 : 1) << "\n";
    out << "break=" << static_cast<int>(c.breakMode) << "\n";
    out << "pwork=" << c.pomodoroWorkMin << "\n";
    out << "pbreak=" << c.pomodoroBreakMin << "\n";
    out << "allowance=" << c.allowanceMin << "\n";
    for (const auto& p : c.allowListExePaths) out << "allow_exe=" << EncodeText(p) << "\n";
    for (const auto& p : c.allowListFolders) out << "allow_dir=" << EncodeText(p) << "\n";
    for (const auto& r : c.processBlockRules) out << "proc=" << EncodeText(r.target) << "\n";
    for (const auto& r : c.windowTitleRules)
        out << "win=" << (r.titleAction == WindowTitleAction::Close ? 0 : 1) << ":" << EncodeText(r.target) << "\n";
    for (const auto& w : c.websiteBlockRules) out << "web=" << EncodeText(w) << "\n";
    out << "pwd_set=" << (s.password.set ? 1 : 0) << "\n";
    if (s.password.set) {
        out << "pwd_salt=" << BytesToHex(s.password.salt.data(), s.password.salt.size()) << "\n";
        out << "pwd_hash=" << BytesToHex(s.password.hash.data(), s.password.hash.size()) << "\n";
    }
    return out.str();
}

std::optional<LockState> Parse(const std::string& text) {
    LockState s;
    SessionConfig& c = s.config;
    bool sawVersion = false;
    bool sawId = false;
    bool sawUnlock = false;

    auto pushText = [](std::vector<std::wstring>& list, const std::string& val) {
        if (list.size() >= kMaxListEntries) return false;
        auto t = DecodeText(val);
        if (!t) return false;
        if (!t->empty()) list.push_back(std::move(*t));   // an empty target is meaningless: drop, don't fail
        return true;
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "v") {
            if (val != "1" && val != "2") return std::nullopt;
            sawVersion = true;
        } else if (key == "id") {
            auto g = Guid::FromString(val);
            if (!g) return std::nullopt;
            s.sessionId = *g;
            sawId = true;
        } else if (key == "unlock") {
            const auto v = ParseInt(val);
            if (!v) return std::nullopt;
            c.absoluteUnlockTime = TimePoint{std::chrono::seconds(*v)};
            sawUnlock = true;
        } else if (key == "start") {
            const auto v = ParseInt(val);
            if (!v) return std::nullopt;
            c.sessionStartTime = TimePoint{std::chrono::seconds(*v)};
        } else if (key == "boot") {
            const auto v = ParseInt(val);
            if (!v) return std::nullopt;
            c.bootEpochSecs = *v;
        } else if (key == "duration") {
            const auto v = ParseInt(val);
            if (!v) return std::nullopt;
            c.duration = ClampDuration(std::chrono::minutes(*v));
        } else if (key == "mode") {
            const auto v = ParseInt(val);
            if (!v || *v < 0 || *v > 2) return std::nullopt;
            c.durationMode = static_cast<SessionDurationMode>(*v);
        } else if (key == "taskmgr") {
            c.blockTaskManager = (val == "1");
        } else if (key == "winsettings") {
            c.blockWindowsSettings = (val == "1");
        } else if (key == "paction") {
            c.processAction = (val == "1") ? WindowTitleAction::Suppress : WindowTitleAction::Close;
        } else if (key == "break") {
            const auto v = ParseInt(val);
            if (!v || *v < 0 || *v > 2) return std::nullopt;
            c.breakMode = static_cast<SessionBreakMode>(*v);
        } else if (key == "pwork" || key == "pbreak" || key == "allowance") {
            const auto v = ParseInt(val);
            if (!v || *v < 1 || *v > 24 * 60) return std::nullopt;
            (key == "pwork" ? c.pomodoroWorkMin : key == "pbreak" ? c.pomodoroBreakMin : c.allowanceMin) =
                static_cast<int>(*v);
        } else if (key == "allow_exe") {
            if (!pushText(c.allowListExePaths, val)) return std::nullopt;
        } else if (key == "allow_dir") {
            if (!pushText(c.allowListFolders, val)) return std::nullopt;
        } else if (key == "web") {
            if (!pushText(c.websiteBlockRules, val)) return std::nullopt;
        } else if (key == "proc") {
            std::vector<std::wstring> one;
            if (!pushText(one, val)) return std::nullopt;
            if (c.processBlockRules.size() >= kMaxListEntries) return std::nullopt;
            if (!one.empty()) c.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, std::move(one[0])));
        } else if (key == "win") {
            const auto colon = val.find(':');
            if (colon != 1 || (val[0] != '0' && val[0] != '1')) return std::nullopt;
            std::vector<std::wstring> one;
            if (!pushText(one, val.substr(2))) return std::nullopt;
            if (c.windowTitleRules.size() >= kMaxListEntries) return std::nullopt;
            if (!one.empty())
                c.windowTitleRules.push_back(MakeRule(
                    RuleType::WindowTitle, std::move(one[0]),
                    val[0] == '0' ? WindowTitleAction::Close : WindowTitleAction::Suppress));
        } else if (key == "pwd_set") {
            s.password.set = (val == "1");
        } else if (key == "pwd_salt") {
            auto bytes = HexToBytes(val);
            if (!bytes || bytes->size() != 16) return std::nullopt;
            std::copy(bytes->begin(), bytes->end(), s.password.salt.begin());
        } else if (key == "pwd_hash") {
            auto bytes = HexToBytes(val);
            if (!bytes || bytes->size() != 32) return std::nullopt;
            std::copy(bytes->begin(), bytes->end(), s.password.hash.begin());
        }
        // unknown keys ignored for forward-compat
    }
    if (!sawVersion || !sawId || !sawUnlock) return std::nullopt;
    return s;
}

// File layout: 64-char hex SHA-256 of payload, then newline, then payload.
std::string WrapWithIntegrity(const std::string& payload) {
    const Sha256Digest d = Sha256(payload);
    return ToHex(d) + "\n" + payload;
}

std::optional<std::string> UnwrapAndVerify(const std::string& file) {
    const auto nl = file.find('\n');
    if (nl == std::string::npos || nl != 64) return std::nullopt;
    const std::string claimed = file.substr(0, 64);
    const std::string payload = file.substr(nl + 1);
    const Sha256Digest d = Sha256(payload);
    if (ToHex(d) != claimed) return std::nullopt;
    return payload;
}

}  // namespace

namespace {

bool WriteWhole(const std::filesystem::path& path, const std::string& body) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        return static_cast<bool>(out);
    }
    return true;
}

std::optional<LockState> LoadOne(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    const auto payload = UnwrapAndVerify(buf.str());
    if (!payload) return std::nullopt;
    return Parse(*payload);
}

}  // namespace

FileLockStateStore::FileLockStateStore(std::filesystem::path path, std::vector<std::filesystem::path> mirrors)
    : path_(std::move(path)), mirrors_(std::move(mirrors)) {}

bool FileLockStateStore::Save(const LockState& state) {
    const std::string body = WrapWithIntegrity(Serialize(state));
    if (!WriteWhole(path_, body)) return false;          // the primary copy must succeed: fail closed
    for (const auto& m : mirrors_) WriteWhole(m, body);  // extra copies are best effort
    return true;
}

// Several copies exist so that deleting (or corrupting) one file does not release the lock. The valid copy
// with the LATEST unlock time wins, and any missing/damaged copy is rewritten from it.
std::optional<LockState> FileLockStateStore::Load() {
    std::vector<std::filesystem::path> all{path_};
    all.insert(all.end(), mirrors_.begin(), mirrors_.end());

    std::optional<LockState> best;
    std::vector<bool> valid(all.size(), false);
    for (std::size_t i = 0; i < all.size(); ++i) {
        auto st = LoadOne(all[i]);
        if (!st) continue;
        valid[i] = true;
        if (!best || st->config.absoluteUnlockTime > best->config.absoluteUnlockTime) best = std::move(st);
    }
    if (!best) return std::nullopt;

    const std::string body = WrapWithIntegrity(Serialize(*best));
    for (std::size_t i = 0; i < all.size(); ++i)
        if (!valid[i]) WriteWhole(all[i], body);
    return best;
}

void FileLockStateStore::Clear() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    for (const auto& m : mirrors_) std::filesystem::remove(m, ec);
}
