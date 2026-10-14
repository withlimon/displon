// Minimal dependency-free unit tests for Phase 1 (engine + ConfigStore).
#include <sqlite3.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <vector>

#include "common/Guid.h"
#include "common/Sha256.h"
#include "common/Utf.h"
#include "data/ConfigStore.h"
#include "engine/EngineCore.h"

#include "tests/TestHarness.h"

using namespace std::chrono;

namespace {
const TimePoint kT0 = system_clock::time_point(seconds(1'800'000'000));

struct FakeClock {
    TimePoint now = kT0;
    EngineCore::NowFn Fn() { return [this] { return now; }; }
};

struct FailingStore final : ILockStateStore {
    bool Save(const LockState&) override { return false; }
    std::optional<LockState> Load() override { return std::nullopt; }
    void Clear() override {}
};

SessionConfig BasicConfig(minutes d = minutes(30)) {
    SessionConfig c;
    c.duration = d;
    return c;
}
bool HasRule(const std::vector<BlockRule>& v, RuleType t, std::wstring_view target) {
    for (const auto& r : v)
        if (r.type == t && r.target == target) return true;
    return false;
}
}  // namespace

// ---------- common ----------
TEST(Sha256_known_vectors) {
    CHECK(ToHex(Sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ToHex(Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ToHex(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Utf8_round_trip_including_bangla_and_emoji) {
    const std::wstring samples[] = {L"plain.exe", L"\u09AB\u09CB\u09B2\u09CD\u09A1 \u099F\u09BE\u09B0\u09CD\u0995\u09BF",
                                    L"caf\u00E9", L"\U0001F983 turkey"};
    for (const auto& s : samples) CHECK(Utf8ToWide(WideToUtf8(s)) == s);
    CHECK(WideToUtf8(L"A") == "A");
    CHECK(WideToUtf8(L"\u09AB").size() == 3);                 // Bengali letter = 3 UTF-8 bytes
    CHECK(Utf8ToWide(std::string("\xFF\xFE", 2)) == L"\uFFFD\uFFFD");   // invalid bytes -> U+FFFD
    CHECK(Utf8ToWide(std::string("\xC0\x80", 2)) == L"\uFFFD\uFFFD");   // overlong NUL rejected
}

TEST(Guid_round_trip_and_uniqueness) {
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        Guid g = Guid::Generate();
        CHECK(!g.IsNull());
        auto parsed = Guid::FromString(g.ToString());
        CHECK(parsed && *parsed == g);
        seen.insert(g.ToString());
    }
    CHECK(seen.size() == 100);
    CHECK(!Guid::FromString("not-a-guid"));
    CHECK(!Guid::FromString("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz"));
    CHECK(Guid{}.IsNull());
}

// ---------- engine ----------
TEST(Duration_clamp_boundaries) {
    CHECK(ClampDuration(minutes(-10)) == minutes(1));
    CHECK(ClampDuration(minutes(0)) == minutes(1));
    CHECK(ClampDuration(minutes(1)) == minutes(1));
    CHECK(ClampDuration(minutes(5)) == minutes(5));
    CHECK(ClampDuration(minutes(1440)) == minutes(1440));
    CHECK(ClampDuration(minutes(1441)) == minutes(1440));
    CHECK(ClampDuration(minutes(100000)) == minutes(1440));
}

TEST(AllowList_engaged_detection) {
    SessionConfig c;
    CHECK(!IsAllowListEngaged(c));
    c.allowListFolders.push_back(L"C:\\Code");
    CHECK(IsAllowListEngaged(c));
    c.allowListFolders.clear();
    c.allowListExePaths.push_back(L"C:\\x.exe");
    CHECK(IsAllowListEngaged(c));
}

TEST(StartSession_sets_absolute_unlock_and_clamps) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    CHECK(eng.State() == EngineState::Idle);

    SessionConfig c = BasicConfig(minutes(5000));           // over the max (24 h)
    c.absoluteUnlockTime = kT0 + hours(1000);               // caller-supplied value must be ignored
    CHECK(eng.StartSession(c) == StartResult::Started);
    CHECK(eng.State() == EngineState::Locked);
    CHECK(*eng.UnlockTime() == kT0 + minutes(1440));
    CHECK(eng.ActiveConfig()->duration == minutes(1440));
    CHECK(store.Load().has_value());

    EngineCore eng2(store, clk.Fn());                        // fresh engine, below-min duration
    InMemoryLockStateStore store2;
    EngineCore eng3(store2, clk.Fn());
    CHECK(eng3.StartSession(BasicConfig(minutes(0))) == StartResult::Started);
    CHECK(*eng3.UnlockTime() == kT0 + minutes(1));
}

TEST(Only_one_session_at_a_time) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    CHECK(eng.StartSession(BasicConfig()) == StartResult::Started);
    auto first = eng.ActiveSessionId();
    CHECK(eng.StartSession(BasicConfig()) == StartResult::AlreadyLocked);
    CHECK(eng.ActiveSessionId() == first);
}

TEST(ActiveRules_flatten_all_groups_together) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());

    SessionConfig c = BasicConfig();
    c.allowListExePaths = {L"C:\\Tools\\code.exe"};
    c.allowListFolders = {L"C:\\Work"};
    c.processBlockRules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    c.windowTitleRules = {MakeRule(RuleType::WindowTitle, L"YouTube", WindowTitleAction::Suppress)};
    c.websiteBlockRules = {L"reddit.com"};
    c.blockTaskManager = true;
    CHECK(eng.StartSession(c) == StartResult::Started);

    const auto& r = eng.ActiveRules();
    CHECK(HasRule(r, RuleType::AllowListExe, L"C:\\Tools\\code.exe"));
    CHECK(HasRule(r, RuleType::AllowListFolder, L"C:\\Work"));
    CHECK(HasRule(r, RuleType::ProcessExe, L"game.exe"));
    CHECK(HasRule(r, RuleType::WindowTitle, L"YouTube"));     // active WITH an allow-list engaged
    CHECK(HasRule(r, RuleType::Website, L"reddit.com"));
    CHECK(HasRule(r, RuleType::ProcessExe, L"taskmgr.exe"));  // implicit Task Manager rule
    CHECK(r.size() == 6);
    CHECK(eng.ActiveRules()[0].id == r[0].id);                // ids are stable between calls
}

TEST(Task_manager_rule_not_duplicated_and_absent_when_off) {
    FakeClock clk;
    {
        InMemoryLockStateStore store;
        EngineCore eng(store, clk.Fn());
        SessionConfig c = BasicConfig();
        c.processBlockRules = {MakeRule(RuleType::ProcessExe, L"taskmgr.exe")};
        c.blockTaskManager = true;
        eng.StartSession(c);
        CHECK(eng.ActiveRules().size() == 1);
    }
    {
        InMemoryLockStateStore store;
        EngineCore eng(store, clk.Fn());
        eng.StartSession(BasicConfig());
        CHECK(!HasRule(eng.ActiveRules(), RuleType::ProcessExe, L"taskmgr.exe"));
    }
}

TEST(Untrusted_empty_targets_are_dropped) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    SessionConfig c = BasicConfig();
    BlockRule bad;                       // empty target, built without MakeRule
    bad.id = Guid::Generate();
    c.processBlockRules = {bad, MakeRule(RuleType::ProcessExe, L"ok.exe")};
    c.websiteBlockRules = {L"", L"a.com"};
    c.allowListExePaths = {L""};
    eng.StartSession(c);
    CHECK(eng.ActiveRules().size() == 2);
    CHECK(!IsAllowListEngaged(*eng.ActiveConfig()));
}

