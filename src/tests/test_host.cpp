// Phase 4 (portable part): EngineHost wiring, tested against fakes with REAL watcher threads.
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "data/ConfigStore.h"
#include "engine/DryRun.h"
#include "engine/EngineHost.h"
#include "tests/TestHarness.h"

using namespace std::chrono;

namespace {

struct FixedZone final : ITimeZone {   // +06:00, no DST
    minutes offset{360};
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

struct HostProcs final : IProcessOps {
    std::mutex m;
    std::vector<ProcessInfo> procs;
    std::vector<std::wstring> killed;
    void Add(std::uint32_t pid, std::wstring name) {
        std::lock_guard<std::mutex> l(m);
        procs.push_back({pid, pid, L"C:\\Apps\\" + name, name});
    }
    std::vector<ProcessInfo> Enumerate() override { std::lock_guard<std::mutex> l(m); return procs; }
    bool Terminate(std::uint32_t pid) override {
        std::lock_guard<std::mutex> l(m);
        for (size_t i = 0; i < procs.size(); ++i)
            if (procs[i].pid == pid) { killed.push_back(procs[i].name); procs.erase(procs.begin() + i); return true; }
        return false;
    }
    size_t Alive() { std::lock_guard<std::mutex> l(m); return procs.size(); }
    size_t Killed() { std::lock_guard<std::mutex> l(m); return killed.size(); }
};

struct HostWindows final : IWindowOps {
    std::mutex m;
    std::vector<WindowInfo> wins;
    int closes = 0;
    std::vector<WindowInfo> Enumerate() override { std::lock_guard<std::mutex> l(m); return wins; }
    bool Close(std::uint64_t id) override {
        std::lock_guard<std::mutex> l(m);
        for (size_t i = 0; i < wins.size(); ++i)
            if (wins[i].id == id) { wins.erase(wins.begin() + i); ++closes; return true; }
        return false;
    }
    bool Minimize(std::uint64_t) override { return true; }
    int Closes() { std::lock_guard<std::mutex> l(m); return closes; }
};

// Remembers whether the hosts block already existed whenever the resolver was asked (order check).
struct HostResolver final : IResolver {
    InMemoryHostsFile* hosts = nullptr;
    std::map<std::string, std::vector<IpAddress>> table;
    int calls = 0;
    bool sawHostsBlockWhileResolving = false;
    std::vector<IpAddress> Resolve(const std::string& h) override {
        ++calls;
        if (hosts && hosts->content.find("Displon BEGIN") != std::string::npos) sawHostsBlockWhileResolving = true;
        auto it = table.find(h);
        return it == table.end() ? std::vector<IpAddress>{} : it->second;
    }
};

bool WaitFor(const std::function<bool()>& pred, int ms = 3000) {
    const auto end = steady_clock::now() + milliseconds(ms);
    while (steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
}

const TimePoint kT0 = system_clock::time_point(seconds(1'800'000'000));   // 2027-01-15 ~08:00 UTC, a Friday

struct Rig {
    std::unique_ptr<ConfigStore> config = ConfigStore::Open(":memory:");
    InMemoryLockStateStore lockStore;
    FixedZone zone;
    HostProcs procs;
    HostWindows wins;
    InMemoryHostsFile hostsFile;
    NullNetworkBackend net;
    HostResolver resolver;
    TimePoint now = kT0;
    std::vector<bool> tmCalls;   // every setTaskManagerPolicy(x) call, in order
    std::unique_ptr<EngineHost> host;

    Rig() {
        resolver.hosts = &hostsFile;
        resolver.table["example.com"] = {IpAddress::V4(93, 184, 216, 34)};
        resolver.table["www.example.com"] = {IpAddress::V4(93, 184, 216, 35)};
        MakeHost();
    }
    void MakeHost() {
        host.reset();
        EngineHostDeps d{*lockStore_(), *config, zone, procs, wins, hostsFile, net, resolver,
                         DefaultExemptions(L"C:\\Windows")};
        d.setTaskManagerPolicy = [this](bool on) { tmCalls.push_back(on); return true; };
        d.now = [this] { return now; };
        d.watcherPollInterval = milliseconds(10);
        d.hostsRepairInterval = seconds(1);
        host = std::make_unique<EngineHost>(std::move(d));
    }
    ILockStateStore* lockStore_() { return &lockStore; }
    void Advance(seconds s) { now += s; host->Tick(); }
};

bool HostsBlocked(const Rig& r) { return r.hostsFile.content.find("Displon BEGIN") != std::string::npos; }

SessionConfig Cfg(minutes d = minutes(30)) {
    SessionConfig c;
    c.duration = d;
    return c;
}

}  // namespace

TEST(Host_process_rule_enforced_then_released_on_stop) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg();
    c.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, L"game.exe"));
    CHECK(r.host->StartSession(c, L"pw") == StartResult::Started);

