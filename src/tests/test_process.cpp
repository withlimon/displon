// Phase 2 tests: path helpers, process matching/decisions, enforcer loop, watcher thread.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "common/PathUtil.h"
#include "engine/EngineCore.h"
#include "engine/ProcessEnforcer.h"
#include "engine/ProcessMatcher.h"
#include "engine/ProcessWatcher.h"
#include "tests/TestHarness.h"

using namespace std::chrono;

namespace {

ProcessInfo Proc(std::uint32_t pid, std::wstring path, std::uint64_t start = 1) {
    ProcessInfo p;
    p.pid = pid;
    p.startTime = start;
    p.imagePath = path;
    p.name = FileNameOf(path);
    return p;
}
ProcessInfo NameOnly(std::uint32_t pid, std::wstring name) {   // OS refused to give us the path
    ProcessInfo p;
    p.pid = pid;
    p.startTime = 1;
    p.name = std::move(name);
    return p;
}
ProcessPolicy Deny(std::initializer_list<const wchar_t*> targets) {
    ProcessPolicy pol;
    for (auto t : targets) pol.denyRules.push_back(MakeRule(RuleType::ProcessExe, t));
    return pol;
}
const ExemptionPolicy kExempt = DefaultExemptions(L"C:\\Windows", {L"C:\\Program Files\\Windows Defender"}, {4242});

// Thread-safe fake OS.
class FakeOps final : public IProcessOps {
public:
    std::vector<ProcessInfo> Enumerate() override {
        std::lock_guard<std::mutex> l(m);
        ++enumerateCalls;
        return procs;
    }
    bool Terminate(std::uint32_t pid) override {
        std::lock_guard<std::mutex> l(m);
        terminated.push_back(pid);
        if (!terminateOk) return false;
        if (dieOnTerminate)
            for (auto it = procs.begin(); it != procs.end(); ++it)
                if (it->pid == pid) { procs.erase(it); break; }
        return true;
    }
    void Add(ProcessInfo p) { std::lock_guard<std::mutex> l(m); procs.push_back(std::move(p)); }
    void Remove(std::uint32_t pid) {
        std::lock_guard<std::mutex> l(m);
        for (auto it = procs.begin(); it != procs.end(); ++it)
            if (it->pid == pid) { procs.erase(it); break; }
    }
    int TerminateCount(std::uint32_t pid) {
        std::lock_guard<std::mutex> l(m);
        int n = 0;
        for (auto p : terminated) n += (p == pid);
        return n;
    }
    int EnumerateCalls() { std::lock_guard<std::mutex> l(m); return enumerateCalls; }

