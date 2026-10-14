// EngineHost (plan Phase 4, portable part): the one object that wires EngineCore, Scheduler, the process and
// window watchers, the hosts-file layer, the WFP layer and the Task Manager policy together. The future
// Windows Service (Module D/H) owns exactly one EngineHost and calls Tick() from a timer; the IPC server
// calls the public methods. Everything OS-specific is injected through seams, so this is unit-tested on any host.
//
// Contract:
//  * Enforcement always follows the engine: whenever the active session changes (start, stop, expiry,
//    schedule start, reboot resume) the rules are re-applied in ONE place (ApplyEnforcement).
//  * Website order: WFP filters first (the resolver must still see real IPs), hosts block second.
//  * While locked, Tick() repairs a tampered hosts block and refreshes WFP IPs once a minute.
//  * Public methods are thread-safe (one mutex): the timer thread and the IPC thread may both call in.
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "data/ConfigStore.h"
#include "engine/EngineCore.h"
#include "engine/HostsManager.h"
#include "engine/NetworkFilter.h"
#include "engine/ProcessWatcher.h"
#include "engine/Scheduler.h"
#include "engine/WindowTitleWatcher.h"

struct EngineHostDeps {
    ILockStateStore&   lockStore;
    ConfigStore&       config;
    const ITimeZone&   timeZone;
    IProcessOps&       processes;
    IWindowOps&        windows;
    IHostsFile&        hostsFile;
    INetworkBackend&   network;
    IResolver&         resolver;
    ExemptionPolicy    exemptions;
    // Sets/clears the DisableTaskMgr policy; returns success. Empty = no Task Manager policy layer.
    std::function<bool(bool disabled)> setTaskManagerPolicy = {};
    EngineCore::NowFn  now = [] { return std::chrono::system_clock::now(); };
    std::chrono::milliseconds watcherPollInterval{250};
    std::chrono::seconds      hostsRepairInterval{1};
    std::chrono::seconds      networkRefreshInterval{60};
    EngineCore::BootFn        bootEpoch = [] { return CurrentBootEpochSecs(); };   // tests inject a fake reboot
};

struct HostStatus {
    EngineState                    state = EngineState::Idle;
    std::optional<TimePoint>       unlockTime;        // absolute, from the engine
    int                            failedUnlockAttempts = 0;
    std::optional<Occurrence>      nextScheduled;
    HostsManager::Result           hostsResult = HostsManager::Result::Ok;   // of the last apply
    NetworkFilter::Result          networkResult = NetworkFilter::Result::Ok;
    size_t                         hostsDomains = 0;
    size_t                         blockedIps = 0;
    bool                           taskManagerPolicyActive = false;
    bool                           inBreakWindow = false;
};

enum class ShutdownMode {
    ReleaseAll,     // stop watchers AND undo hosts/WFP/Task Manager changes (dev host, tests)
    LeaveInPlace,   // stop watchers only; the next boot's Start() resumes the persisted lock (service stop)
};

class EngineHost {
public:
    explicit EngineHost(EngineHostDeps deps);
    ~EngineHost();   // ReleaseAll if Shutdown() was not called
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    // Boot: clean strays from a crashed run, load schedules, resume a persisted lock (ORIGINAL unlock
    // time), start the watcher threads and apply enforcement. Idempotent.
    void Start();
    void Shutdown(ShutdownMode mode = ShutdownMode::ReleaseAll);

    // Service timer, every ~500 ms. Expires sessions, runs the Scheduler, re-applies on any change.
    void Tick();

    StartResult StartSession(const SessionConfig& config, std::wstring_view password = {});
    bool        StopSession(const Guid& id, std::wstring_view password);

    // Schedules are persisted in ConfigStore and mirrored into the Scheduler.
    bool AddSchedule(const ScheduleEntry& e);
    bool RemoveSchedule(const Guid& id);
    bool SetScheduleEnabled(const Guid& id, bool enabled);   // does not end an already-started session
    void ReloadSchedules();                                   // after rules/schedules changed in the store

    HostStatus Status() const;
    std::optional<Guid> ActiveSessionId() const;
    // Copy of the running session's configuration (the persisted one after a reboot), if locked.
    std::optional<SessionConfig> ActiveConfig() const;

    // Call before Start(). Callbacks run on the watcher threads.
    void SetProcessEventCallback(std::function<void(const ProcessEvent&)> cb);
    void SetWindowEventCallback(std::function<void(const WindowEvent&)> cb);

private:
    void ApplyEnforcementLocked();
    void SyncIfSessionChangedLocked();
    void ReloadSchedulesLocked();
    void ShutdownLocked(ShutdownMode mode);

    EngineHostDeps deps_;
    mutable std::mutex mutex_;
    EngineCore engine_;
    Scheduler scheduler_;
    HostsManager hosts_;
    NetworkFilter netFilter_;
    ProcessWatcher procWatcher_;
    WindowTitleWatcher winWatcher_;

    bool started_ = false;
    bool shutDown_ = false;
    std::optional<Guid> appliedSession_;     // session the current enforcement was applied for
    bool taskMgrActive_ = false;
    HostsManager::Result hostsResult_ = HostsManager::Result::Ok;
    NetworkFilter::Result netResult_ = NetworkFilter::Result::Ok;
    TimePoint lastRepair_{};
    TimePoint lastNetRefresh_{};
    bool enforcementPaused_ = false;
};
