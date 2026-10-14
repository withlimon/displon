#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "ipc/Dispatcher.h"

namespace ipc {

// Named-pipe server (Windows). On non-Windows this is a no-op stub.
class PipeServer {
public:
    using Handler = std::function<Reply(const Message&)>;

    explicit PipeServer(Handler handler,
                        std::wstring pipeName = L"\\\\.\\pipe\\Displon");
    ~PipeServer();

    bool Start();
    void Stop();

private:
    void AcceptLoop();
    void ServeClient(void* pipeHandle);

    Handler handler_;
    std::wstring pipeName_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace ipc
