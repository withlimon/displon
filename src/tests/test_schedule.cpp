// Phase 5 / Module F: schedule math, serialization, ConfigStore persistence and the Scheduler.
#include <chrono>

#include "data/ConfigStore.h"
#include "engine/Scheduler.h"
#include "tests/TestHarness.h"

using namespace std::chrono;

namespace {

// Fixed UTC offset zone (e.g. +06:00 Dhaka). Real DST behavior is the Win32 backend's job.
struct FixedZone final : ITimeZone {
    minutes offset;
    explicit FixedZone(int hoursOff = 6) : offset(hours(hoursOff)) {}
    LocalTime ToLocal(TimePoint t) const override {
        const TimePoint l = t + offset;
        const sys_days d = floor<days>(l);
        const year_month_day ymd{d};
        LocalTime lt;
        lt.year = static_cast<int>(ymd.year());
        lt.month = static_cast<int>(static_cast<unsigned>(ymd.month()));
        lt.day = static_cast<int>(static_cast<unsigned>(ymd.day()));
        lt.minuteOfDay = static_cast<int>(duration_cast<minutes>(l - d).count());
        lt.weekday = static_cast<int>((weekday{d}.c_encoding() + 6) % 7);
        return lt;
    }
    TimePoint FromLocal(const LocalTime& lt) const override {
        const sys_days d = sys_days{year{lt.year} / month{static_cast<unsigned>(lt.month)} / day{static_cast<unsigned>(lt.day)}};
        return TimePoint(d) + minutes(lt.minuteOfDay) - offset;
    }
};

// Local wall-clock time in the +6 zone. 2026-10-12 is a Monday.
TimePoint L(int m, int d, int hh, int mm = 0, int sec = 0) {
    return FixedZone().FromLocal({2026, m, d, hh * 60 + mm, 0}) + seconds(sec);
}

struct MemStore final : ILockStateStore {
    std::optional<LockState> st;
    bool Save(const LockState& s) override { st = s; return true; }
    std::optional<LockState> Load() override { return st; }
    void Clear() override { st.reset(); }
};

struct Clock { TimePoint now; };

struct Rig {
    FixedZone zone;
    MemStore store;
    Clock clock;
    EngineCore engine;
    std::vector<BlockRule> rules;
    Scheduler sched;
    explicit Rig(TimePoint t)
        : clock{t}, engine(store, [this] { return clock.now; }), sched(engine, zone, [this] { return rules; }) {}
};

ScheduleEntry Weekdays9to10(const std::vector<BlockRule>& rules) {
    std::vector<Guid> ids;
    for (auto& r : rules) ids.push_back(r.id);
    return MakeSchedule(L"Work", kMon | kTue | kWed | kThu | kFri, 9 * 60, minutes(60), ids);
}

}  // namespace

TEST(Sched_validation) {
    auto e = MakeSchedule(L"x", kMon, 0, minutes(30), {});
    CHECK(IsValidSchedule(e));
    auto bad = e; bad.daysMask = 0; CHECK(!IsValidSchedule(bad));
    bad = e; bad.daysMask = 128; CHECK(!IsValidSchedule(bad));
    bad = e; bad.startMinute = 1440; CHECK(!IsValidSchedule(bad));
    bad = e; bad.startMinute = -1; CHECK(!IsValidSchedule(bad));
    bad = e; bad.duration = minutes(0); CHECK(!IsValidSchedule(bad));
    bad = e; bad.duration = minutes(24 * 60 + 1); CHECK(!IsValidSchedule(bad));
    bad = e; bad.name.clear(); CHECK(!IsValidSchedule(bad));
    bad = e; bad.id = Guid{}; CHECK(!IsValidSchedule(bad));
    e.duration = minutes(5); CHECK(IsValidSchedule(e));
    e.duration = minutes(180); CHECK(IsValidSchedule(e));
}

