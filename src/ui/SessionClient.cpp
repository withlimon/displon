#include "ui/SessionClient.h"
#include "ipc/PipeClient.h"
#include "ipc/Protocol.h"

SessionClientResult SessionClient::StartRemote(const SessionConfig& cfg, const std::wstring& password) {
    SessionClientResult out;
#ifdef _WIN32
    ipc::PipeClient client;
    if (!client.Connect(1500)) {
        out.detail = "service not reachable (is DisplonService running?)";
        return out;
    }
    ipc::Message m;
    m.cmd = ipc::Command::StartSession;
    m.duration = cfg.duration;
    m.blockTaskManager = cfg.blockTaskManager;
    m.blockWindowsSettings = cfg.blockWindowsSettings;
    m.untilRestart = (cfg.durationMode == SessionDurationMode::UntilRestart);
    m.breakMode = static_cast<int>(cfg.breakMode);
    m.pomodoroWork = cfg.pomodoroWorkMin;
    m.pomodoroBreak = cfg.pomodoroBreakMin;
    m.allowance = cfg.allowanceMin;
    if (!password.empty()) m.password = password;
    m.allowExes = cfg.allowListExePaths;
    m.allowFolders = cfg.allowListFolders;
    m.websites = cfg.websiteBlockRules;
    for (const auto& r : cfg.processBlockRules) m.processBlocks.push_back(r.target);
    for (const auto& r : cfg.windowTitleRules) m.windowTitles.push_back(r.target);

    auto reply = client.Call(m);
    if (!reply) {
        out.detail = "no reply from service";
        return out;
    }
    out.ok = (reply->code == ipc::ReplyCode::Ok);
    out.detail = reply->detail;
    out.unlockEpochSecs = reply->unlockEpochSecs;
#else
    (void)cfg; (void)password;
    out.detail = "not Windows";
#endif
    return out;
}

SessionClientResult SessionClient::StatusRemote() {
    SessionClientResult out;
#ifdef _WIN32
    ipc::PipeClient client;
    if (!client.Connect(500)) {
        out.detail = "offline";
        return out;
    }
    ipc::Message m;
    m.cmd = ipc::Command::GetStatus;
    auto reply = client.Call(m);
    if (!reply) return out;
    out.ok = (reply->code == ipc::ReplyCode::Ok);
    out.detail = reply->state;
    out.unlockEpochSecs = reply->unlockEpochSecs;
#endif
    return out;
}
