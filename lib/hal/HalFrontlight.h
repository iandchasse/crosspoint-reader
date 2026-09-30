#pragma once

#include <FrontlightManager.h>

// Thin firmware HAL over the SDK frontlight manager. It is inert on boards
// without a frontlight, so callers do not need board-specific conditionals.
class HalFrontlight {
 public:
  static HalFrontlight& getInstance() { return instance; }

  void begin(uint8_t brightness, uint8_t warmth, bool on);

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }
  // Warmth picks one string (warm at >= 50%) instead of blending two
  // (FrontlightConfig::colorSelectGpio).
  bool warmthIsSelection() const;

  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  // Off after the SDK's LED over-voltage monitor tripped (fault()), even though
  // the user left it on; switching it on again is the retry. setOn(false) always
  // clears the user's "on" and re-arms the monitor, faulted or not.
  bool isOn() const { return lit && !manager.fault(); }
  bool fault() const { return manager.fault(); }

  // Take the light dark and release its pads before deep sleep. begin() also
  // runs it from a shutdown handler before every software restart, so the
  // driver is dark before the reset lets COLOR_SEL fall to its pull-down.
  void prepareForSleep() { manager.prepareForSleep(); }

 private:
#if CROSSPOINT_TEST_SUITE
  friend struct TestSuiteAccess;  // the hardware test firmware (src/activities/testsuite/)
#endif
  HalFrontlight() = default;

  FrontlightManager manager;
  // The SDK represents off as brightness 0. Keep the selected brightness so
  // toggling back on restores it.
  uint8_t lastBrightness = 60;
  bool lit = false;

  static HalFrontlight instance;
};

#define Frontlight HalFrontlight::getInstance()
