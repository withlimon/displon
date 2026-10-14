// Identifies "this boot of the machine" so an "until restart" lock can tell a real reboot from the app being
// relaunched inside the same Windows session. Portable (Windows: GetTickCount64, elsewhere: CLOCK_BOOTTIME).
#pragma once
#include <cstdint>

// Wall-clock seconds at which the current boot started (now - uptime). 0 if it cannot be determined.
// Two calls within one boot differ by a few seconds at most; two different boots differ by at least the time
// between them, so callers compare with a tolerance (see kSameBootToleranceSecs).
std::int64_t CurrentBootEpochSecs();

inline constexpr std::int64_t kSameBootToleranceSecs = 300;
