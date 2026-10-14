// Website blocking, layer 1 (plan Module C): a marked block in the Windows hosts file.
// Portable core: text transforms + HostsManager run against an IHostsFile seam, so they are tested on
// any host. Win32HostsFile (engine/Win32HostsFile.*) is the only Windows-specific part.
//
// Limits (by design - WFP in NetworkFilter is the second layer): hosts has no wildcards, so
// subdomains other than www. are NOT covered; DoH/VPN/hard-coded IPs bypass it; it needs admin rights.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/BlockRule.h"

// "https://WWW.Reddit.com:443/r/x?y" -> "reddit.com". Strips scheme, userinfo, port, path, query,
// fragment, a leading "*." and a leading "www.". Returns nullopt for anything that is not a plain
// ASCII registrable-looking name (no dot, bad label, IP literal, "localhost", non-ASCII/IDN - TODO punycode).
std::optional<std::string> NormalizeDomain(std::wstring_view input);

// Normalizes + de-duplicates (order of first appearance). Rejected inputs go to *rejected if given.
std::vector<std::string> NormalizeDomains(const std::vector<std::wstring>& inputs,
                                          std::vector<std::wstring>* rejected = nullptr);

// Pure transforms on hosts-file text. Our entries live between BEGIN/END marker lines.
std::string ApplyManagedBlock(std::string_view hosts, const std::vector<std::string>& domains);
std::string RemoveManagedBlock(std::string_view hosts);   // also cleans an orphaned BEGIN (END lost)
bool IsManagedBlockIntact(std::string_view hosts, const std::vector<std::string>& domains);

class IHostsFile {
public:
    virtual ~IHostsFile() = default;
    virtual bool Read(std::string& out) = 0;
    virtual bool Write(const std::string& content) = 0;
    // Called with the ORIGINAL (pre-Displon) content before our first write. The backend keeps the
    // first one only and never overwrites it. A failed backup does not stop blocking.
    virtual bool WriteBackupIfAbsent(const std::string& original) = 0;
    virtual void FlushDnsCache() {}
};

class HostsManager {
public:
    enum class Result { Ok, ReadFailed, WriteFailed, VerifyFailed };

    explicit HostsManager(IHostsFile& file) : file_(file) {}

    Result BlockDomains(const std::vector<std::wstring>& domains);   // empty/all-invalid => RestoreHosts()
    Result RestoreHosts();                                           // removes only OUR block; user edits stay
    Result Sync(const std::vector<BlockRule>& activeRules);          // uses RuleType::Website rules only
    bool   IsIntact();                                               // block present and equals what we applied
    Result Repair();                                                 // re-applies if someone removed/edited it

    const std::vector<std::string>& AppliedDomains() const { return applied_; }
    const std::vector<std::wstring>& LastRejected() const { return rejected_; }

private:
    Result Write(const std::vector<std::string>& domains);

    IHostsFile& file_;
    std::vector<std::string> applied_;
    std::vector<std::wstring> rejected_;
};
