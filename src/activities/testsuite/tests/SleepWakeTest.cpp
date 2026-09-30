#if CROSSPOINT_TEST_SUITE
// P5: the sleep paths, the wake, and the sleep-current procedure (bring-up B3,
// B10; POWER.md §4 MEAS-01). It arms one of the reader's own sleep paths (the
// timeout, the awake ceiling or the input cap lowered to 1 min in RAM, or
// nothing for the paths the operator triggers) and never sleeps by itself, so
// every entry goes through main.cpp's loop and enterDeepSleep(). The sleep
// screen leaves the current targets and the pins to probe on the glass. After
// the wake this screen shows what slept (SleepRecord), the wake and the time
// asleep, and takes the current the meter showed.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "RtcAccess.h"
#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/SleepRecord.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"
#include "activities/testsuite/TestStrings.h"
#include "activities/testsuite/TestSuite.h"

namespace testsuite {
namespace {

// MEAS-01 (POWER.md §1, §4): the typical floor, and the budget's datasheet
// maximum above which a reading fails. With an FT6336U tail, +55 uA hibernated.
constexpr uint16_t TYPICAL_LOW_UA = 58;
constexpr uint16_t TYPICAL_HIGH_UA = 73;
constexpr uint16_t LIMIT_UA = 95;
constexpr uint16_t TOUCH_TYPICAL_LOW_UA = 113;
constexpr uint16_t TOUCH_TYPICAL_HIGH_UA = 130;
constexpr uint16_t TOUCH_LIMIT_UA = 150;
// "All paths agree within about 2 uA", plus the meter's last digit.
constexpr int AGREE_UA = 3;
constexpr uint16_t MAX_UA = 9999;
// A 1 min path sleeps 60 s after the arming press, give or take the loop's pace.
constexpr unsigned long TIMED_MIN_MS = 58000;
constexpr unsigned long TIMED_MAX_MS = 75000;
// No input after a wake: 180 s from the wake press's release.
constexpr unsigned long WAKE_IDLE_MIN_MS = 178000;
constexpr unsigned long WAKE_IDLE_MAX_MS = 200000;
constexpr unsigned long SAMPLE_MS = 1000;

constexpr int QUESTION_OBSERVED = 1;
constexpr const char* GHOST_ANSWERS[] = {"Short blips, screen unchanged", "A tap booted the board",
                                         "Blips longer than 0.3 s", "Not measured"};
constexpr char GHOST_BODY[] =
    "While it slept, did each short tap on POWER wake the chip for 0.3 s or less (current logger) and leave the "
    "screen unchanged?";
constexpr const char* RELEASE_ANSWERS[] = {"Floor + ~30 uA, one short wake", "Current stayed high",
                                           "Several wakes, or it booted", "Not measured"};
constexpr char RELEASE_BODY[] =
    "With POWER held down, did the current fall to the floor + about 30 uA within about 5 s, and did the release "
    "give one short wake and then sleep again?";
constexpr char OBSERVED_HEADING[] = "What the meter showed";

constexpr char ARM_INTRO[] =
    "MEAS-01: a uA meter or logger in series with the cell (or 3.8 V) at J5, USB and any UART adapter off, card "
    "fitted, light off. Wait 60 s after the sleep screen, then average 60 s. Left/Right pick a path, Confirm arms it.";
constexpr char RESULT_INTRO[] =
    "Enter the average the meter showed while asleep: Left/Right -/+1 uA, Up/Down -/+10 uA. Confirm records. Back "
    "skips to arming another path.";
constexpr char STAGE_ONE_NOTE[] =
    "No input after a wake: the first sleep is done. Opening this screen was input, so the 180 s timeout will not "
    "fire this boot: arm again, sleep, wake and touch nothing.";

struct PathInfo {
  SleepPath path;
  const char* steps;
  bool batteryOnly;  // the loop skips it while USB_STAT reports USB power
};

constexpr PathInfo PATHS[] = {
    {SleepPath::LongPress, "Arm, then hold POWER until the sleep screen (longer than 0.4 s).", false},
    {SleepPath::AutoSleep,
     "Unplug the USB data cable (a host holds auto-sleep off). Arm, then leave the keys and the panel alone: the "
     "timeout, 1 min in RAM, sleeps the board about 60 s later.",
     false},
    {SleepPath::Ceiling,
     "On the cell, no USB. Arm (timeout Never and the awake ceiling 1 min, in RAM), then leave the board alone: it "
     "sleeps about 60 s later.",
     true},
    {SleepPath::InputCap,
     "On the cell, no USB. Arm (the input-only cap 1 min, in RAM), then leave the board alone: it sleeps about 60 s "
     "later.",
     true},
    {SleepPath::WakeNoInput,
     "On the cell, no USB. Arm and hold POWER to sleep. Wake with POWER and touch nothing: 180 s later it sleeps by "
     "itself. Measure that sleep, then wake it and open P5.",
     true},
    {SleepPath::GhostWake,
     "Arm and hold POWER to sleep. While it sleeps, tap POWER briefly a few times: each tap may wake the chip for "
     "0.3 s at most and must not change the screen. Then wake it with a hold and open P5.",
     false},
    {SleepPath::ReleaseWake,
     "Arm, then tape POWER down (or keep holding it) past 0.4 s: it sleeps with the button held, and the current "
     "falls to the floor + about 30 uA within 5 s. Release it: one short wake, then asleep. Wake it with a hold and "
     "open P5.",
     false},
};
constexpr int PATH_COUNT = static_cast<int>(sizeof(PATHS) / sizeof(PATHS[0]));

const PathInfo* pathInfo(const SleepPath path) {
  for (const auto& info : PATHS) {
    if (info.path == path) return &info;
  }
  return nullptr;
}

class SleepWakeTest final : public TestScreen {
 public:
  SleepWakeTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  struct Saved {
    bool active = false;
    uint8_t sleepTimeoutMinutes = 0;
    uint8_t awakeCeilingMin = 0;
    uint8_t awakeInputCapMin = 0;
  };

