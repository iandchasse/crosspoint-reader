#if CROSSPOINT_TEST_SUITE
// T10: the front light's LED over-voltage trip and its switch-on load probe
// (bring-up B8), forced without unplugging the load: the trip level
// (FrontlightConfig::monitorTripMv, which the SDK monitor reads on every 100 ms
// tick and the probe reads during its window) is lowered in RAM below the lit
// reading. The light, lit, must trip in use; switched on again at that level, the
// probe must take the load for absent and leave it off. The level only ever goes
// down, never above the profile's, and goes back on every way out of the screen
// (end of run, Back, exit, sleep) and when the menu opens; a restart restores it
// anyway. The main loop's trip handler switches the light off before this screen
// runs, so the in-use trip is read from the SDK's monitor record, not from fault().
//
// Step 0 (BRING_UP section 7.4) is the one deliberate exception to "never light
// the boost without a load": opt-in, after the operator names the driver build
// (refused on the TPS923611 swap unless D3 is an SMAJ33A, and on a cut-down
// board with the swap) and confirms J3 and J6 are empty, it switches the light on
// once through the reader's own path and checks that the SDK caught the open load.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "LightCheck.h"
#include "TestFactories.h"
#include "TextBuffer.h"
#include "activities/testsuite/BoardOptions.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr uint8_t RUN_BRIGHTNESS = 50;
constexpr unsigned long BASELINE_MS = 600;       // lit at the profile's level: several monitor ticks
constexpr unsigned long TRIP_WITHIN_MS = 300;    // the 100 ms tick plus settling
constexpr uint32_t PROBE_DARK_WITHIN_US = 1000;  // the probe's reading above the level to IO42 LOW: "at once"
constexpr unsigned long TRIP_TIMEOUT_MS = 1000;  // then the trip failed
constexpr unsigned long WATCH_MS = 2000;         // dark after a trip, no retry by itself
constexpr unsigned long RESTORE_GAP_MS = 300;
constexpr unsigned long RESTORED_MS = 1000;  // lit at the profile's level again, no trip
// The lowered level sits this far below the lit reading (at least 2 V, or a
// fifth: 12 V for the bonded 15 V strings), and at least this far above the dark rail.
constexpr uint32_t MIN_DROP_MV = 2000;
constexpr uint32_t MIN_ABOVE_DARK_MV = 1000;

// Step 0: a real open load (BRING_UP section 7.4). With no load the boost climbs
// to U10's OVP at any duty, so the reader's lowest brightness step is used.
constexpr uint8_t OPEN_BRIGHTNESS = 10;
constexpr unsigned long OPEN_TRIP_WITHIN_MS = 500;    // PASS: the SDK tripped within the probe window
constexpr unsigned long OPEN_TRIP_TIMEOUT_MS = 1000;  // no trip by then: the monitor missed it
constexpr unsigned long OPEN_WATCH_MS = 2000;         // after the switch-on decided: dark, no relight
constexpr unsigned long OPEN_COOL_MS = 10000;         // LED_SW logged this long after the last trip (C9's decay)
constexpr unsigned long OPEN_REDRAW_MS = 3000;
constexpr uint32_t OPEN_RISE_PIN_MV = 50;  // LED_MONIT rising this much over the watch: the boost ran again

constexpr int QUESTION_SEEN = 1;
constexpr const char* SEEN_ANSWERS[] = {"Yes", "No"};
constexpr char SEEN_HEADING[] = "Did the light do this?";
constexpr char SEEN_BODY[] = "Lit, went dark, stayed dark when switched on again, then lit steadily for a second.";

constexpr int QUESTION_OPEN_BUILD = 2;
constexpr const char* OPEN_BUILD_ANSWERS[] = {"Cancel", "TPS923610, D3 SMAJ26A (standard)", "TPS923611 (driver swap)"};
constexpr char OPEN_BUILD_HEADING[] = "Step 0: which light driver?";
constexpr char OPEN_BUILD_BODY[] =
    "Step 0 switches the light on with NO load: U10 climbs to its OVP (24.25-25.5 V) and latches off. That is safe "
    "on the standard build. The TPS923611 swap is safe only with an SMAJ33A at D3.";
