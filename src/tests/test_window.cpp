// Module B window-title tests: matching, decisions, enforcer loop, watcher thread, shared RuleWatcher.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "engine/ProcessWatcher.h"
#include "engine/WindowEnforcer.h"
#include "engine/WindowMatcher.h"
#include "engine/WindowTitleWatcher.h"
#include "tests/TestHarness.h"

using namespace std::chrono;

namespace {

WindowInfo Win(std::uint64_t id, std::wstring title, std::uint32_t pid = 500, std::wstring owner = L"chrome.exe",
               bool minimized = false) {
    WindowInfo w;
    w.id = id;
    w.pid = pid;
    w.ownerName = std::move(owner);
    w.title = std::move(title);
    w.minimized = minimized;
    return w;
}
BlockRule TitleRule(std::wstring t, WindowTitleAction a = WindowTitleAction::Close) {
    return MakeRule(RuleType::WindowTitle, std::move(t), a);
}
const ExemptionPolicy kEx = DefaultExemptions(L"C:\\Windows", {}, {4242});
const SteadyTime kW0 = SteadyTime{} + hours(1);

class FakeWindows final : public IWindowOps {
public:
    std::vector<WindowInfo> Enumerate() override {
        std::lock_guard<std::mutex> l(m);
        ++enumerateCalls;
        return wins;
    }
    bool Close(std::uint64_t id) override {
        std::lock_guard<std::mutex> l(m);
        closed.push_back(id);
        if (closeWorks)
            for (auto it = wins.begin(); it != wins.end(); ++it)
                if (it->id == id) { wins.erase(it); break; }
        return true;
    }
    bool Minimize(std::uint64_t id) override {
        std::lock_guard<std::mutex> l(m);
        minimized.push_back(id);
        for (auto& w : wins)
            if (w.id == id) w.minimized = true;
        return true;
    }
    void Add(WindowInfo w) { std::lock_guard<std::mutex> l(m); wins.push_back(std::move(w)); }
    void SetTitle(std::uint64_t id, std::wstring t) {
        std::lock_guard<std::mutex> l(m);
        for (auto& w : wins) if (w.id == id) w.title = std::move(t);
    }
    void Restore(std::uint64_t id) {
        std::lock_guard<std::mutex> l(m);
        for (auto& w : wins) if (w.id == id) w.minimized = false;
    }
    int CloseCount(std::uint64_t id) {
        std::lock_guard<std::mutex> l(m);
        int n = 0;
        for (auto c : closed) n += (c == id);
        return n;
    }
    int MinimizeCount(std::uint64_t id) {
        std::lock_guard<std::mutex> l(m);
        int n = 0;
        for (auto c : minimized) n += (c == id);
        return n;
    }
    int EnumerateCalls() { std::lock_guard<std::mutex> l(m); return enumerateCalls; }

