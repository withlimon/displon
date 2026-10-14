// Windows backends for NetworkFilter: WFP (user-mode, no driver) and a getaddrinfo resolver.
// Needs Administrator to add filters.
#pragma once
#include <string>
#include <vector>

#include "engine/NetworkFilter.h"

class Win32NetworkBackend final : public INetworkBackend {
public:
    // dynamic=true: the WFP session is dynamic, so every filter vanishes when this process exits or
    // crashes (right for the dev host: a crash can never leave sites blocked forever). The real service
    // (Module D) will use dynamic=false + persistent objects so blocks survive a service restart.
    explicit Win32NetworkBackend(bool dynamic = true);
    ~Win32NetworkBackend() override;
    Win32NetworkBackend(const Win32NetworkBackend&) = delete;
    Win32NetworkBackend& operator=(const Win32NetworkBackend&) = delete;

    bool IsOpen() const { return engine_ != nullptr; }
    unsigned long LastError() const { return lastError_; }   // WFP/Win32 error code of the last failure

    bool AddBlock(const IpAddress& ip, std::uint64_t& filterId) override;
    bool Delete(std::uint64_t filterId) override;
    bool DeleteAllOurs() override;

private:
    void* engine_ = nullptr;   // HANDLE (kept opaque so this header needs no Windows includes)
    bool persistent_ = false;
    unsigned long lastError_ = 0;
};

class Win32Resolver final : public IResolver {
public:
    Win32Resolver();
    ~Win32Resolver() override;
    std::vector<IpAddress> Resolve(const std::string& host) override;   // A + AAAA via getaddrinfo
private:
    bool started_ = false;
};
