#include "engine/WindowTitleWatcher.h"

WindowTitleWatcher::WindowTitleWatcher(IWindowOps& ops, ExemptionPolicy exempt,
                                       std::chrono::milliseconds pollInterval)
    : RuleWatcher(pollInterval), enforcer_(ops, std::move(exempt)) {}

void WindowTitleWatcher::SetEventCallback(std::function<void(const WindowEvent&)> cb) {
    enforcer_.SetEventCallback(std::move(cb));
}
