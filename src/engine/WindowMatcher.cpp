#include "engine/WindowMatcher.h"

#include "common/PathUtil.h"

bool WildcardMatch(std::wstring_view pattern, std::wstring_view text) {
    const std::wstring p = CaseFold(pattern);
    const std::wstring t = CaseFold(text);
    std::size_t pi = 0, ti = 0, star = std::wstring::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == L'?' || p[pi] == t[ti])) { ++pi; ++ti; }
        else if (pi < p.size() && p[pi] == L'*') { star = pi++; mark = ti; }
        else if (star != std::wstring::npos) { pi = star + 1; ti = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == L'*') ++pi;
    return pi == p.size();
}

bool MatchesWindowTitle(std::wstring_view title, std::wstring_view target) {
    if (title.empty() || target.empty()) return false;
    if (target.find_first_of(L"*?") != std::wstring_view::npos) return WildcardMatch(target, title);
    return CaseFold(title).find(CaseFold(target)) != std::wstring::npos;
}

std::optional<WindowTitleAction> EvaluateWindow(const WindowInfo& w, const std::vector<BlockRule>& titleRules,
                                                const ExemptionPolicy& exempt) {
    if (w.title.empty() || IsProtectedProcess(w.pid, w.ownerName, exempt)) return std::nullopt;
    std::optional<WindowTitleAction> result;
    for (const auto& r : titleRules) {
        if (r.type != RuleType::WindowTitle || !MatchesWindowTitle(w.title, r.target)) continue;
        if (r.titleAction == WindowTitleAction::Close) return WindowTitleAction::Close;
        result = WindowTitleAction::Suppress;
    }
    return result;
}
