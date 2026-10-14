// Tiny RAII owner for Win32 handle types (replaces WIL, see plan Section 3.1).
#pragma once
#include <windows.h>
#include <winsvc.h>
#include <utility>

// One template, several real instantiations (HANDLE, HKEY, SC_HANDLE, HMODULE).
template <typename Traits>
class UniqueHandle {
public:
    using Type = typename Traits::Type;

    UniqueHandle() = default;
    explicit UniqueHandle(Type h) noexcept : handle_(h) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& o) noexcept : handle_(o.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) Reset(o.Release());
        return *this;
    }

    Type Get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return Traits::IsValid(handle_); }

    Type Release() noexcept { return std::exchange(handle_, Traits::Null()); }
    void Reset(Type h = Traits::Null()) noexcept {
        if (Traits::IsValid(handle_)) Traits::Close(handle_);
        handle_ = h;
    }
    // For out-parameters such as RegOpenKeyExW(..., key.Put()).
    Type* Put() noexcept { Reset(); return &handle_; }

private:
    Type handle_ = Traits::Null();
};

struct KernelHandleTraits {   // CreateFile/CreateEvent/OpenProcess: null AND INVALID_HANDLE_VALUE are invalid
    using Type = HANDLE;
    static Type Null() noexcept { return nullptr; }
    static bool IsValid(Type h) noexcept { return h && h != INVALID_HANDLE_VALUE; }
    static void Close(Type h) noexcept { ::CloseHandle(h); }
};
struct RegKeyTraits {
    using Type = HKEY;
    static Type Null() noexcept { return nullptr; }
    static bool IsValid(Type h) noexcept { return h != nullptr; }
    static void Close(Type h) noexcept { ::RegCloseKey(h); }
};
struct ScHandleTraits {
    using Type = SC_HANDLE;
    static Type Null() noexcept { return nullptr; }
    static bool IsValid(Type h) noexcept { return h != nullptr; }
    static void Close(Type h) noexcept { ::CloseServiceHandle(h); }
};
struct ModuleTraits {
    using Type = HMODULE;
    static Type Null() noexcept { return nullptr; }
    static bool IsValid(Type h) noexcept { return h != nullptr; }
    static void Close(Type h) noexcept { ::FreeLibrary(h); }
};

using UniqueKernelHandle = UniqueHandle<KernelHandleTraits>;
using UniqueRegKey       = UniqueHandle<RegKeyTraits>;
using UniqueScHandle     = UniqueHandle<ScHandleTraits>;
using UniqueModule       = UniqueHandle<ModuleTraits>;
