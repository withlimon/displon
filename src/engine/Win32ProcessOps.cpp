#include "engine/Win32ProcessOps.h"

#include <windows.h>
#include <tlhelp32.h>

#include <map>
#include <unordered_set>

#include "common/UniqueHandle.h"

namespace {

std::uint64_t ToU64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring QueryImagePath(HANDLE process) {
    std::wstring buf(512, L'\0');
    for (int i = 0; i < 7; ++i) {                       // grow up to 32K chars
        DWORD len = static_cast<DWORD>(buf.size());
        if (QueryFullProcessImageNameW(process, 0, buf.data(), &len)) {
            buf.resize(len);
            return buf;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
        buf.resize(buf.size() * 2);
    }
    return {};
}

}  // namespace

std::vector<ProcessInfo> Win32ProcessOps::Enumerate() {
    std::vector<ProcessInfo> out;
    UniqueKernelHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap) return out;

    std::unordered_set<std::uint32_t> seen;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    for (BOOL ok = Process32FirstW(snap.Get(), &e); ok; ok = Process32NextW(snap.Get(), &e)) {
        if (e.th32ProcessID == 0) continue;
        ProcessInfo p;
        p.pid = e.th32ProcessID;
        p.name = e.szExeFile;
        seen.insert(p.pid);

        UniqueKernelHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid));
        if (h) {
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(h.Get(), &created, &exited, &kernel, &user)) p.startTime = ToU64(created);
            auto it = cache_.find(p.pid);
            if (it != cache_.end() && it->second.start == p.startTime) {
                p.imagePath = it->second.path;
            } else {
                p.imagePath = QueryImagePath(h.Get());
                cache_[p.pid] = {p.startTime, p.imagePath};
            }
        }
        out.push_back(std::move(p));
    }
    for (auto it = cache_.begin(); it != cache_.end();)
        it = seen.count(it->first) ? std::next(it) : cache_.erase(it);
    return out;
}

bool Win32ProcessOps::Terminate(std::uint32_t pid) {
    UniqueKernelHandle h(OpenProcess(PROCESS_TERMINATE, FALSE, pid));
    if (!h) return false;
    return TerminateProcess(h.Get(), 1) != FALSE;
}

bool Win32ProcessOps::MinimizeWindows(std::uint32_t pid) {
    struct Ctx { DWORD pid; bool any; } ctx{pid, false};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD owner = 0;
            GetWindowThreadProcessId(h, &owner);
            if (owner != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
            c->any = true;
            if (!IsIconic(h)) ShowWindowAsync(h, SW_FORCEMINIMIZE);   // async: a hung app cannot stall the watcher
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.any;
}

std::wstring QueryImagePathByPid(std::uint32_t pid) {
    UniqueKernelHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    return h ? QueryImagePath(h.Get()) : std::wstring();
}

std::vector<std::uint32_t> SelfAndAncestorPids() {
    std::map<std::uint32_t, std::uint32_t> parentOf;
    UniqueKernelHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap) {
        PROCESSENTRY32W e{};
        e.dwSize = sizeof(e);
        for (BOOL ok = Process32FirstW(snap.Get(), &e); ok; ok = Process32NextW(snap.Get(), &e))
            parentOf[e.th32ProcessID] = e.th32ParentProcessID;
    }
    std::vector<std::uint32_t> chain;
    std::uint32_t pid = GetCurrentProcessId();
    while (pid != 0 && chain.size() < 64) {              // bounded: parent links can be stale/cyclic
        chain.push_back(pid);
        auto it = parentOf.find(pid);
        if (it == parentOf.end() || it->second == pid) break;
        pid = it->second;
    }
    return chain;
}
