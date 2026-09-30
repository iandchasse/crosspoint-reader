#if CROSSPOINT_TEST_SUITE
// T10: the front light's LED over-voltage trip (bring-up B8), forced without
// unplugging the load: the trip level (FrontlightConfig::monitorTripMv, which
// the SDK monitor reads on every 100 ms tick) is lowered in RAM below the lit
// reading. It only ever goes down, never above the profile's, and goes back on
// every way out of the screen (end of run, Back, exit, sleep) and when the menu
// opens; a restart restores it anyway. The main loop's trip handler switches the
// light off before this screen runs, so the trip is read from the SDK's monitor
// record, not from fault().
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "LightCheck.h"
#include "TestFactories.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr uint8_t RUN_BRIGHTNESS = 50;
constexpr unsigned long BASELINE_MS = 600;       // lit at the profile's level: several monitor ticks
constexpr unsigned long TRIP_WITHIN_MS = 300;    // the 100 ms tick plus settling
constexpr unsigned long TRIP_TIMEOUT_MS = 1000;  // then the trip failed
constexpr unsigned long WATCH_MS = 2000;         // dark after a trip, no retry by itself
constexpr unsigned long RESTORE_GAP_MS = 300;
constexpr unsigned long RESTORED_MS = 1000;  // lit at the profile's level again, no trip
// The lowered level sits this far below the lit reading (at least 2 V, or a
// fifth: 12 V for the bonded 15 V strings), and at least this far above the dark rail.
constexpr uint32_t MIN_DROP_MV = 2000;
constexpr uint32_t MIN_ABOVE_DARK_MV = 1000;

constexpr int QUESTION_SEEN = 1;
constexpr const char* SEEN_ANSWERS[] = {"Yes", "No"};
constexpr char SEEN_HEADING[] = "Did the light do this?";
constexpr char SEEN_BODY[] = "Lit, went dark, lit briefly and went dark again, then lit steadily for a second.";

constexpr char INTRO[] =
    "Confirm lights the cool string at 50 % once you name the load, then lowers the trip level in RAM below the "
    "lit reading. The monitor must switch the light off within 300 ms and keep it off, trip again when it is "
    "switched on, and light normally once the profile's level is back. About 5 s; Back stops.";
constexpr char NOT_BUILT[] =
    "No front light with an LED monitor in this build or on this board (frontlight_flags): nothing to test.";

enum class Phase : uint8_t { Idle, Baseline, Lowered, Watch, Retry, Restore, Restored, Asking };

const char* phaseName(const Phase phase) {
  switch (phase) {
    case Phase::Idle:
      return "idle";
    case Phase::Baseline:
      return "lit at the profile's level";
    case Phase::Lowered:
      return "level lowered, waiting for the trip";
    case Phase::Watch:
      return "tripped: must stay dark";
    case Phase::Retry:
      return "switched on again: must trip";
    case Phase::Restore:
    case Phase::Restored:
      return "profile's level back: must stay lit";
    case Phase::Asking:
      return "done";
  }
  return "?";
}

struct Trip {
  bool seen = false;
  uint32_t latencyMs = 0;  // level lowered (or switched on) to the trip, by the SDK's clock
  uint32_t ledMv = 0;
  uint32_t limitMv = 0;
  bool adimLow = false;  // IO42 held LOW by the GPIO output register when the trip was seen
};