  void onStart() override {
    reset = esp_reset_reason();
    wake = esp_sleep_get_wakeup_cause();
    ext1Mask = wake == ESP_SLEEP_WAKEUP_EXT1 ? esp_sleep_get_ext1_wakeup_status() : 0;
    powerWake = powerButtonWake();
    hasRecord = wokeFromSleep(slept);
    stageOne = hasRecord && slept.path == SleepPath::WakeNoInput && slept.stage == 1;
    resultView = hasRecord && slept.path != SleepPath::None && !stageOne && !slept.resultRecorded;
    if (resultView) {
      microamps = slept.touchActive ? TOUCH_TYPICAL_LOW_UA : TYPICAL_LOW_UA;
      measureAsleep();
    }
    readingCount = currentReadings(readings, MAX_CURRENT_READINGS);
    sample();
  }

  void onExit() override {
    restoreOverrides();
    disarmSleepPath();
    TestScreen::onExit();
  }

  bool onBack() override {
    if (!resultView) return false;
    resultView = false;  // skipped: nothing recorded, the record stays for another opening
    requestUpdate();
    return true;
  }

  void onTick() override {
    if (millis() - sampledAt < SAMPLE_MS) return;
    const bool before[2] = {hostAttached, usbPower};
    sample();
    if (hostAttached != before[0] || usbPower != before[1]) requestUpdate();
  }

  void sample() {
    sampledAt = millis();
    hostAttached = usbHostAttached();
    usbPower = gpio.isUsbConnected();
  }

