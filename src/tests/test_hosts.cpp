// Phase 3 / Module C, layer 1: hosts-file blocking. Everything runs against an in-memory IHostsFile.
#include <string>
#include <vector>

#include "engine/HostsManager.h"
#include "tests/TestHarness.h"

namespace {

struct FakeHosts final : IHostsFile {
    std::string content;
    std::string backup;
    bool hasBackup = false;
    int  flushes = 0, backupCalls = 0;
    bool failRead = false, failWrite = false, dropWrites = false;

    bool Read(std::string& out) override { if (failRead) return false; out = content; return true; }
    bool Write(const std::string& c) override {
        if (failWrite) return false;
        if (!dropWrites) content = c;
        return true;
    }
    bool WriteBackupIfAbsent(const std::string& o) override {
        ++backupCalls;
        if (!hasBackup) { backup = o; hasBackup = true; }
        return true;
    }
    void FlushDnsCache() override { ++flushes; }
};

const std::string kOrig =
    "# Copyright (c) 1993-2009 Microsoft Corp.\r\n"
    "127.0.0.1       localhost\r\n"
    "::1             localhost\r\n";

bool Contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
std::optional<std::string> N(const wchar_t* s) { return NormalizeDomain(s); }

}  // namespace

// ---------- domain normalization ----------
TEST(Hosts_normalize_accepts_common_forms) {
    CHECK(N(L"reddit.com") == "reddit.com");
    CHECK(N(L"  Reddit.COM  ") == "reddit.com");
    CHECK(N(L"https://www.reddit.com/r/programming?x=1#top") == "reddit.com");
    CHECK(N(L"http://user:pw@www.Example.org:8080/a/b") == "example.org");
    CHECK(N(L"*.youtube.com") == "youtube.com");
    CHECK(N(L"www.facebook.com.") == "facebook.com");
    CHECK(N(L"news.ycombinator.com") == "news.ycombinator.com");
    CHECK(N(L"my_host.example.co.uk") == "my_host.example.co.uk");
}

TEST(Hosts_normalize_rejects_garbage) {
    CHECK(!N(L""));
    CHECK(!N(L"   "));
    CHECK(!N(L"localhost"));              // single label: never block
    CHECK(!N(L"192.168.1.1"));            // IP literal
    CHECK(!N(L"[::1]:80"));               // IPv6 literal
    CHECK(!N(L"bad domain.com"));         // space
    CHECK(!N(L"-bad.com"));
    CHECK(!N(L"bad-.com"));
    CHECK(!N(L"a..com"));
    CHECK(!N(L"exa#mple.com"));
    CHECK(!N(L"evil.com\nmore.com"));     // control char: no line injection into hosts
    CHECK(!N(L"example.com:abc"));
    CHECK(!N(L"\u09AB\u09CB\u09B2\u09CD\u09A1.com"));   // Bangla/IDN: unsupported until punycode exists
    CHECK(!N(L"https://"));
    CHECK(!N(L"www."));
    CHECK(!N((std::wstring(64, L'a') + L".com").c_str()));   // label > 63
}

TEST(Hosts_normalize_list_dedups_and_reports_rejects) {
    std::vector<std::wstring> rej;
    const auto d = NormalizeDomains({L"reddit.com", L"https://www.reddit.com/", L"bad domain", L"x.com", L"REDDIT.com"}, &rej);
    CHECK(d.size() == 2 && d[0] == "reddit.com" && d[1] == "x.com");
    CHECK(rej.size() == 1 && rej[0] == L"bad domain");
}

// ---------- pure transforms ----------
TEST(Hosts_apply_adds_block_keeps_original_and_crlf) {
    const std::string out = ApplyManagedBlock(kOrig, {"reddit.com"});
    CHECK(out.rfind(kOrig, 0) == 0);
    CHECK(Contains(out, "0.0.0.0 reddit.com\r\n"));
    CHECK(Contains(out, ":: reddit.com\r\n"));
    CHECK(Contains(out, "0.0.0.0 www.reddit.com\r\n"));
    CHECK(Contains(out, ":: www.reddit.com\r\n"));
    CHECK(out.find("\r\n") != std::string::npos);
    for (size_t i = 0; i < out.size(); ++i)                   // every LF is part of CRLF
        if (out[i] == '\n') CHECK(i > 0 && out[i - 1] == '\r');
}

