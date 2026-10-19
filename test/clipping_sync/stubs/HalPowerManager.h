#pragma once
// Stand-in for lib/hal/HalPowerManager.h: ClippingSync's request() polls the low-battery cutoff (Silkscreen, BAT-01).
#include <climits>

#include "SecureHttpClient.h"

namespace fake {
inline int cutoffAfterRequests = INT_MAX;  // the cutoff is tripped once this many requests have been sent
}  // namespace fake
class HalPowerManager {
 public:
  bool lowBatteryCutoffTripped() const { return fake::requests >= fake::cutoffAfterRequests; }
};
inline HalPowerManager powerManager;
