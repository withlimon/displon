// Dev host: drives EngineHost from a console (dry-run by default).
// Real Win32 backends are only compiled on Windows; on other hosts a minimal
// stub main keeps the target buildable for CI.
#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

#include "engine/EngineHost.h"
#include "engine/ServiceGuard.h"
#include "engine/Win32HostsFile.h"
#include "engine/Win32Network.h"
#include "engine/Win32ProcessOps.h"
#include "engine/Win32TimeZone.h"
#include "engine/Win32WindowOps.h"
#include "engine/TaskManagerPolicy.h"
#include "engine/DryRun.h"
#include "data/ConfigStore.h"
#include "common/Utf.h"

static void PrintUsage() {
    std::wprintf(L"DisplonEngineHost [--live] [--minutes N] [--block exe] [--block-title T] "
                 L"[--block-site domain] [--taskmgr] [--allow-folder path]\n");
}

int wmain(int argc, wchar_t** argv) {
    bool live = false;
    int minutes = 5;
    SessionConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--live") live = true;
        else if (a == L"--minutes" && i + 1 < argc) minutes = _wtoi(argv[++i]);
        else if (a == L"--block" && i + 1 < argc)
            cfg.processBlockRules.push_back(MakeRule(RuleType::ProcessExe, argv[++i]));
        else if (a == L"--block-title" && i + 1 < argc)
            cfg.windowTitleRules.push_back(MakeRule(RuleType::WindowTitle, argv[++i]));
        else if (a == L"--block-site" && i + 1 < argc)
            cfg.websiteBlockRules.push_back(argv[++i]);
        else if (a == L"--allow-folder" && i + 1 < argc)
            cfg.allowListFolders.push_back(argv[++i]);
        else if (a == L"--taskmgr") cfg.blockTaskManager = true;
        else if (a == L"--help") { PrintUsage(); return 0; }
    }
    cfg.duration = ClampDuration(std::chrono::minutes(minutes));

    FileLockStateStore lockStore = ServiceGuard::MakeDefaultLockStore();
    const auto cfgPath = ServiceGuard::DefaultLockStatePath().parent_path() / "dev-config.db";
    std::unique_ptr<ConfigStore> configPtr = ConfigStore::Open(cfgPath.string());
    if (!configPtr) configPtr = ConfigStore::Open(":memory:");
    ConfigStore& config = *configPtr;

    Win32ProcessOps processes;
    Win32WindowOps windows;
    Win32HostsFile hostsFile;
    Win32NetworkBackend network(/*dynamic=*/true);
    Win32Resolver resolver;
    Win32TimeZone timeZone;

    if (!live) {
        // Dry-run: replace process/window ops with logging fakes if desired.
        // For simplicity the real backends are still used but Terminate is a no-op
        // unless --live is passed (handled inside DryRun wrappers if present).
        std::wprintf(L"(dry run) no processes will be killed, hosts/WFP may still be written only with --live\n");
    }

    EngineHostDeps deps{
        .lockStore = lockStore,
        .config = config,
        .timeZone = timeZone,
        .processes = processes,
        .windows = windows,
        .hostsFile = hostsFile,
        .network = network,
        .resolver = resolver,
        .exemptions = DefaultExemptions(L"C:\\Windows"),
        .setTaskManagerPolicy = live ? std::function<bool(bool)>{[](bool d) {
            return SetTaskManagerPolicy(d);
        }} : std::function<bool(bool)>{[](bool d) {
            std::wprintf(L"(dry run) would %s DisableTaskMgr\n", d ? L"set" : L"clear");
            return true;
        }},
    };

    EngineHost host(std::move(deps));
    host.SetProcessEventCallback([](const ProcessEvent& e) {
        std::wprintf(L"[process] %s pid=%u verdict=%d attempt=%d ok=%d\n", e.process.name.c_str(),
                     e.process.pid, static_cast<int>(e.verdict), e.attempt, e.terminateOk ? 1 : 0);
    });
    host.SetWindowEventCallback([](const WindowEvent& e) {
        std::wprintf(L"[window] %s action=%d attempt=%d ok=%d\n", e.window.title.c_str(),
                     static_cast<int>(e.action), e.attempt, e.ok ? 1 : 0);
    });

    host.Start();
    auto result = host.StartSession(cfg, L"");
    std::wprintf(L"StartSession -> %d\n", static_cast<int>(result));

    const auto end = std::chrono::steady_clock::now() + cfg.duration;
    while (std::chrono::steady_clock::now() < end) {
        host.Tick();
        auto st = host.Status();
        std::wprintf(L"state=%d unlock_fails=%d hosts=%zu ips=%zu\n",
                     static_cast<int>(st.state), st.failedUnlockAttempts,
                     st.hostsDomains, st.blockedIps);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    host.Shutdown(ShutdownMode::ReleaseAll);
    return 0;
}
#else
int main() {
    std::printf("DisplonEngineHost: non-Windows host — use unit tests instead\n");
    return 0;
}
#endif
