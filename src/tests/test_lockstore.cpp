#include "tests/TestHarness.h"
#include "common/BootTime.h"
#include "common/Guid.h"
#include "common/Sha256.h"
#include "engine/EngineCore.h"
#include "engine/FileLockStateStore.h"
#include "engine/LockState.h"
#include "engine/ProcessMatcher.h"

#include <filesystem>
#include <fstream>

static std::filesystem::path TempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

TEST(LockStore_SaveLoad_round_trip) {
    const auto path = TempPath("ft-lock-test.state");
    std::filesystem::remove(path);

    FileLockStateStore store(path);
    LockState s;
    s.sessionId = Guid::Generate();
    s.config.duration = std::chrono::minutes(15);
    s.config.blockTaskManager = true;
    s.config.absoluteUnlockTime =
        std::chrono::system_clock::now() + std::chrono::minutes(15);
    s.password = MakePasswordRecord(L"hunter2");

    CHECK(store.Save(s));
    auto loaded = store.Load();
    CHECK(loaded.has_value());
    CHECK(loaded->sessionId == s.sessionId);
    CHECK(loaded->config.duration.count() == 15);
    CHECK(loaded->config.blockTaskManager);
    CHECK(loaded->password.set);
    CHECK(VerifyPassword(loaded->password, L"hunter2"));
    CHECK(!VerifyPassword(loaded->password, L"wrong"));

    store.Clear();
    CHECK(!store.Load().has_value());
    std::filesystem::remove(path);
}

TEST(LockStore_Tamper_detected) {
    const auto path = TempPath("ft-lock-tamper.state");
    std::filesystem::remove(path);

    FileLockStateStore store(path);
    LockState s;
    s.sessionId = Guid::Generate();
    s.config.duration = std::chrono::minutes(10);
    s.config.absoluteUnlockTime = std::chrono::system_clock::now() + std::chrono::minutes(10);
    CHECK(store.Save(s));

    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out << "evil";
    }
    CHECK(!store.Load().has_value());
    std::filesystem::remove(path);
}

TEST(LockStore_Missing_file) {
    const auto path = TempPath("ft-lock-missing.state");
    std::filesystem::remove(path);
    FileLockStateStore store(path);
    CHECK(!store.Load().has_value());
}

// ---------------------------------------------------------------------------------------------------------
// Regression: "the lock fails after a PC restart". The on-disk store used to keep only the unlock time and the
// password, so a resumed session was "Locked" but carried NO rules and enforced nothing.
// ---------------------------------------------------------------------------------------------------------
namespace {

std::filesystem::path FreshDir(const char* name) {
    const auto d = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

bool HasR(const std::vector<BlockRule>& v, RuleType t, std::wstring_view target) {
    for (const auto& r : v)
        if (r.type == t && r.target == target) return true;
    return false;
}

TimePoint At(long long secs) { return TimePoint{std::chrono::seconds(secs)}; }

void WriteSigned(const std::filesystem::path& p, const std::string& payload) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << ToHex(Sha256(payload)) << "\n" << payload;
}

}  // namespace