  const char* slotLabel(const Slot slot) const override {
    if (resultView) {
      switch (slot) {
        case Slot::Confirm:
          return "Record";
        case Slot::Left:
          return "-1 uA";
        case Slot::Right:
          return "+1 uA";
        default:
          return nullptr;
      }
    }
    const bool armed = armedSleepPath() != SleepPath::None;
    switch (slot) {
      case Slot::Confirm:
        return armed ? "Disarm" : "Arm";
      case Slot::Left:
        return armed ? nullptr : "Prev";
      case Slot::Right:
        return armed ? nullptr : "Next";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (resultView) {
      if (slot == Slot::Confirm) confirmResult();
      if (slot == Slot::Left) adjust(-1);
      if (slot == Slot::Right) adjust(+1);
      return;
    }
    const bool armed = armedSleepPath() != SleepPath::None;
    if (slot == Slot::Confirm) {
      if (armed) {
        restoreOverrides();
        disarmSleepPath();
      } else {
        arm(PATHS[selected].path);
      }
    } else if (!armed && slot == Slot::Left) {
      selected = (selected + PATH_COUNT - 1) % PATH_COUNT;
    } else if (!armed && slot == Slot::Right) {
      selected = (selected + 1) % PATH_COUNT;
    }
    requestUpdate();
  }

  void onStep(const int direction) override {
    if (resultView) adjust(direction * 10);
  }

  void adjust(const int delta) {
    int value = static_cast<int>(microamps) + delta;
    if (value < 0) value = 0;
    if (value > MAX_UA) value = MAX_UA;
    microamps = static_cast<uint16_t>(value);
    entered = true;
    requestUpdate();
  }

  // --- arming ----------------------------------------------------------------

  void arm(const SleepPath path) {
    restoreOverrides();
    saved.active = true;
    saved.sleepTimeoutMinutes = SETTINGS.sleepTimeoutMinutes;
    auto& power = BoardConfig::ACTIVE.power;
    saved.awakeCeilingMin = power.awakeCeilingMin;
    saved.awakeInputCapMin = power.awakeInputCapMin;
    switch (path) {
      case SleepPath::AutoSleep:
        SETTINGS.sleepTimeoutMinutes = 1;
        break;
      case SleepPath::Ceiling:
        SETTINGS.sleepTimeoutMinutes = CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES;
        power.awakeCeilingMin = 1;
        break;
      case SleepPath::InputCap:
        power.awakeInputCapMin = 1;
        break;
      default:
        break;
    }
    armSleepPath(path);
  }

  // The pinned timeout and the profile's ceiling and cap, in RAM.
  void restoreOverrides() {
    if (!saved.active) return;
    SETTINGS.sleepTimeoutMinutes = saved.sleepTimeoutMinutes;
    auto& power = BoardConfig::ACTIVE.power;
    power.awakeCeilingMin = saved.awakeCeilingMin;
    power.awakeInputCapMin = saved.awakeInputCapMin;
    saved = Saved{};
  }

  // --- after the wake --------------------------------------------------------

  void measureAsleep() {
    if (!slept.rtcValid || rtc::writtenThisBoot()) return;
    uint32_t epoch = 0;
    unsigned long atMs = 0;
    if (!rtc::readAtTick(epoch, atMs)) return;
    const int64_t bootAtMs = static_cast<int64_t>(epoch) * 1000 - static_cast<int64_t>(atMs);
    const int64_t drawnAtMs = static_cast<int64_t>(slept.rtcEpoch) * 1000 - slept.tickAfterDrawnMs;
    asleepS = static_cast<long>((bootAtMs - drawnAtMs) / 1000);
    asleepKnown = true;
  }

  uint16_t limitUa() const { return slept.touchActive ? TOUCH_LIMIT_UA : LIMIT_UA; }

  // Whether the entry came the way the armed path should bring it.
  void checkPath(char* fails, const size_t size) const {
    switch (slept.path) {
      case SleepPath::LongPress:
      case SleepPath::GhostWake:
      case SleepPath::ReleaseWake:
        if (slept.fromTimeout) addFailure(fails, size, "not_power_button");
        break;
      case SleepPath::AutoSleep:
      case SleepPath::Ceiling:
      case SleepPath::InputCap:
        if (!slept.fromTimeout) addFailure(fails, size, "not_timeout");
        if (slept.armedAgoMs < TIMED_MIN_MS || slept.armedAgoMs > TIMED_MAX_MS) addFailure(fails, size, "timing");
        if (slept.path != SleepPath::AutoSleep && slept.usbConnected) addFailure(fails, size, "usb_power");
        break;
      case SleepPath::WakeNoInput:
        if (!slept.fromTimeout) addFailure(fails, size, "not_timeout");
        if (!slept.bootWasPowerWake) addFailure(fails, size, "not_after_wake");
        if (slept.uptimeMs < WAKE_IDLE_MIN_MS || slept.uptimeMs > WAKE_IDLE_MAX_MS) addFailure(fails, size, "timing");
        if (slept.usbConnected) addFailure(fails, size, "usb_power");
        break;
      case SleepPath::None:
        break;
    }
  }

  void confirmResult() {
    if (slept.path == SleepPath::GhostWake) {
      ask(QUESTION_OBSERVED, OBSERVED_HEADING, GHOST_BODY, GHOST_ANSWERS, 4);
    } else if (slept.path == SleepPath::ReleaseWake) {
      ask(QUESTION_OBSERVED, OBSERVED_HEADING, RELEASE_BODY, RELEASE_ANSWERS, 4);
    } else {
      recordSleep(-1);
    }
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_OBSERVED && answer >= 0) recordSleep(answer);
  }

