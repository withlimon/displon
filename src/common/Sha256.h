#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest Sha256(const std::uint8_t* data, std::size_t len);
inline Sha256Digest Sha256(std::string_view s) {
    return Sha256(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}
std::string ToHex(const Sha256Digest& d);
