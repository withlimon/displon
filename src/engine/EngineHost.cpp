#include "engine/EngineHost.h"

#include <algorithm>

using namespace std::chrono;

EngineHost::EngineHost(EngineHostDeps deps)
    : deps_(std::move(deps)),
      engine_(deps_.lockStore, deps_.now, deps_.bootEpoch),
      scheduler_(engine_, deps_.timeZone, [this] { return deps_.config.LoadRules(); }),
      hosts_(deps_.hostsFile),
      netFilter_(deps_.network, deps_.resolver),
      procWatcher_(deps_.processes, deps_.exemptions, deps_.watcherPollInterval),
      winWatcher_(deps_.windows, deps_.exemptions, deps_.watcherPollInterval) {}

EngineHost::~EngineHost() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!shutDown_) ShutdownLocked(ShutdownMode::ReleaseAll);
}

void EngineHost::SetProcessEventCallback(std::function<void(const ProcessEvent&)> cb) {
    procWatcher_.SetEventCallback(std::move(cb));
}
void EngineHost::SetWindowEventCallback(std::function<void(const WindowEvent&)> cb) {
    winWatcher_.SetEventCallback(std::move(cb));
}

void EngineHost::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_ || shutDown_) return;
    started_ = true;

    // Strays from a crashed run: a Task Manager policy / WFP filters nobody owns any more.
    // (Hosts strays are removed by the first ApplyEnforcement below when no session is active.)
    if (deps_.setTaskManagerPolicy) deps_.setTaskManagerPolicy(false);
    netFilter_.ClearRules();

    ReloadSchedulesLocked();
    engine_.ResumeFromPersistedState();   // re-locks with the ORIGINAL unlock time, or does nothing

    procWatcher_.Start();
    winWatcher_.Start();
    ApplyEnforcementLocked();
}

void EngineHost::Shutdown(ShutdownMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    ShutdownLocked(mode);
}

void EngineHost::ShutdownLocked(ShutdownMode mode) {
    if (shutDown_) return;
    shutDown_ = true;
    if (mode == ShutdownMode::ReleaseAll) {
        procWatcher_.OnRuleChanged({});
        winWatcher_.OnRuleChanged({});
    }
    procWatcher_.Stop();
    winWatcher_.Stop();
    if (mode == ShutdownMode::ReleaseAll) {
        if (deps_.setTaskManagerPolicy && taskMgrActive_) {
            deps_.setTaskManagerPolicy(false);
            taskMgrActive_ = false;
        }
        netFilter_.ClearRules();
        hosts_.RestoreHosts();
    }
}

void EngineHost::Tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_ || shutDown_) return;

    engine_.Tick();
    const TimePoint now = deps_.now();
    scheduler_.Tick(now);
    SyncIfSessionChangedLocked();

    if (engine_.State() != EngineState::Locked) return;

    // Pomodoro / Allowance: temporarily clear enforcement during break/free windows.
    const SessionConfig* cfg = engine_.ActiveConfig();
    if (cfg) {
        const bool paused = IsEnforcementPaused(*cfg, cfg->sessionStartTime, now);
        if (paused != enforcementPaused_) {
            enforcementPaused_ = paused;
            if (paused) {
                procWatcher_.OnRuleChanged({});
                winWatcher_.OnRuleChanged({});
                netFilter_.ClearRules();
                hosts_.RestoreHosts();
                if (deps_.setTaskManagerPolicy && taskMgrActive_) {
                    deps_.setTaskManagerPolicy(false);
                    taskMgrActive_ = false;
                }
            } else {
                ApplyEnforcementLocked();
            }
        }
    }

    if (enforcementPaused_) return;  // skip hosts repair while paused

    if (now < lastRepair_) lastRepair_ = now;           // wall clock went backwards: don't stall
    if (now - lastRepair_ >= deps_.hostsRepairInterval) {
        const auto r = hosts_.Repair();                  // someone edited/removed our block: put it back
        if (r != HostsManager::Result::Ok) hostsResult_ = r;
        lastRepair_ = now;
    }
    if (now < lastNetRefresh_) lastNetRefresh_ = now;
    if (!netFilter_.Domains().empty() && now - lastNetRefresh_ >= deps_.networkRefreshInterval) {
        netResult_ = netFilter_.Refresh();               // DNS answers change: add new IPs (never removes)
        lastNetRefresh_ = now;
    }
}

