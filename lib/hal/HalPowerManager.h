#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds
  mutable bool _noCellCached = false;            // noChargeableCell()'s last answer
  mutable unsigned long _noCellPolledMs = 0;     // and when it was read, 0 = never

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode
  unsigned long lastClientTrafficMs = 0;  // noteClientTraffic()
  unsigned long lastInputMs = 0;          // noteInput()
  bool cutoffTripped = false;             // lowBatteryCutoffTripped() latch

  [[noreturn]] void sleepImpl(HalGPIO& gpio, bool sleepCommands) const;

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  void startDeepSleep(HalGPIO& gpio) const;
  // Deep sleep without a word to any peripheral (no touch sleep command): for a
  // boot that sleeps before starting them, or that must not rerun code that may
  // have crashed it. Pins and rails are parked exactly as startDeepSleep() does.
  void startDeepSleepMinimal(HalGPIO& gpio) const;

  // A network client talked to the device (web server request or WebSocket
  // frame). The awake ceiling (awakeCeilingMs()) counts idle time from the later
  // of this and the last input.
  void noteClientTraffic() { lastClientTrafficMs = millis(); }
  unsigned long msSinceClientTraffic() const { return millis() - lastClientTrafficMs; }

  // A key, touch or tilt input (loop(), which also starts this clock on its
  // first pass). The awake ceiling, the input cap and the wake and storage-error
  // timeouts count from it.
  void noteInput() { lastInputMs = millis(); }
  unsigned long msSinceInput() const { return millis() - lastInputMs; }

  // The awake ceiling in ms: the longer of PowerConfig::awakeCeilingMin and the
  // user's sleep timeout (sleepTimeoutMs, 0 = Never), so it bounds Never and the
  // screens that hold auto-sleep off without cutting short a timeout the user
  // chose. 0 on boards without a ceiling.
  static unsigned long awakeCeilingMs(unsigned long sleepTimeoutMs);
  // On battery, no input and no client traffic for awakeCeilingMs() (always
  // false on boards without a ceiling).
  bool awakeCeilingPassed(unsigned long sleepTimeoutMs) const;
  // PowerConfig::awakeInputCapMin (always false on boards without one): on
  // battery, no input for the cap, whatever the client traffic.
  bool awakeInputCapPassed() const;

  // PowerConfig::lowBatteryCutoffMv, the awake ceiling and awakeInputCapMin
  // (always false on boards without them): network work nobody is waiting on
  // (the plugin event drains) must stop or not start. Such work blocks loop(),
  // whose cutoff, ceiling and cap checks cannot run meanwhile; loop() sleeps on
  // them once it returns. sleepTimeoutMs as for awakeCeilingMs(). Call from the
  // loop task.
  bool backgroundNetworkMustStop(unsigned long sleepTimeoutMs);

  // PowerConfig::lowBatteryCutoffMv (always false on boards without one): true
  // once BatteryMonitor::isBelowCutoff() fires (with the radio allowance while
  // Wi-Fi is on), and from then on for the session, so a reading that recovers
  // when a transfer's load stops cannot undo it. At most one reading a second.
  // Call from the loop task, including a blocking transfer's callbacks (which
  // abort on it); loop() then sleeps.
  bool lowBatteryCutoffTripped();

  // PowerConfig::radioOffWhenIdle: a screen whose work has ended stops skipping
  // the loop delay, and a network screen turns Wi-Fi off then instead of on exit.
  static bool radioOffWhenIdle();
  // With radioOffWhenIdle, switch Wi-Fi off now if it is on. Returns true when it
  // did; the caller still restarts on exit to clear the heap the radio left.
  static bool releaseIdleRadio(const char* tag);

  // True when writing a firmware image now could brown out: on battery below
  // PowerConfig::firmwareUpdateMinMv (less PowerConfig::cutoffRadioAllowanceMv
  // while the radio is on). False on boards without the check, on external
  // power, and with no cell fitted.
  bool firmwareUpdateBlockedByBattery() const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

#if SILKSCREEN_BATLOG
  // The battery log's reads of state this class keeps: the smoothed battery value in tenths of a percent (0 before
  // the first read) and whether the CPU is at its normal clock.
  int batteryCachedTenths() const { return _batteryCachedPercent; }
  bool atFullClock() const { return !isLowPower; }
#endif

  // PowerConfig::noChargeableCellNotice: the charge-status ladder shows the
  // charger's no-battery blink, or external power runs the board and the battery
  // reads below BatteryMonitor::CELL_PRESENT_MIN_MV (until it reads
  // NO_CELL_CLEAR_MV again), so there is no cell or one too flat to charge;
  // battery displays say that instead of a charge state. Always false on boards
  // without the field. Reads at most once per BATTERY_POLL_MS.
  bool noChargeableCell() const;
  static constexpr uint16_t NO_CELL_CLEAR_MV = BatteryMonitor::CELL_PRESENT_MIN_MV + 100;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
