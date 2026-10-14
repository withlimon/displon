#include "engine/ProcessMatcher.h"

#include <algorithm>
#include <cassert>

#include "common/PathUtil.h"

namespace {

std::wstring EffectiveName(const ProcessInfo& p) { return p.name.empty() ? FileNameOf(p.imagePath) : p.name; }

}  // namespace

bool IsProtectedProcess(std::uint32_t pid, std::wstring_view name, const ExemptionPolicy& ex) {
    if (pid == 0 || pid == 4) return true;  // Idle, System
    if (std::find(ex.exemptPids.begin(), ex.exemptPids.end(), pid) != ex.exemptPids.end()) return true;
    const std::wstring folded = CaseFold(name);
    return std::any_of(ex.protectedNames.begin(), ex.protectedNames.end(),
                       [&](const std::wstring& n) { return CaseFold(n) == folded; });
}

ProcessPolicy BuildProcessPolicy(const std::vector<BlockRule>& activeRules) {
    ProcessPolicy pol;
    for (const auto& r : activeRules) {
        switch (r.type) {
        case RuleType::ProcessExe:        pol.denyRules.push_back(r); break;
        case RuleType::AllowListExe:      pol.allowExePaths.push_back(r.target); break;
        case RuleType::AllowListFolder:   pol.allowFolders.push_back(r.target); break;
        default: break;  // WindowTitle / Website belong to other modules
        }
    }
    return pol;
}

ExemptionPolicy DefaultExemptions(std::wstring_view systemRoot, std::vector<std::wstring> extraExemptFolders,
                                  std::vector<std::uint32_t> exemptPids) {
    ExemptionPolicy ex;
    ex.protectedNames = {L"system",    L"system idle process", L"registry", L"memory compression",
                         L"smss.exe",  L"csrss.exe",           L"wininit.exe", L"winlogon.exe",
                         L"services.exe", L"lsass.exe",        L"svchost.exe", L"dwm.exe",
                         L"fontdrvhost.exe", L"explorer.exe", L"ctfmon.exe", L"sihost.exe",
                         L"taskhostw.exe", L"runtimebroker.exe", L"conhost.exe", L"audiodg.exe",
                         // Displon itself must never be killed by its own allow-list.
                         L"displonui.exe", L"displonservice.exe", L"displonwatchdog.exe",
                         L"displonenginehost.exe"};
    if (!systemRoot.empty()) {
        ex.exemptFolders.emplace_back(systemRoot);  // explorer.exe, cmd.exe, DriverStore services, ...
        // Windows keeps more of its own plumbing outside C:\Windows (Defender, Security, Store runtime).
        // Killing those makes the machine unstable, so they are never "not allowed".
        if (systemRoot.size() >= 2 && systemRoot[1] == L':') {
            const std::wstring drive(systemRoot.substr(0, 2));
            ex.exemptFolders.push_back(drive + L"\\ProgramData\\Microsoft");
            ex.exemptFolders.push_back(drive + L"\\Program Files\\Windows Defender");
            ex.exemptFolders.push_back(drive + L"\\Program Files\\Windows Defender Advanced Threat Protection");
            ex.exemptFolders.push_back(drive + L"\\Program Files\\WindowsApps\\Microsoft.Windows");
            ex.exemptFolders.push_back(drive + L"\\Program Files\\Common Files\\Microsoft Shared");
        }
    }
    for (auto& f : extraExemptFolders) ex.exemptFolders.push_back(std::move(f));
    ex.exemptPids = std::move(exemptPids);
    return ex;
}

bool MatchesBlockedProcess(const ProcessInfo& p, const std::vector<BlockRule>& denyRules) {
    const std::wstring name = CaseFold(EffectiveName(p));
    for (const auto& r : denyRules) {
        if (r.type != RuleType::ProcessExe || r.target.empty()) continue;
        if (HasPathSeparator(r.target)) {
            if (!p.imagePath.empty() && PathEquals(p.imagePath, r.target)) return true;
        } else if (CaseFold(r.target) == name) {
            return true;
        }
    }
    return false;
}

bool MatchesAllowListFolder(const ProcessInfo& p, const std::vector<std::wstring>& allowedFolders) {
    if (p.imagePath.empty()) return false;
    return std::any_of(allowedFolders.begin(), allowedFolders.end(),
                       [&](const std::wstring& f) { return IsPathUnder(p.imagePath, f); });
}

bool IsAllowed(const ProcessInfo& p, const ProcessPolicy& policy) {
    assert(policy.AllowListEngaged() && "IsAllowed is only meaningful when an allow-list is engaged");
    const std::wstring name = CaseFold(EffectiveName(p));
    for (const auto& exe : policy.allowExePaths) {
        if (HasPathSeparator(exe)) {
            if (!p.imagePath.empty() && PathEquals(p.imagePath, exe)) return true;
        } else if (!exe.empty() && CaseFold(exe) == name) {
            return true;   // bare name ("firefox.exe") whitelists by file name
        }
    }
    return MatchesAllowListFolder(p, policy.allowFolders);
}

Verdict EvaluateProcess(const ProcessInfo& p, const ProcessPolicy& policy, const ExemptionPolicy& exempt) {
    if (IsProtectedProcess(p.pid, EffectiveName(p), exempt)) return Verdict::Allow;
    if (MatchesBlockedProcess(p, policy.denyRules)) return Verdict::TerminateBlocked;
    if (policy.AllowListEngaged()) {
        // Unknown path (access denied) -> fail open: never kill what we cannot even identify.
        if (p.imagePath.empty()) return Verdict::Allow;
        for (const auto& f : exempt.exemptFolders)
            if (IsPathUnder(p.imagePath, f)) return Verdict::Allow;
        if (!IsAllowed(p, policy)) return Verdict::TerminateNotAllowed;
    }
    return Verdict::Allow;
}
