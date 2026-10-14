#include "tests/TestHarness.h"
#include "ipc/Protocol.h"
#include "engine/BlockRule.h"

TEST(EnforcementPaused_Allowance) {
    SessionConfig c;
    c.breakMode = SessionBreakMode::Allowance;
    c.allowanceMin = 5;
    const TimePoint start = std::chrono::system_clock::time_point(std::chrono::seconds(1000));
    CHECK(IsEnforcementPaused(c, start, start + std::chrono::minutes(2)));
    CHECK(!IsEnforcementPaused(c, start, start + std::chrono::minutes(6)));
}

TEST(EnforcementPaused_Pomodoro) {
    SessionConfig c;
    c.breakMode = SessionBreakMode::Pomodoro;
    c.pomodoroWorkMin = 25;
    c.pomodoroBreakMin = 5;
    const TimePoint start = std::chrono::system_clock::time_point(std::chrono::seconds(0));
    CHECK(!IsEnforcementPaused(c, start, start + std::chrono::minutes(10)));
    CHECK(IsEnforcementPaused(c, start, start + std::chrono::minutes(27)));
    CHECK(!IsEnforcementPaused(c, start, start + std::chrono::minutes(31)));
}

TEST(EnforcementPaused_None) {
    SessionConfig c;
    c.breakMode = SessionBreakMode::None;
    const TimePoint start = std::chrono::system_clock::now();
    CHECK(!IsEnforcementPaused(c, start, start + std::chrono::minutes(10)));
}

TEST(Protocol_Frame_round_trip) {
    const std::string body = "cmd=Ping\n\n";
    auto framed = ipc::Frame(body);
    CHECK(framed.size() == 4 + body.size());
    std::string buf = framed;
    auto def = ipc::TryDeframe(buf);
    CHECK(def.has_value());
    CHECK(*def == body);
    CHECK(buf.empty());
}

TEST(Protocol_UntilRestart_and_break_fields) {
    ipc::Message m;
    m.cmd = ipc::Command::StartSession;
    m.duration = std::chrono::minutes(30);
    m.untilRestart = true;
    m.breakMode = 1;
    m.pomodoroWork = 25;
    m.pomodoroBreak = 5;
    auto body = ipc::EncodeMessage(m);
    auto d = ipc::DecodeMessage(body);
    CHECK(d.has_value());
    CHECK(d->untilRestart && *d->untilRestart);
    CHECK(d->breakMode && *d->breakMode == 1);
    CHECK(d->pomodoroWork && *d->pomodoroWork == 25);
}
