#include "engine/LockState.h"

#include <vector>

#include "common/Random.h"
#include "common/Utf.h"

namespace {
constexpr int kRounds = 10000;

Sha256Digest Derive(const std::array<std::uint8_t, 16>& salt, std::wstring_view password) {
    const std::string utf8 = WideToUtf8(password);
    std::vector<std::uint8_t> buf(salt.begin(), salt.end());
    buf.insert(buf.end(), utf8.begin(), utf8.end());
    Sha256Digest d = Sha256(buf.data(), buf.size());
    for (int i = 0; i < kRounds; ++i) {
        std::vector<std::uint8_t> next(d.begin(), d.end());
        next.insert(next.end(), salt.begin(), salt.end());
        d = Sha256(next.data(), next.size());
    }
    return d;
}
}  // namespace

PasswordRecord MakePasswordRecord(std::wstring_view password) {
    PasswordRecord r;
    if (password.empty()) return r;
    r.set = true;
    FillRandom(r.salt.data(), r.salt.size());
    r.hash = Derive(r.salt, password);
    return r;
}

bool VerifyPassword(const PasswordRecord& record, std::wstring_view password) {
    if (!record.set) return false;
    const Sha256Digest d = Derive(record.salt, password);
    std::uint8_t diff = 0;  // constant-time compare
    for (std::size_t i = 0; i < d.size(); ++i) diff |= static_cast<std::uint8_t>(d[i] ^ record.hash[i]);
    return diff == 0;
}