TEST(Sched_serialize_round_trip_incl_bangla_and_hostile_name) {
    auto e = MakeSchedule(L"\u09AB\u09CB\u09B2\u09CD\u09A1; name=evil%41\nrules=x", kSat | kSun, 1439, minutes(180),
                          {Guid::Generate(), Guid::Generate()}, true);
    e.enabled = false;
    const auto p = ParseSchedule(e.id.ToString(), SerializeSchedule(e));
    CHECK(p.has_value());
    if (p) {
        CHECK(p->name == e.name && p->daysMask == e.daysMask && p->startMinute == 1439);
        CHECK(p->duration == minutes(180) && !p->enabled && p->blockTaskManager);
        CHECK(p->ruleIds.size() == 2 && p->ruleIds[0] == e.ruleIds[0] && p->ruleIds[1] == e.ruleIds[1]);
    }
}

TEST(Sched_parse_rejects_tampered_specs) {
    auto e = MakeSchedule(L"x", kMon, 60, minutes(30), {});
    const std::string id = e.id.ToString();
    const std::string good = SerializeSchedule(e);
    CHECK(ParseSchedule(id, good));
    CHECK(!ParseSchedule("not-a-guid", good));
    CHECK(!ParseSchedule(id, ""));
    CHECK(!ParseSchedule(id, "garbage"));
    CHECK(!ParseSchedule(id, "v=2;name=x;days=1;start=0;dur=30;on=1;tm=0;rules="));          // unknown version
    CHECK(!ParseSchedule(id, "v=1;name=x;days=0;start=0;dur=30;on=1;tm=0;rules="));          // no days
    CHECK(!ParseSchedule(id, "v=1;name=x;days=1;start=9999;dur=30;on=1;tm=0;rules="));       // bad start
    CHECK(!ParseSchedule(id, "v=1;name=x;days=1;start=0;dur=9999;on=1;tm=0;rules="));        // bad duration
    CHECK(!ParseSchedule(id, "v=1;name=x;days=1;days=2;start=0;dur=30;on=1;tm=0;rules="));   // duplicate key
    CHECK(!ParseSchedule(id, "v=1;name=x;days=1;start=0;dur=30;on=1;tm=0;rules=zzz"));       // bad rule id
    CHECK(ParseSchedule(id, good + ";future=1"));                                            // unknown keys tolerated
}

TEST(Sched_active_occurrence_boundaries) {
    FixedZone z;
    const auto e = Weekdays9to10({});
    CHECK(!ActiveOccurrence(e, L(10, 12, 8, 59, 59), z));          // Monday 08:59:59
    CHECK(ActiveOccurrence(e, L(10, 12, 9, 0, 0), z));             // exactly at start: active
    CHECK(ActiveOccurrence(e, L(10, 12, 9, 59, 59), z));
    CHECK(!ActiveOccurrence(e, L(10, 12, 10, 0, 0), z));           // exactly at end: over
    CHECK(!ActiveOccurrence(e, L(10, 17, 9, 30), z));              // Saturday
    CHECK(!ActiveOccurrence(e, L(10, 18, 9, 30), z));              // Sunday
    CHECK(ActiveOccurrence(e, L(10, 16, 9, 30), z));               // Friday
    auto off = e; off.enabled = false;
    CHECK(!ActiveOccurrence(off, L(10, 12, 9, 30), z));
}

TEST(Sched_overnight_window_crosses_midnight_and_weekday_rule) {
    FixedZone z;
    auto e = MakeSchedule(L"Night", kFri, 22 * 60, minutes(180), {});   // Fri 22:00 -> Sat 01:00
    CHECK(ActiveOccurrence(e, L(10, 16, 23, 0), z));                    // Fri 23:00
    const auto sat = ActiveOccurrence(e, L(10, 17, 0, 30), z);          // Sat 00:30 still belongs to Friday's window
    CHECK(sat.has_value());
    if (sat) CHECK(sat->start == L(10, 16, 22, 0) && sat->end == L(10, 17, 1, 0));
    CHECK(!ActiveOccurrence(e, L(10, 17, 1, 0), z));
    CHECK(!ActiveOccurrence(e, L(10, 17, 22, 30), z));                  // Saturday night is NOT a Friday window
}