TEST(Hosts_apply_is_idempotent_and_replaces_old_block) {
    const std::string once = ApplyManagedBlock(kOrig, {"a.com", "b.com"});
    CHECK(ApplyManagedBlock(once, {"a.com", "b.com"}) == once);
    const std::string swapped = ApplyManagedBlock(once, {"c.com"});
    CHECK(!Contains(swapped, "a.com") && !Contains(swapped, "b.com") && Contains(swapped, "0.0.0.0 c.com"));
    CHECK(swapped.rfind(kOrig, 0) == 0);
}

TEST(Hosts_remove_round_trips) {
    CHECK(RemoveManagedBlock(ApplyManagedBlock(kOrig, {"reddit.com"})) == kOrig);
    CHECK(RemoveManagedBlock(kOrig) == kOrig);
    CHECK(RemoveManagedBlock("") == "");
    CHECK(ApplyManagedBlock(kOrig, {}) == kOrig);              // empty list == remove
}

TEST(Hosts_lf_files_stay_lf_and_missing_final_newline_is_handled) {
    const std::string lf = "127.0.0.1 localhost\n";
    const std::string out = ApplyManagedBlock(lf, {"x.com"});
    CHECK(out.find('\r') == std::string::npos);
    CHECK(RemoveManagedBlock(out) == lf);

    const std::string noNl = "127.0.0.1 localhost";           // no trailing newline
    const std::string out2 = ApplyManagedBlock(noNl, {"x.com"});
    // No EOL anywhere to copy, so the Windows default (CRLF) is used; block starts on its own line.
    CHECK(out2.rfind("127.0.0.1 localhost\r\n", 0) == 0);
    CHECK(RemoveManagedBlock(out2) == noNl + "\r\n");

    const std::string lfNoFinal = "a\n127.0.0.1 localhost";    // LF file, last line unterminated: keep LF
    const std::string out3 = ApplyManagedBlock(lfNoFinal, {"x.com"});
    CHECK(out3.find('\r') == std::string::npos && out3.rfind("a\n127.0.0.1 localhost\n", 0) == 0);

    const std::string fromEmpty = ApplyManagedBlock("", {"x.com"});
    CHECK(Contains(fromEmpty, "0.0.0.0 x.com") && RemoveManagedBlock(fromEmpty).empty());
}

TEST(Hosts_user_lines_after_block_survive_removal) {
    std::string h = ApplyManagedBlock(kOrig, {"reddit.com"});
    h += "10.0.0.5 devbox\r\n";                                // user edits after we blocked
    const std::string r = RemoveManagedBlock(h);
    CHECK(r == kOrig + "10.0.0.5 devbox\r\n");
}

TEST(Hosts_orphaned_begin_does_not_eat_user_content) {
    // END marker lost (user deleted it); user line follows our entries.
    std::string h = kOrig +
        "# >>> Displon BEGIN - managed block, do not edit >>>\r\n"
        "0.0.0.0 reddit.com\r\n"
        ":: reddit.com\r\n"
        "10.0.0.5 devbox\r\n"
        "# my note\r\n";
    CHECK(RemoveManagedBlock(h) == kOrig + "10.0.0.5 devbox\r\n# my note\r\n");
    // BEGIN at EOF with nothing after it
    CHECK(RemoveManagedBlock(kOrig + "# >>> Displon BEGIN - managed block, do not edit >>>\r\n") == kOrig);
}

TEST(Hosts_multiple_blocks_all_removed) {
    const std::string one = ApplyManagedBlock(kOrig, {"a.com"});
    const std::string two = one + one.substr(kOrig.size());    // block appended twice by accident
    CHECK(RemoveManagedBlock(two) == kOrig);
}

TEST(Hosts_intact_detects_tampering) {
    const std::vector<std::string> d{"reddit.com"};
    const std::string h = ApplyManagedBlock(kOrig, d);
    CHECK(IsManagedBlockIntact(h, d));
    CHECK(!IsManagedBlockIntact(kOrig, d));                                   // removed
    std::string edited = h;
    edited.replace(edited.find("0.0.0.0 reddit.com"), 7, "1.2.3.4");          // entry redirected elsewhere
    CHECK(!IsManagedBlockIntact(edited, d));
    CHECK(IsManagedBlockIntact(kOrig, {}));
    CHECK(!IsManagedBlockIntact(h, {}));                                      // block present but none expected
}

