#include "engine/NetworkFilter.h"

#include <cstdio>
#include <set>

#include "engine/HostsManager.h"   // NormalizeDomains

IpAddress IpAddress::V4(std::uint8_t a, std::uint8_t b1, std::uint8_t c, std::uint8_t d) {
    IpAddress ip;
    ip.b[0] = a; ip.b[1] = b1; ip.b[2] = c; ip.b[3] = d;
    return ip;
}

std::string IpAddress::ToString() const {
    char buf[64];
    if (!v6) {
        std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        return buf;
    }
    std::string s;
    for (int i = 0; i < 16; i += 2) {
        std::snprintf(buf, sizeof buf, "%x", (b[i] << 8) | b[i + 1]);
        if (i) s += ':';
        s += buf;
    }
    return s;
}

namespace {
bool BlockableV4(const std::uint8_t* a) {
    if (a[0] == 0 || a[0] == 127 || a[0] == 10 || a[0] >= 224) return false;   // this-net, loopback, private, mcast/reserved
    if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return false;
    if (a[0] == 192 && a[1] == 168) return false;
    if (a[0] == 169 && a[1] == 254) return false;
    if (a[0] == 100 && a[1] >= 64 && a[1] <= 127) return false;                // CGNAT
    return true;
}
}  // namespace

bool IsBlockableAddress(const IpAddress& ip) {
    if (!ip.v6) return BlockableV4(ip.b.data());
    bool allZeroPrefix = true;
    for (int i = 0; i < 10; ++i) allZeroPrefix = allZeroPrefix && ip.b[i] == 0;
    if (allZeroPrefix && ip.b[10] == 0xff && ip.b[11] == 0xff) return BlockableV4(ip.b.data() + 12);   // ::ffff:a.b.c.d
    bool first15Zero = true;
    for (int i = 0; i < 15; ++i) first15Zero = first15Zero && ip.b[i] == 0;
    if (first15Zero && ip.b[15] <= 1) return false;                              // :: and ::1
    if (ip.b[0] == 0xfe && (ip.b[1] & 0xc0) == 0x80) return false;               // fe80::/10 link-local
    if ((ip.b[0] & 0xfe) == 0xfc) return false;                                  // fc00::/7 unique-local
    if (ip.b[0] == 0xff) return false;                                           // multicast
    return true;
}

NetworkFilter::Result NetworkFilter::ApplyRules(const std::vector<BlockRule>& activeRules) {
    std::vector<std::wstring> sites;
    for (const auto& r : activeRules)
        if (r.type == RuleType::Website) sites.push_back(r.target);
    const auto names = NormalizeDomains(sites);
    if (names.empty()) { ClearRules(); return Result::Ok; }

    domains_.clear();
    for (const auto& n : names) { domains_.push_back(n); domains_.push_back("www." + n); }
    return Sync(true);
}

NetworkFilter::Result NetworkFilter::Refresh() { return domains_.empty() ? Result::Ok : Sync(false); }

NetworkFilter::Result NetworkFilter::Sync(bool allowRemove) {
    std::set<IpAddress> wanted;
    for (const auto& d : domains_) {
        size_t taken = 0;
        for (const auto& ip : res_.Resolve(d)) {
            if (!IsBlockableAddress(ip)) continue;
            if (taken >= kMaxIpsPerHost) break;
            if (wanted.insert(ip).second) ++taken;
        }
    }
    bool partial = false;
    if (allowRemove) {
        for (auto it = filters_.begin(); it != filters_.end();) {
            if (wanted.count(it->first)) { ++it; continue; }
            if (be_.Delete(it->second)) it = filters_.erase(it);
            else { partial = true; ++it; }
        }
    }
    bool anyAdded = false, anyFailed = false;
    for (const auto& ip : wanted) {
        if (filters_.count(ip)) continue;
        if (filters_.size() >= kMaxTotalIps) { partial = true; break; }
        std::uint64_t id = 0;
        if (be_.AddBlock(ip, id)) { filters_[ip] = id; anyAdded = true; }
        else anyFailed = true;
    }
    if (anyFailed && !anyAdded && filters_.empty()) return Result::BackendFailed;
    return (anyFailed || partial) ? Result::PartialFailure : Result::Ok;
}

void NetworkFilter::ClearRules() {
    be_.DeleteAllOurs();   // also removes strays from a crashed earlier run
    filters_.clear();
    domains_.clear();
}
