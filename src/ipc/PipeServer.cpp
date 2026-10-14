#include "ipc/PipeServer.h"

#include "ipc/Protocol.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ipc {

PipeServer::PipeServer(Handler handler, std::wstring pipeName)
    : handler_(std::move(handler)), pipeName_(std::move(pipeName)) {}

PipeServer::~PipeServer() { Stop(); }

bool PipeServer::Start() {
#ifdef _WIN32
    if (thread_.joinable()) return true;
    stop_ = false;
    thread_ = std::thread([this] { AcceptLoop(); });
    return true;
#else
    return false;
#endif
}

void PipeServer::Stop() {
    stop_ = true;
#ifdef _WIN32
    // Unblock ConnectNamedPipe by opening a dummy client.
    HANDLE h = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
#endif
    if (thread_.joinable()) thread_.join();
}

void PipeServer::AcceptLoop() {
#ifdef _WIN32
    while (!stop_) {
        HANDLE pipe = CreateNamedPipeW(
            pipeName_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }
        const BOOL ok = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (stop_) {
            CloseHandle(pipe);
            break;
        }
        if (ok) {
            ServeClient(pipe);
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
#endif
}

void PipeServer::ServeClient(void* pipeHandle) {
#ifdef _WIN32
    HANDLE pipe = static_cast<HANDLE>(pipeHandle);
    std::string buffer;
    char chunk[4096];
    while (!stop_) {
        DWORD read = 0;
        if (!ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) || read == 0) break;
        buffer.append(chunk, chunk + read);
        while (auto body = TryDeframe(buffer)) {
            auto msg = DecodeMessage(*body);
            Reply reply;
            if (!msg) {
                reply.code = ReplyCode::Invalid;
                reply.detail = "bad message";
            } else {
                reply = handler_(*msg);
            }
            const std::string framed = Frame(EncodeReply(reply));
            DWORD written = 0;
            if (!WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr))
                return;
        }
    }
#else
    (void)pipeHandle;
#endif
}

}  // namespace ipc
