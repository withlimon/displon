// Scheduler (plan Module F): arms a session when a schedule's window is open. Portable; driven by Tick(now).
// Rules: one session at a time (a window that opens during another session starts as soon as the engine
// is free, if still open); each occurrence starts at most once; a late start (service was down at the
// window start) runs only for the REMAINING time; scheduled sessions have no password (timer-only).
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "engine/EngineCore.h"
#include "engine/Schedule.h"

class Scheduler {
public:
    using RulesFn = std::function<std::vector<BlockRule>()>;   // e.g. [&]{ return store.LoadRules(); }

    enum class Outcome { Nothing, Started, EngineBusy, SkippedTooShort, SkippedNoRules, PersistFailed };
    struct TickResult { Outcome outcome = Outcome::Nothing; Guid schedule; };

    Scheduler(EngineCore& engine, const ITimeZone& tz, RulesFn rules) : engine_(engine), tz_(tz), rules_(std::move(rules)) {}

    bool AddSchedule(const ScheduleEntry& e);      // upsert by id; invalid entries rejected
    bool RemoveSchedule(const Guid& id);
    void SetEnabled(const Guid& id, bool enabled);
    const std::vector<ScheduleEntry>& Schedules() const { return entries_; }

    TickResult Tick(TimePoint now);                // call every few seconds
    // Soonest upcoming start over all enabled schedules (for the UI / "Armed" display).
    std::optional<Occurrence> NextStart(TimePoint now) const;

private:
    EngineCore& engine_;
    const ITimeZone& tz_;
    RulesFn rules_;
    std::vector<ScheduleEntry> entries_;
    std::map<std::pair<std::string, long long>, bool> handled_;   // (schedule id, occurrence start ms)
};
