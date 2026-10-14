// Website blocking, layer 2 (plan Module C): block the IP addresses a listed domain resolves to, via the
// Windows Filtering Platform. Portable core: resolving, diffing and safety rules run against two seams
// (IResolver, INetworkBackend) and are tested on any host. Win32NetworkBackend is the WFP part.
//
// Limits (by design): IPs behind shared CDNs also block other sites on that IP; a domain whose IPs
// change is only caught on Refresh(); DoH/VPN-tunnelled traffic to other IPs is not covered.
// Order matters: resolve BEFORE the hosts block is applied, or the resolver only sees 0.0.0.0.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/BlockRule.h"

struct IpAddress {
    bool v6 = false;
    std::array<std::uint8_t, 16> b{};   // v4 uses b[0..3]

    static IpAddress V4(std::uint8_t a, std::uint8_t b1, std::uint8_t c, std::uint8_t d);
    std::string ToString() const;                    // stable unique key (not necessarily canonical v6)
    bool operator<(const IpAddress& o) const { return v6 != o.v6 ? v6 < o.v6 : b < o.b; }
    bool operator==(const IpAddress& o) const { return v6 == o.v6 && b == o.b; }
};

// False for addresses we must never block: unspecified, loopback, private/link-local, multicast,
// reserved, and IPv4-mapped forms of those. Blocking these could cut off the router or the machine itself.
bool IsBlockableAddress(const IpAddress& ip);

class IResolver {
public:
    virtual ~IResolver() = default;
    virtual std::vector<IpAddress> Resolve(const std::string& host) = 0;
};

class INetworkBackend {
public:
    virtual ~INetworkBackend() = default;
    virtual bool AddBlock(const IpAddress& ip, std::uint64_t& filterId) = 0;
    virtual bool Delete(std::uint64_t filterId) = 0;
    virtual bool DeleteAllOurs() = 0;   // removes every filter of ours, incl. ones from a crashed run
};

class NetworkFilter {
public:
    enum class Result { Ok, PartialFailure, BackendFailed };
    static constexpr size_t kMaxIpsPerHost = 32;
    static constexpr size_t kMaxTotalIps = 2000;

    NetworkFilter(INetworkBackend& be, IResolver& res) : be_(be), res_(res) {}

    // Uses RuleType::Website rules only. Empty => ClearRules(). Removes filters for IPs no longer wanted.
    Result ApplyRules(const std::vector<BlockRule>& activeRules);
    Result Refresh();      // re-resolve current domains and ADD new IPs (never removes mid-session)
    void   ClearRules();   // removes everything of ours

    size_t BlockedCount() const { return filters_.size(); }
    const std::vector<std::string>& Domains() const { return domains_; }

private:
    Result Sync(bool allowRemove);

    INetworkBackend& be_;
    IResolver& res_;
    std::vector<std::string> domains_;                 // normalized, incl. "www." variants
    std::map<IpAddress, std::uint64_t> filters_;       // ip -> backend filter id
};