  // observed: the operator's answer for the ghost and release paths (0 = as
  // expected, 3 = not measured), -1 when the path asks nothing.
  void recordSleep(const int observed) {
    char fails[96] = "";
    checkPath(fails, sizeof(fails));
    if (!powerWake) addFailure(fails, sizeof(fails), "wake");
    if (entered && microamps > limitUa()) addFailure(fails, sizeof(fails), "current");
    if (observed == 1 || observed == 2) addFailure(fails, sizeof(fails), "observed");
    // Agreement with the other paths measured in the same configuration.
    int spread = -1;
    if (entered) {
      uint16_t lo = microamps;
      uint16_t hi = microamps;
      int compared = 0;
      for (int i = 0; i < readingCount; ++i) {
        if (readings[i].touchActive != slept.touchActive) continue;
        if (readings[i].microamps < lo) lo = readings[i].microamps;
        if (readings[i].microamps > hi) hi = readings[i].microamps;
        ++compared;
      }
      if (compared > 0) {
        spread = hi - lo;
        if (spread > AGREE_UA) addFailure(fails, sizeof(fails), "paths_disagree");
      }
      addCurrentReading({slept.path, slept.touchActive, microamps});
      readingCount = currentReadings(readings, MAX_CURRENT_READINGS);
    }
    markSleepResultRecorded();
    slept.resultRecorded = true;
    resultView = false;
    Verdict verdict = fails[0] != '\0' ? Verdict::Fail : Verdict::Pass;
    // Nothing measured: the path's own checks, recorded as a reading.
    if (verdict == Verdict::Pass && (!entered || observed == 3)) verdict = Verdict::Info;
    char current[12] = "none";
    if (entered) snprintf(current, sizeof(current), "%u", microamps);
    char asleep[16] = "n/a";
    if (asleepKnown) snprintf(asleep, sizeof(asleep), "%ld", asleepS);
    char mask[24];
    formatWakeMask(mask, sizeof(mask), ext1Mask);
    record(verdict,
           "path=%s stage=%u from_timeout=%d armed_ago_s=%lu uptime_s=%lu usb_at_entry=%d host_at_entry=%d touch=%d "
           "wake=%s ext1=%s power_wake=%d asleep_s=%s current_ua=%s limit_ua=%u spread_ua=%d observed=%d fails=%s",
           sleepPathCode(slept.path), slept.stage, slept.fromTimeout ? 1 : 0,
           static_cast<unsigned long>(slept.armedAgoMs / 1000), static_cast<unsigned long>(slept.uptimeMs / 1000),
           slept.usbConnected ? 1 : 0, slept.usbHost ? 1 : 0, slept.touchActive ? 1 : 0, wakeCauseName(wake), mask,
           powerWake ? 1 : 0, asleep, current, limitUa(), spread, observed, fails[0] == '\0' ? "none" : fails);
  }

  // --- screen ----------------------------------------------------------------

  void drawBody(TestPage& page) const override {
    if (resultView) {
      drawResultView(page);
    } else {
      drawArmView(page);
    }
  }