// ---------- HostsManager ----------
TEST(HostsManager_block_backs_up_original_once_and_flushes_dns) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.BlockDomains({L"reddit.com", L"https://www.youtube.com/watch"}) == HostsManager::Result::Ok);
    CHECK(Contains(f.content, "0.0.0.0 reddit.com") && Contains(f.content, "0.0.0.0 youtube.com"));
    CHECK(f.hasBackup && f.backup == kOrig);
    CHECK(f.flushes == 1);
    CHECK(m.IsIntact());

    CHECK(m.BlockDomains({L"x.com"}) == HostsManager::Result::Ok);            // re-block: backup must stay ORIGINAL
    CHECK(f.backup == kOrig && f.backupCalls == 1);
}

TEST(HostsManager_restore_removes_only_our_block) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.BlockDomains({L"reddit.com"}) == HostsManager::Result::Ok);
    f.content += "10.0.0.5 devbox\r\n";
    CHECK(m.RestoreHosts() == HostsManager::Result::Ok);
    CHECK(f.content == kOrig + "10.0.0.5 devbox\r\n");
    CHECK(m.AppliedDomains().empty());
    CHECK(f.flushes == 2);
}

TEST(HostsManager_no_write_when_nothing_changes) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.RestoreHosts() == HostsManager::Result::Ok);                      // nothing to remove
    CHECK(f.content == kOrig && f.flushes == 0 && !f.hasBackup);
    CHECK(m.BlockDomains({L"a.com"}) == HostsManager::Result::Ok);
    const int fl = f.flushes;
    CHECK(m.BlockDomains({L"a.com"}) == HostsManager::Result::Ok);            // identical: no rewrite, no flush
    CHECK(f.flushes == fl);
}

TEST(HostsManager_sync_uses_only_website_rules) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    const std::vector<BlockRule> rules{MakeRule(RuleType::Website, L"reddit.com"),
                                       MakeRule(RuleType::ProcessExe, L"game.exe"),
                                       MakeRule(RuleType::WindowTitle, L"YouTube"),
                                       MakeRule(RuleType::Website, L"news.example.com")};
    CHECK(m.Sync(rules) == HostsManager::Result::Ok);
    CHECK(Contains(f.content, "0.0.0.0 reddit.com") && Contains(f.content, "0.0.0.0 news.example.com"));
    CHECK(!Contains(f.content, "game.exe") && !Contains(f.content, "YouTube"));
    CHECK(m.Sync({}) == HostsManager::Result::Ok);                            // session ended
    CHECK(f.content == kOrig);
}

TEST(HostsManager_invalid_entries_are_reported_not_written) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.BlockDomains({L"ok.com", L"bad host", L"evil.com\r\n127.0.0.1 bank.com"}) == HostsManager::Result::Ok);
    CHECK(m.LastRejected().size() == 2);
    CHECK(!Contains(f.content, "bank.com") && !Contains(f.content, "bad host"));
    CHECK(Contains(f.content, "0.0.0.0 ok.com"));
}

TEST(HostsManager_all_invalid_means_no_block) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.BlockDomains({L"localhost", L"10.0.0.1"}) == HostsManager::Result::Ok);
    CHECK(f.content == kOrig && m.LastRejected().size() == 2);
}

TEST(HostsManager_repair_restores_a_tampered_block) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    CHECK(m.BlockDomains({L"reddit.com"}) == HostsManager::Result::Ok);
    CHECK(m.Repair() == HostsManager::Result::Ok && f.flushes == 1);          // intact: nothing done
    f.content = kOrig;                                                        // user deletes our block
    CHECK(!m.IsIntact());
    CHECK(m.Repair() == HostsManager::Result::Ok);
    CHECK(m.IsIntact() && Contains(f.content, "0.0.0.0 reddit.com"));
    CHECK(f.backup == kOrig);

    HostsManager idle(f);                                                     // never applied anything: nothing to repair
    CHECK(idle.Repair() == HostsManager::Result::Ok);
}

TEST(HostsManager_reports_io_failures_and_fails_closed) {
    FakeHosts f; f.content = kOrig;
    HostsManager m(f);
    f.failRead = true;
    CHECK(m.BlockDomains({L"a.com"}) == HostsManager::Result::ReadFailed);
    f.failRead = false; f.failWrite = true;
    CHECK(m.BlockDomains({L"a.com"}) == HostsManager::Result::WriteFailed);
    CHECK(m.AppliedDomains().empty());                                        // never claims a block it did not write
    f.failWrite = false; f.dropWrites = true;                                 // e.g. AV/ACL silently ignored the write
    CHECK(m.BlockDomains({L"a.com"}) == HostsManager::Result::VerifyFailed);
    CHECK(m.AppliedDomains().empty());
}