TEST(Timer_expiry_releases_session_and_clears_store) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    eng.StartSession(BasicConfig(minutes(30)));

    clk.now = kT0 + minutes(30) - seconds(1);
    eng.Tick();
    CHECK(eng.State() == EngineState::Locked);

    clk.now = kT0 + minutes(30);                              // exactly at the unlock time
    eng.Tick();
    CHECK(eng.State() == EngineState::Idle);
    CHECK(eng.ActiveRules().empty());
    CHECK(!store.Load().has_value());
}

TEST(Expired_session_does_not_block_a_new_start) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    eng.StartSession(BasicConfig(minutes(5)));
    clk.now = kT0 + minutes(6);                               // no explicit Tick()
    CHECK(eng.StartSession(BasicConfig(minutes(10))) == StartResult::Started);
    CHECK(*eng.UnlockTime() == kT0 + minutes(16));
}

TEST(Password_unlock_and_failure_counting) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    eng.StartSession(BasicConfig(), L"s3cret-\u09AB");
    const Guid id = *eng.ActiveSessionId();

    CHECK(!eng.StopSession(id, L"wrong"));
    CHECK(!eng.StopSession(id, L""));
    CHECK(eng.FailedUnlockAttempts() == 2);
    CHECK(eng.State() == EngineState::Locked);
    CHECK(!eng.StopSession(Guid::Generate(), L"s3cret-\u09AB"));   // wrong session id
    CHECK(eng.State() == EngineState::Locked);

    CHECK(eng.StopSession(id, L"s3cret-\u09AB"));
    CHECK(eng.State() == EngineState::Idle);
    CHECK(!store.Load().has_value());
}

