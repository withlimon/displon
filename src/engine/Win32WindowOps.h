// Windows backend for IWindowOps (EnumWindows / WM_CLOSE / minimize). Windows-only.
#pragma once
#include "engine/WindowEnforcer.h"

class Win32WindowOps final : public IWindowOps {
public:
    std::vector<WindowInfo> Enumerate() override;
    bool Close(std::uint64_t id) override;
    bool Minimize(std::uint64_t id) override;
};
