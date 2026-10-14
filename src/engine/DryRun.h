// In-memory stand-ins for the OS seams, used by the dev host's dry-run mode and by tests.
// Portable: no Windows headers. They behave like a well-behaved OS (writes stick, ids unique).
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "engine/HostsManager.h"
#include "engine/NetworkFilter.h"

// A hosts file that lives in memory only. `content` can be edited by tests to simulate tampering.
class InMemoryHostsFile final : public IHostsFile {
public:
    std::string content = "127.0.0.1 localhost\r\n";
    std::string backup;
    int writes = 0;
    int flushes = 0;
    bool Read(std::string& out) override { out = content; return true; }
    bool Write(const std::string& c) override { content = c; ++writes; return true; }
    bool WriteBackupIfAbsent(const std::string& original) override {
        if (backup.empty()) backup = original;
        return true;
    }
    void FlushDnsCache() override { ++flushes; }
};

// Pretends to install WFP filters. Counts what a real backend would hold.
class NullNetworkBackend final : public INetworkBackend {
public:
    std::map<std::uint64_t, IpAddress> live;
    int clears = 0;
    bool AddBlock(const IpAddress& ip, std::uint64_t& id) override { id = next_++; live[id] = ip; return true; }
    bool Delete(std::uint64_t id) override { return live.erase(id) > 0; }
    bool DeleteAllOurs() override { live.clear(); ++clears; return true; }
private:
    std::uint64_t next_ = 1;
};
