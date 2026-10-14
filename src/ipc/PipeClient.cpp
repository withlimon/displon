#include "ipc/PipeClient.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ipc {

PipeClient::PipeClient(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}
PipeClient::~PipeClient() { Close(); }

bool PipeClient::Connect(int timeoutMs) {
#ifdef _WIN32
    Close();
    const DWORD start = GetTickCount();
    for (;;) {
        HANDLE h = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            handle_ = h;
            return true;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) return false;
        if (static_cast<int>(GetTickCount() - start) >= timeoutMs) return false;
        WaitNamedPipeW(pipeName_.c_str(), 200);
    }
#else
    (void)timeoutMs;
    return false;
#endif
}

void PipeClient::Close() {
#ifdef _WIN32
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#endif
}

std::optional<Reply> PipeClient::Call(const Message& msg) {
#ifdef _WIN32
    if (!handle_) return std::nullopt;
    const std::string framed = Frame(EncodeMessage(msg));
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), framed.data(),
                   static_cast<DWORD>(framed.size()), &written, nullptr))
        return std::nullopt;

    std::string buffer;
    char chunk[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(static_cast<HANDLE>(handle_), chunk, sizeof(chunk), &read, nullptr) || read == 0)
            return std::nullopt;
        buffer.append(chunk, chunk + read);
        if (auto body = TryDeframe(buffer)) {
            return DecodeReply(*body);
        }
    }
#else
    (void)msg;
    return std::nullopt;
#endif
}

}  // namespace ipc
