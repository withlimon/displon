#include "common/Guid.h"

#include "common/Random.h"

Guid Guid::Generate() {
    Guid g;
    FillRandom(g.bytes.data(), g.bytes.size());
    g.bytes[6] = static_cast<std::uint8_t>((g.bytes[6] & 0x0F) | 0x40);  // version 4
    g.bytes[8] = static_cast<std::uint8_t>((g.bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant
    return g;
}

std::string Guid::ToString() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back('-');
        s.push_back(kHex[bytes[i] >> 4]);
        s.push_back(kHex[bytes[i] & 0x0F]);
    }
    return s;
}

bool Guid::IsNull() const {
    for (auto b : bytes)
        if (b != 0) return false;
    return true;
}

std::optional<Guid> Guid::FromString(std::string_view s) {
    if (s.size() != 36) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Guid g;
    std::size_t out = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return std::nullopt;
            ++i;
            continue;
        }
        int hi = nibble(s[i]), lo = nibble(s[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        g.bytes[out++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return g;
}
