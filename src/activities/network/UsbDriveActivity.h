#pragma once

#include <HalStorage.h>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

class UsbDriveActivity final : public Activity, private UiAppHost {
 public:
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbDrive", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Connected || (!startFailed && state == State::IoError); }
  bool requiresExclusiveStorageLoop() const override { return true; }

 private:
  using State = UsbDriveState;

  static constexpr unsigned long HOST_WAIT_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  static constexpr unsigned long START_FAILURE_TIMEOUT_MS = 30UL * 1000UL;
  static constexpr unsigned long FORCED_DISCONNECT_TIMEOUT_MS = 1000UL;
  static constexpr unsigned long HOST_SUSPEND_TIMEOUT_MS = 2000UL;
  // A charge-status ladder that has seen external power this session and then
  // reads "on battery" this long ends the session: the cable was pulled.
  static constexpr unsigned long CABLE_PULLED_TIMEOUT_MS = 3000UL;

  static void driveScreen(UiScreen& screen, void* user);
  void buildDriveScreen(UiScreen& screen) const;
  void restartToHome();
  bool cablePulledOnBattery();

  State state = State::Unsupported;
  bool preparing = true;
  bool startFailed = false;
  bool restartRequested = false;
  bool forcedDisconnectRequested = false;
  bool hostSuspendPending = false;
  unsigned long hostWaitStartedAt = 0;
  unsigned long startFailureStartedAt = 0;
  unsigned long forcedDisconnectRequestedAt = 0;
  unsigned long hostSuspendStartedAt = 0;
  bool sawExternalPower = false;
  unsigned long onBatterySince = 0;
};
