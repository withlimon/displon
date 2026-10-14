#pragma once
#include "ui/Theme.h"

// Returns black (dark) or cream (light) theme from OS preference. No user toggle.
carbon::Theme DetectSystemCarbonTheme();
bool SystemPrefersDark();