TEST(Sched_next_occurrence_and_month_year_rollover) {
    FixedZone z;
    const auto e = Weekdays9to10({});
    const auto n = NextOccurrence(e, L(10, 12, 9, 0), z);               // exactly at start: next is tomorrow
    CHECK(n && n->start == L(10, 13, 9, 0) && n->end == L(10, 13, 10, 0));
    const auto fri = NextOccurrence(e, L(10, 16, 10, 0), z);            // after Friday's window: Monday
    CHECK(fri && fri->start == L(10, 19, 9, 0));
    const auto ny = NextOccurrence(MakeSchedule(L"Y", kEveryDay, 0, minutes(5), {}), L(12, 31, 23, 59), z);
    CHECK(ny && z.ToLocal(ny->start).year == 2027 && z.ToLocal(ny->start).month == 1 && z.ToLocal(ny->start).day == 1);
    CHECK(!NextOccurrence(MakeSchedule(L"x", kMon, 0, minutes(5), {}), L(10, 12, 1), z) == false);
}

TEST(Sched_build_config_groups_rules_and_ignores_dangling) {
    std::vector<BlockRule> all{MakeRule(RuleType::ProcessExe, L"game.exe"), MakeRule(RuleType::WindowTitle, L"YouTube"),
                               MakeRule(RuleType::Website, L"reddit.com"), MakeRule(RuleType::AllowListExe, L"C:\\a.exe"),
                               MakeRule(RuleType::AllowListFolder, L"C:\\Work"), MakeRule(RuleType::ProcessExe, L"unused.exe")};
    auto e = MakeSchedule(L"x", kMon, 0, minutes(45), {all[0].id, all[1].id, all[2].id, all[3].id, all[4].id, Guid::Generate()}, true);
    const auto c = BuildSessionConfig(e, all);
    CHECK(c.processBlockRules.size() == 1 && c.windowTitleRules.size() == 1 && c.websiteBlockRules.size() == 1);
    CHECK(c.allowListExePaths.size() == 1 && c.allowListFolders.size() == 1);
    CHECK(c.blockTaskManager && c.duration == minutes(45));
    CHECK(IsAllowListEngaged(c));
}

TEST(Sched_store_persists_schedules_and_skips_corrupt_rows) {
    auto store = ConfigStore::Open(":memory:");
    CHECK(store != nullptr);
    if (!store) return;
    const auto a = MakeSchedule(L"A", kMon, 60, minutes(30), {});
    const auto b = MakeSchedule(L"B", kSun, 120, minutes(60), {Guid::Generate()}, true);
    CHECK(store->SaveSchedule(a) && store->SaveSchedule(b));
    auto a2 = a; a2.name = L"A2"; a2.enabled = false;
    CHECK(store->SaveSchedule(a2));                                  // upsert
    auto all = store->LoadSchedules();
    CHECK(all.size() == 2 && all[0].name == L"A2" && !all[0].enabled && all[1].name == L"B");
    auto invalid = a; invalid.daysMask = 0;
    CHECK(!store->SaveSchedule(invalid));
    CHECK(store->DeleteSchedule(a.id) && !store->DeleteSchedule(a.id));
    CHECK(store->LoadSchedules().size() == 1);
}

// ---------- Scheduler ----------
TEST(Sched_starts_session_at_window_open_with_correct_unlock_time) {
    Rig r(L(10, 12, 8, 59, 50));
    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    CHECK(r.sched.AddSchedule(Weekdays9to10(r.rules)));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Nothing);
    CHECK(r.engine.State() == EngineState::Idle);
    r.clock.now = L(10, 12, 9, 0, 0);
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Started);
    CHECK(r.engine.State() == EngineState::Locked);
    CHECK(r.engine.UnlockTime() && *r.engine.UnlockTime() == L(10, 12, 10, 0, 0));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Nothing);   // not started twice
}

