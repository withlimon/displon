#include "common/PathUtil.h"

#include <cwctype>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

wchar_t FoldChar(wchar_t c) {
    if (c < 0x80) return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
#ifdef _WIN32
    CharLowerBuffW(&c, 1);  // honours the user's Windows casing tables for non-ASCII
    return c;
#else
    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
#endif
}

std::wstring CaseFold(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) c = FoldChar(c);
    return out;
}

bool HasPathSeparator(std::wstring_view s) { return s.find_first_of(L"\\/") != std::wstring_view::npos; }

std::wstring FileNameOf(std::wstring_view path) {
    const auto pos = path.find_last_of(L"\\/");
    return std::wstring(pos == std::wstring_view::npos ? path : path.substr(pos + 1));
}

std::wstring NormalizePath(std::wstring_view input) {
    std::wstring p(input);
    for (auto& c : p)
        if (c == L'/') c = L'\\';

    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0) p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0) p = p.substr(4);

    std::wstring prefix;
    std::size_t start = 0;
    if (p.rfind(L"\\\\", 0) == 0) { prefix = L"\\\\"; start = 2; }
    else if (!p.empty() && p[0] == L'\\') { prefix = L"\\"; start = 1; }

    std::vector<std::wstring> parts;
    std::size_t i = start;
    while (i <= p.size()) {
        std::size_t j = p.find(L'\\', i);
        if (j == std::wstring::npos) j = p.size();
        std::wstring comp = CaseFold(std::wstring_view(p).substr(i, j - i));
        i = j + 1;
        if (comp.empty() || comp == L".") continue;
        if (comp == L"..") {
            const bool atDriveRoot = parts.size() == 1 && parts[0].size() == 2 && parts[0][1] == L':';
            if (!parts.empty() && parts.back() != L"..") {
                if (!atDriveRoot) parts.pop_back();
            } else if (prefix.empty()) {
                parts.push_back(comp);
            }
            continue;
        }
        parts.push_back(std::move(comp));
    }

    std::wstring out = prefix;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) out.push_back(L'\\');
        out += parts[k];
    }
    return out;
}

bool PathEquals(std::wstring_view a, std::wstring_view b) { return NormalizePath(a) == NormalizePath(b); }

bool IsPathUnder(std::wstring_view path, std::wstring_view folder) {
    const std::wstring p = NormalizePath(path);
    const std::wstring f = NormalizePath(folder);
    if (f.empty() || p.size() <= f.size()) return false;
    if (p.compare(0, f.size(), f) != 0) return false;
    return p[f.size()] == L'\\' || f.back() == L'\\';
}
