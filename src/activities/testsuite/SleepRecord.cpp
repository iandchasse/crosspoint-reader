#if CROSSPOINT_TEST_SUITE
#include "SleepRecord.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_system.h>

#include "TestSuite.h"
#include "tests/RtcAccess.h"

namespace testsuite {
namespace {

constexpr uint32_t MAGIC = 0x534C5032;  // "SLP2"

struct Stored {
  uint32_t magic;
  SleepRecord record;
  uint8_t readingCount;
  CurrentReading readings[MAX_CURRENT_READINGS];
};
// The bootloader reloads RTC data (zeros here) at every reset but a deep-sleep wake.
RTC_DATA_ATTR Stored stored;

// The entry this boot woke from, copied before the next entry overwrites it.
bool wokeLoaded = false;
bool wokeValid = false;
SleepRecord woke{};

SleepPath armed = SleepPath::None;
unsigned long armedAt = 0;

bool pendingValid = false;
SleepRecord pending{};

void loadWoke() {
  if (wokeLoaded) return;
  wokeLoaded = true;
  wokeValid = esp_reset_reason() == ESP_RST_DEEPSLEEP && stored.magic == MAGIC && stored.record.sequence != 0;
  if (wokeValid) woke = stored.record;
}

// Starts the RTC record on a boot that has none, keeping nothing from before.
void claimStore() {
  if (stored.magic == MAGIC) return;
  stored = Stored{};
  stored.magic = MAGIC;
}

}  // namespace

const char* sleepPathCode(const SleepPath path) {
  switch (path) {
    case SleepPath::None:
      return "none";
    case SleepPath::LongPress:
      return "long_press";
    case SleepPath::AutoSleep:
      return "auto_sleep";
    case SleepPath::Ceiling:
      return "ceiling";
    case SleepPath::InputCap:
      return "input_cap";
    case SleepPath::WakeNoInput:
      return "wake_no_input";
    case SleepPath::GhostWake:
      return "ghost_wake";
    case SleepPath::ReleaseWake:
      return "release_wake";
  }
  return "?";
}

const char* sleepPathTitle(const SleepPath path) {
  switch (path) {
    case SleepPath::None:
      return "None";
    case SleepPath::LongPress:
      return "Long press";
    case SleepPath::AutoSleep:
      return "Auto-sleep (1 min)";
    case SleepPath::Ceiling:
      return "Awake ceiling (1 min)";
    case SleepPath::InputCap:
      return "Input cap (1 min)";
    case SleepPath::WakeNoInput:
      return "No input after a wake";
    case SleepPath::GhostWake:
      return "Ghost wake";
    case SleepPath::ReleaseWake:
      return "Release wake";
  }
  return "?";
}

void armSleepPath(const SleepPath path) {
  armed = path;
  armedAt = millis();
  LOG_INF("TEST", "P5: sleep path %s armed", sleepPathCode(path));
}

void disarmSleepPath() { armed = SleepPath::None; }

SleepPath armedSleepPath() { return armed; }

void beginSleepEntry(const bool fromTimeout) {
  loadWoke();
  pending = SleepRecord{};
  pending.sequence = static_cast<uint16_t>((stored.magic == MAGIC ? stored.record.sequence : 0) + 1);
  pending.fromTimeout = fromTimeout;
  pending.bootWasPowerWake = esp_reset_reason() == ESP_RST_DEEPSLEEP;
  pending.usbConnected = gpio.isUsbConnected();
  pending.usbHost = usbHostAttached();
  pending.touchActive = gpio.hasTouch();
  pending.uptimeMs = millis();
  if (armed != SleepPath::None) {
    pending.path = armed;
    pending.stage = armed == SleepPath::WakeNoInput ? 1 : 0;
    pending.armedAgoMs = millis() - armedAt;
  } else if (wokeValid && woke.path == SleepPath::WakeNoInput && woke.stage == 1 && pending.bootWasPowerWake) {
    // The wake after P5 armed "no input after a wake": this is the sleep it checks.
    pending.path = SleepPath::WakeNoInput;
    pending.stage = 2;
  }
  pendingValid = true;
  LOG_INF("TEST", "Sleep entry %u: %s, uptime %lu ms, P5 path %s", pending.sequence,
          fromTimeout ? "timeout" : "power button or cutoff", static_cast<unsigned long>(pending.uptimeMs),
          sleepPathCode(pending.path));
}

SleepPath pendingSleepPath() { return pendingValid ? pending.path : SleepPath::None; }

void finishSleepEntry() {
  if (!pendingValid) beginSleepEntry(false);
  const unsigned long drawnAt = millis();
  uint32_t epoch = 0;
  unsigned long tickAt = 0;
  pending.rtcValid = rtc::readAtTick(epoch, tickAt);
  if (pending.rtcValid) {
    pending.rtcEpoch = epoch;
    pending.tickAfterDrawnMs = static_cast<uint16_t>(tickAt - drawnAt);
  }
  claimStore();
  stored.record = pending;
  pendingValid = false;
  LOG_INF("TEST", "Sleep entry %u recorded: RTC %s (epoch %lu, tick %u ms after the screen)", pending.sequence,
          pending.rtcValid ? "valid" : "not read", static_cast<unsigned long>(pending.rtcEpoch),
          pending.tickAfterDrawnMs);
}

bool wokeFromSleep(SleepRecord& out) {
  loadWoke();
  if (!wokeValid) return false;
  out = woke;
  return true;
}

void markSleepResultRecorded() {
  loadWoke();
  if (!wokeValid) return;
  woke.resultRecorded = true;
  if (stored.magic == MAGIC && stored.record.sequence == woke.sequence) stored.record.resultRecorded = true;
}

int currentReadings(CurrentReading* out, const int max) {
  if (stored.magic != MAGIC) return 0;
  const int count = stored.readingCount < max ? stored.readingCount : max;
  for (int i = 0; i < count; ++i) out[i] = stored.readings[i];
  return count;
}

void addCurrentReading(const CurrentReading& reading) {
  // The first reading of a boot that did not wake from a recorded entry starts
  // the store; its record stays empty (sequence 0), so no wake trusts it.
  claimStore();
  if (stored.readingCount >= MAX_CURRENT_READINGS) {
    for (int i = 1; i < MAX_CURRENT_READINGS; ++i) stored.readings[i - 1] = stored.readings[i];
    stored.readingCount = MAX_CURRENT_READINGS - 1;
  }
  stored.readings[stored.readingCount++] = reading;
}

}  // namespace testsuite
#endif
