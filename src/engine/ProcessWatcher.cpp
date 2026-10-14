#include "engine/ProcessWatcher.h"

ProcessWatcher::ProcessWatcher(IProcessOps& ops, ExemptionPolicy exempt, std::chrono::milliseconds pollInterval)
    : RuleWatcher(pollInterval), enforcer_(ops, std::move(exempt)) {}

void ProcessWatcher::SetEventCallback(std::function<void(const ProcessEvent&)> cb) {
    enforcer_.SetEventCallback(std::move(cb));
}

void ProcessWatcher::ApplyRules(const std::vector<BlockRule>& rules) {
    enforcer_.SetPolicy(BuildProcessPolicy(rules));
}
