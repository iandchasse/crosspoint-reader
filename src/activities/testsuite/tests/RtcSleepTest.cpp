#if CROSSPOINT_TEST_SUITE
// T14: the RTC keeps time through a deep sleep (bring-up B7). The sleep screen
// stores the RTC's time in RTC memory once its refresh has returned (the moment
// the operator starts a stopwatch), timed to the RTC's next seconds tick; this
// screen times the RTC the same way after the wake and works back to the moment
// the boot began, which is the power-button press that woke the board. There is
// no timer wake (ZD-01): the operator's stopwatch is the reference.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalClock.h>
#include <Memory.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstdio>
#include <cstdlib>

#include "RtcAccess.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/SleepRecord.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

// |RTC - stopwatch|: 1 s per end for the operator and the stopwatch, and the
// boot's own time before millis() starts (rtc-accuracy: 5 ppm is 3 ms in 10 min).
constexpr long TOLERANCE_MS = 2000;
constexpr long MIN_SLEEP_MS = 10L * 60L * 1000L;
constexpr long FINE_STEP_MS = 100;
constexpr long COARSE_STEP_MS = 1000;
constexpr long STOPWATCH_MAX_MS = 24L * 3600L * 1000L;

constexpr char INTRO[] =
    "1. Hold POWER to sleep. Start a stopwatch when the sleep screen has finished drawing. 2. Wait at least 10 min "
    "(MEAS-01 can run meanwhile). 3. Stop the stopwatch as you press POWER to wake, and hold it until the splash. "
    "4. Open T14 and enter the stopwatch (it starts at the RTC's figure): Left/Right -/+0.1 s, Up/Down -/+1 s. "
    "Record without an entry logs INFO.";
constexpr char NO_RECORD[] =
    "No sleep to judge: this boot is not a wake from a sleep entry of this firmware (a restart or power-on clears "
    "the record). Follow the steps above.";

