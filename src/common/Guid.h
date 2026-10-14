// Portable GUID (no windows.h so the engine builds and unit-tests on any host).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct Guid {
    std::array<std::uint8_t, 16> bytes{};

    static Guid Generate();                                  // random (v4)
    static std::optional<Guid> FromString(std::string_view); // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    std::string ToString() const;
    bool IsNull() const;

    friend bool operator==(const Guid&, const Guid&) = default;
};
