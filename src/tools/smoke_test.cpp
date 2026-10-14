// Phase 0 toolchain smoke test (plan §8.1).
// References the hard APIs so the linker proves headers + import libs exist.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <fwpmu.h>
#include <tdh.h>
#include <dwmapi.h>
#include <cstdio>

int main() {
    // WFP
    HANDLE engine = nullptr;
    (void)FwpmEngineOpen0;
    // ETW
    (void)StartTraceW;
    // SCM
    (void)RegisterServiceCtrlHandlerExW;
    // DWM
    (void)DwmSetWindowAttribute;
    // TDH
    (void)TdhEnumerateProviders;

    std::printf("DisplonSmoke: all required imports resolved\n");
    return 0;
}
#else
#include <cstdio>
int main() {
    std::printf("DisplonSmoke: non-Windows host — skip\n");
    return 0;
}
#endif
