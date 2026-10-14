#include "tests/TestHarness.h"
#include "ipc/Protocol.h"
#include "common/Utf.h"

using namespace ipc;

TEST(Ipc_Ping_round_trip) {
    Message m;
    m.cmd = Command::Ping;
    auto body = EncodeMessage(m);
    auto decoded = DecodeMessage(body);
    CHECK(decoded.has_value());
    CHECK(decoded->cmd == Command::Ping);
}

TEST(Ipc_StartSession_fields) {
    Message m;
    m.cmd = Command::StartSession;
    m.duration = std::chrono::minutes(30);
    m.blockTaskManager = true;
    m.password = L"secret";
    m.processBlocks = {L"notepad.exe", L"chrome.exe"};
    m.websites = {L"reddit.com"};
    m.allowFolders = {L"C:\\Work"};

    auto body = EncodeMessage(m);
    auto d = DecodeMessage(body);
    CHECK(d.has_value());
    CHECK(d->cmd == Command::StartSession);
    CHECK(d->duration && d->duration->count() == 30);
    CHECK(d->blockTaskManager && *d->blockTaskManager);
    CHECK(d->password && *d->password == L"secret");
    CHECK(d->processBlocks.size() == 2);
    CHECK(d->processBlocks[0] == L"notepad.exe");
    CHECK(d->websites.size() == 1 && d->websites[0] == L"reddit.com");
    CHECK(d->allowFolders.size() == 1);
}

TEST(Ipc_Reply_round_trip) {
    Reply r;
    r.code = ReplyCode::Ok;
    r.detail = "locked";
    r.state = "Locked";
    r.unlockEpochSecs = 1700000000;
    r.failedAttempts = 2;
    auto body = EncodeReply(r);
    auto d = DecodeReply(body);
    CHECK(d.has_value());
    CHECK(d->code == ReplyCode::Ok);
    CHECK(d->detail == "locked");
    CHECK(d->state == "Locked");
    CHECK(d->unlockEpochSecs && *d->unlockEpochSecs == 1700000000);
    CHECK(d->failedAttempts == 2);
}

TEST(Ipc_Reject_bad_cmd) {
    auto d = DecodeMessage("cmd=NotARealCommand\n\n");
    CHECK(!d.has_value());
}

TEST(Ipc_Password_validation) {
    CHECK(IsValidPassword(L""));
    CHECK(IsValidPassword(L"hello"));
    CHECK(!IsValidPassword(std::wstring(200, L'x')));
    CHECK(!IsValidPassword(L"bad\x01pass"));
}

TEST(Ipc_Duration_validation) {
    // Limits are 1 minute .. 24 hours (kMinSessionDuration / kMaxSessionDuration).
    CHECK(!IsValidDuration(std::chrono::minutes(0)));
    CHECK(IsValidDuration(std::chrono::minutes(1)));
    CHECK(IsValidDuration(std::chrono::minutes(24 * 60)));
    CHECK(!IsValidDuration(std::chrono::minutes(24 * 60 + 1)));
}

TEST(Ipc_Escape_round_trip) {
    Message m;
    m.cmd = Command::StartSession;
    m.password = L"a=b%c\nd";
    auto body = EncodeMessage(m);
    auto d = DecodeMessage(body);
    CHECK(d.has_value());
    CHECK(d->password && *d->password == L"a=b%c\nd");
}
