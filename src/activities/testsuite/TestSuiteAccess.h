#pragma once

// The HAL classes that declare `friend struct TestSuiteAccess` (under
// CROSSPOINT_TEST_SUITE only) let the tests reach the SDK object behind them
// without new public API in the reader.
#if CROSSPOINT_TEST_SUITE
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>

struct TestSuiteAccess {
  static EInkDisplay& eink(HalDisplay& hal) { return hal.einkDisplay; }
  static FrontlightManager& frontlight(HalFrontlight& hal) { return hal.manager; }
  static InputManager& input(HalGPIO& hal) { return hal.inputMgr; }
  // The RTC driver HalClock began at boot (its address, raw time and set()).
  static Rtc& rtc(HalClock& hal) { return hal._sdkRtc; }
  // After a test sets the RTC, HalClock's next read goes to the chip instead of
  // its 10 s cache.
  static void dropClockCache(HalClock& hal) { hal._lastPollMs = 0; }
  // HalStorage's mutex, for the SD manager's bring-up record (P3), which no
  // HalStorage method wraps. Recursive.
  static SemaphoreHandle_t storageMutex() { return HalStorage::getInstance().storageMutex; }
};
#endif