TEST(Session_without_password_cannot_be_ended_early) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    eng.StartSession(BasicConfig());
    const Guid id = *eng.ActiveSessionId();
    CHECK(!eng.StopSession(id, L""));
    CHECK(!eng.StopSession(id, L"anything"));
    CHECK(eng.State() == EngineState::Locked);
}

TEST(Password_is_not_stored_in_plaintext) {
    PasswordRecord r = MakePasswordRecord(L"hunter2");
    CHECK(r.set);
    CHECK(VerifyPassword(r, L"hunter2"));
    CHECK(!VerifyPassword(r, L"hunter3"));
    CHECK(!MakePasswordRecord(L"").set);
    PasswordRecord r2 = MakePasswordRecord(L"hunter2");       // fresh salt -> different hash
    CHECK(r.hash != r2.hash);
}

TEST(Reboot_mid_session_resumes_with_original_unlock_time) {
    InMemoryLockStateStore store;                             // stands in for the on-disk lock state
    TimePoint originalUnlock;
    Guid sessionId;
    {
        FakeClock clk;
        EngineCore eng(store, clk.Fn());
        SessionConfig c = BasicConfig(minutes(60));
        c.websiteBlockRules = {L"x.com"};
        eng.StartSession(c, L"pw");
        originalUnlock = *eng.UnlockTime();
        sessionId = *eng.ActiveSessionId();
    }                                                         // "power off": engine destroyed

    FakeClock afterBoot;
    afterBoot.now = kT0 + minutes(20);                        // machine back 20 min later
    EngineCore eng(store, afterBoot.Fn());
    CHECK(eng.State() == EngineState::Idle);
    CHECK(eng.ResumeFromPersistedState());
    CHECK(eng.State() == EngineState::Locked);
    CHECK(*eng.UnlockTime() == originalUnlock);               // NOT now + 60 min
    CHECK(*eng.UnlockTime() == kT0 + minutes(60));
    CHECK(*eng.ActiveSessionId() == sessionId);
    CHECK(HasRule(eng.ActiveRules(), RuleType::Website, L"x.com"));
    CHECK(eng.StopSession(sessionId, L"pw"));                 // password record survived too
}

TEST(Reboot_after_expiry_does_not_relock) {
    InMemoryLockStateStore store;
    {
        FakeClock clk;
        EngineCore eng(store, clk.Fn());
        eng.StartSession(BasicConfig(minutes(30)));
    }
    FakeClock late;
    late.now = kT0 + minutes(31);
    EngineCore eng(store, late.Fn());
    CHECK(!eng.ResumeFromPersistedState());
    CHECK(eng.State() == EngineState::Idle);
    CHECK(!store.Load().has_value());
}

TEST(Resume_with_nothing_persisted_is_a_noop) {
    FakeClock clk;
    InMemoryLockStateStore store;
    EngineCore eng(store, clk.Fn());
    CHECK(!eng.ResumeFromPersistedState());
    CHECK(eng.State() == EngineState::Idle);
}

TEST(Fail_closed_when_lock_state_cannot_be_persisted) {
    FakeClock clk;
    FailingStore store;
    EngineCore eng(store, clk.Fn());
    CHECK(eng.StartSession(BasicConfig()) == StartResult::PersistFailed);
    CHECK(eng.State() == EngineState::Idle);
}

