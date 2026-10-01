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

  // The switch-on load probe (FrontlightConfig::loadProbeMs); these are constant
  // false without FREEINK_CAP_FRONTLIGHT_PROBE. A switch-on that finds no light
  // leaves it off, as setOn(false) would, and switching it on again is the retry.
  //
  // No light answered the last switch-on. Kept across deep sleep, so a wake does
  // not restore the light into nothing; a cold boot clears it.
  bool noLightFound() const { return manager.load() == freeink::frontlight::Load::Absent; }
  // A single-colour light: this string tripped the probe while the other one lit.
  // Warmth then stays on the other string until a cold boot (a no-light result
  // keeps the mark); the reader also saves its warmth on that string.
  bool stringMissing(const bool warm) const { return manager.stringMissing(warm); }
  // True once after a switch-on or colour change the user asked for found no
  // light, so the UI can say so.
  bool takeNoLightNotice() {
#if FREEINK_CAP_FRONTLIGHT_PROBE
    const bool notice = noLightNotice;
    noLightNotice = false;
    return notice;
#else
    return false;
#endif
  }
  // True once after begin() found no light: Restore Light on Wake, or the
  // replay of a lit light after a silent restart. The boost has just run
  // unloaded, so the UI gives the same message and connect advice.
  bool takeRestoreNoLightNotice() {
#if FREEINK_CAP_FRONTLIGHT_PROBE
    const bool notice = restoreNoLightNotice;
    restoreNoLightNotice = false;
    return notice;
#else
    return false;
#endif
  }

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
#if FREEINK_CAP_FRONTLIGHT_PROBE
  bool noLightNotice = false;
  bool restoreNoLightNotice = false;
  // After a call that may have switched the light on: a probe that found no light
  // leaves it off here too, and raises the notice (the restore one from begin()).
  void settleSwitchOn(bool userAsked);
#endif

  static HalFrontlight instance;
};

#define Frontlight HalFrontlight::getInstance()
