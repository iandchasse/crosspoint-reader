#pragma once

#include <BoardConfig.h>

#if FREEINK_CAP_POWER_POLICY
#include "RenderWatchdog.h"
#endif

// Long renders report progress; builds without the watchdog keep no hook.
inline void watchdogProgress() {
#if FREEINK_CAP_POWER_POLICY
  renderwatchdog::progress();
#endif
}
