// Windows backend for ITimeZone: the machine's current time zone (incl. DST rules).
#pragma once
#include "engine/Schedule.h"

class Win32TimeZone final : public ITimeZone {
public:
    LocalTime ToLocal(TimePoint t) const override;
    TimePoint FromLocal(const LocalTime& lt) const override;
};
