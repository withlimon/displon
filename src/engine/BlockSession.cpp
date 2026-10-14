#include "engine/BlockSession.h"

bool BlockSession::TryUnlock(std::wstring_view password) {
    if (VerifyPassword(state_.password, password)) {
        released_ = true;
        return true;
    }
    ++failedAttempts_;
    return false;
}