TEST(Sched_late_start_runs_only_remaining_time) {
    Rig r(L(10, 12, 9, 20));                                  // service came up 20 min into the window
    r.rules = {MakeRule(RuleType::Website, L"reddit.com")};
    r.sched.AddSchedule(Weekdays9to10(r.rules));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Started);
    CHECK(r.engine.UnlockTime() && *r.engine.UnlockTime() == L(10, 12, 10, 0));
}

TEST(Sched_remaining_under_minimum_is_skipped_not_extended) {
    Rig r(L(10, 12, 9, 59) + std::chrono::seconds(30));      // 30 s left < 1 min engine minimum
    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    r.sched.AddSchedule(Weekdays9to10(r.rules));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::SkippedTooShort);
    CHECK(r.engine.State() == EngineState::Idle);
    r.clock.now = L(10, 12, 9, 59) + std::chrono::seconds(45);
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Nothing);   // handled once
}

TEST(Sched_busy_engine_defers_then_starts_with_remaining_time) {
    Rig r(L(10, 12, 9, 0));
    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    SessionConfig manual; manual.duration = minutes(20); manual.processBlockRules = r.rules;
    CHECK(r.engine.StartSession(manual) == StartResult::Started);           // manual 09:00-09:20
    r.sched.AddSchedule(Weekdays9to10(r.rules));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::EngineBusy);
    r.clock.now = L(10, 12, 9, 20, 1);
    r.engine.Tick();                                                         // manual session expires
    CHECK(r.engine.State() == EngineState::Idle);
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Started);
    CHECK(r.engine.UnlockTime() && *r.engine.UnlockTime() <= L(10, 12, 10, 1));   // ends with the window (+<1 min rounding)
}

TEST(Sched_no_rules_or_disabled_never_start) {
    Rig r(L(10, 12, 9, 10));
    r.sched.AddSchedule(MakeSchedule(L"empty", kEveryDay, 9 * 60, minutes(60), {Guid::Generate()}));   // dangling rule id
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::SkippedNoRules);
    CHECK(r.engine.State() == EngineState::Idle);

    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    auto e = Weekdays9to10(r.rules); e.enabled = false;
    Rig r2(L(10, 12, 9, 10)); r2.rules = r.rules;
    r2.sched.AddSchedule(e);
    CHECK(r2.sched.Tick(r2.clock.now).outcome == Scheduler::Outcome::Nothing);
    r2.sched.SetEnabled(e.id, true);
    CHECK(r2.sched.Tick(r2.clock.now).outcome == Scheduler::Outcome::Started);
}

TEST(Sched_task_manager_flag_and_next_start) {
    Rig r(L(10, 12, 7, 0));
    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    auto a = Weekdays9to10(r.rules);
    auto b = MakeSchedule(L"Early", kMon, 8 * 60, minutes(30), {r.rules[0].id}, true);
    r.sched.AddSchedule(a); r.sched.AddSchedule(b);
    const auto n = r.sched.NextStart(r.clock.now);
    CHECK(n && n->start == L(10, 12, 8, 0));
    r.clock.now = L(10, 12, 8, 0);
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Started);
    CHECK(r.engine.ActiveConfig() && r.engine.ActiveConfig()->blockTaskManager);
    CHECK(r.sched.RemoveSchedule(a.id) && !r.sched.RemoveSchedule(a.id));
    CHECK(!r.sched.AddSchedule(MakeSchedule(L"bad", 0, 0, minutes(30), {})));
}

TEST(Sched_overnight_window_starts_session_after_midnight_boot) {
    Rig r(L(10, 17, 0, 30));                                  // Sat 00:30, Friday-night window still open
    r.rules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    r.sched.AddSchedule(MakeSchedule(L"Night", kFri, 22 * 60, minutes(180), {r.rules[0].id}));
    CHECK(r.sched.Tick(r.clock.now).outcome == Scheduler::Outcome::Started);
    CHECK(r.engine.UnlockTime() && *r.engine.UnlockTime() == L(10, 17, 1, 0));
}
