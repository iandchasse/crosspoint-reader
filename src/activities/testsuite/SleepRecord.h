#pragma once

// What the test firmware knew about each deep-sleep entry, for T14 (RTC across
// sleep) and P5 (sleep paths and current) on the next boot (CROSSPOINT_TEST_SUITE
// builds only). Kept in RTC_DATA memory: it survives deep sleep (and the ghost
// and release wakes that go straight back to sleep), and any other reset clears
// it. Nothing here arms a wake source or enters sleep: the reader's own sleep
// paths do, and ActivityManager::goToSleep() and the sleep screen fill the record.
#if CROSSPOINT_TEST_SUITE
#include <cstdint>

namespace testsuite {

// The sleep paths P5 can arm (POWER.md §4, MEAS-01). Values are stored in RTC
// memory: append only.
enum class SleepPath : uint8_t {
  None,
  LongPress,    // POWER held longer than 0.4 s
  AutoSleep,    // the sleep timeout, set to 1 min in RAM
  Ceiling,      // Never + the awake ceiling at 1 min in RAM (battery only)
  InputCap,     // the input-only cap at 1 min in RAM (battery only)
  WakeNoInput,  // a power-button wake left alone sleeps after 180 s (battery only)
  GhostWake,    // short taps on POWER while asleep
  ReleaseWake,  // POWER held down through the sleep entry, then released
};
const char* sleepPathCode(SleepPath path);   // "long_press", ... for results
const char* sleepPathTitle(SleepPath path);  // "Long press", ... for screens

struct SleepRecord {
  uint16_t sequence;          // entries recorded since the RTC memory was cleared
  SleepPath path;             // what P5 had armed (None: nothing)
  uint8_t stage;              // WakeNoInput: 1 = the sleep before the wake, 2 = the timed sleep
  bool fromTimeout;           // ActivityManager::goToSleep(fromTimeout): a timeout, not POWER or the cutoff
  bool bootWasPowerWake;      // the boot that slept began with a power-button wake
  bool usbConnected;          // HalGPIO::isUsbConnected() at the entry (USB_STAT ladder)
  bool usbHost;               // a USB data host was attached
  bool touchActive;           // a touch controller was in use, so the entry hibernated it
  bool rtcValid;              // rtcEpoch holds a time
  bool resultRecorded;        // P5 has recorded its result for this entry
  uint32_t uptimeMs;          // millis() at the entry
  uint32_t armedAgoMs;        // from arming to the entry; 0 when nothing was armed this boot
  uint32_t rtcEpoch;          // RTC (UTC seconds) at its first tick after the sleep screen was drawn
  uint16_t tickAfterDrawnMs;  // the sleep screen finished this long before that tick
};

// P5: the next sleep entry this boot carries `path`. Disarm on leaving P5.
void armSleepPath(SleepPath path);
void disarmSleepPath();
SleepPath armedSleepPath();

// ActivityManager::goToSleep(), before the current screen closes.
void beginSleepEntry(bool fromTimeout);
// The sleep screen, once its refresh has returned: times the RTC at its next
// tick (at most about 1.1 s) and stores the record.
void finishSleepEntry();
// The path the pending entry carries, for the sleep screen.
SleepPath pendingSleepPath();

// The record of the entry this boot woke from: false after any boot that is
// not a deep-sleep wake, or when the test firmware recorded no entry.
bool wokeFromSleep(SleepRecord& out);
void markSleepResultRecorded();

// Sleep currents P5 recorded since the RTC memory was cleared, newest last.
struct CurrentReading {
  SleepPath path;
  bool touchActive;
  uint16_t microamps;
  bool flexOut;  // measured with the J2 panel flex unplugged (the HW-01 A/B)
};
inline constexpr int MAX_CURRENT_READINGS = 8;
int currentReadings(CurrentReading* out, int max);
void addCurrentReading(const CurrentReading& reading);

}  // namespace testsuite
#endif
