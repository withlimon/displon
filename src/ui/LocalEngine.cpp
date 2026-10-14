#include "ui/LocalEngine.h"

#ifdef _WIN32
#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "common/PathUtil.h"
#include "ui/Autostart.h"
#include "data/ConfigStore.h"
#include "engine/EngineHost.h"
#include "engine/FileLockStateStore.h"
#include "engine/ServiceGuard.h"
#include "engine/TaskManagerPolicy.h"
#include "engine/Win32HostsFile.h"
#include "engine/Win32Network.h"
#include "engine/Win32ProcessOps.h"
#include "engine/Win32TimeZone.h"
#include "engine/Win32WindowOps.h"

namespace {

std::wstring WindowsDir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring(L"C:\\Windows");
}

std::wstring OwnDir() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    const auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}

}  // namespace

struct LocalEngine::Impl {
    FileLockStateStore            lockStore = ServiceGuard::MakeDefaultLockStore();
    std::unique_ptr<ConfigStore>  config;
    Win32ProcessOps               processes;
    Win32WindowOps                windows;
    Win32HostsFile                hostsFile;
    Win32NetworkBackend           network{/*dynamic=*/true};
    Win32Resolver                 resolver;
    Win32TimeZone                 timeZone;
    std::unique_ptr<EngineHost>   host;
    std::thread                   ticker;
    std::atomic<bool>             stop{false};
    bool                          started = false;
    std::mutex                    cbMu;
    std::function<void(const BlockedApp&)> onBlocked;
};

LocalEngine::LocalEngine() : impl_(std::make_unique<Impl>()) {}
LocalEngine::~LocalEngine() { Shutdown(); }

LocalEngine& LocalEngine::Instance() {
    static LocalEngine inst;
    return inst;
}

void LocalEngine::EnsureStarted() {
    Impl& d = *impl_;
    if (d.started) return;

    const auto cfgPath = ServiceGuard::DefaultLockStatePath().parent_path() / L"ui-config.db";
    d.config = ConfigStore::Open(cfgPath.string());
    if (!d.config) d.config = ConfigStore::Open(":memory:");

    // Never kill: Windows itself, our own process tree (the terminal that launched us), and our own folder.
    std::vector<std::wstring> extraFolders;
    const std::wstring own = OwnDir();
    if (!own.empty()) extraFolders.push_back(own);

    EngineHostDeps deps{
        .lockStore = d.lockStore,
        .config = *d.config,
        .timeZone = d.timeZone,
        .processes = d.processes,
        .windows = d.windows,
        .hostsFile = d.hostsFile,
        .network = d.network,
        .resolver = d.resolver,
        .exemptions = DefaultExemptions(WindowsDir(), std::move(extraFolders), SelfAndAncestorPids()),
        .setTaskManagerPolicy = [](bool disabled) { return SetTaskManagerPolicy(disabled); },
    };
    d.host = std::make_unique<EngineHost>(std::move(deps));
    d.host->SetProcessEventCallback([&d](const ProcessEvent& e) {
        if (e.verdict == Verdict::Allow || e.attempt != 1) return;   // once per launch, not per retry
        BlockedApp app;
        app.name = e.process.name;
        if (app.name.empty()) app.name = FileNameOf(e.process.imagePath);
        app.imagePath = e.process.imagePath;
        app.onBlockList = (e.verdict == Verdict::TerminateBlocked);
        std::function<void(const BlockedApp&)> cb;
        { std::lock_guard<std::mutex> l(d.cbMu); cb = d.onBlocked; }
        if (cb && !app.name.empty()) cb(app);
    });
    d.host->Start();
    d.started = true;

    d.stop = false;
    d.ticker = std::thread([&d] {
        using Clock = std::chrono::steady_clock;
        bool wasLocked = false;
        Clock::time_point lastEnsure{}, lastGuard{};
        while (!d.stop) {
            d.host->Tick();

            // Restart-proofing: while locked keep the sign-in task registered (and put it back if someone
            // deleted it); once the lock has genuinely ended, remove it so the app stops launching.
            const bool locked = d.host->Status().state == EngineState::Locked;
            const auto now = Clock::now();
            if (locked) {
                if (!wasLocked || now - lastEnsure >= std::chrono::seconds(60)) {
                    Autostart::Ensure();
                    lastEnsure = now;
                }
            } else if (wasLocked) {
                Autostart::Remove();
            }
            if (locked && now - lastGuard >= std::chrono::seconds(5)) {
                Autostart::EnsureGuard();   // watchdog relaunches us if we are killed; we relaunch it
                lastGuard = now;
            }
            wasLocked = locked;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
}

LocalEngine::Result LocalEngine::StartSession(const SessionConfig& cfg, const std::wstring& password) {
    EnsureStarted();
    Result out;
    const StartResult sr = impl_->host->StartSession(cfg, password);
    switch (sr) {
    case StartResult::Started:
        out.ok = true;
        out.detail = L"Session started (in-process engine).";
        break;
    case StartResult::AlreadyLocked:
        out.detail = L"A session is already locked.";
        break;
    case StartResult::PersistFailed:
        out.detail = L"Could not save the lock state (check %LOCALAPPDATA%\\Displon permissions).";
        break;
    }
    out.unlockTime = impl_->host->Status().unlockTime;
    return out;
}

void LocalEngine::SetBlockedCallback(std::function<void(const BlockedApp&)> cb) {
    std::lock_guard<std::mutex> l(impl_->cbMu);
    impl_->onBlocked = std::move(cb);
}

LocalEngine::Status LocalEngine::GetStatus() {
    EnsureStarted();
    Status st;
    const HostStatus hs = impl_->host->Status();
    st.locked = (hs.state == EngineState::Locked);
    st.unlockTime = hs.unlockTime;
    if (st.locked) st.config = impl_->host->ActiveConfig();
    return st;
}

void LocalEngine::Shutdown() {
    Impl& d = *impl_;
    if (!d.started) return;
    d.stop = true;
    if (d.ticker.joinable()) d.ticker.join();
    if (d.host) d.host->Shutdown(ShutdownMode::LeaveInPlace);
    d.host.reset();
    d.started = false;
}

#endif  // _WIN32
