#pragma once

#include "activities/Activity.h"

// Manual NTP resync action. Runs a forced sync (bypassing the once-per-device debounce),
// reports success/failure, then waits for Back. If WiFi is not connected yet, it reuses the
// normal WiFi selection flow first.
class ClockSyncActivity final : public Activity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // Spin only while the sync runs (PowerConfig::radioOffWhenIdle; always on
  // other boards).
  bool skipLoopDelay() override;
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  bool shouldTearDownWifiOnExit = false;
  // Wi-Fi was switched off at the result (PowerConfig::radioOffWhenIdle);
  // onExit() still restarts to clear the heap the radio left fragmented.
  bool radioReleased = false;

  void runSync();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
};