    std::mutex m;
    std::vector<WindowInfo> wins;
    std::vector<std::uint64_t> closed, minimized;
    bool closeWorks = true;
    int enumerateCalls = 0;
};

template <typename Pred>
bool WaitUntil(Pred pred, milliseconds limit) {
    const auto end = steady_clock::now() + limit;
    while (steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
}

}  // namespace

// ---------- matching ----------
TEST(Wildcard_cases) {
    CHECK(WildcardMatch(L"*", L""));
    CHECK(WildcardMatch(L"*", L"anything"));
    CHECK(WildcardMatch(L"YouTube*", L"youtube - Home"));
    CHECK(!WildcardMatch(L"YouTube*", L"Watch - YouTube"));
    CHECK(WildcardMatch(L"*- YouTube", L"Funny cats - YouTube"));
    CHECK(WildcardMatch(L"*game*", L"My GAME launcher"));
    CHECK(WildcardMatch(L"a?c", L"abc"));
    CHECK(!WildcardMatch(L"a?c", L"ac"));
    CHECK(WildcardMatch(L"*a*b*c", L"xxaYYbZZc"));      // needs star backtracking
    CHECK(!WildcardMatch(L"*a*b*c", L"xxaYYcZZb"));
    CHECK(WildcardMatch(L"**x**", L"x"));
    CHECK(!WildcardMatch(L"", L"x"));
    CHECK(WildcardMatch(L"", L""));
}
TEST(Title_substring_default_and_wildcard_when_star_present) {
    CHECK(MatchesWindowTitle(L"Watch cats - YouTube - Google Chrome", L"youtube"));   // substring, ci
    CHECK(MatchesWindowTitle(L"Watch cats - YouTube", L"YOUTUBE"));
    CHECK(!MatchesWindowTitle(L"Watch cats", L"youtube"));
    CHECK(!MatchesWindowTitle(L"", L"x"));
    CHECK(!MatchesWindowTitle(L"x", L""));
    CHECK(!MatchesWindowTitle(L"Watch - YouTube", L"YouTube*"));                      // '*' => whole-title mode
    CHECK(MatchesWindowTitle(L"YouTube - Home", L"YouTube*"));
    CHECK(MatchesWindowTitle(L"Reddit: \u09AB\u09CB\u09B2\u09CD\u09A1", L"\u09AB\u09CB\u09B2\u09CD\u09A1"));   // Bangla
}
TEST(EvaluateWindow_close_beats_suppress_and_ignores_other_rule_types) {
    std::vector<BlockRule> rules = {TitleRule(L"Discord", WindowTitleAction::Suppress),
                                    TitleRule(L"Discord", WindowTitleAction::Close),
                                    MakeRule(RuleType::ProcessExe, L"Discord.exe")};
    CHECK(EvaluateWindow(Win(1, L"Discord - general"), rules, kEx) == WindowTitleAction::Close);
    rules.erase(rules.begin() + 1);
    CHECK(EvaluateWindow(Win(1, L"Discord - general"), rules, kEx) == WindowTitleAction::Suppress);
    CHECK(!EvaluateWindow(Win(2, L"Notes"), rules, kEx));
    CHECK(!EvaluateWindow(Win(3, L""), rules, kEx));
    CHECK(!EvaluateWindow(Win(4, L"Discord"), {MakeRule(RuleType::ProcessExe, L"Discord")}, kEx));
}
TEST(EvaluateWindow_never_touches_protected_or_own_windows) {
    const std::vector<BlockRule> rules = {TitleRule(L"YouTube")};
    CHECK(!EvaluateWindow(Win(1, L"YouTube", 4242, L"WindowsTerminal.exe"), rules, kEx));   // our terminal
    CHECK(!EvaluateWindow(Win(2, L"YouTube", 900, L"svchost.exe"), rules, kEx));            // protected name
    CHECK(!EvaluateWindow(Win(3, L"YouTube", 4, L"System"), rules, kEx));
    CHECK(EvaluateWindow(Win(4, L"YouTube", 900, L"firefox.exe"), rules, kEx));
}

// ---------- enforcer ----------
TEST(WindowEnforcer_idle_without_title_rules_does_not_enumerate) {
    FakeWindows ops;
    ops.Add(Win(1, L"YouTube"));
    WindowEnforcer enf(ops, kEx);
    enf.Poll(kW0);
    enf.SetRules({MakeRule(RuleType::ProcessExe, L"x.exe"), MakeRule(RuleType::Website, L"y.com")});
    enf.Poll(kW0);
    CHECK(ops.EnumerateCalls() == 0 && ops.closed.empty());
}
TEST(WindowEnforcer_closes_matching_window_once_when_it_goes_away) {
    FakeWindows ops;
    ops.Add(Win(1, L"Cats - YouTube"));
    ops.Add(Win(2, L"Notes"));
    WindowEnforcer enf(ops, kEx);
    std::vector<WindowEvent> events;
    enf.SetEventCallback([&](const WindowEvent& e) { events.push_back(e); });
    enf.SetRules({TitleRule(L"youtube")});
    enf.Poll(kW0);
    enf.Poll(kW0 + seconds(5));
    CHECK(ops.closed == std::vector<std::uint64_t>{1});
    CHECK(events.size() == 1 && events[0].action == WindowTitleAction::Close && events[0].attempt == 1 && events[0].ok);
}
TEST(WindowEnforcer_retries_ignored_close_with_backoff_then_gives_up) {
    FakeWindows ops;
    ops.closeWorks = false;                          // app shows a "save changes?" dialog and stays open
    ops.Add(Win(1, L"Doc - YouTube"));
    WindowEnforcer enf(ops, kEx);
    std::vector<WindowEvent> events;
    enf.SetEventCallback([&](const WindowEvent& e) { events.push_back(e); });
    enf.SetRules({TitleRule(L"YouTube")});

    enf.Poll(kW0);                                    CHECK(ops.CloseCount(1) == 1);
    enf.Poll(kW0 + milliseconds(250));                CHECK(ops.CloseCount(1) == 1);   // inside backoff
    enf.Poll(kW0 + milliseconds(500));                CHECK(ops.CloseCount(1) == 2);
    enf.Poll(kW0 + milliseconds(1000));               CHECK(ops.CloseCount(1) == 2);   // next gap is 1 s
    enf.Poll(kW0 + milliseconds(1500));               CHECK(ops.CloseCount(1) == 3);
    CHECK(WindowEnforcer::CloseBackoff(1) == milliseconds(500));
    CHECK(WindowEnforcer::CloseBackoff(5) == milliseconds(5000));
    CHECK(WindowEnforcer::CloseBackoff(99) == milliseconds(5000));
    for (int i = 1; i <= 60; ++i) enf.Poll(kW0 + seconds(i * 10));
    CHECK(ops.CloseCount(1) == WindowEnforcer::kMaxCloseAttempts);
    CHECK(!events.empty() && events.back().gaveUp);
}
TEST(WindowEnforcer_suppress_minimizes_and_reminimizes_after_restore) {
    FakeWindows ops;
    ops.Add(Win(1, L"Discord - general"));
    WindowEnforcer enf(ops, kEx);
    enf.SetRules({TitleRule(L"Discord", WindowTitleAction::Suppress)});
    enf.Poll(kW0);                           CHECK(ops.MinimizeCount(1) == 1);
    enf.Poll(kW0 + milliseconds(250));       CHECK(ops.MinimizeCount(1) == 1);   // still minimized: leave it
    ops.Restore(1);                                                               // user brings it back
    enf.Poll(kW0 + milliseconds(500));       CHECK(ops.MinimizeCount(1) == 2);
    CHECK(ops.closed.empty());
}
TEST(WindowEnforcer_reacts_to_title_changes_both_ways) {
    FakeWindows ops;
    ops.closeWorks = false;
    ops.Add(Win(1, L"New Tab"));                     // browser tab title changes as you navigate
    WindowEnforcer enf(ops, kEx);
    enf.SetRules({TitleRule(L"YouTube")});
    enf.Poll(kW0);                                    CHECK(ops.CloseCount(1) == 0);
    ops.SetTitle(1, L"Cats - YouTube");
    enf.Poll(kW0 + seconds(1));                       CHECK(ops.CloseCount(1) == 1);
    ops.SetTitle(1, L"Docs");                         // navigated away: state resets
    enf.Poll(kW0 + seconds(2));                       CHECK(ops.CloseCount(1) == 1);
    ops.SetTitle(1, L"YouTube again");
    enf.Poll(kW0 + seconds(3));                       CHECK(ops.CloseCount(1) == 2);        // fresh attempt, no old backoff
}
TEST(WindowEnforcer_new_rules_reset_and_session_end_goes_idle) {
    FakeWindows ops;
    ops.Add(Win(1, L"YouTube"));
    WindowEnforcer enf(ops, kEx);
    enf.SetRules({TitleRule(L"nothing")});
    enf.Poll(kW0);
    CHECK(ops.closed.empty());
    enf.SetRules({TitleRule(L"YouTube")});
    enf.Poll(kW0 + seconds(1));
    CHECK(ops.CloseCount(1) == 1);
    enf.SetRules({});
    ops.Add(Win(2, L"YouTube"));
    const int enumerated = ops.EnumerateCalls();
    enf.Poll(kW0 + seconds(2));
    CHECK(ops.CloseCount(2) == 0 && ops.EnumerateCalls() == enumerated);
}
TEST(WindowEnforcer_protected_windows_untouched_while_others_closed) {
    FakeWindows ops;
    ops.Add(Win(1, L"YouTube - terminal", 4242, L"WindowsTerminal.exe"));
    ops.Add(Win(2, L"YouTube", 900, L"firefox.exe"));
    WindowEnforcer enf(ops, kEx);
    enf.SetRules({TitleRule(L"YouTube")});
    enf.Poll(kW0);
    CHECK(ops.closed == std::vector<std::uint64_t>{2});
}

// ---------- watchers (real threads, fake OS) ----------
TEST(WindowWatcher_closes_matching_window_within_one_second) {
    FakeWindows ops;
    WindowTitleWatcher w(ops, kEx);
    w.Start();
    w.OnRuleChanged({TitleRule(L"YouTube")});
    const auto t0 = steady_clock::now();
    ops.Add(Win(9, L"Cats - YouTube"));
    CHECK(WaitUntil([&] { return ops.CloseCount(9) >= 1; }, milliseconds(2000)));
    CHECK(steady_clock::now() - t0 < milliseconds(1000));
    w.Stop();
}
TEST(WindowWatcher_rule_change_acts_immediately_and_session_end_stops_it) {
    FakeWindows ops;
    ops.Add(Win(1, L"YouTube"));
    WindowTitleWatcher w(ops, kEx, milliseconds(10000));   // slow poll: must be woken
    w.Start();
    w.OnRuleChanged({TitleRule(L"YouTube")});
    CHECK(WaitUntil([&] { return ops.CloseCount(1) >= 1; }, milliseconds(1000)));
    w.OnRuleChanged({});
    ops.Add(Win(2, L"YouTube"));
    std::this_thread::sleep_for(milliseconds(150));
    CHECK(ops.CloseCount(2) == 0);
    w.Stop();
}
TEST(Watchers_share_one_rule_set_and_each_acts_only_on_its_own_rules) {
    FakeWindows wins;
    // process side: tiny fake
    struct Procs final : IProcessOps {
        std::mutex m; std::vector<ProcessInfo> list; std::vector<std::uint32_t> killed;
        std::vector<ProcessInfo> Enumerate() override { std::lock_guard<std::mutex> l(m); return list; }
        bool Terminate(std::uint32_t pid) override {
            std::lock_guard<std::mutex> l(m);
            killed.push_back(pid);
            for (auto it = list.begin(); it != list.end(); ++it) if (it->pid == pid) { list.erase(it); break; }
            return true;
        }
    } procs;
    ProcessInfo p; p.pid = 33; p.startTime = 1; p.name = L"game.exe"; p.imagePath = L"C:\\g\\game.exe";
    procs.list.push_back(p);
    wins.Add(Win(5, L"YouTube"));

    ProcessWatcher pw(procs, kEx, milliseconds(20));
    WindowTitleWatcher ww(wins, kEx, milliseconds(20));
    pw.Start();
    ww.Start();
    const std::vector<BlockRule> active = {MakeRule(RuleType::ProcessExe, L"game.exe"), TitleRule(L"YouTube")};
    pw.OnRuleChanged(active);
    ww.OnRuleChanged(active);
    CHECK(WaitUntil([&] { std::lock_guard<std::mutex> l(procs.m); return !procs.killed.empty(); }, milliseconds(1000)));
    CHECK(WaitUntil([&] { return wins.CloseCount(5) >= 1; }, milliseconds(1000)));
    pw.Stop();
    ww.Stop();
    CHECK(procs.killed == std::vector<std::uint32_t>{33});
    CHECK(wins.closed == std::vector<std::uint64_t>{5});
}
TEST(WindowWatcher_destroying_a_running_watcher_is_safe) {
    FakeWindows ops;
    const auto t0 = steady_clock::now();
    {
        WindowTitleWatcher w(ops, kEx, milliseconds(60000));
        w.Start();
        w.OnRuleChanged({TitleRule(L"x")});
        std::this_thread::sleep_for(milliseconds(30));
    }                                                   // destructor calls Stop(): no hang, no crash
    CHECK(steady_clock::now() - t0 < milliseconds(1000));
}