    r.procs.Add(100, L"game.exe");
    r.procs.Add(101, L"notes.exe");
    CHECK(WaitFor([&] { return r.procs.Killed() == 1; }));
    CHECK(r.procs.Alive() == 1);   // notes.exe untouched

    const auto id = r.host->ActiveSessionId();
    CHECK(id.has_value());
    CHECK(!r.host->StopSession(*id, L"wrong"));
    CHECK(r.host->Status().state == EngineState::Locked);
    CHECK(r.host->StopSession(*id, L"pw"));
    CHECK(r.host->Status().state == EngineState::Idle);

    r.procs.Add(102, L"game.exe");   // rules are gone: must survive
    std::this_thread::sleep_for(milliseconds(150));
    CHECK(r.procs.Alive() == 2);
}

TEST(Host_window_title_rule_closes_window) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg();
    c.windowTitleRules.push_back(MakeRule(RuleType::WindowTitle, L"YouTube"));
    CHECK(r.host->StartSession(c) == StartResult::Started);
    {
        std::lock_guard<std::mutex> l(r.wins.m);
        WindowInfo w;
        w.id = 7; w.pid = 900; w.title = L"Cats - YouTube"; w.ownerName = L"browser.exe";
        r.wins.wins.push_back(w);
    }
    CHECK(WaitFor([&] { return r.wins.Closes() == 1; }));
}

TEST(Host_websites_wfp_before_hosts_and_both_released_on_expiry) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg(minutes(10));
    c.websiteBlockRules = {L"example.com"};
    CHECK(r.host->StartSession(c) == StartResult::Started);

    CHECK(HostsBlocked(r));
    CHECK(r.net.live.size() == 2);                  // example.com + www.example.com
    CHECK(!r.resolver.sawHostsBlockWhileResolving); // WFP resolved before the hosts block existed
    const auto st = r.host->Status();
    CHECK(st.hostsResult == HostsManager::Result::Ok);
    CHECK(st.hostsDomains == 1);                 // normalized names; hosts adds the www. entries itself
    CHECK(st.blockedIps == 2);

    r.Advance(minutes(9));
    CHECK(HostsBlocked(r));
    r.Advance(minutes(2));                          // past the unlock time
    CHECK(!HostsBlocked(r));
    CHECK(r.net.live.empty());
    CHECK(r.host->Status().state == EngineState::Idle);
}

TEST(Host_task_manager_policy_follows_session) {
    Rig r;
    r.host->Start();
    CHECK(r.tmCalls.size() == 1 && r.tmCalls[0] == false);   // crash cleanup at boot
    SessionConfig c = Cfg(minutes(5));
    c.blockTaskManager = true;
    CHECK(r.host->StartSession(c) == StartResult::Started);
    CHECK(r.tmCalls.size() == 2 && r.tmCalls[1] == true);
    CHECK(r.host->Status().taskManagerPolicyActive);
    r.Advance(minutes(6));
    CHECK(r.tmCalls.size() == 3 && r.tmCalls[2] == false);
    CHECK(!r.host->Status().taskManagerPolicyActive);
}

