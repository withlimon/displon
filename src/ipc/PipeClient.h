#pragma once
#include <optional>
#include <string>

#include "ipc/Protocol.h"

namespace ipc {

class PipeClient {
public:
    explicit PipeClient(std::wstring pipeName = L"\\\\.\\pipe\\Displon");
    ~PipeClient();

    bool Connect(int timeoutMs = 2000);
    void Close();
    std::optional<Reply> Call(const Message& msg);

private:
    std::wstring pipeName_;
#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE
#endif
};

}  // namespace ipc
