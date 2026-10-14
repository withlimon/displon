// Windows backend for IHostsFile: %SystemRoot%\System32\drivers\etc\hosts. Needs admin rights to write.
#pragma once
#include <string>

#include "engine/HostsManager.h"

class Win32HostsFile final : public IHostsFile {
public:
    explicit Win32HostsFile(std::wstring path = {});   // empty = the real hosts file (tests pass a temp path)

    bool Read(std::string& out) override;
    bool Write(const std::string& content) override;           // temp file + atomic replace, in-place fallback
    bool WriteBackupIfAbsent(const std::string& original) override;   // "<hosts>.displon.bak", first one wins
    void FlushDnsCache() override;                              // best effort (undocumented dnsapi export)

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
};
