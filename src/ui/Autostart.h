// Brings the in-process lock back after a restart / sign-in (Windows only).
//
// DisplonUI.exe is built with requireAdministrator. Windows silently refuses to launch elevated programs from
// the HKCU\...\Run key at sign-in, so a Run key alone can never resume a lock. The reliable way to start an
// elevated program at logon without a UAC prompt is a Task Scheduler task with the "Highest privileges" run
// level and a logon trigger; that is what Ensure() registers. A Run key is only written as a fallback.
//
// The task runs `DisplonUI.exe --resume`: the app loads the persisted lock, enforces it, and exits straight
// away (removing the task) if there is nothing to resume.
#pragma once

namespace Autostart {

// Registers (or refreshes) the sign-in task for THIS executable. Idempotent; spawns schtasks.exe, so call it from
// a background thread. Returns true if some autostart mechanism is now in place.
bool Ensure();

// Starts DisplonWatchdog.exe in --guard-ui mode (next to this exe) unless it already runs. It relaunches the UI
// if the UI process is killed while a lock is persisted. Cheap; call every few seconds while locked.
void EnsureGuard();

// Removes the task and any Run-key fallback. Safe to call when nothing is registered.
void Remove();

}  // namespace Autostart
