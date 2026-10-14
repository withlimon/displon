// Module D: self-protection helpers that do not require a custom kernel driver.
// Portable pieces (path helpers, lock-file path) live here; Win32 ACL hardening
// is behind #ifdef _WIN32 so the rest of the tree still builds on Linux.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "engine/FileLockStateStore.h"

namespace ServiceGuard {

// Default location for the lock-state file (under the user's LocalAppData on
// Windows, under $HOME/.local/share/displon on other hosts for tests).
std::filesystem::path DefaultLockStatePath();

// Extra places the lock state is mirrored to (Windows: %PROGRAMDATA% and %APPDATA%), so that deleting the one
// file in %LOCALAPPDATA% does not release a lock. Empty on non-Windows hosts.
std::vector<std::filesystem::path> DefaultLockMirrorPaths();

// Create a FileLockStateStore pointed at the default path (with the mirrors above).
FileLockStateStore MakeDefaultLockStore();

#ifdef _WIN32
// Harden a service so non-SYSTEM callers cannot stop/delete it.
// Returns false on any failure (caller should log and continue — better a
// running service than a failed install).
bool HardenServiceACL(void* scHandle /* SC_HANDLE */);

// Ensure a Run-key entry so the watchdog restarts after login.
bool EnsureRunKey(const std::wstring& exePath);

// Ensure a scheduled task that launches the service/watchdog at boot.
bool EnsureScheduledTask(const std::wstring& exePath);
#endif

}  // namespace ServiceGuard
