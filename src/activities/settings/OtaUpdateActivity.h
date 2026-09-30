#pragma once

#include <HalPowerManager.h>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  // Cancel/Update confirmation over the version info (replaces the old
  // hand-rolled bottom tap rects).
  OptionPopup confirmPopup;
  // Wi-Fi was switched off at a terminal state (PowerConfig::radioOffWhenIdle);
  // onExit() still restarts to clear the heap the radio left fragmented.
  bool radioReleased = false;

  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();
  // Enter NO_UPDATE or FAILED: nothing left to do but leave the screen.
  void finishCheck(State terminalState, const char* detail = nullptr);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  // Prevent power-saving mode while the update works. With
  // PowerConfig::radioOffWhenIdle only then: a result screen needs no spinning.
  bool skipLoopDelay() override {
    return !HalPowerManager::radioOffWhenIdle() || state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS;
  }
};