TEST(Host_second_session_while_locked_changes_nothing) {
    Rig r;
    r.host->Start();
    SessionConfig a = Cfg();
    a.websiteBlockRules = {L"example.com"};
    CHECK(r.host->StartSession(a) == StartResult::Started);
    const auto id = r.host->ActiveSessionId();
    const size_t ips = r.net.live.size();

    SessionConfig b = Cfg();
    b.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, L"other.exe"));
    CHECK(r.host->StartSession(b) == StartResult::AlreadyLocked);
    CHECK(r.host->ActiveSessionId() == id);
    CHECK(r.net.live.size() == ips);
    CHECK(HostsBlocked(r));
}

TEST(Host_reboot_resume_relocks_with_original_time_and_reapplies) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg(minutes(60));
    c.websiteBlockRules = {L"example.com"};
    c.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, L"game.exe"));
    CHECK(r.host->StartSession(c) == StartResult::Started);
    const auto unlock = r.host->Status().unlockTime;
    CHECK(unlock.has_value());

    // "Reboot": engine host gone without releasing (service stop), strays wiped from the OS side except hosts.
    r.host->Shutdown(ShutdownMode::LeaveInPlace);
    CHECK(HostsBlocked(r));              // LeaveInPlace keeps the block
    r.net.live.clear();                  // WFP dynamic filters die with the process
    r.now += minutes(20);
    r.MakeHost();
    r.host->Start();

    const auto st = r.host->Status();
    CHECK(st.state == EngineState::Locked);
    CHECK(st.unlockTime == unlock);      // ORIGINAL end time, not now + 60
    CHECK(HostsBlocked(r));
    CHECK(r.net.live.size() == 2);       // WFP re-applied
    r.procs.Add(5, L"game.exe");
    CHECK(WaitFor([&] { return r.procs.Killed() == 1; }));
}

TEST(Host_reboot_after_expiry_cleans_leftover_hosts_block) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg(minutes(10));
    c.websiteBlockRules = {L"example.com"};
    CHECK(r.host->StartSession(c) == StartResult::Started);
    r.host->Shutdown(ShutdownMode::LeaveInPlace);
    CHECK(HostsBlocked(r));
    r.now += minutes(30);                // machine was off past the unlock time
    r.MakeHost();
    r.host->Start();
    CHECK(r.host->Status().state == EngineState::Idle);
    CHECK(!HostsBlocked(r));             // stale block removed
    CHECK(r.net.live.empty());
}

TEST(Host_shutdown_release_all_undoes_everything) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg();
    c.websiteBlockRules = {L"example.com"};
    c.blockTaskManager = true;
    CHECK(r.host->StartSession(c) == StartResult::Started);
    CHECK(HostsBlocked(r));
    r.host->Shutdown(ShutdownMode::ReleaseAll);
    CHECK(!HostsBlocked(r));
    CHECK(r.net.live.empty());
    CHECK(r.tmCalls.back() == false);
    r.host->Shutdown(ShutdownMode::ReleaseAll);   // idempotent
}

TEST(Host_start_clears_stray_wfp_filters_when_idle) {
    Rig r;
    std::uint64_t id = 0;
    r.net.AddBlock(IpAddress::V4(1, 2, 3, 4), id);   // left by a crashed run
    r.hostsFile.content += "# >>> Displon BEGIN - managed block, do not edit >>>\r\n0.0.0.0 old.com\r\n# <<< Displon END <<<\r\n";
    r.host->Start();
    CHECK(r.net.live.empty());
    CHECK(!HostsBlocked(r));
}

TEST(Host_tampered_hosts_block_is_repaired_while_locked) {
    Rig r;
    r.host->Start();
    SessionConfig c = Cfg();
    c.websiteBlockRules = {L"example.com"};
    CHECK(r.host->StartSession(c) == StartResult::Started);
    r.hostsFile.content = "127.0.0.1 localhost\r\n";   // user deletes the block
    r.Advance(seconds(2));
    CHECK(HostsBlocked(r));
}

TEST(Host_no_hosts_repair_when_idle) {
    Rig r;
    r.host->Start();
    const int w = r.hostsFile.writes;
    r.Advance(seconds(5));
    r.Advance(seconds(5));
    CHECK(r.hostsFile.writes == w);
}

