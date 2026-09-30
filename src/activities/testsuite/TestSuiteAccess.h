#pragma once

// The HAL classes that declare `friend struct TestSuiteAccess` (under
// CROSSPOINT_TEST_SUITE only) let the tests reach the SDK object behind them
// without new public API in the reader.
#if CROSSPOINT_TEST_SUITE
#include <HalDisplay.h>

struct TestSuiteAccess {
  static EInkDisplay& eink(HalDisplay& hal) { return hal.einkDisplay; }
};
#endif
