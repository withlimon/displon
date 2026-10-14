// Scheduled blocks (plan Module F): data model, serialization and the pure time math. Portable.
// A schedule = recurring weekly window (days + local start time + duration) that references saved
// rules by id. Local-time conversion goes through ITimeZone so the math is testable without a real zone.
#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "engine/BlockRule.h"

struct LocalTime {
    int year = 1970, month = 1, day = 1;
    int minuteOfDay = 0;   // 0..1439
    int weekday = 0;       // 0 = Monday ... 6 = Sunday (filled by ToLocal; ignored by FromLocal)
};

class ITimeZone {
public:
    virtual ~ITimeZone() = default;
    virtual LocalTime ToLocal(TimePoint t) const = 0;
    // Local wall-clock -> instant. DST gaps/overlaps follow the backend's rule (Win32: gap = shifted forward).
    virtual TimePoint FromLocal(const LocalTime& lt) const = 0;
};

enum DayBit : unsigned { kMon = 1, kTue = 2, kWed = 4, kThu = 8, kFri = 16, kSat = 32, kSun = 64, kEveryDay = 127 };

struct ScheduleEntry {
    Guid id;
    std::wstring name;
    unsigned daysMask = 0;                     // DayBit flags
    int startMinute = 0;                       // local minute of day, 0..1439
    std::chrono::minutes duration{30};         // 5..180 (same contract as a manual session)
    bool enabled = true;
    bool blockTaskManager = false;
    std::vector<Guid> ruleIds;                 // rules in the rule store this schedule activates
};

struct Occurrence {
    TimePoint start, end;
};

bool IsValidSchedule(const ScheduleEntry& e);
ScheduleEntry MakeSchedule(std::wstring name, unsigned daysMask, int startMinute, std::chrono::minutes duration,
                           std::vector<Guid> ruleIds, bool blockTaskManager = false);

// Text form stored in the `schedules.spec` column. Parse validates (the DB file can be edited).
std::string SerializeSchedule(const ScheduleEntry& e);
std::optional<ScheduleEntry> ParseSchedule(const std::string& id, const std::string& spec);

// The occurrence of `e` containing `now` (start <= now < end), including one that began yesterday
// and runs past midnight. nullopt if none or the schedule is disabled/invalid.
std::optional<Occurrence> ActiveOccurrence(const ScheduleEntry& e, TimePoint now, const ITimeZone& tz);
// The first occurrence starting strictly after `now` (searches 8 days ahead).
std::optional<Occurrence> NextOccurrence(const ScheduleEntry& e, TimePoint now, const ITimeZone& tz);

// Builds the session groups for a schedule from the rule store. Dangling ids are ignored;
// `duration` is left at the schedule's value (the Scheduler overrides it for late starts).
SessionConfig BuildSessionConfig(const ScheduleEntry& e, const std::vector<BlockRule>& allRules);
