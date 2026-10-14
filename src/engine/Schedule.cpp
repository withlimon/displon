#include "engine/Schedule.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "common/Utf.h"

using namespace std::chrono;

namespace {

LocalTime AddDays(const LocalTime& lt, int days) {
    const sys_days d = sys_days{year{lt.year} / month{static_cast<unsigned>(lt.month)} / day{static_cast<unsigned>(lt.day)}} + std::chrono::days{days};
    const year_month_day ymd{d};
    LocalTime out = lt;
    out.year = static_cast<int>(ymd.year());
    out.month = static_cast<int>(static_cast<unsigned>(ymd.month()));
    out.day = static_cast<int>(static_cast<unsigned>(ymd.day()));
    out.weekday = static_cast<int>((weekday{d}.c_encoding() + 6) % 7);   // c_encoding: Sun=0 -> Mon=0
    return out;
}

// Percent-escape so names/ids can never break the line format.
std::string Esc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '%' || c == '\n' || c == '\r' || c == '=' || c == ';' || c < 0x20) { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
        else o += static_cast<char>(c);
    }
    return o;
}
std::string Unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return o;
}
bool ParseInt(const std::string& s, long& out) {
    if (s.empty() || s.size() > 9) return false;
    char* end = nullptr;
    out = std::strtol(s.c_str(), &end, 10);
    return end && *end == '\0';
}

}  // namespace

bool IsValidSchedule(const ScheduleEntry& e) {
    return !e.id.IsNull() && !e.name.empty() && e.name.size() <= 100 && (e.daysMask & ~kEveryDay) == 0 &&
           e.daysMask != 0 && e.startMinute >= 0 && e.startMinute < 1440 && e.duration >= kMinSessionDuration &&
           e.duration <= kMaxSessionDuration && e.ruleIds.size() <= 500;
}

ScheduleEntry MakeSchedule(std::wstring name, unsigned daysMask, int startMinute, minutes duration,
                           std::vector<Guid> ruleIds, bool blockTaskManager) {
    ScheduleEntry e;
    e.id = Guid::Generate();
    e.name = std::move(name);
    e.daysMask = daysMask;
    e.startMinute = startMinute;
    e.duration = duration;
    e.ruleIds = std::move(ruleIds);
    e.blockTaskManager = blockTaskManager;
    return e;
}

std::string SerializeSchedule(const ScheduleEntry& e) {
    std::string s = "v=1;name=" + Esc(WideToUtf8(e.name)) + ";days=" + std::to_string(e.daysMask) +
                    ";start=" + std::to_string(e.startMinute) + ";dur=" + std::to_string(e.duration.count()) +
                    ";on=" + (e.enabled ? "1" : "0") + ";tm=" + (e.blockTaskManager ? "1" : "0") + ";rules=";
    for (size_t i = 0; i < e.ruleIds.size(); ++i) s += (i ? "," : "") + e.ruleIds[i].ToString();
    return s;
}

std::optional<ScheduleEntry> ParseSchedule(const std::string& id, const std::string& spec) {
    auto gid = Guid::FromString(id);
    if (!gid) return std::nullopt;
    ScheduleEntry e;
    e.id = *gid;
    std::set<std::string> seen;
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t semi = spec.find(';', pos);
        if (semi == std::string::npos) semi = spec.size();
        const std::string kv = spec.substr(pos, semi - pos);
        pos = semi + 1;
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) return std::nullopt;
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        if (!seen.insert(k).second) return std::nullopt;
        long n = 0;
        if (k == "v") { if (!ParseInt(v, n) || n != 1) return std::nullopt; }
        else if (k == "name") e.name = Utf8ToWide(Unesc(v));
        else if (k == "days") { if (!ParseInt(v, n) || n < 0) return std::nullopt; e.daysMask = static_cast<unsigned>(n); }
        else if (k == "start") { if (!ParseInt(v, n)) return std::nullopt; e.startMinute = static_cast<int>(n); }
        else if (k == "dur") { if (!ParseInt(v, n)) return std::nullopt; e.duration = minutes(n); }
        else if (k == "on") e.enabled = v == "1";
        else if (k == "tm") e.blockTaskManager = v == "1";
        else if (k == "rules") {
            size_t p = 0;
            while (p < v.size()) {
                size_t c = v.find(',', p);
                if (c == std::string::npos) c = v.size();
                auto g = Guid::FromString(v.substr(p, c - p));
                if (!g) return std::nullopt;
                e.ruleIds.push_back(*g);
                p = c + 1;
            }
        }   // unknown keys are ignored (forward compatibility)
    }
    if (!IsValidSchedule(e)) return std::nullopt;
    return e;
}

std::optional<Occurrence> ActiveOccurrence(const ScheduleEntry& e, TimePoint now, const ITimeZone& tz) {
    if (!e.enabled || !IsValidSchedule(e)) return std::nullopt;
    const LocalTime today = tz.ToLocal(now);
    for (int back = 1; back >= 0; --back) {   // yesterday first: a window may run past midnight (max 3 h)
        LocalTime d = AddDays(today, -back);
        if (!(e.daysMask & (1u << d.weekday))) continue;
        d.minuteOfDay = e.startMinute;
        const TimePoint start = tz.FromLocal(d);
        const TimePoint end = start + e.duration;
        if (start <= now && now < end) return Occurrence{start, end};
    }
    return std::nullopt;
}

std::optional<Occurrence> NextOccurrence(const ScheduleEntry& e, TimePoint now, const ITimeZone& tz) {
    if (!e.enabled || !IsValidSchedule(e)) return std::nullopt;
    const LocalTime today = tz.ToLocal(now);
    for (int ahead = 0; ahead <= 8; ++ahead) {
        LocalTime d = AddDays(today, ahead);
        if (!(e.daysMask & (1u << d.weekday))) continue;
        d.minuteOfDay = e.startMinute;
        const TimePoint start = tz.FromLocal(d);
        if (start > now) return Occurrence{start, start + e.duration};
    }
    return std::nullopt;
}

SessionConfig BuildSessionConfig(const ScheduleEntry& e, const std::vector<BlockRule>& allRules) {
    SessionConfig c;
    c.duration = e.duration;
    c.blockTaskManager = e.blockTaskManager;
    std::set<std::string> wanted;
    for (const auto& g : e.ruleIds) wanted.insert(g.ToString());
    for (const auto& r : allRules) {
        if (!wanted.count(r.id.ToString())) continue;
        switch (r.type) {
            case RuleType::ProcessExe: c.processBlockRules.push_back(r); break;
            case RuleType::WindowTitle: c.windowTitleRules.push_back(r); break;
            case RuleType::Website: c.websiteBlockRules.push_back(r.target); break;
            case RuleType::AllowListExe: c.allowListExePaths.push_back(r.target); break;
            case RuleType::AllowListFolder: c.allowListFolders.push_back(r.target); break;
        }
    }
    return c;
}
