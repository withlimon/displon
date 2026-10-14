#include "common/BootTime.h"

#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ctime>
#endif

std::int64_t CurrentBootEpochSecs() {
    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
#ifdef _WIN32
    const std::uint64_t upMs = GetTickCount64();   // counts sleep/hibernate, resets on a real boot
#else
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return 0;
    const std::uint64_t upMs = static_cast<std::uint64_t>(ts.tv_sec) * 1000ULL +
                               static_cast<std::uint64_t>(ts.tv_nsec) / 1000000ULL;
#endif
    return now - static_cast<std::int64_t>(upMs / 1000ULL);
}