TEST(Host_scheduler_starts_and_ends_enforcement) {
    Rig r;
    r.host->Start();
    BlockRule site = MakeRule(RuleType::Website, L"example.com");
    CHECK(r.config->SaveRule(site));
    // kT0 is Friday 14:00 local (+6). Window 14:30-15:30 local, every day.
    const LocalTime lt = r.zone.ToLocal(kT0);
    CHECK(lt.minuteOfDay == 14 * 60);   // fixture sanity
    auto e = MakeSchedule(L"Focus", kEveryDay, 14 * 60 + 30, minutes(60), {site.id});
    CHECK(r.host->AddSchedule(e));
    CHECK(r.config->LoadSchedules().size() == 1);   // persisted

    r.Advance(minutes(10));
    CHECK(r.host->Status().state == EngineState::Idle);
    CHECK(r.host->Status().nextScheduled.has_value());
    CHECK(!HostsBlocked(r));

    r.Advance(minutes(21));                          // 14:31 - inside the window
    CHECK(r.host->Status().state == EngineState::Locked);
    CHECK(HostsBlocked(r));
    CHECK(r.net.live.size() == 2);

    r.Advance(minutes(60));                          // 15:31 - window over
    CHECK(r.host->Status().state == EngineState::Idle);
    CHECK(!HostsBlocked(r));
    CHECK(r.net.live.empty());
}

TEST(Host_schedule_survives_host_restart_and_can_be_disabled_or_removed) {
    Rig r;
    r.host->Start();
    auto e = MakeSchedule(L"S", kEveryDay, 0, minutes(30), {});
    CHECK(r.host->AddSchedule(e));
    CHECK(!r.host->AddSchedule(MakeSchedule(L"", kEveryDay, 0, minutes(30), {})));   // invalid rejected
    r.host->Shutdown(ShutdownMode::LeaveInPlace);
    r.MakeHost();
    r.host->Start();
    CHECK(r.host->Status().nextScheduled.has_value());   // loaded from the store

    CHECK(r.host->SetScheduleEnabled(e.id, false));
    CHECK(!r.host->Status().nextScheduled.has_value());
    CHECK(!r.config->LoadSchedules()[0].enabled);         // persisted
    CHECK(!r.host->SetScheduleEnabled(Guid::Generate(), true));

    CHECK(r.host->RemoveSchedule(e.id));
    CHECK(r.config->LoadSchedules().empty());
    r.host->ReloadSchedules();
    CHECK(!r.host->Status().nextScheduled.has_value());
}

TEST(Host_reload_schedules_picks_up_store_changes) {
    Rig r;
    r.host->Start();
    auto e = MakeSchedule(L"External", kEveryDay, 60, minutes(30), {});
    CHECK(r.config->SaveSchedule(e));                     // written behind the host's back (e.g. a tool)
    CHECK(!r.host->Status().nextScheduled.has_value());
    r.host->ReloadSchedules();
    CHECK(r.host->Status().nextScheduled.has_value());
    CHECK(r.config->DeleteSchedule(e.id));
    r.host->ReloadSchedules();
    CHECK(!r.host->Status().nextScheduled.has_value());
}

TEST(Host_calls_before_start_and_after_shutdown_are_safe) {
    Rig r;
    r.host->Tick();                                       // not started: ignored
    r.host->Start();
    r.host->Start();                                      // idempotent
    r.host->Shutdown();
    r.host->Tick();
    r.host->Start();                                      // cannot restart after shutdown
    CHECK(r.host->Status().state == EngineState::Idle);
}

TEST(Host_concurrent_ticks_and_commands_are_safe) {
    Rig r;
    r.host->Start();
    std::atomic<bool> stop{false};
    std::thread ticker([&] { while (!stop) { r.host->Tick(); (void)r.host->Status(); } });
    SessionConfig c = Cfg(minutes(5));
    c.websiteBlockRules = {L"example.com"};
    for (int i = 0; i < 50; ++i) {
        if (r.host->StartSession(c, L"pw") == StartResult::Started) {
            const auto id = r.host->ActiveSessionId();
            if (id) r.host->StopSession(*id, L"pw");
        }
    }
    stop = true;
    ticker.join();
    CHECK(r.host->Status().state == EngineState::Idle);
    CHECK(!HostsBlocked(r));
}