constexpr char OPEN_BUILD_BODY_CUT[] =
    "Step 0 switches the light on with NO load: U10 climbs to its OVP (24.25-25.5 V) and latches off. Cut-down "
    "board: D3 is cut away, so U10's OVP is the only limit, and the TPS923611 swap is refused.";
constexpr int QUESTION_OPEN_D3 = 3;
constexpr const char* OPEN_D3_ANSWERS[] = {"No, or not sure: cancel", "Yes: D3 is an SMAJ33A"};
constexpr char OPEN_D3_HEADING[] = "Is D3 an SMAJ33A?";
constexpr char OPEN_D3_BODY[] =
    "The TPS923611's OVP (29.6-31.4 V) overlaps the stock SMAJ26A's 28.9 V breakdown, so the driver could push "
    "current into D3. Read the marking on D3 (93.4, 58.8) first.";
constexpr int QUESTION_OPEN_EMPTY = 4;
constexpr const char* OPEN_EMPTY_ANSWERS[] = {"Cancel", "J3 and J6 are both empty"};
constexpr const char* OPEN_EMPTY_ANSWERS_CUT[] = {"Cancel", "J3 is empty"};
constexpr char OPEN_EMPTY_HEADING[] = "Nothing on J3 or J6?";
constexpr char OPEN_EMPTY_BODY[] =
    "Unplug the light's flex from J3 and leave J6 empty. Hands off TP3 and J3: LED_SW reaches about 25 V. After "
    "the step, wait at least 10 s before plugging a light in.";
constexpr char OPEN_ADVICE[] =
    "EXCEPTION: step 0 lights the boost with NO load. Wait at least 10 s after it before plugging a light into J3 "
    "or J6: C9 is left charged near 25 V.";

constexpr char INTRO[] =
    "Confirm, with a light connected: the cool string at 50 %, then the trip level lowered in RAM below the lit "
    "reading. The monitor must switch the light off within 300 ms and keep it off; switched on again, the probe "
    "must find no load; at the profile's level it lights again. About 5 s. Left: step 0, an opt-in open load with "
    "J3 and J6 empty. Back stops.";
constexpr char NOT_BUILT[] =
    "No front light with an LED monitor in this build or on this board (frontlight_flags): nothing to test.";

enum class Phase : uint8_t {
  Idle,
  Baseline,
  Lowered,
  Watch,
  Restore,
  Restored,
  Asking,
  OpenDeciding,  // step 0: switched on with no load, waiting for the SDK's trip
  OpenWatch,     // step 0: decided, must stay dark
  OpenCooling,   // step 0: C9 discharging; LED_SW read at the end
};

bool openPhase(const Phase phase) {
  return phase == Phase::OpenDeciding || phase == Phase::OpenWatch || phase == Phase::OpenCooling;
}

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
    case Phase::Restore:
    case Phase::Restored:
      return "profile's level back: must stay lit";
    case Phase::Asking:
      return "done";
    case Phase::OpenDeciding:
      return "step 0: on with no load, waiting for the trip";
    case Phase::OpenWatch:
      return "step 0: tripped, must stay dark 2 s";
    case Phase::OpenCooling:
      return "step 0: C9 discharging, wait 10 s";
  }
  return "?";
}

// Step 0's run: the first trip of the switch-on, how it ended, the watch after it.
struct OpenLoad {
  bool swapDriver = false;  // the TPS923611, with the operator's word that D3 is an SMAJ33A
  light::Reading dark;
  unsigned long enabledAt = 0;
  uint32_t probes0 = 0;
  uint32_t probeTrips0 = 0;
  uint32_t monitorTrips0 = 0;
  uint32_t railTimeouts0 = 0;
  // the first trip, from the SDK's records
  bool tripSeen = false;
  bool byMonitor = false;      // the 100 ms monitor's in-use trip, after the probe window
  bool firstRecord = true;     // the probe record polled was the first string's
  int32_t tripMs = -1;         // setOn() to the trip, by the SDK's millis() stamp
  uint32_t afterUs = 0;        // probe: carrier start to the reading above the trip
  uint32_t tripLedMv = 0;      // that reading
  uint32_t darkAfterUs = 0;    // that reading to IO42 held LOW (the SDK's record)
  bool io42LowAtTrip = false;  // IO42 held LOW by the GPIO register when the trip was seen
  // the switch-on's end
  uint32_t strings = 0;  // probe trips in the switch-on: the strings tried
  freeink::frontlight::Load load = freeink::frontlight::Load::Unknown;
  bool railTimeout = false;
  unsigned long decidedAt = 0;
  unsigned long lastTripAt = 0;  // millis() of the last trip (or the switch-off), for the 10 s
  uint32_t probesAtDecision = 0;
  uint32_t monitorTripsAtDecision = 0;
  // the watch and the cool-down
  light::Reading watchStart;
  light::Reading watchEnd;
  bool io42LowThroughout = true;
  bool relit = false;
  bool after10sTaken = false;
  light::Reading after10s;
  char fails[96] = "";
};

