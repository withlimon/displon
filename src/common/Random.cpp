#include "common/Random.h"

#include <random>

void FillRandom(std::uint8_t* out, std::size_t count) {
    std::random_device rd;
    for (std::size_t i = 0; i < count; i += 4) {
        std::uint32_t v = rd();
        for (std::size_t j = 0; j < 4 && i + j < count; ++j) out[i + j] = static_cast<std::uint8_t>(v >> (8 * j));
    }
}
