// Phase 3 / Module C, layer 2: NetworkFilter against fake resolver + fake WFP backend.
#include <map>
#include <set>

#include "engine/NetworkFilter.h"
#include "tests/TestHarness.h"

namespace {

struct FakeBackend final : INetworkBackend {
    std::map<std::uint64_t, IpAddress> live;
    std::uint64_t next = 1;
    bool failAdd = false, failDelete = false;
    int adds = 0, deletes = 0, clears = 0;

    bool AddBlock(const IpAddress& ip, std::uint64_t& id) override {
        if (failAdd) return false;
        id = next++; live[id] = ip; ++adds; return true;
    }
    bool Delete(std::uint64_t id) override {
        if (failDelete) return false;
        ++deletes; return live.erase(id) > 0;
    }
    bool DeleteAllOurs() override { live.clear(); ++clears; return true; }
    bool Has(const IpAddress& ip) const { for (auto& [id, a] : live) if (a == ip) return true; return false; }
};

struct FakeResolver final : IResolver {
    std::map<std::string, std::vector<IpAddress>> table;
    std::vector<IpAddress> Resolve(const std::string& h) override {
        auto it = table.find(h);
        return it == table.end() ? std::vector<IpAddress>{} : it->second;
    }
};

IpAddress A(int a, int b, int c, int d) { return IpAddress::V4(a, b, c, d); }
IpAddress V6(std::initializer_list<int> bytes) {
    IpAddress ip; ip.v6 = true; size_t i = 0;
    for (int v : bytes) ip.b[i++] = static_cast<std::uint8_t>(v);
    return ip;
}
std::vector<BlockRule> Sites(std::initializer_list<const wchar_t*> s) {
    std::vector<BlockRule> r;
    for (auto* x : s) r.push_back(MakeRule(RuleType::Website, x));
    return r;
}

}  // namespace