// ---------- ConfigStore ----------
TEST(ConfigStore_save_load_update_delete) {
    std::string err;
    auto store = ConfigStore::Open(":memory:", &err);
    CHECK(store != nullptr);
    if (!store) return;
    CHECK(store->LoadRules().empty());

    BlockRule a = MakeRule(RuleType::ProcessExe, L"game.exe");
    BlockRule b = MakeRule(RuleType::WindowTitle, L"\u09AB\u09CB\u09B2\u09CD\u09A1 - YouTube", WindowTitleAction::Suppress);
    BlockRule c = MakeRule(RuleType::AllowListFolder, L"C:\\Users\\\U0001F983\\Code");
    CHECK(store->SaveRule(a));
    CHECK(store->SaveRule(b));
    CHECK(store->SaveRule(c));

    auto loaded = store->LoadRules();
    CHECK(loaded.size() == 3);
    if (loaded.size() == 3) {
        CHECK(loaded[0].id == a.id && loaded[0].target == a.target && loaded[0].type == RuleType::ProcessExe);
        CHECK(loaded[1].id == b.id && loaded[1].target == b.target);
        CHECK(loaded[1].titleAction == WindowTitleAction::Suppress);
        CHECK(loaded[2].target == c.target);
    }

    a.target = L"other.exe";                                  // upsert keeps position and id
    CHECK(store->SaveRule(a));
    loaded = store->LoadRules();
    CHECK(loaded.size() == 3 && loaded[0].id == a.id && loaded[0].target == L"other.exe");

    CHECK(store->DeleteRule(b.id));
    CHECK(!store->DeleteRule(b.id));                          // already gone
    CHECK(store->LoadRules().size() == 2);
}

TEST(ConfigStore_rejects_invalid_rules) {
    auto store = ConfigStore::Open(":memory:");
    CHECK(store != nullptr);
    if (!store) return;
    BlockRule empty;
    empty.id = Guid::Generate();
    CHECK(!store->SaveRule(empty));                           // empty target
    BlockRule nullId;
    nullId.target = L"x.exe";
    CHECK(!store->SaveRule(nullId));                          // null id
    CHECK(store->LoadRules().empty());
}

TEST(ConfigStore_persists_across_reopen_and_has_all_four_tables) {
    const auto path = std::filesystem::temp_directory_path() / "displon_test_config.db";
    std::filesystem::remove(path);
    BlockRule r = MakeRule(RuleType::Website, L"news.example.com");
    {
        auto store = ConfigStore::Open(path.string());
        CHECK(store != nullptr);
        if (!store) return;
        CHECK(store->SaveRule(r));
    }
    {
        auto store = ConfigStore::Open(path.string());
        CHECK(store != nullptr);
        if (!store) return;
        auto loaded = store->LoadRules();
        CHECK(loaded.size() == 1 && loaded[0].id == r.id && loaded[0].target == r.target);
    }
    sqlite3* db = nullptr;                                    // inspect the schema directly
    CHECK(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    std::set<std::string> tables;
    sqlite3_exec(db, "SELECT name FROM sqlite_master WHERE type='table'",
                 [](void* p, int, char** v, char**) {
                     static_cast<std::set<std::string>*>(p)->insert(v[0]);
                     return 0;
                 }, &tables, nullptr);
    sqlite3_close(db);
    CHECK(tables.count("rules") && tables.count("sessions") && tables.count("schedules") && tables.count("history"));
    std::filesystem::remove(path);
}

TEST(ConfigStore_skips_corrupt_rows_from_a_tampered_file) {
    const auto path = std::filesystem::temp_directory_path() / "displon_test_tampered.db";
    std::filesystem::remove(path);
    {
        auto store = ConfigStore::Open(path.string());
        CHECK(store != nullptr);
        if (!store) return;
        CHECK(store->SaveRule(MakeRule(RuleType::ProcessExe, L"good.exe")));
    }
    sqlite3* db = nullptr;
    sqlite3_open(path.string().c_str(), &db);
    sqlite3_exec(db, "INSERT INTO rules VALUES('not-a-guid',0,'bad1.exe',0);"
                     "INSERT INTO rules VALUES('11111111-2222-4333-8444-555555555555',99,'bad2.exe',0);"
                     "INSERT INTO rules VALUES('11111111-2222-4333-8444-555555555556',0,'',0);",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);
    auto store = ConfigStore::Open(path.string());
    CHECK(store != nullptr);
    if (store) {
        auto loaded = store->LoadRules();
        CHECK(loaded.size() == 1 && loaded[0].target == L"good.exe");
    }
    store.reset();
    std::filesystem::remove(path);
}

int main() {
    int run = 0;
    for (const auto& t : Registry()) {
        const int before = g_failures;
        t.fn();
        ++run;
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d failed check(s)\n", run, g_failures);
    return g_failures == 0 ? 0 : 1;
}