TEST(LockStore_persists_every_rule_group_and_mode) {
    const auto dir = FreshDir("ft-lock-full");
    FileLockStateStore store(dir / "lock.state");

    LockState s;
    s.sessionId = Guid::Generate();
    SessionConfig& c = s.config;
    c.allowListExePaths = {L"C:\\Program Files\\Proteus 9\\BIN\\PROTEUS.EXE", L"chrome.exe",
                           L"C:\\\u09AC\\\u09BE\\\u0982\\\u09B2\\\u09BE.exe"};   // a Bangla folder name
    c.allowListFolders = {L"D:\\Work"};
    c.processBlockRules = {MakeRule(RuleType::ProcessExe, L"SystemSettings.exe")};
    c.windowTitleRules = {MakeRule(RuleType::WindowTitle, L"line1\nline2 = odd, title", WindowTitleAction::Suppress),
                          MakeRule(RuleType::WindowTitle, L"Close me", WindowTitleAction::Close)};
    c.websiteBlockRules = {L"youtube.com", L"x.com"};
    c.blockTaskManager = true;
    c.blockWindowsSettings = true;
    c.processAction = WindowTitleAction::Suppress;
    c.durationMode = SessionDurationMode::UntilDateTime;
    c.duration = std::chrono::minutes(90);
    c.absoluteUnlockTime = At(1'800'005'400);
    c.sessionStartTime = At(1'800'000'000);
    c.bootEpochSecs = 1'799'000'000;
    c.breakMode = SessionBreakMode::Pomodoro;
    c.pomodoroWorkMin = 50;
    c.pomodoroBreakMin = 10;
    c.allowanceMin = 7;
    s.password = MakePasswordRecord(L"pw");
    CHECK(store.Save(s));

    const auto l = store.Load();
    CHECK(l.has_value());
    if (!l) return;
    CHECK(l->config.allowListExePaths == c.allowListExePaths);
    CHECK(l->config.allowListFolders == c.allowListFolders);
    CHECK(l->config.processBlockRules.size() == 1 && l->config.processBlockRules[0].target == L"SystemSettings.exe");
    CHECK(l->config.windowTitleRules.size() == 2);
    if (l->config.windowTitleRules.size() == 2) {
        CHECK(l->config.windowTitleRules[0].target == L"line1\nline2 = odd, title");
        CHECK(l->config.windowTitleRules[0].titleAction == WindowTitleAction::Suppress);
        CHECK(l->config.windowTitleRules[1].titleAction == WindowTitleAction::Close);
    }
    CHECK(l->config.websiteBlockRules == c.websiteBlockRules);
    CHECK(l->config.blockTaskManager && l->config.blockWindowsSettings);
    CHECK(l->config.processAction == WindowTitleAction::Suppress);
    CHECK(l->config.durationMode == SessionDurationMode::UntilDateTime);
    CHECK(l->config.absoluteUnlockTime == c.absoluteUnlockTime);
    CHECK(l->config.sessionStartTime == c.sessionStartTime);
    CHECK(l->config.bootEpochSecs == c.bootEpochSecs);
    CHECK(l->config.breakMode == SessionBreakMode::Pomodoro);
    CHECK(l->config.pomodoroWorkMin == 50 && l->config.pomodoroBreakMin == 10 && l->config.allowanceMin == 7);
    CHECK(VerifyPassword(l->password, L"pw"));
    std::filesystem::remove_all(dir);
}

TEST(LockStore_reboot_on_disk_still_enforces_the_allow_list) {
    const auto dir = FreshDir("ft-lock-reboot");
    const auto path = dir / "lock.state";
    Guid id;
    {   // before the restart
        FileLockStateStore store(path);
        EngineCore eng(store, [] { return At(1'800'000'000); });
        SessionConfig c;
        c.duration = std::chrono::minutes(60);
        c.allowListExePaths = {L"chrome.exe"};
        c.websiteBlockRules = {L"x.com"};
        c.blockTaskManager = true;
        CHECK(eng.StartSession(c, L"pw") == StartResult::Started);
        id = *eng.ActiveSessionId();
    }
    {   // after the restart: a brand new process, brand new store object, same file
        FileLockStateStore store(path);
        EngineCore eng(store, [] { return At(1'800'000'000 + 1200); });
        CHECK(eng.ResumeFromPersistedState());
        CHECK(*eng.UnlockTime() == At(1'800'000'000 + 3600));
        CHECK(HasR(eng.ActiveRules(), RuleType::AllowListExe, L"chrome.exe"));
        CHECK(HasR(eng.ActiveRules(), RuleType::Website, L"x.com"));
        CHECK(HasR(eng.ActiveRules(), RuleType::ProcessExe, L"taskmgr.exe"));
        // The part that used to be missing: the process policy is engaged, so non-whitelisted apps die.
        CHECK(BuildProcessPolicy(eng.ActiveRules()).AllowListEngaged());
        CHECK(eng.StopSession(id, L"pw"));
    }
    std::filesystem::remove_all(dir);
}

TEST(LockStore_deleting_or_corrupting_one_copy_does_not_release_the_lock) {
    const auto dir = FreshDir("ft-lock-mirror");
    const auto primary = dir / "a" / "lock.state";
    const auto mirror = dir / "b" / "lock.state";
    FileLockStateStore store(primary, {mirror});

    LockState s;
    s.sessionId = Guid::Generate();
    s.config.allowListExePaths = {L"code.exe"};
    s.config.absoluteUnlockTime = At(1'800'003'600);
    CHECK(store.Save(s));
    CHECK(std::filesystem::exists(primary) && std::filesystem::exists(mirror));

    std::filesystem::remove(primary);                          // user deletes the obvious file
    auto l = store.Load();
    CHECK(l.has_value() && l->config.allowListExePaths.size() == 1);
    CHECK(std::filesystem::exists(primary));                   // ...and it is put back

    {   // user garbles the mirror
        std::ofstream out(mirror, std::ios::binary | std::ios::trunc);
        out << "garbage";
    }
    l = FileLockStateStore(primary, {mirror}).Load();
    CHECK(l.has_value());
    CHECK(FileLockStateStore(mirror).Load().has_value());     // mirror repaired from the good copy

    store.Clear();                                             // a normal unlock removes every copy
    CHECK(!std::filesystem::exists(primary) && !std::filesystem::exists(mirror));
    CHECK(!store.Load().has_value());
    std::filesystem::remove_all(dir);
}

TEST(LockStore_the_copy_with_the_latest_unlock_wins) {
    const auto dir = FreshDir("ft-lock-latest");
    const auto a = dir / "a.state", b = dir / "b.state";
    LockState early, late;
    early.sessionId = late.sessionId = Guid::Generate();
    early.config.absoluteUnlockTime = At(1'800'000'600);
    late.config.absoluteUnlockTime = At(1'800'009'000);
    CHECK(FileLockStateStore(a).Save(early));
    CHECK(FileLockStateStore(b).Save(late));
    const auto l = FileLockStateStore(a, {b}).Load();
    CHECK(l.has_value() && l->config.absoluteUnlockTime == At(1'800'009'000));
    std::filesystem::remove_all(dir);
}

TEST(LockStore_v1_files_from_older_builds_still_load) {
    const auto dir = FreshDir("ft-lock-v1");
    const auto path = dir / "lock.state";
    WriteSigned(path, "v=1\nid=" + Guid::Generate().ToString() + "\nunlock=1800003600\nduration=60\ntaskmgr=1\npwd_set=0\n");
    const auto l = FileLockStateStore(path).Load();
    CHECK(l.has_value());
    if (l) {
        CHECK(l->config.blockTaskManager);
        CHECK(l->config.absoluteUnlockTime == At(1'800'003'600));
        CHECK(l->config.allowListExePaths.empty());
    }
    std::filesystem::remove_all(dir);
}

TEST(LockStore_rejects_malformed_fields_even_with_a_valid_checksum) {
    const auto dir = FreshDir("ft-lock-bad");
    const auto path = dir / "lock.state";
    const std::string head = "v=2\nid=" + Guid::Generate().ToString() + "\nunlock=1800003600\n";
    for (const char* bad : {"allow_exe=zz\n", "allow_exe=abc\n", "mode=7\n", "break=-1\n", "pwork=0\n",
                            "win=9:41\n", "win=0\n", "unlock=12x\n", "no_equals_sign\n"}) {
        WriteSigned(path, head + bad);
        CHECK(!FileLockStateStore(path).Load().has_value());
    }
    WriteSigned(path, head + "allow_exe=" + "63686f6d652e657865" + "\n");   // "chrome.exe" - the good case
    CHECK(FileLockStateStore(path).Load().has_value());
    std::filesystem::remove_all(dir);
}

TEST(Resume_until_restart_ends_on_a_new_boot_but_survives_the_same_boot) {
    InMemoryLockStateStore store;
    const auto clock = [] { return At(1'800'000'000); };
    {
        EngineCore eng(store, clock, [] { return std::int64_t{1'799'990'000}; });
        SessionConfig c;
        c.durationMode = SessionDurationMode::UntilRestart;
        c.allowListExePaths = {L"chrome.exe"};
        CHECK(eng.StartSession(c) == StartResult::Started);
    }
    {   // the app is relaunched inside the SAME Windows boot (crash, watchdog, manual start): still locked
        EngineCore eng(store, clock, [] { return std::int64_t{1'799'990'020}; });
        CHECK(eng.ResumeFromPersistedState());
        CHECK(eng.State() == EngineState::Locked);
    }
    {   // the machine was restarted: "until restart" has been reached, the session must end
        EngineCore eng(store, clock, [] { return std::int64_t{1'800'000'500}; });
        CHECK(!eng.ResumeFromPersistedState());
        CHECK(eng.State() == EngineState::Idle);
        CHECK(!store.Load().has_value());
    }
}

TEST(Resume_timed_lock_ignores_the_boot_identity) {
    InMemoryLockStateStore store;
    const auto clock = [] { return At(1'800'000'000); };
    {
        EngineCore eng(store, clock, [] { return std::int64_t{1'799'990'000}; });
        SessionConfig c;
        c.duration = std::chrono::minutes(120);
        CHECK(eng.StartSession(c) == StartResult::Started);
    }
    EngineCore eng(store, [] { return At(1'800'000'000 + 600); }, [] { return std::int64_t{1'800'000'300}; });
    CHECK(eng.ResumeFromPersistedState());                    // rebooted, but a timed lock keeps counting down
    CHECK(eng.State() == EngineState::Locked);
}

TEST(BootTime_is_stable_within_a_boot) {
    const auto a = CurrentBootEpochSecs();
    const auto b = CurrentBootEpochSecs();
    CHECK(a != 0);
    CHECK(std::llabs(a - b) <= 2);
}
