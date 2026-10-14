#include "engine/Win32Network.h"

// winsock2 must come before windows.h; fwpmu.h needs both.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fwpmu.h>

#include <cstring>

// mingw-w64's fwpmu.h/fwpmtypes.h (11.0.1) ships the structs and functions but NOT these flag
// macros or the layer/condition GUIDs (found by the first cross-build, see plan.md Section 11).
// Values are from the Windows SDK; each guard keeps this file valid if a newer mingw adds them.
#ifndef FWPM_SESSION_FLAG_DYNAMIC
#define FWPM_SESSION_FLAG_DYNAMIC 0x00000001
#endif
#ifndef FWPM_PROVIDER_FLAG_PERSISTENT
#define FWPM_PROVIDER_FLAG_PERSISTENT 0x00000001
#endif
#ifndef FWPM_SUBLAYER_FLAG_PERSISTENT
#define FWPM_SUBLAYER_FLAG_PERSISTENT 0x00000001
#endif
#ifndef FWPM_FILTER_FLAG_PERSISTENT
#define FWPM_FILTER_FLAG_PERSISTENT 0x00000001
#endif

namespace {

// {B235AE9A-1D64-49B8-A44C-5FF3D9095045}  FWPM_CONDITION_IP_REMOTE_ADDRESS
const GUID kCondIpRemoteAddress = {0xb235ae9a, 0x1d64, 0x49b8, {0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45}};
// {C38D57D1-05A7-4C33-904F-7FBCEEE60E82}  FWPM_LAYER_ALE_AUTH_CONNECT_V4
const GUID kLayerConnectV4 = {0xc38d57d1, 0x05a7, 0x4c33, {0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82}};
// {4A72393B-319F-44BC-84C3-BA54DCB3B6B4}  FWPM_LAYER_ALE_AUTH_CONNECT_V6
const GUID kLayerConnectV6 = {0x4a72393b, 0x319f, 0x44bc, {0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4}};

// Fixed identities so DeleteAllOurs() can find our objects again after a crash/restart.
// {6B1F3C52-7A40-4E8D-9C21-5D0A8E3F4B17}
const GUID kProviderKey = {0x6b1f3c52, 0x7a40, 0x4e8d, {0x9c, 0x21, 0x5d, 0x0a, 0x8e, 0x3f, 0x4b, 0x17}};
// {C94E2A07-1D58-4B6F-8A93-2E7F60B1D4C8}
const GUID kSubLayerKey = {0xc94e2a07, 0x1d58, 0x4b6f, {0x8a, 0x93, 0x2e, 0x7f, 0x60, 0xb1, 0xd4, 0xc8}};

bool Ok(DWORD rc) { return rc == ERROR_SUCCESS; }

}  // namespace

Win32NetworkBackend::Win32NetworkBackend(bool dynamic) {
    FWPM_SESSION0 session = {};
    session.displayData.name = const_cast<wchar_t*>(L"Displon");
    if (dynamic) session.flags = FWPM_SESSION_FLAG_DYNAMIC;

    HANDLE h = nullptr;
    DWORD rc = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &h);
    if (!Ok(rc)) { lastError_ = rc; return; }

    FWPM_PROVIDER0 provider = {};
    provider.providerKey = kProviderKey;
    provider.displayData.name = const_cast<wchar_t*>(L"Displon");
    if (!dynamic) provider.flags = FWPM_PROVIDER_FLAG_PERSISTENT;
    rc = FwpmProviderAdd0(h, &provider, nullptr);
    if (!Ok(rc) && rc != static_cast<DWORD>(FWP_E_ALREADY_EXISTS)) { lastError_ = rc; FwpmEngineClose0(h); return; }

    FWPM_SUBLAYER0 sub = {};
    sub.subLayerKey = kSubLayerKey;
    sub.displayData.name = const_cast<wchar_t*>(L"Displon blocks");
    sub.providerKey = const_cast<GUID*>(&kProviderKey);
    sub.weight = 0x100;
    if (!dynamic) sub.flags = FWPM_SUBLAYER_FLAG_PERSISTENT;
    rc = FwpmSubLayerAdd0(h, &sub, nullptr);
    if (!Ok(rc) && rc != static_cast<DWORD>(FWP_E_ALREADY_EXISTS)) { lastError_ = rc; FwpmEngineClose0(h); return; }

    persistent_ = !dynamic;
    engine_ = h;
}

Win32NetworkBackend::~Win32NetworkBackend() {
    if (engine_) FwpmEngineClose0(static_cast<HANDLE>(engine_));
}

