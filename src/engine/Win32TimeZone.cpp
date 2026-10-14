#include "engine/Win32TimeZone.h"

#include <windows.h>

#include <cstdint>

namespace {
constexpr std::int64_t kUnixToFileTimeSecs = 11644473600LL;   // 1601 -> 1970

FILETIME ToFileTime(TimePoint t) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
    const std::uint64_t ticks = static_cast<std::uint64_t>(ms * 10000 + kUnixToFileTimeSecs * 10000000LL);
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
}
TimePoint FromFileTime(const FILETIME& ft) {
    const std::uint64_t ticks = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    const std::int64_t ms = static_cast<std::int64_t>(ticks / 10000) - kUnixToFileTimeSecs * 1000;
    return TimePoint(std::chrono::milliseconds(ms));
}
}  // namespace

LocalTime Win32TimeZone::ToLocal(TimePoint t) const {
    const FILETIME ft = ToFileTime(t);
    SYSTEMTIME utc = {}, local = {};
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);   // nullptr = current time zone
    LocalTime lt;
    lt.year = local.wYear;
    lt.month = local.wMonth;
    lt.day = local.wDay;
    lt.minuteOfDay = local.wHour * 60 + local.wMinute;
    lt.weekday = (local.wDayOfWeek + 6) % 7;   // Windows: 0 = Sunday -> 0 = Monday
    return lt;
}

TimePoint Win32TimeZone::FromLocal(const LocalTime& lt) const {
    SYSTEMTIME local = {}, utc = {};
    local.wYear = static_cast<WORD>(lt.year);
    local.wMonth = static_cast<WORD>(lt.month);
    local.wDay = static_cast<WORD>(lt.day);
    local.wHour = static_cast<WORD>(lt.minuteOfDay / 60);
    local.wMinute = static_cast<WORD>(lt.minuteOfDay % 60);
    TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc);
    FILETIME ft;
    SystemTimeToFileTime(&utc, &ft);
    return FromFileTime(ft);
}