struct Trip {
  bool seen = false;
  uint32_t latencyMs = 0;  // level lowered to the trip, by the SDK's clock
  uint32_t ledMv = 0;
  uint32_t limitMv = 0;
  bool adimLow = false;    // IO42 held LOW by the GPIO output register when the trip was seen
  bool asNoLight = false;  // recorded as a switch-on's "no load" instead of an in-use trip
};

// The switch-on probe at the lowered level (the operator's retry).
struct ProbeTrip {
  bool seen = false;
  uint32_t afterUs = 0;      // the carrier start to the reading above the level
  uint32_t darkAfterUs = 0;  // that reading to IO42 held LOW by the probe (the SDK's record)
  uint32_t peakMv = 0;       // that reading
  uint32_t strings = 0;      // strings the switch-on tried (the other one follows the first)
  bool noLight = false;      // it ended with load absent and the light off
  bool adimLow = false;      // IO42 held LOW afterwards (the end state, after the switch-off)
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

  // A run is bounded (about 5 s, step 0 about 15 s) and gets no input.
  bool preventAutoSleep() override { return phase != Phase::Idle && phase != Phase::Asking; }

  const char* slotLabel(const Slot slot) const override {
    if (!present) return slot == Slot::Confirm ? "Record" : nullptr;
    if (phase != Phase::Idle) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Run";
      case Slot::Left:
        return "Step 0";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (!present) {
      if (slot == Slot::Confirm) record(Verdict::Skip, "light=not_built");
      return;
    }
    if (phase != Phase::Idle) return;
    if (slot == Slot::Confirm) {
      showOpen = false;
      confirmLightLoad();
    } else if (slot == Slot::Left) {
      ask(QUESTION_OPEN_BUILD, OPEN_BUILD_HEADING, options::cutDown() ? OPEN_BUILD_BODY_CUT : OPEN_BUILD_BODY,
          OPEN_BUILD_ANSWERS, 3);
    }
  }

  bool onBack() override {
    if (phase == Phase::Idle || phase == Phase::Asking) return false;
    LOG_INF("TEST", "T10: run stopped in phase %s", phaseName(phase));
    if (phase == Phase::OpenCooling) {
      // The checks are done: record them without the 10 s reading.
      recordOpenLoad();
      return true;
    }
    endRun();
    requestUpdate();
    return true;
  }

  void onLightLoadConfirmed(const LightLoad chosen) override {
    load = chosen;
    startRun();
  }

  void onAnswer(const int question, const int answer) override {
    switch (question) {
      case QUESTION_SEEN:
        recordRun(answer);
        return;
      case QUESTION_OPEN_BUILD:
        // Cancel and Back record nothing.
        if (answer == 1) {
          openRun = OpenLoad{};
          askOpenEmpty();
        } else if (answer == 2 && options::cutDown()) {
          record(Verdict::Skip, "step=0 reason=swap_on_cut_down (no D3 on a cut-down board: TPS923611 refused)");
        } else if (answer == 2) {
          ask(QUESTION_OPEN_D3, OPEN_D3_HEADING, OPEN_D3_BODY, OPEN_D3_ANSWERS, 2);
        }
        return;
      case QUESTION_OPEN_D3:
        if (answer == 1) {
          openRun = OpenLoad{};
          openRun.swapDriver = true;
          askOpenEmpty();
        } else if (answer == 0) {
          record(Verdict::Skip, "step=0 reason=swap_without_smaj33a (TPS923611 refused: D3 not an SMAJ33A)");
        }
        return;
      case QUESTION_OPEN_EMPTY:
        if (answer == 1) startOpenLoad();
        return;
      default:
        return;
    }
  }