class RtcSleepTest final : public TestScreen {
 public:
  RtcSleepTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    hasRecord = wokeFromSleep(slept);
    powerWake = powerButtonWake();
    wake = esp_sleep_get_wakeup_cause();
    reset = esp_reset_reason();
    written = rtc::writtenThisBoot();
    validNow = halClock.hasValidTime();
    if (!hasRecord || !slept.rtcValid) return;
    uint32_t epoch = 0;
    unsigned long atMs = 0;
    if (!rtc::readAtTick(epoch, atMs)) return;
    // The RTC's time at the moment millis() started, and at the moment the sleep
    // screen finished, both in ms (the tick is a whole second in either).
    const int64_t bootAtMs = static_cast<int64_t>(epoch) * 1000 - static_cast<int64_t>(atMs);
    const int64_t drawnAtMs = static_cast<int64_t>(slept.rtcEpoch) * 1000 - slept.tickAfterDrawnMs;
    elapsedMs = static_cast<long>(bootAtMs - drawnAtMs);
    measured = true;
    stopwatchMs = elapsedMs / FINE_STEP_MS * FINE_STEP_MS;
  }

  const char* slotLabel(const Slot slot) const override {
    if (!measured) return slot == Slot::Confirm ? "Record" : nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Record";
      case Slot::Left:
        return "-0.1 s";
      case Slot::Right:
        return "+0.1 s";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm) recordStopwatch();
    if (slot == Slot::Left && measured) adjust(-FINE_STEP_MS);
    if (slot == Slot::Right && measured) adjust(+FINE_STEP_MS);
  }

  void onStep(const int direction) override {
    if (measured) adjust(direction * COARSE_STEP_MS);
  }

  void adjust(const long deltaMs) {
    stopwatchMs += deltaMs;
    if (stopwatchMs < 0) stopwatchMs = 0;
    if (stopwatchMs > STOPWATCH_MAX_MS) stopwatchMs = STOPWATCH_MAX_MS;
    entered = true;
    requestUpdate();
  }

  void recordStopwatch() {
    // The RTC is optional (rtc-devices): with none fitted there is nothing to time.
    if (!rtc::present()) {
      record(Verdict::Skip, "reason=no_rtc sleep_record=%d reset=%s", hasRecord ? 1 : 0, resetReasonName(reset));
      return;
    }
    if (!hasRecord) {
      record(Verdict::Skip, "reason=no_sleep_record reset=%s", resetReasonName(reset));
      return;
    }
    if (!measured) {
      record(Verdict::Fail, "reason=%s valid_now=%d", slept.rtcValid ? "no_tick_now" : "rtc_not_read_at_sleep",
             validNow ? 1 : 0);
      return;
    }
    char fails[64] = "";
    const long error = elapsedMs - stopwatchMs;
    if (entered && labs(error) > TOLERANCE_MS) addFailure(fails, sizeof(fails), "elapsed");
    if (!validNow) addFailure(fails, sizeof(fails), "time_lost");
    if (!powerWake) addFailure(fails, sizeof(fails), "wake");
    if (written) addFailure(fails, sizeof(fails), "rtc_set_since_wake");
    Verdict verdict = fails[0] != '\0' ? Verdict::Fail : Verdict::Pass;
    // No stopwatch value, or a sleep shorter than the procedure asks: a reading, not a pass.
    if (verdict == Verdict::Pass && (!entered || elapsedMs < MIN_SLEEP_MS)) verdict = Verdict::Info;
    record(verdict,
           "rtc_elapsed_ms=%ld stopwatch_ms=%ld error_ms=%ld limit_ms=%ld sleep_entry=%u from_timeout=%d wake=%s "
           "power_wake=%d valid_now=%d rtc_set_since_wake=%d short=%d fails=%s",
           elapsedMs, entered ? stopwatchMs : -1L, entered ? error : 0L, TOLERANCE_MS, slept.sequence,
           slept.fromTimeout ? 1 : 0, wakeCauseName(wake), powerWake ? 1 : 0, validNow ? 1 : 0, written ? 1 : 0,
           elapsedMs < MIN_SLEEP_MS ? 1 : 0, fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 9);
    page.gap(4);
    if (!hasRecord) {
      page.paragraph(NO_RECORD, 4);
      page.rowf("Reset / wake", "%s / %s", resetReasonName(reset), wakeCauseName(wake));
      return;
    }
    page.setCompact(true);
    page.rowf("Sleep entry", "#%u, %s, after %lu s awake", slept.sequence,
              slept.fromTimeout ? "timeout" : "power button or cutoff",
              static_cast<unsigned long>(slept.uptimeMs / 1000));
    page.rowf("Wake", "%s%s", wakeCauseName(wake), powerWake ? " on POWER (IO18)" : " (not the power button)");
    page.rowf("RTC now", "%s", validNow ? "time valid (OSF 0)" : "time LOST");
    if (written) page.row("RTC written since the wake", "not judged: T13/T15 set it");
    if (!slept.rtcValid) {
      page.row("At sleep", "RTC not read: nothing to compare");
    } else if (!measured) {
      page.row("Now", "RTC did not tick: nothing to compare");
    } else {
      const long error = elapsedMs - stopwatchMs;
      page.rowf("RTC elapsed", "%ld.%01ld s (%ld min)", elapsedMs / 1000, labs(elapsedMs % 1000) / 100,
                elapsedMs / 60000);
      page.rowf("Stopwatch", "%ld.%01ld s%s", stopwatchMs / 1000, (stopwatchMs % 1000) / 100,
                entered ? "" : " (not entered)");
      if (entered) {
        page.rowf("RTC - stopwatch", "%+ld ms%s", error, labs(error) > TOLERANCE_MS ? " (over 2 s)" : "");
      }
      if (elapsedMs < MIN_SLEEP_MS) page.row("Sleep", "under 10 min: recorded as INFO");
    }
    page.setCompact(false);
  }

  SleepRecord slept{};
  bool hasRecord = false;
  bool measured = false;
  bool entered = false;
  bool powerWake = false;
  bool validNow = false;
  bool written = false;
  esp_sleep_wakeup_cause_t wake = ESP_SLEEP_WAKEUP_UNDEFINED;
  esp_reset_reason_t reset = ESP_RST_UNKNOWN;
  long elapsedMs = 0;
  long stopwatchMs = 0;
};

}  // namespace

std::unique_ptr<Activity> createRtcSleepTest(const TestInfo& info, GfxRenderer& renderer,
                                             MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RtcSleepTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