StartResult EngineHost::StartSession(const SessionConfig& config, std::wstring_view password) {
    std::lock_guard<std::mutex> lock(mutex_);
    const StartResult r = engine_.StartSession(config, password);
    SyncIfSessionChangedLocked();   // also catches an old session that expired inside StartSession
    return r;
}

bool EngineHost::StopSession(const Guid& id, std::wstring_view password) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool ok = engine_.StopSession(id, password);
    SyncIfSessionChangedLocked();
    return ok;
}

bool EngineHost::AddSchedule(const ScheduleEntry& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!IsValidSchedule(e)) return false;
    if (!deps_.config.SaveSchedule(e)) return false;   // persist first: what is not saved is not scheduled
    return scheduler_.AddSchedule(e);
}

bool EngineHost::RemoveSchedule(const Guid& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool stored = deps_.config.DeleteSchedule(id);
    const bool live = scheduler_.RemoveSchedule(id);
    return stored || live;
}

bool EngineHost::SetScheduleEnabled(const Guid& id, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& all = scheduler_.Schedules();
    const auto it = std::find_if(all.begin(), all.end(), [&](const ScheduleEntry& e) { return e.id == id; });
    if (it == all.end()) return false;
    ScheduleEntry copy = *it;
    copy.enabled = enabled;
    if (!deps_.config.SaveSchedule(copy)) return false;
    scheduler_.SetEnabled(id, enabled);
    return true;
}

void EngineHost::ReloadSchedules() {
    std::lock_guard<std::mutex> lock(mutex_);
    ReloadSchedulesLocked();
}

void EngineHost::ReloadSchedulesLocked() {
    const auto stored = deps_.config.LoadSchedules();
    std::vector<Guid> gone;
    for (const auto& live : scheduler_.Schedules()) {
        const bool inStore = std::any_of(stored.begin(), stored.end(), [&](const ScheduleEntry& s) { return s.id == live.id; });
        if (!inStore) gone.push_back(live.id);
    }
    for (const auto& id : gone) scheduler_.RemoveSchedule(id);
    for (const auto& s : stored) scheduler_.AddSchedule(s);
}

HostStatus EngineHost::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HostStatus s;
    s.state = engine_.State();
    s.unlockTime = engine_.UnlockTime();
    s.failedUnlockAttempts = engine_.FailedUnlockAttempts();
    s.nextScheduled = scheduler_.NextStart(deps_.now());
    s.hostsResult = hostsResult_;
    s.networkResult = netResult_;
    s.hostsDomains = hosts_.AppliedDomains().size();
    s.blockedIps = netFilter_.BlockedCount();
    s.taskManagerPolicyActive = taskMgrActive_;
    s.inBreakWindow = enforcementPaused_;
    return s;
}

std::optional<Guid> EngineHost::ActiveSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return engine_.ActiveSessionId();
}

std::optional<SessionConfig> EngineHost::ActiveConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const SessionConfig* c = engine_.ActiveConfig();
    if (!c) return std::nullopt;
    return *c;
}

void EngineHost::SyncIfSessionChangedLocked() {
    if (!started_ || shutDown_) return;
    if (engine_.ActiveSessionId() != appliedSession_) ApplyEnforcementLocked();
}

// The single place where "what the engine says is active" becomes "what the OS enforces".
void EngineHost::ApplyEnforcementLocked() {
    const std::vector<BlockRule> rules = engine_.ActiveRules();   // empty when idle
    const SessionConfig* cfg = engine_.ActiveConfig();
    appliedSession_ = engine_.ActiveSessionId();
    if (!cfg) enforcementPaused_ = false;

    const bool wantTaskMgr = cfg != nullptr && cfg->blockTaskManager;
    if (deps_.setTaskManagerPolicy && wantTaskMgr != taskMgrActive_) {
        if (deps_.setTaskManagerPolicy(wantTaskMgr)) taskMgrActive_ = wantTaskMgr;
    }

    // WFP first: the resolver must still see the sites' real IPs, not the 0.0.0.0 the hosts block adds.
    netResult_ = netFilter_.ApplyRules(rules);   // no website rules => removes our filters
    hostsResult_ = hosts_.Sync(rules);           // no website rules => removes our hosts block

    procWatcher_.SetMinimizeOnly(cfg != nullptr && cfg->processAction == WindowTitleAction::Suppress);
    procWatcher_.OnRuleChanged(rules);
    winWatcher_.OnRuleChanged(rules);

    lastRepair_ = lastNetRefresh_ = deps_.now();
}