  void askOpenEmpty() {
    const bool cutDown = options::cutDown();
    ask(QUESTION_OPEN_EMPTY, OPEN_EMPTY_HEADING, OPEN_EMPTY_BODY, cutDown ? OPEN_EMPTY_ANSWERS_CUT : OPEN_EMPTY_ANSWERS,
        2);
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
    probeTrips0 = light::probe().trips;
    run.dark = light::readMonitor();
    if (run.dark.pinMv < light::DARK_MIN_PIN_MV) {
      record(Verdict::Fail, "dark_pin=%u fails=dark_blind (LED_MONIT reads nothing: the trip is blind; not lit)",
             static_cast<unsigned>(run.dark.pinMv));
      return;
    }
    Frontlight.setWarmth(0);
    Frontlight.setBrightness(RUN_BRIGHTNESS);
    Frontlight.setOn(true);  // the switch-on probe runs on its timer, at the profile's level
    if (!light::awaitSwitchOn()) {
      failRun("probe_undecided");
      return;
    }
    if (!Frontlight.isOn()) {
      failRun(Frontlight.noLightFound() ? "no_light" : "not_lit");
      return;
    }
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
    trip.asNoLight = Frontlight.noLightFound();
    return trip;
  }

  // The operator's retry with the level still lowered: the SDK's switch-on probe
  // runs on its timer once setOn() returns, and must take the lit load for absent.
  ProbeTrip retryAtLoweredLevel() {
    const uint32_t before = light::probe().trips;
    Frontlight.setOn(true);
    light::awaitSwitchOn();  // undecided: the checks below fail it
    const auto probe = light::probe();
    ProbeTrip result;
    result.seen = probe.trips != before && probe.lastTripped;
    result.afterUs = probe.lastTripAfterUs;
    result.darkAfterUs = probe.lastDarkAfterTripUs;
    result.peakMv = probe.lastPeakLedMv;
    result.strings = probe.trips - before;
    result.noLight = !Frontlight.isOn() && Frontlight.noLightFound();
    result.adimLow = light::padHeldAt(light::readPad(BoardConfig::ACTIVE.frontlight.gpio),
                                      !BoardConfig::ACTIVE.frontlight.activeHigh);
    LOG_INF("TEST", "T10: retry at the lowered level: %u string(s) tripped the probe, last %u mV after %u us, %s",
            static_cast<unsigned>(result.strings), static_cast<unsigned>(result.peakMv),
            static_cast<unsigned>(result.afterUs), result.noLight ? "no light, off" : "still lit");
    return result;
  }

