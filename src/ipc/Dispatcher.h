// Maps validated IPC messages onto EngineHost (portable).
#pragma once
#include "engine/EngineHost.h"
#include "ipc/Protocol.h"

namespace ipc {

class Dispatcher {
public:
    explicit Dispatcher(EngineHost& host) : host_(host) {}

    Reply Handle(const Message& msg);

private:
    SessionConfig BuildConfig(const Message& msg) const;
    Reply StatusReply() const;

    EngineHost& host_;
};

}  // namespace ipc