class LightTripTest final : public TestScreen {
 public:
  LightTripTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}
  ~LightTripTest() override { light::restoreTripMv(); }

 private:
  void onStart() override {
    present = light::monitorConfigured();
    light::restoreTripMv();
  }

  void onExit() override {
    light::restoreTripMv();
    TestScreen::onExit();  // light off
  }

  const char* slotLabel(const Slot slot) const override {
    if (slot != Slot::Confirm) return nullptr;
    if (!present) return "Record";
    return phase == Phase::Idle ? "Run" : nullptr;
  }

  void onSlot(const Slot slot) override {
    if (slot != Slot::Confirm) return;
    if (!present) {
      record(Verdict::Skip, "light=not_built");
      return;
    }
    if (phase == Phase::Idle) confirmLightLoad();
  }

  bool onBack() override {
    if (phase == Phase::Idle || phase == Phase::Asking) return false;
    LOG_INF("TEST", "T10: run stopped in phase %s", phaseName(phase));
    endRun();
    requestUpdate();
    return true;
  }

  void onLightLoadConfirmed(const LightLoad chosen) override {
    load = chosen;
    startRun();
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_SEEN) recordRun(answer);
  }

  void setPhase(const Phase next) {
    phase = next;
    phaseAt = millis();
    monitorNow = light::monitor();
    requestUpdate();
  }

  void startRun() {
    run = RunResult{};
    fails[0] = '\0';
    trips0 = light::monitor().trips;
    run.dark = light::readMonitor();
    if (run.dark.pinMv < light::DARK_MIN_PIN_MV) {
      record(Verdict::Fail, "dark_pin=%u fails=dark_blind (LED_MONIT reads nothing: the trip is blind; not lit)",
             static_cast<unsigned>(run.dark.pinMv));
      return;
    }
    Frontlight.setWarmth(0);
    Frontlight.setBrightness(RUN_BRIGHTNESS);
    Frontlight.setOn(true);
    LOG_INF("TEST", "T10: lit at %u %% cool, trip level %u mV", RUN_BRIGHTNESS, light::profileTripMv());
    setPhase(Phase::Baseline);
  }

  // Light off, level back; nothing recorded.
  void endRun() {
    Frontlight.setOn(false);
    light::restoreTripMv();
    phase = Phase::Idle;
  }

  // An automatic check failed: stop and record it.
  void failRun(const char* what) {
    addFailure(fails, sizeof(fails), what);
    LOG_ERR("TEST", "T10: %s in phase %s", what, phaseName(phase));
    endRun();
    recordRun(-1);
  }

  Trip readTrip(const unsigned long sinceMs) const {
    const auto monitor = light::monitor();
    Trip trip;
    trip.seen = true;
    // The SDK stamps the trip on the esp_timer task; it can precede sinceMs by a tick of the clock.
    const int32_t latency = static_cast<int32_t>(monitor.lastTripMs - static_cast<uint32_t>(sinceMs));
    trip.latencyMs = latency > 0 ? static_cast<uint32_t>(latency) : 0;
    trip.ledMv = monitor.lastTripLedMv;
    trip.limitMv = monitor.lastTripLimitMv;
    trip.adimLow = light::padHeldAt(light::readPad(BoardConfig::ACTIVE.frontlight.gpio),
                                    !BoardConfig::ACTIVE.frontlight.activeHigh);
    return trip;
  }

  void onTick() override {
    if (phase == Phase::Idle || phase == Phase::Asking) return;
    const unsigned long now = millis();
    const uint32_t trips = light::monitor().trips - trips0;
    const unsigned long inPhase = now - phaseAt;
    switch (phase) {
      case Phase::Baseline: {
        if (trips != 0 || !Frontlight.isOn()) {
          failRun("trip_at_profile_level");
          return;
        }
        if (inPhase < BASELINE_MS) return;
        const auto monitor = light::monitor();
        run.litLedMv = monitor.lastLedMv;
        const uint32_t drop = run.litLedMv / 5 > MIN_DROP_MV ? run.litLedMv / 5 : MIN_DROP_MV;
        run.trialMv = run.litLedMv > drop ? run.litLedMv - drop : 0;
        if (run.trialMv < run.dark.ledMv + MIN_ABOVE_DARK_MV) {
          failRun("lit_too_low");
          return;
        }
        light::setTripMv(static_cast<uint16_t>(run.trialMv));
        setPhase(Phase::Lowered);
        return;
      }
      case Phase::Lowered:
        if (trips >= 1) {
          run.first = readTrip(phaseAt);
          setPhase(Phase::Watch);
        } else if (inPhase > TRIP_TIMEOUT_MS) {
          failRun("no_trip");
        }
        return;
      case Phase::Watch:
        // No retry by itself: dark, IO42 held LOW, no further trip.
        if (Frontlight.isOn() || trips != 1 ||
            !light::padHeldAt(light::readPad(BoardConfig::ACTIVE.frontlight.gpio),
                              !BoardConfig::ACTIVE.frontlight.activeHigh)) {
          run.stayedDark = false;
        }
        if (run.darkAfter.pinMv == 0 && inPhase >= light::SETTLE_MS) run.darkAfter = light::readMonitor();
        if (inPhase < WATCH_MS) return;
        Frontlight.setOn(true);  // the operator's retry, with the level still lowered
        setPhase(Phase::Retry);
        return;
      case Phase::Retry:
        if (trips >= 2) {
          run.second = readTrip(phaseAt);
          light::restoreTripMv();
          setPhase(Phase::Restore);
        } else if (inPhase > TRIP_TIMEOUT_MS) {
          failRun("no_second_trip");
        }
        return;
      case Phase::Restore:
        // The loop's trip handler has switched the light off by now.
        if (inPhase < RESTORE_GAP_MS) return;
        Frontlight.setOn(true);
        setPhase(Phase::Restored);
        return;
      case Phase::Restored:
        if (trips != 2 || !Frontlight.isOn()) {
          failRun("trip_after_restore");
          return;
        }
        if (inPhase < RESTORED_MS) return;
        run.restoredLedMv = light::monitor().lastLedMv;
        run.restoredOk = true;
        Frontlight.setOn(false);
        setPhase(Phase::Asking);
        if (!ask(QUESTION_SEEN, SEEN_HEADING, SEEN_BODY, SEEN_ANSWERS, 2)) recordRun(-1);
        return;
      default:
        return;
    }
  }

  // answer: the operator's (0 yes, 1 no), or -1 when not asked or not answered.
  void recordRun(const int answer) {
    phase = Phase::Idle;
    light::restoreTripMv();
    if (run.first.seen) {
      if (run.first.latencyMs > TRIP_WITHIN_MS) addFailure(fails, sizeof(fails), "trip_slow");
      if (!run.first.adimLow) addFailure(fails, sizeof(fails), "io42_not_low");
      if (!run.stayedDark) addFailure(fails, sizeof(fails), "not_dark");
      if (run.darkAfter.pinMv > light::DARK_MAX_PIN_MV) addFailure(fails, sizeof(fails), "dark_after_high");
    }
    if (run.second.seen && run.second.latencyMs > TRIP_WITHIN_MS) addFailure(fails, sizeof(fails), "trip2_slow");
    if (BoardConfig::ACTIVE.frontlight.monitorTripMv != light::profileTripMv()) {
      addFailure(fails, sizeof(fails), "level_not_restored");
    }
    if (answer == 1) addFailure(fails, sizeof(fails), "operator");
    const bool complete = run.restoredOk;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : complete && answer == 0 ? Verdict::Pass : Verdict::Info;
    monitorNow = light::monitor();
    record(verdict,
           "load=%s dark_pin=%u lit_led=%u trial=%u trip1=%ums@%u io42_low=%d dark_after_pin=%u stayed_dark=%d "
           "trip2=%ums restored=%d restored_led=%u level=%u trips=%u seen=%s fails=%s",
           load == LightLoad::J3Panel ? "J3" : "J6", static_cast<unsigned>(run.dark.pinMv),
           static_cast<unsigned>(run.litLedMv), static_cast<unsigned>(run.trialMv),
           static_cast<unsigned>(run.first.latencyMs), static_cast<unsigned>(run.first.ledMv),
           run.first.adimLow ? 1 : 0, static_cast<unsigned>(run.darkAfter.pinMv), run.stayedDark ? 1 : 0,
           static_cast<unsigned>(run.second.latencyMs), run.restoredOk ? 1 : 0,
           static_cast<unsigned>(run.restoredLedMv), BoardConfig::ACTIVE.frontlight.monitorTripMv,
           static_cast<unsigned>(monitorNow.trips - trips0),
           answer == 0   ? "yes"
           : answer == 1 ? "no"
                         : "none",
           fails[0] != '\0' ? fails : "none");
  }

  void drawTrip(TestPage& page, const char* label, const Trip& trip) const {
    if (!trip.seen) {
      page.row(label, "-");
      return;
    }
    page.rowf(label, "%u ms at %u mV (limit %u)%s", static_cast<unsigned>(trip.latencyMs),
              static_cast<unsigned>(trip.ledMv), static_cast<unsigned>(trip.limitMv),
              trip.latencyMs > TRIP_WITHIN_MS ? " SLOW" : "");
  }

  void drawBody(TestPage& page) const override {
    if (!present) {
      page.paragraph(NOT_BUILT, 3);
      return;
    }
    page.paragraph(INTRO, 5);
    page.gap(4);
    page.setCompact(true);
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    page.row("Phase", phaseName(phase));
    page.rowf("Trip level now / profile", "%u / %u mV", fl.monitorTripMv, light::profileTripMv());
    if (run.dark.pinMv != 0) page.rowf("Dark, before", "%u mV pin", static_cast<unsigned>(run.dark.pinMv));
    if (run.litLedMv != 0) {
      page.rowf("Lit at the profile's level", "%u mV LED_SW", static_cast<unsigned>(run.litLedMv));
      page.rowf("Lowered level", "%u mV LED_SW", static_cast<unsigned>(run.trialMv));
    }
    drawTrip(page, "Trip 1 (within 300 ms)", run.first);
    if (run.first.seen) {
      page.row("IO42 at the trip", run.first.adimLow ? "LOW (GPIO)" : "NOT held LOW");
      if (run.darkAfter.pinMv != 0) page.rowf("Dark after", "%u mV pin", static_cast<unsigned>(run.darkAfter.pinMv));
      page.row("Stayed dark 2 s", run.stayedDark ? "yes" : "NO");
    }
    drawTrip(page, "Trip 2, switched on again", run.second);
    if (run.restoredOk) page.rowf("Restored, lit 1 s", "%u mV LED_SW", static_cast<unsigned>(run.restoredLedMv));
    page.rowf("Monitor trips this boot", "%u", static_cast<unsigned>(monitorNow.trips));
    page.setCompact(false);
  }

  struct RunResult {
    light::Reading dark;
    light::Reading darkAfter;
    uint32_t litLedMv = 0;
    uint32_t trialMv = 0;
    Trip first;
    Trip second;
    bool stayedDark = true;
    bool restoredOk = false;
    uint32_t restoredLedMv = 0;
  };

  bool present = false;
  LightLoad load = LightLoad::J3Panel;
  Phase phase = Phase::Idle;
  unsigned long phaseAt = 0;
  uint32_t trips0 = 0;
  RunResult run;
  FrontlightManager::MonitorDiagnostics monitorNow;  // as of the last phase change or record
  char fails[96] = "";
};

}  // namespace

std::unique_ptr<Activity> createLightTripTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<LightTripTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
