// Task Manager block, part (b) of plan Module B: the DisableTaskMgr policy makes Windows itself
// refuse Ctrl+Shift+Esc / launching Task Manager. Windows-only.
//
// Per-USER setting (HKCU). When the real Service runs as SYSTEM it must target the logged-on
// user's hive instead (Module D). Safe against crashes: a marker records that WE set the value, so
// SetTaskManagerPolicy(false) at startup cleans up after a crash and never removes a policy an
// administrator set on purpose.
#pragma once

bool SetTaskManagerPolicy(bool disabled);   // true on success (also true when there is nothing of ours to undo)
bool IsTaskManagerPolicyOurs();