bool Win32NetworkBackend::AddBlock(const IpAddress& ip, std::uint64_t& filterId) {
    if (!engine_) return false;

    FWP_BYTE_ARRAY16 v6addr = {};
    FWPM_FILTER_CONDITION0 cond = {};
    cond.fieldKey = kCondIpRemoteAddress;
    cond.matchType = FWP_MATCH_EQUAL;
    if (ip.v6) {
        std::memcpy(v6addr.byteArray16, ip.b.data(), 16);
        cond.conditionValue.type = FWP_BYTE_ARRAY16_TYPE;
        cond.conditionValue.byteArray16 = &v6addr;
    } else {
        cond.conditionValue.type = FWP_UINT32;   // IPv4 is given in HOST byte order
        cond.conditionValue.uint32 = (static_cast<UINT32>(ip.b[0]) << 24) | (static_cast<UINT32>(ip.b[1]) << 16) |
                                     (static_cast<UINT32>(ip.b[2]) << 8) | static_cast<UINT32>(ip.b[3]);
    }

    FWPM_FILTER0 f = {};
    f.displayData.name = const_cast<wchar_t*>(L"Displon website block");
    f.providerKey = const_cast<GUID*>(&kProviderKey);
    f.layerKey = ip.v6 ? kLayerConnectV6 : kLayerConnectV4;
    f.subLayerKey = kSubLayerKey;
    f.weight.type = FWP_UINT8;
    f.weight.uint8 = 15;
    f.action.type = FWP_ACTION_BLOCK;
    f.numFilterConditions = 1;
    f.filterCondition = &cond;
    if (persistent_) f.flags |= FWPM_FILTER_FLAG_PERSISTENT;

    UINT64 id = 0;
    const DWORD rc = FwpmFilterAdd0(static_cast<HANDLE>(engine_), &f, nullptr, &id);
    if (!Ok(rc)) { lastError_ = rc; return false; }
    filterId = id;
    return true;
}

bool Win32NetworkBackend::Delete(std::uint64_t filterId) {
    if (!engine_) return false;
    const DWORD rc = FwpmFilterDeleteById0(static_cast<HANDLE>(engine_), filterId);
    if (!Ok(rc)) lastError_ = rc;
    return Ok(rc);
}

bool Win32NetworkBackend::DeleteAllOurs() {
    if (!engine_) return false;
    HANDLE h = static_cast<HANDLE>(engine_);

    FWPM_FILTER_ENUM_TEMPLATE0 tmpl = {};
    tmpl.providerKey = const_cast<GUID*>(&kProviderKey);
    tmpl.enumType = FWP_FILTER_ENUM_FULLY_CONTAINED;
    tmpl.actionMask = 0xFFFFFFFF;

    HANDLE en = nullptr;
    DWORD rc = FwpmFilterCreateEnumHandle0(h, &tmpl, &en);
    if (!Ok(rc)) { lastError_ = rc; return false; }

    bool allOk = true;
    for (;;) {
        FWPM_FILTER0** entries = nullptr;
        UINT32 got = 0;
        rc = FwpmFilterEnum0(h, en, 100, &entries, &got);
        if (!Ok(rc)) { lastError_ = rc; allOk = false; break; }
        if (got == 0) { FwpmFreeMemory0(reinterpret_cast<void**>(&entries)); break; }
        for (UINT32 i = 0; i < got; ++i) {
            const DWORD d = FwpmFilterDeleteById0(h, entries[i]->filterId);
            if (!Ok(d)) { lastError_ = d; allOk = false; }
        }
        FwpmFreeMemory0(reinterpret_cast<void**>(&entries));
        if (!allOk) break;   // do not spin on a filter we cannot delete
    }
    FwpmFilterDestroyEnumHandle0(h, en);
    return allOk;
}

Win32Resolver::Win32Resolver() {
    WSADATA wsa;
    started_ = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}
Win32Resolver::~Win32Resolver() {
    if (started_) WSACleanup();
}

std::vector<IpAddress> Win32Resolver::Resolve(const std::string& host) {
    std::vector<IpAddress> out;
    if (!started_) return out;
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return out;
    for (addrinfo* p = res; p; p = p->ai_next) {
        IpAddress ip;
        if (p->ai_family == AF_INET) {
            const auto* a = reinterpret_cast<const sockaddr_in*>(p->ai_addr);
            std::memcpy(ip.b.data(), &a->sin_addr, 4);   // already network order = a.b.c.d
        } else if (p->ai_family == AF_INET6) {
            const auto* a = reinterpret_cast<const sockaddr_in6*>(p->ai_addr);
            ip.v6 = true;
            std::memcpy(ip.b.data(), &a->sin6_addr, 16);
        } else {
            continue;
        }
        out.push_back(ip);
    }
    freeaddrinfo(res);
    return out;
}