    std::mutex m;
    std::vector<ProcessInfo> procs;
    std::vector<std::uint32_t> terminated;
    bool terminateOk = true;
    bool dieOnTerminate = true;
    int enumerateCalls = 0;
};

const SteadyTime kS0 = SteadyTime{} + hours(1);

template <typename Pred>
bool WaitFor(Pred pred, milliseconds limit) {
    const auto end = steady_clock::now() + limit;
    while (steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
}

}  // namespace

// ---------- PathUtil ----------
TEST(Path_normalize) {
    CHECK(NormalizePath(L"C:\\Work\\Sub\\") == L"c:\\work\\sub");
    CHECK(NormalizePath(L"c:/Work//Sub") == L"c:\\work\\sub");
    CHECK(NormalizePath(L"C:\\Work\\.\\a\\..\\b.exe") == L"c:\\work\\b.exe");
    CHECK(NormalizePath(L"C:\\..\\..\\x") == L"c:\\x");               // cannot climb above the drive
    CHECK(NormalizePath(L"\\\\?\\C:\\Work\\x.exe") == L"c:\\work\\x.exe");
    CHECK(NormalizePath(L"\\\\?\\UNC\\srv\\share\\x.exe") == L"\\\\srv\\share\\x.exe");
    CHECK(NormalizePath(L"C:\\") == L"c:");
    CHECK(NormalizePath(L"") == L"");
}
TEST(Path_under_is_component_safe) {
    CHECK(IsPathUnder(L"C:\\Work\\x.exe", L"C:\\Work"));
    CHECK(IsPathUnder(L"C:\\Work\\Sub\\Deep\\x.exe", L"c:/work/"));
    CHECK(IsPathUnder(L"D:\\x.exe", L"D:\\"));                          // whole-drive allow
    CHECK(!IsPathUnder(L"C:\\Workshop\\x.exe", L"C:\\Work"));           // prefix but different folder
    CHECK(!IsPathUnder(L"C:\\Work", L"C:\\Work"));                      // folder itself is not "inside"
    CHECK(!IsPathUnder(L"C:\\Work\\..\\Evil\\x.exe", L"C:\\Work"));     // dot-dot escape
    CHECK(!IsPathUnder(L"C:\\Work\\x.exe", L""));
    CHECK(PathEquals(L"C:\\Tools\\A.EXE", L"c:/tools/a.exe"));
    CHECK(FileNameOf(L"C:\\a\\b\\Game.exe") == L"Game.exe");
    CHECK(FileNameOf(L"game.exe") == L"game.exe");
}

// ---------- matcher ----------
TEST(Match_blocked_by_name_and_by_path) {
    auto pol = Deny({L"Game.EXE", L"C:\\Apps\\tool.exe"});
    CHECK(MatchesBlockedProcess(Proc(1, L"D:\\anywhere\\game.exe"), pol.denyRules));   // name, any folder
    CHECK(MatchesBlockedProcess(Proc(2, L"c:\\apps\\TOOL.exe"), pol.denyRules));       // full path, case-insens.
    CHECK(!MatchesBlockedProcess(Proc(3, L"D:\\other\\tool.exe"), pol.denyRules));     // same name, other path
    CHECK(!MatchesBlockedProcess(Proc(4, L"C:\\x\\notgame.exe"), pol.denyRules));      // not a substring match
    CHECK(MatchesBlockedProcess(NameOnly(5, L"game.exe"), pol.denyRules));             // path unknown, name known
    CHECK(!MatchesBlockedProcess(NameOnly(6, L"tool.exe"), pol.denyRules));            // path rule needs a path
}
TEST(Match_allow_list_folder_and_exe) {
    ProcessPolicy pol;
    pol.allowFolders = {L"C:\\Work"};
    pol.allowExePaths = {L"C:\\Tools\\code.exe"};
    CHECK(pol.AllowListEngaged());
    CHECK(IsAllowed(Proc(1, L"C:\\Work\\app\\a.exe"), pol));
    CHECK(IsAllowed(Proc(2, L"c:\\tools\\CODE.EXE"), pol));
    CHECK(!IsAllowed(Proc(3, L"C:\\Workshop\\a.exe"), pol));
    CHECK(!IsAllowed(Proc(4, L"C:\\Tools\\other.exe"), pol));
    CHECK(!IsAllowed(NameOnly(5, L"code.exe"), pol));                  // no path => cannot be proven allowed
}
TEST(Evaluate_deny_list_only) {
    auto pol = Deny({L"game.exe"});
    CHECK(EvaluateProcess(Proc(10, L"C:\\g\\game.exe"), pol, kExempt) == Verdict::TerminateBlocked);
    CHECK(EvaluateProcess(Proc(11, L"C:\\g\\fine.exe"), pol, kExempt) == Verdict::Allow);   // no allow-list => allow
}
TEST(Evaluate_allow_list_terminates_everything_else) {
    ProcessPolicy pol;
    pol.allowFolders = {L"C:\\Work"};
    CHECK(EvaluateProcess(Proc(10, L"C:\\Work\\ide.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(11, L"C:\\Games\\g.exe"), pol, kExempt) == Verdict::TerminateNotAllowed);
}
TEST(Evaluate_system_exemptions_under_allow_list) {
    ProcessPolicy pol;
    pol.allowFolders = {L"C:\\Work"};
    CHECK(EvaluateProcess(Proc(10, L"C:\\Windows\\explorer.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(11, L"C:\\Windows\\System32\\svchost.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(12, L"C:\\Program Files\\Windows Defender\\MsMpEng.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(4242, L"C:\\Users\\me\\terminal.exe"), pol, kExempt) == Verdict::Allow);  // exempt pid
    CHECK(EvaluateProcess(Proc(4, L"C:\\whatever\\System"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(NameOnly(13, L"unknown.exe"), pol, kExempt) == Verdict::Allow);  // unidentifiable => fail open
    CHECK(EvaluateProcess(Proc(14, L"C:\\Windowsfake\\x.exe"), pol, kExempt) == Verdict::TerminateNotAllowed);
}
TEST(Evaluate_explicit_deny_beats_folder_exemption_but_not_protection) {
    auto pol = Deny({L"taskmgr.exe", L"svchost.exe"});
    CHECK(EvaluateProcess(Proc(10, L"C:\\Windows\\System32\\Taskmgr.exe"), pol, kExempt) == Verdict::TerminateBlocked);
    CHECK(EvaluateProcess(Proc(11, L"C:\\Windows\\System32\\svchost.exe"), pol, kExempt) == Verdict::Allow);  // protected
    CHECK(EvaluateProcess(Proc(0, L"C:\\x\\taskmgr.exe"), pol, kExempt) == Verdict::Allow);                  // pid 0
}
TEST(Evaluate_deny_and_allow_apply_together) {
    ProcessPolicy pol = Deny({L"slack.exe"});
    pol.allowFolders = {L"C:\\Work"};
    CHECK(EvaluateProcess(Proc(1, L"C:\\Work\\slack.exe"), pol, kExempt) == Verdict::TerminateBlocked);  // allowed folder, still denied
    CHECK(EvaluateProcess(Proc(2, L"C:\\Work\\ide.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(3, L"C:\\Other\\a.exe"), pol, kExempt) == Verdict::TerminateNotAllowed);
}
TEST(Policy_built_from_engine_active_rules_includes_task_manager) {
    InMemoryLockStateStore store;
    EngineCore eng(store, [] { return system_clock::time_point(seconds(1'800'000'000)); });
    SessionConfig c;
    c.blockTaskManager = true;
    c.processBlockRules = {MakeRule(RuleType::ProcessExe, L"game.exe")};
    c.allowListFolders = {L"C:\\Work"};
    c.websiteBlockRules = {L"x.com"};
    c.windowTitleRules = {MakeRule(RuleType::WindowTitle, L"YouTube")};
    eng.StartSession(c);
    ProcessPolicy pol = BuildProcessPolicy(eng.ActiveRules());
    CHECK(pol.denyRules.size() == 2);          // game.exe + implicit taskmgr.exe
    CHECK(pol.allowFolders.size() == 1 && pol.allowExePaths.empty());
    CHECK(pol.AllowListEngaged() && !pol.Empty());
    CHECK(BuildProcessPolicy({}).Empty());
}

// ---------- enforcer ----------
TEST(Enforcer_idle_policy_does_not_even_enumerate) {
    FakeOps ops;
    ops.Add(Proc(1, L"C:\\g\\game.exe"));
    ProcessEnforcer enf(ops, kExempt);
    enf.Poll(kS0);
    CHECK(ops.EnumerateCalls() == 0 && ops.terminated.empty());
}
TEST(Enforcer_terminates_blocked_process_once_and_reports) {
    FakeOps ops;
    ops.Add(Proc(1, L"C:\\g\\game.exe"));
    ops.Add(Proc(2, L"C:\\g\\fine.exe"));
    ProcessEnforcer enf(ops, kExempt);
    std::vector<ProcessEvent> events;
    enf.SetEventCallback([&](const ProcessEvent& e) { events.push_back(e); });
    enf.SetPolicy(Deny({L"game.exe"}));
    enf.Poll(kS0);
    enf.Poll(kS0 + seconds(1));
    CHECK(ops.terminated == std::vector<std::uint32_t>{1});
    CHECK(events.size() == 1 && events[0].verdict == Verdict::TerminateBlocked && events[0].attempt == 1 &&
          events[0].terminateOk && !events[0].gaveUp);
}
TEST(Enforcer_retries_stubborn_process_with_backoff_then_gives_up) {
    FakeOps ops;
    ops.dieOnTerminate = false;                       // relauncher / refuses to die
    ops.Add(Proc(1, L"C:\\g\\game.exe"));
    ProcessEnforcer enf(ops, kExempt);
    std::vector<ProcessEvent> events;
    enf.SetEventCallback([&](const ProcessEvent& e) { events.push_back(e); });
    enf.SetPolicy(Deny({L"game.exe"}));

    enf.Poll(kS0);                                   CHECK(ops.TerminateCount(1) == 1);
    enf.Poll(kS0 + milliseconds(100));               CHECK(ops.TerminateCount(1) == 1);   // inside backoff
    enf.Poll(kS0 + milliseconds(250));               CHECK(ops.TerminateCount(1) == 2);
    enf.Poll(kS0 + milliseconds(500));               CHECK(ops.TerminateCount(1) == 2);   // next gap is 500 ms
    enf.Poll(kS0 + milliseconds(750));               CHECK(ops.TerminateCount(1) == 3);
    CHECK(ProcessEnforcer::Backoff(1) == milliseconds(250));
    CHECK(ProcessEnforcer::Backoff(2) == milliseconds(500));
    CHECK(ProcessEnforcer::Backoff(5) == milliseconds(4000));
    CHECK(ProcessEnforcer::Backoff(50) == milliseconds(4000));    // capped

    for (int i = 1; i <= 40; ++i) enf.Poll(kS0 + seconds(i * 5));
    CHECK(ops.TerminateCount(1) == ProcessEnforcer::kMaxAttempts);   // stops trying
    CHECK(!events.empty() && events.back().gaveUp);
}
TEST(Enforcer_failed_terminate_is_reported_and_eventually_abandoned) {
    FakeOps ops;
    ops.terminateOk = false;                          // access denied
    ops.Add(NameOnly(7, L"game.exe"));
    ProcessEnforcer enf(ops, kExempt);
    std::vector<ProcessEvent> events;
    enf.SetEventCallback([&](const ProcessEvent& e) { events.push_back(e); });
    enf.SetPolicy(Deny({L"game.exe"}));
    for (int i = 0; i < 60; ++i) enf.Poll(kS0 + seconds(i * 5));
    CHECK(ops.TerminateCount(7) == ProcessEnforcer::kMaxAttempts);
    CHECK(!events.empty() && !events[0].terminateOk && events.back().gaveUp);
}
TEST(Enforcer_policy_change_reevaluates_running_processes) {
    FakeOps ops;
    ops.Add(Proc(1, L"C:\\g\\game.exe"));
    ProcessEnforcer enf(ops, kExempt);
    enf.SetPolicy(Deny({L"other.exe"}));
    enf.Poll(kS0);
    CHECK(ops.terminated.empty());
    enf.SetPolicy(Deny({L"game.exe"}));               // new session rules arrive
    enf.Poll(kS0 + seconds(1));
    CHECK(ops.terminated == std::vector<std::uint32_t>{1});
    enf.SetPolicy(ProcessPolicy{});                   // session over
    ops.Add(Proc(2, L"C:\\g\\game.exe"));
    enf.Poll(kS0 + seconds(2));
    CHECK(ops.TerminateCount(2) == 0);
}
TEST(Enforcer_pid_reuse_is_a_new_process) {
    FakeOps ops;
    ops.Add(Proc(9, L"C:\\ok\\fine.exe", /*start=*/100));
    ProcessEnforcer enf(ops, kExempt);
    enf.SetPolicy(Deny({L"game.exe"}));
    enf.Poll(kS0);
    CHECK(ops.terminated.empty());
    ops.Remove(9);
    ops.Add(Proc(9, L"C:\\g\\game.exe", /*start=*/200));   // same pid, different process
    enf.Poll(kS0 + seconds(1));
    CHECK(ops.TerminateCount(9) == 1);
}
TEST(Enforcer_allow_list_session_spares_exempt_and_kills_rest) {
    FakeOps ops;
    ops.Add(Proc(1, L"C:\\Work\\ide.exe"));
    ops.Add(Proc(2, L"C:\\Windows\\explorer.exe"));
    ops.Add(Proc(3, L"C:\\Games\\g.exe"));
    ops.Add(Proc(4242, L"C:\\Users\\me\\terminal.exe"));
    ProcessEnforcer enf(ops, kExempt);
    ProcessPolicy pol;
    pol.allowFolders = {L"C:\\Work"};
    enf.SetPolicy(pol);
    enf.Poll(kS0);
    CHECK(ops.terminated == std::vector<std::uint32_t>{3});
}

// ---------- watcher (real thread, fake OS) ----------
TEST(Watcher_terminates_listed_exe_within_one_second) {
    FakeOps ops;
    ProcessWatcher w(ops, kExempt);                    // default 250 ms poll
    w.Start();
    w.OnRuleChanged({MakeRule(RuleType::ProcessExe, L"game.exe")});

    const auto t0 = steady_clock::now();
    ops.Add(Proc(77, L"C:\\g\\game.exe"));             // "launched" after the rules are active
    const bool killed = WaitFor([&] { return ops.TerminateCount(77) >= 1; }, milliseconds(2000));
    const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - t0);
    w.Stop();
    CHECK(killed);
    CHECK(elapsed < milliseconds(1000));
}
TEST(Watcher_kills_already_running_process_immediately_on_rule_change) {
    FakeOps ops;
    ops.Add(Proc(5, L"C:\\g\\game.exe"));
    ProcessWatcher w(ops, kExempt, milliseconds(10000));   // slow poll: must be woken by the rule change
    w.Start();
    const auto t0 = steady_clock::now();
    w.OnRuleChanged({MakeRule(RuleType::ProcessExe, L"game.exe")});
    CHECK(WaitFor([&] { return ops.TerminateCount(5) >= 1; }, milliseconds(1000)));
    CHECK(steady_clock::now() - t0 < milliseconds(1000));
    w.Stop();
}
TEST(Watcher_goes_quiet_when_session_ends) {
    FakeOps ops;
    ProcessWatcher w(ops, kExempt, milliseconds(20));
    w.Start();
    w.OnRuleChanged({MakeRule(RuleType::ProcessExe, L"game.exe")});
    std::this_thread::sleep_for(milliseconds(60));
    w.OnRuleChanged({});                                // empty rules = no session
    std::this_thread::sleep_for(milliseconds(60));
    ops.Add(Proc(8, L"C:\\g\\game.exe"));
    std::this_thread::sleep_for(milliseconds(200));
    w.Stop();
    CHECK(ops.TerminateCount(8) == 0);
}
TEST(Watcher_stop_is_prompt_and_safe_to_repeat) {
    FakeOps ops;
    {
        ProcessWatcher never(ops, kExempt);             // destroyed without Start()
    }
    ProcessWatcher w(ops, kExempt, milliseconds(60000));
    w.Start();
    w.Start();                                          // second Start is a no-op
    const auto t0 = steady_clock::now();
    w.Stop();
    w.Stop();
    CHECK(steady_clock::now() - t0 < milliseconds(500)); // does not wait out the 60 s interval
}
TEST(Watcher_callback_runs_on_worker_thread_with_event_data) {
    FakeOps ops;
    ProcessWatcher w(ops, kExempt, milliseconds(20));
    std::atomic<int> count{0};
    std::atomic<bool> okFlag{false};
    w.SetEventCallback([&](const ProcessEvent& e) { okFlag = e.terminateOk && e.verdict == Verdict::TerminateBlocked; ++count; });
    w.Start();
    ops.Add(Proc(3, L"C:\\g\\game.exe"));
    w.OnRuleChanged({MakeRule(RuleType::ProcessExe, L"game.exe")});
    CHECK(WaitFor([&] { return count.load() >= 1; }, milliseconds(1000)));
    w.Stop();
    CHECK(okFlag.load());
}

// ---- regressions from the "nothing gets blocked / Windows apps offered" bug report ----
TEST(Allow_bare_exe_name_whitelists_by_file_name) {
    ProcessPolicy pol;
    pol.allowExePaths = {L"firefox.exe"};                       // name-only entry (no folder)
    CHECK(EvaluateProcess(Proc(10, L"D:\\Apps\\Firefox\\firefox.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(11, L"D:\\Apps\\Proteus\\proteus.exe"), pol, kExempt) == Verdict::TerminateNotAllowed);
}
TEST(Allow_list_never_kills_windows_plumbing_or_ourselves) {
    ProcessPolicy pol;
    pol.allowExePaths = {L"D:\\Apps\\code.exe"};
    CHECK(EvaluateProcess(Proc(20, L"C:\\Windows\\explorer.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(21, L"C:\\Windows\\System32\\DriverStore\\FileRepository\\x\\IntelCpHDCPSvc.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(22, L"C:\\ProgramData\\Microsoft\\Windows Defender\\Platform\\MsMpEng.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(23, L"E:\\Projects\\Displon\\build\\DisplonUI.exe"), pol, kExempt) == Verdict::Allow);
    CHECK(EvaluateProcess(Proc(24, L"D:\\Apps\\Proteus\\proteus.exe"), pol, kExempt) == Verdict::TerminateNotAllowed);
}
TEST(Enforcer_allow_list_terminates_non_whitelisted_app_only) {
    FakeOps ops;
    ops.Add(Proc(30, L"C:\\Windows\\explorer.exe"));
    ops.Add(Proc(31, L"D:\\Apps\\Firefox\\firefox.exe"));
    ops.Add(Proc(32, L"D:\\Apps\\Proteus\\proteus.exe"));
    ProcessEnforcer enf(ops, kExempt);
    ProcessPolicy pol;
    pol.allowExePaths = {L"firefox.exe"};
    enf.SetPolicy(pol);
    enf.Poll(steady_clock::now());
    CHECK(ops.terminated.size() == 1);
    CHECK(!ops.terminated.empty() && ops.terminated[0] == 32);
}

// ---- "Minimize" action: rejected apps are kept minimized, never terminated ----
namespace {
class MinOps final : public IProcessOps {
public:
    std::vector<ProcessInfo> Enumerate() override { return procs; }
    bool Terminate(std::uint32_t) override { ++terminates; return true; }
    bool MinimizeWindows(std::uint32_t) override { ++minimizes; return true; }
    std::vector<ProcessInfo> procs;
    int terminates = 0, minimizes = 0;
};
}  // namespace

TEST(Minimize_only_keeps_windows_down_and_never_terminates) {
    MinOps ops;
    ops.procs.push_back(Proc(10, L"C:\\Games\\g.exe"));
    ProcessEnforcer enf(ops, kExempt);
    enf.SetMinimizeOnly(true);
    enf.SetPolicy(BuildProcessPolicy({MakeRule(RuleType::AllowListExe, L"C:\\Work\\ok.exe")}));
    std::vector<ProcessEvent> ev;
    enf.SetEventCallback([&](const ProcessEvent& e) { ev.push_back(e); });
    enf.Poll(kS0);
    CHECK(ops.minimizes == 1 && ops.terminates == 0);
    CHECK(ev.size() == 1 && ev[0].attempt == 1 && ev[0].verdict == Verdict::TerminateNotAllowed);
    enf.Poll(kS0 + milliseconds(200));  CHECK(ops.minimizes == 1);   // inside the 500 ms gap
    enf.Poll(kS0 + milliseconds(500));  CHECK(ops.minimizes == 2);   // restored by the user -> minimized again
    for (int i = 1; i <= 40; ++i) enf.Poll(kS0 + milliseconds(500 * (i + 1)));
    CHECK(ops.terminates == 0 && ops.minimizes > 20);                  // no give-up, no kill
}