  void drawResultView(TestPage& page) const {
    page.paragraph(RESULT_INTRO, 3);
    page.gap(4);
    page.setCompact(true);
    page.rowf("Path that slept", "%s%s", sleepPathTitle(slept.path),
              slept.path == SleepPath::WakeNoInput ? " (the timed sleep)" : "");
    page.rowf("Entry", "#%u, %s, %lu s up", slept.sequence, slept.fromTimeout ? "timeout" : "POWER or cutoff",
              static_cast<unsigned long>(slept.uptimeMs / 1000));
    if (slept.armedAgoMs != 0) {
      page.rowf("Armed to sleep", "%lu.%lu s", static_cast<unsigned long>(slept.armedAgoMs / 1000),
                static_cast<unsigned long>(slept.armedAgoMs % 1000 / 100));
    }
    page.rowf("At entry: USB power / host / touch", "%s / %s / %s", slept.usbConnected ? "yes" : "no",
              slept.usbHost ? "yes" : "no", slept.touchActive ? "yes" : "no");
    char mask[24];
    formatWakeMask(mask, sizeof(mask), ext1Mask);
    page.rowf("Reset / wake / ext1", "%s / %s / %s%s", resetReasonName(reset), wakeCauseName(wake), mask,
              powerWake ? "" : " (not POWER)");
    if (asleepKnown) page.rowf("Asleep (RTC)", "%ld min %02ld s", asleepS / 60, asleepS % 60);
    page.rowf("Expected", "%u-%u uA typical, fail above %u", slept.touchActive ? TOUCH_TYPICAL_LOW_UA : TYPICAL_LOW_UA,
              slept.touchActive ? TOUCH_TYPICAL_HIGH_UA : TYPICAL_HIGH_UA, limitUa());
    page.setCompact(false);
    page.gap(8);
    page.rowf("Sleep current", "%u uA%s", microamps, entered ? "" : " (not entered)");
    drawReadings(page);
  }

  void drawArmView(TestPage& page) const {
    page.paragraph(ARM_INTRO, 4);
    page.gap(4);
    const SleepPath armed = armedSleepPath();
    const PathInfo& shown = armed != SleepPath::None && pathInfo(armed) != nullptr ? *pathInfo(armed) : PATHS[selected];
    char heading[48];
    snprintf(heading, sizeof(heading), "%s%s", sleepPathTitle(shown.path), armed != SleepPath::None ? ": ARMED" : "");
    page.heading(heading);
    page.paragraph(shown.steps, 4);
    page.gap(4);
    page.setCompact(true);
    if (stageOne) page.paragraph(STAGE_ONE_NOTE, 3);
    page.rowf("USB host / USB power", "%s / %s", hostAttached ? "attached (holds auto-sleep off)" : "none",
              usbPower ? "yes" : "no");
    if (shown.batteryOnly && usbPower) page.row("This path", "does not fire on USB power");
    page.rowf("Typical, no touch / FT6336U", "%u-%u / %u-%u uA", TYPICAL_LOW_UA, TYPICAL_HIGH_UA, TOUCH_TYPICAL_LOW_UA,
              TOUCH_TYPICAL_HIGH_UA);
    page.setCompact(false);
    page.gap(4);
    page.heading(text::SLEEP_PINS_HEADING);
    for (const char* line : text::SLEEP_PIN_LINES) page.paragraph(line, 1);
    drawReadings(page);
  }

  void drawReadings(TestPage& page) const {
    if (readingCount == 0) return;
    page.gap(4);
    page.setCompact(true);
    for (int i = readingCount - 1; i >= 0; --i) {
      char label[40];
      snprintf(label, sizeof(label), "%s%s", sleepPathTitle(readings[i].path),
               readings[i].touchActive ? ", touch" : "");
      page.rowf(label, "%u uA", readings[i].microamps);
    }
    page.setCompact(false);
  }

  int selected = 0;
  Saved saved;
  SleepRecord slept{};
  bool hasRecord = false;
  bool stageOne = false;
  bool resultView = false;
  bool entered = false;
  uint16_t microamps = 0;
  bool asleepKnown = false;
  long asleepS = 0;
  bool powerWake = false;
  esp_reset_reason_t reset = ESP_RST_UNKNOWN;
  esp_sleep_wakeup_cause_t wake = ESP_SLEEP_WAKEUP_UNDEFINED;
  uint64_t ext1Mask = 0;
  bool hostAttached = false;
  bool usbPower = false;
  unsigned long sampledAt = 0;
  CurrentReading readings[MAX_CURRENT_READINGS] = {};
  int readingCount = 0;
};

}  // namespace

std::unique_ptr<Activity> createSleepWakeTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SleepWakeTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
