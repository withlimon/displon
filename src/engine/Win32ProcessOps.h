// Windows backend for IProcessOps (Toolhelp snapshot + OpenProcess). Windows-only.
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/ProcessEnforcer.h"

class Win32ProcessOps final : public IProcessOps {
public:
    std::vector<ProcessInfo> Enumerate() override;
    bool Terminate(std::uint32_t pid) override;
    bool MinimizeWindows(std::uint32_t pid) override;

private:
    struct Cached { std::uint64_t start; std::wstring path; };
    std::unordered_map<std::uint32_t, Cached> cache_;   // path lookup only on a new (pid,start)
};

// Full image path of any process we are allowed to query; empty on failure.
std::wstring QueryImagePathByPid(std::uint32_t pid);

// This process plus every ancestor (the terminal that launched us): never terminate these.
std::vector<std::uint32_t> SelfAndAncestorPids();
