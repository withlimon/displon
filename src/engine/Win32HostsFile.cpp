#include "engine/Win32HostsFile.h"

#include <windows.h>

#include <algorithm>

#include "common/UniqueHandle.h"

namespace {

bool WriteAll(HANDLE h, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 20));
        if (!WriteFile(h, data.data() + done, chunk, &wrote, nullptr) || wrote == 0) return false;
        done += wrote;
    }
    return true;
}

}  // namespace

Win32HostsFile::Win32HostsFile(std::wstring path) : path_(std::move(path)) {
    if (!path_.empty()) return;
    wchar_t sys[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    path_ = (n > 0 && n < MAX_PATH ? std::wstring(sys, n) : std::wstring(L"C:\\Windows\\System32")) +
            L"\\drivers\\etc\\hosts";
}

bool Win32HostsFile::Read(std::string& out) {
    out.clear();
    UniqueKernelHandle h(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return GetLastError() == ERROR_FILE_NOT_FOUND;   // a missing hosts file is just "empty"
    char buf[16384];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h.Get(), buf, sizeof(buf), &got, nullptr)) return false;
        if (got == 0) break;
        out.append(buf, got);
    }
    return true;
}

bool Win32HostsFile::Write(const std::string& content) {
    const std::wstring tmp = path_ + L".displon.tmp";
    {
        UniqueKernelHandle h(CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (h && WriteAll(h.Get(), content) && FlushFileBuffers(h.Get())) {
            h.Reset();
            if (MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        }
    }
    DeleteFileW(tmp.c_str());
    // Fallback (e.g. replace denied): rewrite in place. Not atomic, but better than failing the block.
    UniqueKernelHandle h(CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    return h && WriteAll(h.Get(), content);
}

bool Win32HostsFile::WriteBackupIfAbsent(const std::string& original) {
    UniqueKernelHandle h(CreateFileW((path_ + L".displon.bak").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return GetLastError() == ERROR_FILE_EXISTS;   // already have the first backup: keep it
    return WriteAll(h.Get(), original);
}

void Win32HostsFile::FlushDnsCache() {
    using Fn = BOOL(WINAPI*)();
    if (HMODULE m = LoadLibraryW(L"dnsapi.dll")) {
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "DnsFlushResolverCache")))) fn();
        FreeLibrary(m);
    }
}
