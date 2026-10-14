// Lexical Windows-path helpers (portable; no filesystem access). Used for rule matching.
#pragma once
#include <string>
#include <string_view>

wchar_t FoldChar(wchar_t c);                          // simple case fold (OS-aware on Windows)
std::wstring CaseFold(std::wstring_view s);
std::wstring NormalizePath(std::wstring_view path);   // folded, '\' only, no trailing '\', dots resolved, \\?\ stripped
bool PathEquals(std::wstring_view a, std::wstring_view b);
bool IsPathUnder(std::wstring_view path, std::wstring_view folder);  // strictly inside `folder`, component-safe
bool HasPathSeparator(std::wstring_view s);
std::wstring FileNameOf(std::wstring_view path);      // last component, original case
