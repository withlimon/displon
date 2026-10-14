// DisplonService.exe — Windows Service host for EngineHost + IPC pipe.
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "data/ConfigStore.h"
#include "engine/EngineHost.h"
#include "engine/ServiceGuard.h"
#include "engine/TaskManagerPolicy.h"
#include "engine/Win32HostsFile.h"
#include "engine/Win32Network.h"
#include "engine/Win32ProcessOps.h"
#include "engine/Win32TimeZone.h"
#include "engine/Win32WindowOps.h"
#include "ipc/Dispatcher.h"
#include "ipc/PipeServer.h"

namespace {

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gStatus{};
std::atomic<bool> gStop{false};
std::unique_ptr<EngineHost> gHost;
std::unique_ptr<ipc::PipeServer> gPipe;

void ReportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    gStatus.dwCurrentState = state;
    gStatus.dwWin32ExitCode = exitCode;
    gStatus.dwWaitHint = waitHint;
    gStatus.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0 : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    SetServiceStatus(gStatusHandle, &gStatus);
}

DWORD WINAPI CtrlHandler(DWORD ctrl, DWORD, LPVOID, LPVOID) {
    if (ctrl == SERVICE_CONTROL_STOP || ctrl == SERVICE_CONTROL_SHUTDOWN) {
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        gStop = true;
        if (gPipe) gPipe->Stop();
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void ServiceMain(DWORD, LPWSTR*) {
    gStatusHandle = RegisterServiceCtrlHandlerExW(L"Displon", CtrlHandler, nullptr);
    if (!gStatusHandle) return;

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 8000);

    static FileLockStateStore lockStore = ServiceGuard::MakeDefaultLockStore();
    const auto cfgPath = ServiceGuard::DefaultLockStatePath().parent_path() / "config.db";
    static std::unique_ptr<ConfigStore> configPtr = ConfigStore::Open(cfgPath.string());
    if (!configPtr) configPtr = ConfigStore::Open(":memory:");   // never run without a store
    ConfigStore& config = *configPtr;

    static Win32ProcessOps processes;
    static Win32WindowOps windows;
    static Win32HostsFile hostsFile;
    static Win32NetworkBackend network(/*dynamic=*/false);
    static Win32Resolver resolver;
    static Win32TimeZone timeZone;

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
        .setTaskManagerPolicy = [](bool disabled) { return SetTaskManagerPolicy(disabled); },
    };

    gHost = std::make_unique<EngineHost>(std::move(deps));
    gHost->Start();

    ipc::Dispatcher dispatcher(*gHost);
    gPipe = std::make_unique<ipc::PipeServer>(
        [&dispatcher](const ipc::Message& m) { return dispatcher.Handle(m); });
    gPipe->Start();

    ReportStatus(SERVICE_RUNNING);

    while (!gStop) {
        gHost->Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (gPipe) {
        gPipe->Stop();
        gPipe.reset();
    }
    gHost->Shutdown(ShutdownMode::LeaveInPlace);
    gHost.reset();
    ReportStatus(SERVICE_STOPPED);
}

}  // namespace

int wmain(int, wchar_t**) {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(L"Displon"), ServiceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table)) return 1;
    return 0;
}

#else
int main() { return 0; }
#endif
