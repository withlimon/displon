#include "engine/Scheduler.h"

#include <algorithm>

using namespace std::chrono;

namespace {
long long Ms(TimePoint t) { return duration_cast<milliseconds>(t.time_since_epoch()).count(); }
}

bool Scheduler::AddSchedule(const ScheduleEntry& e) {
    if (!IsValidSchedule(e)) return false;
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const ScheduleEntry& x) { return x.id == e.id; });
    if (it != entries_.end()) *it = e; else entries_.push_back(e);
    return true;
}

bool Scheduler::RemoveSchedule(const Guid& id) {
    const auto n = std::erase_if(entries_, [&](const ScheduleEntry& x) { return x.id == id; });
    return n > 0;
}

void Scheduler::SetEnabled(const Guid& id, bool enabled) {
    for (auto& e : entries_) if (e.id == id) e.enabled = enabled;
}

Scheduler::TickResult Scheduler::Tick(TimePoint now) {
    // Forget occurrences older than 2 days so the map stays tiny.
    std::erase_if(handled_, [&](const auto& kv) { return kv.first.second < Ms(now - hours(48)); });

    TickResult res;
    for (const auto& e : entries_) {
        const auto occ = ActiveOccurrence(e, now, tz_);
        if (!occ) continue;
        const auto key = std::make_pair(e.id.ToString(), Ms(occ->start));
        if (handled_.count(key)) continue;

        if (engine_.State() == EngineState::Locked) { res = {Outcome::EngineBusy, e.id}; continue; }   // retry next tick

        const auto remaining = duration_cast<seconds>(occ->end - now);
        if (remaining < kMinSessionDuration) { handled_[key] = true; res = {Outcome::SkippedTooShort, e.id}; continue; }

        SessionConfig cfg = BuildSessionConfig(e, rules_());
        const bool empty = cfg.processBlockRules.empty() && cfg.windowTitleRules.empty() && cfg.websiteBlockRules.empty() &&
                           cfg.allowListExePaths.empty() && cfg.allowListFolders.empty() && !cfg.blockTaskManager;
        if (empty) { handled_[key] = true; res = {Outcome::SkippedNoRules, e.id}; continue; }

        cfg.duration = minutes((remaining.count() + 59) / 60);   // round UP; the engine clamps to 5..180
        switch (engine_.StartSession(cfg)) {
            case StartResult::Started: handled_[key] = true; return {Outcome::Started, e.id};
            case StartResult::AlreadyLocked: res = {Outcome::EngineBusy, e.id}; break;
            case StartResult::PersistFailed: res = {Outcome::PersistFailed, e.id}; break;   // not handled: retry
        }
    }
    return res;
}

std::optional<Occurrence> Scheduler::NextStart(TimePoint now) const {
    std::optional<Occurrence> best;
    for (const auto& e : entries_) {
        const auto o = NextOccurrence(e, now, tz_);
        if (o && (!best || o->start < best->start)) best = o;
    }
    return best;
}
