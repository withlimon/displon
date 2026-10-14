// std::wstring <-> UTF-8, correct for both 16-bit (Windows) and 32-bit wchar_t.
// Invalid input becomes U+FFFD instead of failing.
#pragma once
#include <string>
#include <string_view>

std::string  WideToUtf8(std::wstring_view w);
std::wstring Utf8ToWide(std::string_view s);
