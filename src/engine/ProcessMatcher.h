// Pure matching/decision logic for process blocking (plan Module B). Portable and unit-tested.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/BlockRule.h"

struct ProcessInfo {
    std::uint32_t pid = 0;
    std::uint64_t startTime = 0;   // creation time; with pid it identifies one process instance
    std::wstring  imagePath;       // full path; EMPTY when the OS refused to tell us
    std::wstring  name;            // image file name (e.g. "notepad.exe")
};

// Process-relevant slice of the active rules.
struct ProcessPolicy {
    std::vector<BlockRule>    denyRules;      // RuleType::ProcessExe
    std::vector<std::wstring> allowExePaths;  // RuleType::AllowListExe
    std::vector<std::wstring> allowFolders;   // RuleType::AllowListFolder

    bool AllowListEngaged() const { return !allowExePaths.empty() || !allowFolders.empty(); }
    bool Empty() const { return denyRules.empty() && !AllowListEngaged(); }
};
ProcessPolicy BuildProcessPolicy(const std::vector<BlockRule>& activeRules);  // from EngineCore::ActiveRules()

// Things that must NEVER be terminated by the allow-list "kill everything else" logic (and the
// protected names/pids not even by an explicit rule). Without this, an allow-list session would
// kill explorer.exe, svchost.exe, the user's own terminal, ...
struct ExemptionPolicy {
    std::vector<std::wstring>  protectedNames;   // never terminated, period
    std::vector<std::wstring>  exemptFolders;    // exempt from allow-list termination only
    std::vector<std::uint32_t> exemptPids;       // our own process and its ancestors
};
ExemptionPolicy DefaultExemptions(std::wstring_view systemRoot,
                                  std::vector<std::wstring> extraExemptFolders = {},
                                  std::vector<std::uint32_t> exemptPids = {});

// True for processes that must never be terminated / have their windows touched.
bool IsProtectedProcess(std::uint32_t pid, std::wstring_view name, const ExemptionPolicy& exempt);

// Bare name ("game.exe") matches by file name; a rule containing a separator matches the full path.
bool MatchesBlockedProcess(const ProcessInfo& p, const std::vector<BlockRule>& denyRules);
// Path-prefix check against each folder (subfolders included, component-safe).
bool MatchesAllowListFolder(const ProcessInfo& p, const std::vector<std::wstring>& allowedFolders);
// Allow-list check only: exe path listed, or inside a listed folder. Needs AllowListEngaged().
bool IsAllowed(const ProcessInfo& p, const ProcessPolicy& policy);

enum class Verdict { Allow, TerminateBlocked, TerminateNotAllowed };
Verdict EvaluateProcess(const ProcessInfo& p, const ProcessPolicy& policy, const ExemptionPolicy& exempt);