  void onTick() override {
    if (phase == Phase::Idle || phase == Phase::Asking) return;
    if (openPhase(phase)) {
      openTick();
      return;
    }
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
        run.second = retryAtLoweredLevel();
        if (!run.second.seen || !run.second.noLight) {
          failRun("no_probe_trip");
          return;
        }
        light::restoreTripMv();
        setPhase(Phase::Restore);
        return;
      case Phase::Restore:
        if (inPhase < RESTORE_GAP_MS) return;
        Frontlight.setOn(true);  // probed again, now at the profile's level
        if (!light::awaitSwitchOn()) {
          failRun("probe_undecided");
          return;
        }
        if (!Frontlight.isOn()) {
          failRun(Frontlight.noLightFound() ? "no_light_after_restore" : "not_lit_after_restore");
          return;
        }
        setPhase(Phase::Restored);
        return;
      case Phase::Restored:
        // The switch-on probe's trips are not in-use trips: the monitor still counts one.
        if (trips != 1 || !Frontlight.isOn()) {
          failRun("trip_after_restore");
          return;
        }
        if (inPhase < RESTORED_MS) return;
        run.restoredLedMv = light::monitor().lastLedMv;
        run.restoredLoad = light::probe().record.load;
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
      if (run.first.asNoLight) addFailure(fails, sizeof(fails), "trip_as_no_light");
      if (!run.stayedDark) addFailure(fails, sizeof(fails), "not_dark");
      if (run.darkAfter.pinMv > light::DARK_MAX_PIN_MV) addFailure(fails, sizeof(fails), "dark_after_high");
    }
    if (run.second.seen && run.second.darkAfterUs > PROBE_DARK_WITHIN_US) {
      addFailure(fails, sizeof(fails), "probe_io42_slow");
    }
    if (run.second.seen && !run.second.adimLow) addFailure(fails, sizeof(fails), "probe_io42_not_low");
    if (run.restoredOk && run.restoredLoad != freeink::frontlight::Load::Present) {
      addFailure(fails, sizeof(fails), "restored_not_present");
    }
    if (BoardConfig::ACTIVE.frontlight.monitorTripMv != light::profileTripMv()) {
      addFailure(fails, sizeof(fails), "level_not_restored");
    }
    if (answer == 1) addFailure(fails, sizeof(fails), "operator");
    const bool complete = run.restoredOk;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : complete && answer == 0 ? Verdict::Pass : Verdict::Info;
    monitorNow = light::monitor();
    const auto probe = light::probe();
    record(verdict,
           "load=%s dark_pin=%u lit_led=%u trial=%u trip1=%ums@%u io42_low=%d dark_after_pin=%u stayed_dark=%d "
           "probe2=%uus@%u probe2_dark=%uus probe2_strings=%u probe2_load=%s restored=%d restored_load=%s "
           "restored_led=%u level=%u "
           "trips=%u probe_trips=%u seen=%s fails=%s",
           load == LightLoad::J3Panel ? "J3" : "J6", static_cast<unsigned>(run.dark.pinMv),
           static_cast<unsigned>(run.litLedMv), static_cast<unsigned>(run.trialMv),
           static_cast<unsigned>(run.first.latencyMs), static_cast<unsigned>(run.first.ledMv),
           run.first.adimLow ? 1 : 0, static_cast<unsigned>(run.darkAfter.pinMv), run.stayedDark ? 1 : 0,
           static_cast<unsigned>(run.second.afterUs), static_cast<unsigned>(run.second.peakMv),
           static_cast<unsigned>(run.second.darkAfterUs), static_cast<unsigned>(run.second.strings),
           run.second.noLight ? "absent" : "-", run.restoredOk ? 1 : 0, light::loadToken(run.restoredLoad),
           static_cast<unsigned>(run.restoredLedMv), BoardConfig::ACTIVE.frontlight.monitorTripMv,
           static_cast<unsigned>(monitorNow.trips - trips0), static_cast<unsigned>(probe.trips - probeTrips0),
           answer == 0   ? "yes"
           : answer == 1 ? "no"
                         : "none",
           fails[0] != '\0' ? fails : "none");
  }

  // --- step 0: a real open load (BRING_UP section 7.4) ------------------------

  // The operator named the build and confirmed J3 and J6 empty. Starts from a
  // valid dark reading and switches on once, through the reader's own path (the
  // load probe, the other string after the rail wait).
  void startOpenLoad() {
    showOpen = true;
    light::restoreTripMv();  // the profile's level: never a lowered one here
    if (Frontlight.isOn()) Frontlight.setOn(false);
    openRun.dark = light::readMonitor();
    if (openRun.dark.pinMv < light::DARK_MIN_PIN_MV) {
      record(Verdict::Fail, "step=0 dark_pin=%u fails=dark_blind (LED_MONIT reads nothing: the trip is blind; not lit)",
             static_cast<unsigned>(openRun.dark.pinMv));
      return;
    }
    if (openRun.dark.pinMv > light::DARK_MAX_PIN_MV) {
      record(Verdict::Skip, "step=0 reason=not_dark dark_pin=%u (LED_SW still charged: wait 10 s, then again)",
             static_cast<unsigned>(openRun.dark.pinMv));
      return;
    }
    const auto probe = light::probe();
    openRun.probes0 = probe.probes;
    openRun.probeTrips0 = probe.trips;
    openRun.railTimeouts0 = probe.railTimeouts;
    openRun.monitorTrips0 = light::monitor().trips;
    LOG_INF("TEST", "T10 step 0: switching on at %u %% with no load (%s), opt-in", OPEN_BRIGHTNESS,
            openRun.swapDriver ? "TPS923611, D3 SMAJ33A" : "TPS923610, D3 SMAJ26A");
    Frontlight.setWarmth(0);
    Frontlight.setBrightness(OPEN_BRIGHTNESS);
    openRun.enabledAt = millis();
    Frontlight.setOn(true);  // the load probe decides on the SDK's monitor timer
    setPhase(Phase::OpenDeciding);
  }

  void openTick() {
    switch (phase) {
      case Phase::OpenDeciding:
        openDecidingTick();
        return;
      case Phase::OpenWatch:
        openWatchTick();
        return;
      case Phase::OpenCooling:
        openCoolingTick();
        return;
      default:
        return;
    }
  }

  // The first trip of the switch-on, from the SDK's records: the probe's (the
  // expected one), or the in-use monitor's after the probe window. Polled every
  // loop pass: the other string's probe follows only after the rail wait.
  void noteOpenTrip() {
    const auto probe = light::probe();
    const auto monitor = light::monitor();
    uint32_t stampMs = 0;
    if (probe.trips != openRun.probeTrips0) {
      openRun.byMonitor = false;
      openRun.firstRecord = probe.trips - openRun.probeTrips0 == 1;
      stampMs = probe.lastMs;
      openRun.afterUs = probe.lastTripAfterUs;
      openRun.tripLedMv = probe.lastPeakLedMv;
      openRun.darkAfterUs = probe.lastDarkAfterTripUs;
    } else if (monitor.trips != openRun.monitorTrips0) {
      openRun.byMonitor = true;
      stampMs = monitor.lastTripMs;
      openRun.tripLedMv = monitor.lastTripLedMv;
    } else {
      return;
    }
    openRun.tripSeen = true;
    const int32_t ms = static_cast<int32_t>(stampMs - static_cast<uint32_t>(openRun.enabledAt));
    openRun.tripMs = ms > 0 ? ms : 0;
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    openRun.io42LowAtTrip = light::padHeldAt(light::readPad(fl.gpio), !fl.activeHigh);
    LOG_INF("TEST", "T10 step 0: %s trip %ld ms after the switch-on at %u mV, IO42 %s",
            openRun.byMonitor ? "monitor" : "probe", static_cast<long>(openRun.tripMs),
            static_cast<unsigned>(openRun.tripLedMv), openRun.io42LowAtTrip ? "LOW" : "not held LOW");
  }

  void openDecidingTick() {
    const unsigned long since = millis() - openRun.enabledAt;
    if (!openRun.tripSeen) noteOpenTrip();
    if (Frontlight.switchOnPending()) {
      if (since > light::switchOnBoundMs()) {
        addFailure(openRun.fails, sizeof(openRun.fails), "probe_undecided");
        openSwitchedOff();
      } else if (!openRun.tripSeen && since > OPEN_TRIP_TIMEOUT_MS) {
        openNoTrip();
      }
      return;
    }
    if (!openRun.tripSeen) {
      // The probe passed: the 100 ms monitor watches on until the timeout.
      if (since <= OPEN_TRIP_TIMEOUT_MS) return;
      openNoTrip();
      return;
    }
    if (Frontlight.isOn()) {
      // The other string lit: something is on J3 or J6.
      LOG_ERR("TEST", "T10 step 0: a string lit after the first tripped; switching off");
      addFailure(openRun.fails, sizeof(openRun.fails), "lit");
    }
    openSwitchedOff();
  }

  // No trip within OPEN_TRIP_TIMEOUT_MS: neither the probe nor the monitor saw
  // LED_SW above the trip, so U10 latched off by itself (or a load answered).
  void openNoTrip() {
    LOG_ERR("TEST", "T10 step 0: no trip within %lu ms: the monitor missed the open load; switching off",
            OPEN_TRIP_TIMEOUT_MS);
    addFailure(openRun.fails, sizeof(openRun.fails), "no_trip");
    openSwitchedOff();
  }

  // The switch-on has ended, or is ended now: off (a no-op when the HAL already
  // left it off after a no-light result), and the watch starts.
  void openSwitchedOff() {
    Frontlight.setOn(false);
    const auto probe = light::probe();
    const auto monitor = light::monitor();
    openRun.strings = probe.trips - openRun.probeTrips0;
    openRun.load = probe.record.load;
    openRun.railTimeout = probe.railTimeouts != openRun.railTimeouts0;
    openRun.decidedAt = millis();
    // The 10 s run from the last time the boost output was driven up.
    openRun.lastTripAt = openRun.decidedAt;
    if (openRun.strings != 0 && probe.lastTripped) {
      const int32_t ago = static_cast<int32_t>(static_cast<uint32_t>(openRun.decidedAt) - probe.lastMs);
      if (ago > 0) openRun.lastTripAt = openRun.decidedAt - static_cast<unsigned long>(ago);
    }
    openRun.probesAtDecision = probe.probes;
    openRun.monitorTripsAtDecision = monitor.trips;
    openRun.watchStart = light::readMonitor(4);
    setPhase(Phase::OpenWatch);
  }

  // Dark for OPEN_WATCH_MS: IO42 held LOW, no new enable, LED_MONIT not rising.
  void openWatchTick() {
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    if (!light::padHeldAt(light::readPad(fl.gpio), !fl.activeHigh)) openRun.io42LowThroughout = false;
    if (Frontlight.isOn() || light::probe().probes != openRun.probesAtDecision ||
        light::monitor().trips != openRun.monitorTripsAtDecision) {
      openRun.relit = true;
    }
    if (millis() - phaseAt < OPEN_WATCH_MS) return;
    openRun.watchEnd = light::readMonitor(4);
    if (openRun.watchEnd.pinMv > openRun.watchStart.pinMv + OPEN_RISE_PIN_MV) openRun.relit = true;
    setPhase(Phase::OpenCooling);
  }

  void openCoolingTick() {
    const unsigned long now = millis();
    if (now - openRun.lastTripAt < OPEN_COOL_MS) {
      if (now - drawnAt >= OPEN_REDRAW_MS) {
        drawnAt = now;
        requestUpdate();
      }
      return;
    }
    openRun.after10s = light::readMonitor();
    openRun.after10sTaken = true;
    recordOpenLoad();
  }

  void recordOpenLoad() {
    phase = Phase::Idle;
    char fails[96];
    snprintf(fails, sizeof(fails), "%s", openRun.fails);
    if (openRun.tripSeen) {
      if (openRun.tripMs > static_cast<int32_t>(OPEN_TRIP_WITHIN_MS)) addFailure(fails, sizeof(fails), "trip_slow");
      if (!openRun.io42LowAtTrip) addFailure(fails, sizeof(fails), "io42_not_low");
    }
    if (!openRun.io42LowThroughout) addFailure(fails, sizeof(fails), "io42_released");
    if (openRun.relit) addFailure(fails, sizeof(fails), "relit");
    char tripMs[12] = "-";
    if (openRun.tripSeen) snprintf(tripMs, sizeof(tripMs), "%ld", static_cast<long>(openRun.tripMs));
    char led10[12] = "stopped";
    if (openRun.after10sTaken) snprintf(led10, sizeof(led10), "%u", static_cast<unsigned>(openRun.after10s.ledMv));
    monitorNow = light::monitor();
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "step=0 build=%s cut=%d dark_pin=%u trip=%s trip_ms=%s after_us=%u trip_led=%u dark_after_us=%u "
           "io42_low=%d strings=%u load=%s rail_timeout=%d watch_pin=%u-%u relit=%d led_10s=%s fails=%s",
           openRun.swapDriver ? "tps923611_smaj33a" : "tps923610", options::cutDown() ? 1 : 0,
           static_cast<unsigned>(openRun.dark.pinMv),
           !openRun.tripSeen     ? "none"
           : openRun.byMonitor   ? "monitor"
           : openRun.firstRecord ? "probe"
                                 : "probe_late_poll",
           tripMs, static_cast<unsigned>(openRun.afterUs), static_cast<unsigned>(openRun.tripLedMv),
           static_cast<unsigned>(openRun.darkAfterUs), openRun.io42LowAtTrip ? 1 : 0,
           static_cast<unsigned>(openRun.strings), light::loadToken(openRun.load), openRun.railTimeout ? 1 : 0,
           static_cast<unsigned>(openRun.watchStart.pinMv), static_cast<unsigned>(openRun.watchEnd.pinMv),
           openRun.relit ? 1 : 0, led10, fails[0] != '\0' ? fails : "none");
  }

  void drawOpenLoad(TestPage& page) const {
    page.paragraph(OPEN_ADVICE, 3);
    page.gap(4);
    page.setCompact(true);
    page.row("Step 0", phaseName(phase));
    page.row("Driver", openRun.swapDriver ? "TPS923611, D3 SMAJ33A" : "TPS923610, D3 SMAJ26A");
    if (openRun.dark.pinMv != 0) page.rowf("Dark, before", "%u mV pin", static_cast<unsigned>(openRun.dark.pinMv));
    if (openRun.tripSeen) {
      page.rowf("Trip (within 500 ms)", "%s, %ld ms, %u mV%s", openRun.byMonitor ? "monitor" : "probe",
                static_cast<long>(openRun.tripMs), static_cast<unsigned>(openRun.tripLedMv),
                openRun.tripMs > static_cast<int32_t>(OPEN_TRIP_WITHIN_MS) ? " SLOW" : "");
      page.row("IO42 at the trip", openRun.io42LowAtTrip ? "LOW (GPIO)" : "NOT held LOW");
    } else if (phase != Phase::OpenDeciding) {
      page.row("Trip", "NONE within 1 s: the monitor missed the open load; U10 latched off by itself");
    }
    if (phase == Phase::OpenWatch || phase == Phase::OpenCooling || openRun.after10sTaken) {
      page.rowf("Strings tried / load", "%u / %s", static_cast<unsigned>(openRun.strings),
                light::loadToken(openRun.load));
    }
    if (phase == Phase::OpenCooling || openRun.after10sTaken) {
      page.rowf("Dark 2 s, IO42 LOW", "%s, %s", openRun.relit ? "RELIT" : "yes",
                openRun.io42LowThroughout ? "yes" : "RELEASED");
    }
    if (openRun.after10sTaken) {
      page.rowf("LED_SW 10 s after the trip", "%u.%02u V (about 7 V or less)",
                static_cast<unsigned>(openRun.after10s.ledMv / 1000U),
                static_cast<unsigned>(openRun.after10s.ledMv % 1000U / 10U));
    } else if (phase == Phase::OpenCooling) {
      const unsigned long elapsed = millis() - openRun.lastTripAt;
      const unsigned long left = elapsed < OPEN_COOL_MS ? (OPEN_COOL_MS - elapsed + 999) / 1000 : 0;
      page.rowf("LED_SW 10 s after the trip", "in %lu s", left);
    }
    page.setCompact(false);
  }

  void drawProbeTrip(TestPage& page, const char* label, const ProbeTrip& trip) const {
    if (!trip.seen) {
      page.row(label, "-");
      return;
    }
    page.rowf(label, "%u us at %u mV, %u string(s), %s", static_cast<unsigned>(trip.afterUs),
              static_cast<unsigned>(trip.peakMv), static_cast<unsigned>(trip.strings),
              trip.noLight ? "no load, off" : "STILL LIT");
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
    page.paragraph(INTRO, 7);
    page.gap(4);
    if (showOpen) {
      drawOpenLoad(page);
      return;
    }
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
    drawProbeTrip(page, "Switched on again: probe", run.second);
    if (run.second.seen) {
      page.rowf("IO42 LOW after that reading", "%u us%s", static_cast<unsigned>(run.second.darkAfterUs),
                run.second.darkAfterUs > PROBE_DARK_WITHIN_US ? " SLOW" : "");
    }
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
    ProbeTrip second;
    freeink::frontlight::Load restoredLoad = freeink::frontlight::Load::Unknown;
    bool stayedDark = true;
    bool restoredOk = false;
    uint32_t restoredLedMv = 0;
  };

  bool present = false;
  LightLoad load = LightLoad::J3Panel;
  Phase phase = Phase::Idle;
  unsigned long phaseAt = 0;
  uint32_t trips0 = 0;
  uint32_t probeTrips0 = 0;
  RunResult run;
  FrontlightManager::MonitorDiagnostics monitorNow;  // as of the last phase change or record
  char fails[96] = "";
  OpenLoad openRun;       // step 0
  bool showOpen = false;  // the body shows step 0 (it ran last)
  unsigned long drawnAt = 0;
};

}  // namespace

std::unique_ptr<Activity> createLightTripTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<LightTripTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
