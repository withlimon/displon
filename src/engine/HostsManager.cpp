#include "engine/HostsManager.h"

#include <algorithm>
#include <cassert>
#include <set>

namespace {

constexpr std::string_view kBegin = "# >>> Displon BEGIN - managed block, do not edit >>>";
constexpr std::string_view kEnd = "# <<< Displon END <<<";

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool ValidLabel(std::string_view l) {
    if (l.empty() || l.size() > 63 || l.front() == '-' || l.back() == '-') return false;
    return std::all_of(l.begin(), l.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

struct Line { std::string_view text; std::string_view eol; };   // text has no EOL; eol is "", "\n" or "\r\n"

std::vector<Line> SplitLines(std::string_view s) {
    std::vector<Line> out;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t nl = s.find('\n', pos);
        if (nl == std::string_view::npos) { out.push_back({s.substr(pos), {}}); break; }
        const bool cr = nl > pos && s[nl - 1] == '\r';
        out.push_back({s.substr(pos, nl - pos - (cr ? 1 : 0)), cr ? std::string_view("\r\n") : std::string_view("\n")});
        pos = nl + 1;
    }
    return out;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// A line our block would have produced: "0.0.0.0 host" or ":: host" (used to clean an orphaned block).
bool LooksLikeOurEntry(std::string_view line) {
    line = Trim(line);
    for (std::string_view ip : {std::string_view("0.0.0.0"), std::string_view("::")}) {
        if (line.size() > ip.size() + 1 && line.substr(0, ip.size()) == ip &&
            (line[ip.size()] == ' ' || line[ip.size()] == '\t')) {
            const std::string_view host = Trim(line.substr(ip.size()));
            if (host.find_first_of(" \t#") != std::string_view::npos) return false;
            return std::all_of(host.begin(), host.end(), [](char c) { return c != '\0'; }) && !host.empty();
        }
    }
    return false;
}

std::string_view DetectEol(std::string_view s) {
    if (s.find("\r\n") != std::string_view::npos) return "\r\n";
    if (s.find('\n') != std::string_view::npos) return "\n";
    return "\r\n";   // Windows default
}

std::string BuildBlock(const std::vector<std::string>& domains, std::string_view eol) {
    std::string b;
    b.append(kBegin).append(eol);
    for (const auto& d : domains) {
        // www. is added for every name: it is the one subdomain people actually type.
        for (const std::string& host : {d, "www." + d}) {
            b.append("0.0.0.0 ").append(host).append(eol);
            b.append(":: ").append(host).append(eol);
        }
    }
    b.append(kEnd).append(eol);
    return b;
}

}  // namespace

std::optional<std::string> NormalizeDomain(std::wstring_view in) {
    std::string s;
    s.reserve(in.size());
    for (wchar_t c : in) {
        if (c > 0x7E || c < 0x20) return std::nullopt;   // non-ASCII/control: no IDN support yet
        s.push_back(Lower(static_cast<char>(c)));
    }
    std::string_view v = Trim(s);

    if (const size_t p = v.find("://"); p != std::string_view::npos) v.remove_prefix(p + 3);
    v = v.substr(0, v.find_first_of("/?#\\"));                       // path / query / fragment
    if (const size_t at = v.rfind('@'); at != std::string_view::npos) v.remove_prefix(at + 1);   // userinfo
    if (const size_t colon = v.find(':'); colon != std::string_view::npos) {                      // port
        const std::string_view port = v.substr(colon + 1);
        if (port.empty() || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;   // also rejects IPv6 literals
        v = v.substr(0, colon);
    }
    while (!v.empty() && v.back() == '.') v.remove_suffix(1);        // FQDN trailing dot
    while (v.rfind("*.", 0) == 0) v.remove_prefix(2);
    while (v.rfind("www.", 0) == 0) v.remove_prefix(4);

    if (v.empty() || v.size() > 253 || v.find('.') == std::string_view::npos) return std::nullopt;

    size_t start = 0;
    std::string_view last;
    while (true) {
        const size_t dot = v.find('.', start);
        const std::string_view label = v.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (!ValidLabel(label)) return std::nullopt;
        last = label;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (std::all_of(last.begin(), last.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;   // "1.2.3.4": an IP, not a domain (hosts cannot block those meaningfully)
    return std::string(v);
}

std::vector<std::string> NormalizeDomains(const std::vector<std::wstring>& inputs, std::vector<std::wstring>* rejected) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& in : inputs) {
        if (auto d = NormalizeDomain(in)) {
            if (seen.insert(*d).second) out.push_back(std::move(*d));
        } else if (rejected) {
            rejected->push_back(in);
        }
    }
    return out;
}

std::string RemoveManagedBlock(std::string_view hosts) {
    const auto lines = SplitLines(hosts);
    std::string out;
    out.reserve(hosts.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        if (Trim(lines[i].text) != kBegin) {
            out.append(lines[i].text).append(lines[i].eol);
            continue;
        }
        // Find a matching END. If it is missing (file edited/truncated) only drop the entry lines that
        // directly follow BEGIN - never swallow the user's own content.
        size_t end = lines.size();
        for (size_t j = i + 1; j < lines.size(); ++j) {
            if (Trim(lines[j].text) == kEnd) { end = j; break; }
            if (!LooksLikeOurEntry(lines[j].text)) break;
        }
        if (end != lines.size()) { i = end; continue; }
        size_t j = i + 1;
        while (j < lines.size() && LooksLikeOurEntry(lines[j].text)) ++j;
        i = j - 1;
    }
    return out;
}

std::string ApplyManagedBlock(std::string_view hosts, const std::vector<std::string>& domains) {
    std::string base = RemoveManagedBlock(hosts);
    if (domains.empty()) return base;
    const std::string_view eol = DetectEol(hosts);
    if (!base.empty() && base.back() != '\n') base.append(eol);   // block always starts on its own line
    base += BuildBlock(domains, eol);
    return base;
}

bool IsManagedBlockIntact(std::string_view hosts, const std::vector<std::string>& domains) {
    if (domains.empty()) return RemoveManagedBlock(hosts) == hosts;   // nothing expected, nothing present
    const std::string_view eol = DetectEol(hosts);
    const std::string want = BuildBlock(domains, eol);
    const size_t at = hosts.find(want);
    if (at == std::string_view::npos) return false;
    return at == 0 || hosts[at - 1] == '\n';
}

HostsManager::Result HostsManager::Write(const std::vector<std::string>& domains) {
    std::string cur;
    if (!file_.Read(cur)) return Result::ReadFailed;
    const std::string next = ApplyManagedBlock(cur, domains);
    if (next != cur) {
        if (applied_.empty() && !domains.empty() && RemoveManagedBlock(cur) == cur) file_.WriteBackupIfAbsent(cur);
        if (!file_.Write(next)) return Result::WriteFailed;
        file_.FlushDnsCache();
    }
    std::string check;
    if (!file_.Read(check) || !IsManagedBlockIntact(check, domains)) return Result::VerifyFailed;
    applied_ = domains;
    return Result::Ok;
}

HostsManager::Result HostsManager::BlockDomains(const std::vector<std::wstring>& domains) {
    rejected_.clear();
    const auto norm = NormalizeDomains(domains, &rejected_);
    return Write(norm);   // empty list removes the block
}

HostsManager::Result HostsManager::RestoreHosts() { return Write({}); }

HostsManager::Result HostsManager::Sync(const std::vector<BlockRule>& activeRules) {
    std::vector<std::wstring> sites;
    for (const auto& r : activeRules)
        if (r.type == RuleType::Website) sites.push_back(r.target);
    return BlockDomains(sites);
}

bool HostsManager::IsIntact() {
    std::string cur;
    return file_.Read(cur) && IsManagedBlockIntact(cur, applied_);
}

HostsManager::Result HostsManager::Repair() {
    if (applied_.empty() || IsIntact()) return Result::Ok;
    return Write(std::vector<std::string>(applied_));
}
