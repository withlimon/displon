#pragma once
#include <cstddef>
#include <cstdint>

// Fills `out` with OS-seeded random bytes (std::random_device).
void FillRandom(std::uint8_t* out, std::size_t count);
