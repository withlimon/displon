// Window-title matching + decision (plan Module B, WindowTitleWatcher). Portable.
//
// Default matching (open decision, see plan Section 10): case-insensitive SUBSTRING.
// If the rule text contains '*' or '?', it is a WHOLE-TITLE wildcard instead
// ("YouTube*" = starts with, "*- Chrome" = ends with, "*game*" = contains).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/BlockRule.h"
#include "engine/ProcessMatcher.h"

struct WindowInfo {
    std::uint64_t id = 0;        // HWND on Windows
    std::uint32_t pid = 0;       // owning process
    std::wstring  ownerName;     // owning exe name, may be empty
    std::wstring  title;
    bool          minimized = false;
};

bool WildcardMatch(std::wstring_view pattern, std::wstring_view text);   // whole string, '*' and '?', case-insens.
bool MatchesWindowTitle(std::wstring_view title, std::wstring_view ruleTarget);

// Close wins over Suppress when both match. nullopt = leave the window alone.
// Windows of protected processes / our own process tree are never touched.
std::optional<WindowTitleAction> EvaluateWindow(const WindowInfo& w, const std::vector<BlockRule>& titleRules,
                                                const ExemptionPolicy& exempt);