TEST(Net_blockable_address_rules) {
    CHECK(IsBlockableAddress(A(104, 16, 0, 1)));
    CHECK(!IsBlockableAddress(A(0, 0, 0, 0)));
    CHECK(!IsBlockableAddress(A(127, 0, 0, 1)));
    CHECK(!IsBlockableAddress(A(10, 1, 2, 3)));
    CHECK(!IsBlockableAddress(A(172, 16, 0, 1)) && !IsBlockableAddress(A(172, 31, 255, 255)));
    CHECK(IsBlockableAddress(A(172, 15, 0, 1)) && IsBlockableAddress(A(172, 32, 0, 1)));
    CHECK(!IsBlockableAddress(A(192, 168, 1, 1)));
    CHECK(!IsBlockableAddress(A(169, 254, 1, 1)));
    CHECK(!IsBlockableAddress(A(100, 64, 0, 1)));
    CHECK(!IsBlockableAddress(A(224, 0, 0, 1)) && !IsBlockableAddress(A(255, 255, 255, 255)));
    CHECK(!IsBlockableAddress(V6({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));             // ::
    CHECK(!IsBlockableAddress(V6({0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));             // ::1
    CHECK(!IsBlockableAddress(V6({0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));        // link-local
    CHECK(!IsBlockableAddress(V6({0xfd,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));          // unique-local
    CHECK(!IsBlockableAddress(V6({0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));          // multicast
    CHECK(!IsBlockableAddress(V6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,0,1})));   // mapped private
    CHECK(IsBlockableAddress(V6({0,0,0,0,0,0,0,0,0,0,0xff,0xff,104,16,0,1})));      // mapped public
    CHECK(IsBlockableAddress(V6({0x26,0x06,0x47,0,0,0,0,0,0,0,0,0,0,0,0x68,0x10})));
}

TEST(Net_apply_blocks_resolved_public_ips_for_name_and_www) {
    FakeBackend be; FakeResolver res;
    res.table["reddit.com"] = {A(151, 101, 1, 140), A(10, 0, 0, 1)};          // private one is skipped
    res.table["www.reddit.com"] = {A(151, 101, 1, 140), A(151, 101, 65, 140)};
    NetworkFilter nf(be, res);
    CHECK(nf.ApplyRules(Sites({L"https://Reddit.com/r/x", L"ignored-not-website"})) == NetworkFilter::Result::Ok);
    CHECK(nf.BlockedCount() == 2 && be.Has(A(151, 101, 1, 140)) && be.Has(A(151, 101, 65, 140)));
    CHECK(!be.Has(A(10, 0, 0, 1)));
    CHECK(be.adds == 2);                                                      // shared IP added once
}

TEST(Net_only_website_rules_are_used) {
    FakeBackend be; FakeResolver res;
    res.table["game.exe"] = {A(8, 8, 8, 8)};
    NetworkFilter nf(be, res);
    std::vector<BlockRule> r{MakeRule(RuleType::ProcessExe, L"game.exe"), MakeRule(RuleType::WindowTitle, L"YouTube")};
    CHECK(nf.ApplyRules(r) == NetworkFilter::Result::Ok);
    CHECK(nf.BlockedCount() == 0 && be.adds == 0 && be.clears == 1);          // no sites => cleared
}

TEST(Net_reapply_diffs_adds_and_removes) {
    FakeBackend be; FakeResolver res;
    res.table["a.com"] = {A(1, 1, 1, 1)};
    res.table["b.com"] = {A(2, 2, 2, 2)};
    NetworkFilter nf(be, res);
    nf.ApplyRules(Sites({L"a.com"}));
    CHECK(be.Has(A(1, 1, 1, 1)) && be.adds == 1);
    nf.ApplyRules(Sites({L"a.com"}));                                          // identical: no churn
    CHECK(be.adds == 1 && be.deletes == 0);
    nf.ApplyRules(Sites({L"b.com"}));                                          // swap
    CHECK(!be.Has(A(1, 1, 1, 1)) && be.Has(A(2, 2, 2, 2)) && be.deletes == 1);
}

TEST(Net_refresh_adds_new_ips_but_never_removes) {
    FakeBackend be; FakeResolver res;
    res.table["a.com"] = {A(1, 1, 1, 1)};
    NetworkFilter nf(be, res);
    nf.ApplyRules(Sites({L"a.com"}));
    res.table["a.com"] = {A(3, 3, 3, 3)};                                      // DNS moved
    CHECK(nf.Refresh() == NetworkFilter::Result::Ok);
    CHECK(be.Has(A(1, 1, 1, 1)) && be.Has(A(3, 3, 3, 3)) && be.deletes == 0);
    NetworkFilter idle(be, res);
    CHECK(idle.Refresh() == NetworkFilter::Result::Ok);                        // nothing applied: no-op
}

TEST(Net_clear_removes_everything_of_ours) {
    FakeBackend be; FakeResolver res;
    res.table["a.com"] = {A(1, 1, 1, 1)};
    NetworkFilter nf(be, res);
    nf.ApplyRules(Sites({L"a.com"}));
    nf.ClearRules();
    CHECK(be.live.empty() && nf.BlockedCount() == 0 && nf.Domains().empty());
    CHECK(nf.ApplyRules({}) == NetworkFilter::Result::Ok);
}

TEST(Net_caps_per_host_and_never_blocks_everything) {
    FakeBackend be; FakeResolver res;
    std::vector<IpAddress> many;
    for (int i = 1; i <= 100; ++i) many.push_back(A(50, 0, 0, i));
    res.table["big.com"] = many;
    NetworkFilter nf(be, res);
    nf.ApplyRules(Sites({L"big.com"}));
    CHECK(nf.BlockedCount() == NetworkFilter::kMaxIpsPerHost);                 // www shares the same list: still capped by the set
}

TEST(Net_reports_backend_failures_honestly) {
    FakeBackend be; FakeResolver res;
    res.table["a.com"] = {A(1, 1, 1, 1)};
    NetworkFilter nf(be, res);
    be.failAdd = true;
    CHECK(nf.ApplyRules(Sites({L"a.com"})) == NetworkFilter::Result::BackendFailed);
    CHECK(nf.BlockedCount() == 0);
    be.failAdd = false;
    CHECK(nf.ApplyRules(Sites({L"a.com"})) == NetworkFilter::Result::Ok);
    be.failDelete = true;
    res.table["a.com"] = {A(2, 2, 2, 2)};
    CHECK(nf.ApplyRules(Sites({L"a.com"})) == NetworkFilter::Result::PartialFailure);   // stale filter stays, reported
}

TEST(Net_unresolvable_domain_blocks_nothing_without_error) {
    FakeBackend be; FakeResolver res;
    NetworkFilter nf(be, res);
    CHECK(nf.ApplyRules(Sites({L"nowhere.example"})) == NetworkFilter::Result::Ok);
    CHECK(nf.BlockedCount() == 0 && nf.Domains().size() == 2);                 // kept, so Refresh() can retry
}
