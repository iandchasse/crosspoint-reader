#pragma once

#include <functional>

#include "activities/Activity.h"

/**
 * Activity for testing KOReader credentials, or — in sign-up mode — creating a
 * new account on the sync server with the entered username/password.
 * Connects to WiFi, then authenticates or registers.
 */
class KOReaderAuthActivity final : public Activity {
 public:
  enum class Mode { AUTHENTICATE, SIGN_UP };

  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::AUTHENTICATE)
      : Activity("KOReaderAuth", renderer, mappedInput), mode(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  Mode mode = Mode::AUTHENTICATE;
  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;
  // Wi-Fi was switched off at the result (PowerConfig::radioOffWhenIdle);
  // onExit() still restarts to clear the heap the radio left fragmented.
  bool radioReleased = false;

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
