#if CROSSPOINT_TEST_SUITE
// T9: front light (bring-up B8). The boost lights only after the operator names
// the LED load, at every Confirm that lights it. Both strings step through
// 10/25/50/75/100% brightness while LED_MONIT (IO2) is read through freeink::adc and
// the SDK's monitor record is watched for trips: the trip handler in the main
// loop switches the light off before this screen could see fault(), so a trip
// shows here as the light dropping out and the record's count rising. Every
// switch-on and string change also runs the SDK's load probe, on its timer: the
// screen waits for it to decide, then shows and records its result (a load or
// none, and which strings answered). The record carries each string's LED_SW at
// maximum and one tenth of its duty and their difference (dVf, BRING_UP section 7.2).
// Where the SDK blends (FrontlightConfig::colorBlendHz), Left also offers Mix: a
// 50 % warmth on COLOR_SEL's blend carrier, read through the same steps.
// While lit, the switch run (BRING_UP section 7.3) repeats the reader's colour
// change at 100%, 66% and 25% for a scope on TP3 and COLOR_SEL, and
// fails on any trip or a light that goes out.
#include <Arduino.h>
#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "LightCheck.h"
#include "TestFactories.h"
#include "TextBuffer.h"
#include "activities/testsuite/BoardOptions.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr uint8_t STEPS[] = {10, 25, 50, 75, 100};
constexpr int STEP_COUNT = static_cast<int>(sizeof(STEPS) / sizeof(STEPS[0]));
constexpr unsigned long SAMPLE_MS = 1000;
constexpr unsigned long REDRAW_MS = 3000;
constexpr unsigned long EARLY_CHECK_MS[] = {50, 100, 200};
constexpr uint32_t J6_MIN_RISE_MV = 1000;
// dVf at the capped maximum and one tenth of that output.
constexpr int STEP_FULL = 4;
constexpr int STEP_TENTH_DUTY = 1;
static_assert(STEPS[STEP_FULL] == 100 && STEPS[STEP_TENTH_DUTY] == 25, "dVf uses 100% and 25%");
constexpr uint8_t SWITCH_BRIGHTNESS[] = {100, 66, 25};
constexpr uint16_t SWITCH_DUTY[] = {static_cast<uint16_t>(freeink::frontlight::mappedDuty(
                                        100, 1023, BoardConfig::SILKSCREEN.frontlight.maximumLegacyPercent)),
                                    static_cast<uint16_t>(freeink::frontlight::mappedDuty(
                                        66, 1023, BoardConfig::SILKSCREEN.frontlight.maximumLegacyPercent)),
                                    static_cast<uint16_t>(freeink::frontlight::mappedDuty(
                                        25, 1023, BoardConfig::SILKSCREEN.frontlight.maximumLegacyPercent))};
constexpr int SWITCH_DUTIES = static_cast<int>(sizeof(SWITCH_BRIGHTNESS));
constexpr int SWITCHES_PER_DUTY = 10;
// One switch a second: the probe window (500 ms) and a settled reading fit in it.
constexpr unsigned long SWITCH_PERIOD_MS = 1000;
constexpr unsigned long SWITCH_REDRAW_MS = 5000;

constexpr int QUESTION_SEEN = 1;
constexpr const char* SEEN_ANSWERS[] = {"Both, brighter each step", "Only one string lit", "No rise, or flicker",
                                        "Never lit"};
constexpr const char* SEEN_CODES[] = {"both", "one_string", "no_rise_or_flicker", "never_lit"};
constexpr char SEEN_HEADING[] = "What did the light do?";
constexpr char SEEN_BODY[] =
    "Cool and warm through 10/25/50/75/100%: did each string light, get brighter at every step, and stay free of "
    "flicker?";

constexpr int QUESTION_SWITCH = 2;
constexpr const char* SWITCH_ANSWERS[] = {"Clean: re-slew within 350 us, below 23 V", "Spike, slow re-slew or dropout",
                                          "Not scoped"};
constexpr const char* SWITCH_CODES[] = {"clean", "spike_or_slow", "not_scoped"};
constexpr char SWITCH_HEADING[] = "What did the scope show?";
constexpr char SWITCH_BODY[] =
    "CH1 TP3 (LED_SW), CH2 COLOR_SEL (U12.2): at each switch LED_SW re-slews by dVf in about 100-150 us (350 us "
    "at most) and stays below 23 V.";

constexpr char INTRO[] =
    "Confirm lights the boost once you name the load. Up/Down: 10/25/50/75/100%. Left: cool/warm (and mix). "
    "Right, lit: 30 colour switches for a scope. Take both strings through every step, switch off, then Record "
    "(Right).";
constexpr char SCOPE_NOTE[] =
    "Scope (optional): IO42 LOW for at least 3 ms, HIGH for the enable pulse, then the carrier. IO40 (HIGH = warm) "
    "moves only while IO42 is LOW, at least 3 ms after it fell; in Mix it runs its blend carrier while lit.";

// Left cycles these; Mix only where the SDK blends.
enum Colour : int { COOL = 0, WARM = 1, MIX = 2 };
constexpr uint8_t COLOUR_WARMTH[] = {0, 100, 50};
constexpr const char* COLOUR_NAMES[] = {"cool", "warm", "mix"};
constexpr char SWITCH_NOTE[] =
    "Switch run: CH1 TP3 (LED_SW), CH2 COLOR_SEL (U12.2), 10x probes. One colour change a second, 10 at each duty. "
    "Back stops.";
constexpr char BLIND_NOTE[] =
    "LED_MONIT reads about 0 V with the light off, so the over-voltage watch cannot see LED_SW. The light stays off: "
    "check the R39/R41 divider and C31.";
constexpr char NOT_BUILT[] =
    "No front light with an LED monitor in this build or on this board (frontlight_flags): nothing to test.";

class FrontlightTest final : public TestScreen {
 public:
  FrontlightTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    present = light::monitorConfigured();
    openedAt = millis();
    if (!present) return;
    tripsAtOpen = light::monitor().trips;
    probeNow = light::probe();
    probeTripsAtOpen = probeNow.trips;
    Frontlight.setOn(false);
    Frontlight.setWarmth(0);
    Frontlight.setBrightness(STEPS[step]);
  }

  // The switch run is bounded (30 switches, about 35 s) and gets no input.
  bool preventAutoSleep() override { return sw.active; }

  bool onBack() override {
    if (!sw.active) return false;
    LOG_INF("TEST", "T9: switch run stopped after %d switches", sw.total);
    sw.stop = "back";
    endSwitchRun();
    recordSwitchRun(-1);
    return true;
  }

  void onTick() override {
    if (!present) return;
    const unsigned long now = millis();
    // The dark reading waits for LED_MONIT to settle from anything lit before.
    if (!darkTaken && now - openedAt >= light::SETTLE_MS) takeDarkReading();
    if (sw.active) {
      switchRunTick();
      return;
    }
    if (lit && !Frontlight.isOn()) {
      onTripped();
      return;
    }
    if (lit && now - changedAt >= light::SETTLE_MS && now - sampledAt >= SAMPLE_MS) sample();
    if (afterOffPending && now - offAt >= light::SETTLE_MS) {
      afterOff = light::readMonitor();
      afterOffPending = false;
      readPads();
      checkPads(false);
      redraw();
      return;
    }
    if (lit && now - drawnAt >= REDRAW_MS) redraw();
  }

  const char* slotLabel(const Slot slot) const override {
    if (sw.active) return nullptr;  // Back stops the switch run
    switch (slot) {
      case Slot::Confirm:
        if (!present) return "Record";
        return lit ? "Off" : "Light on";
      case Slot::Left:
        if (!present || !Frontlight.hasColourSelect()) return nullptr;
        switch (nextColour()) {
          case WARM:
            return "Warm";
          case MIX:
            return "Mix";
          default:
            return "Cool";
        }
      case Slot::Right:
        // Lit: the switch run, which needs both strings behind a colour select.
        // Dark: Record.
        if (!present) return nullptr;
        if (lit) return Frontlight.hasColourSelect() ? "Switch run" : nullptr;
        return "Record";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (!present) {
      if (slot == Slot::Confirm) record(Verdict::Skip, "light=not_built");
      return;
    }
    switch (slot) {
      case Slot::Confirm:
        if (lit) {
          switchOff();
        } else {
          requestLight();
        }
        break;
      case Slot::Left:
        colour = nextColour();
        // Lit: the probe runs on the new string; Mix blends at once once both strings
        // have lit since the reset, and first lights the one that has not.
        Frontlight.setWarmth(COLOUR_WARMTH[colour]);
        if (lit && !light::awaitSwitchOn()) {
          abortUndecided();
          break;
        }
        changedAt = millis();
        if (lit && checkNoLight()) break;
        redraw();
        break;
      case Slot::Right:
        if (lit) {
          startSwitchRun();
          break;
        }
        // The question is about what the operator already saw, dark, with the
        // after-off reading taken now.
        completeAfterOff();
        if (!everLit) {
          recordResult(-1);
        } else {
          ask(QUESTION_SEEN, SEEN_HEADING, SEEN_BODY, SEEN_ANSWERS, 4);
        }
        break;
      default:
        break;
    }
  }

  void onStep(const int direction) override {
    if (!present || sw.active) return;
    const int next = step + direction;
    if (next < 0 || next >= STEP_COUNT) return;
    step = next;
    Frontlight.setBrightness(STEPS[step]);
    changedAt = millis();
    redraw();
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_SEEN && answer >= 0) recordResult(answer);
    if (question == QUESTION_SWITCH) recordSwitchRun(answer);
  }

  void onLightLoadConfirmed(const LightLoad chosen) override {
    load = chosen;
    loadKnown = true;
    lightUp();
  }

  void redraw() {
    drawnAt = millis();
    monitorNow = light::monitor();
    probeNow = light::probe();
    requestUpdate();
  }

  void takeDarkReading() {
    const unsigned long since = millis() - openedAt;
    if (since < light::SETTLE_MS) delay(light::SETTLE_MS - since);
    dark = light::readMonitor();
    darkTaken = true;
    readPads();
    checkPads(false);
    redraw();
  }

  void requestLight() {
    if (!darkTaken) takeDarkReading();
    if (dark.pinMv < light::DARK_MIN_PIN_MV) {
      // Without LED_SW on the monitor pin the software trip cannot act.
      LOG_ERR("TEST", "T9: LED_MONIT %u mV with the light off; not lighting", static_cast<unsigned>(dark.pinMv));
      record(Verdict::Fail, "dark_pin=%u fails=dark_blind (LED_MONIT reads nothing: the trip is blind; not lit)",
             static_cast<unsigned>(dark.pinMv));
      return;
    }
    confirmLightLoad();
  }

  // Cool, warm, then Mix where the SDK blends.
  int nextColour() const {
    if (colour == COOL) return WARM;
    if (colour == WARM && light::blends()) return MIX;
    return COOL;
  }

  // The column a lit reading belongs to: the blend, or the string the SDK lit.
  static int litColour() {
    if (light::blending()) return MIX;
    return light::selectsWarm() ? WARM : COOL;
  }

  void lightUp() {
    Frontlight.setBrightness(STEPS[step]);
    Frontlight.setWarmth(COLOUR_WARMTH[colour]);
    Frontlight.setOn(true);  // the SDK's load probe runs on its timer
    const bool decided = light::awaitSwitchOn();
    lit = true;
    if (checkNoLight()) return;
    if (!decided) {
      abortUndecided();
      return;
    }
    const unsigned long start = millis();
    everLit = true;
    changedAt = start;
    for (const unsigned long at : EARLY_CHECK_MS) {
      const unsigned long elapsed = millis() - start;
      if (elapsed < at) delay(at - elapsed);
      if (!Frontlight.isOn()) break;  // the monitor tripped; onTick() reports it
      const light::Reading reading = light::readMonitor(4);
      if (reading.ledMv > light::LIT_MAX_LED_MV) {
        abortOverVoltage(reading, millis() - start);
        return;
      }
    }
    LOG_INF("TEST", "T9: lit %s at %u %% (asked %s)", COLOUR_NAMES[litColour()], STEPS[step], COLOUR_NAMES[colour]);
    redraw();
  }

  // The switch-on probe found no load on any string it tried, and the HAL left the
  // light off. FAIL: the operator named a load.
  bool checkNoLight() {
    if (Frontlight.startupBlocked()) {
      lit = false;
      startupFailed = true;
      offAt = millis();
      afterOffPending = true;
      record(Verdict::Fail, "fails=startup_check (frontlight startup check failed; light off)");
      redraw();
      return true;
    }
    if (Frontlight.isOn() || !Frontlight.noLightFound()) return false;
    lit = false;
    noLight = true;
    offAt = millis();
    afterOffPending = true;
    monitorNow = light::monitor();
    probeNow = light::probe();
    LOG_ERR("TEST", "T9: the switch-on probe found no load (%s string, %u mV)", probeNow.lastWarm ? "warm" : "cool",
            static_cast<unsigned>(probeNow.lastPeakLedMv));
    record(Verdict::Fail,
           "load=%s probe=absent last=%s peak=%u after_us=%u probe_trips=%u fails=no_light (the switch-on probe found "
           "no load on any string; light off)",
           load == LightLoad::J3Panel ? "J3" : "J6", probeNow.lastWarm ? "warm" : "cool",
           static_cast<unsigned>(probeNow.lastPeakLedMv), static_cast<unsigned>(probeNow.lastTripAfterUs),
           static_cast<unsigned>(probeNow.trips - probeTripsAtOpen));
    redraw();
    return true;
  }

  // The switch-on probe had not decided within LightCheck's bound: its timer
  // never ran its window. Off, and FAIL.
  void abortUndecided() {
    Frontlight.setOn(false);
    lit = false;
    offAt = millis();
    afterOffPending = true;
    monitorNow = light::monitor();
    probeNow = light::probe();
    record(Verdict::Fail, "fails=probe_undecided (the switch-on probe did not decide in time; switched off)");
  }

  void abortOverVoltage(const light::Reading& reading, const unsigned long afterMs) {
    Frontlight.setOn(false);
    lit = false;
    overVoltage = true;
    if (reading.ledMv > maxLitLedMv) maxLitLedMv = reading.ledMv;
    offAt = millis();
    afterOffPending = true;
    monitorNow = light::monitor();
    LOG_ERR("TEST", "T9: LED_SW %u mV %lu ms after lighting: open load or miswired J3 suspected; switched off",
            static_cast<unsigned>(reading.ledMv), afterMs);
    record(Verdict::Fail, "led=%u after=%lums fails=over_22v (open load or miswired J3 suspected; switched off)",
           static_cast<unsigned>(reading.ledMv), afterMs);
  }

  void switchOff() {
    Frontlight.setOn(false);
    lit = false;
    offAt = millis();
    afterOffPending = true;
    redraw();
  }

  // The after-off reading onTick() would take, now (blocks at most SETTLE_MS).
  void completeAfterOff() {
    if (!afterOffPending) return;
    const unsigned long since = millis() - offAt;
    if (since < light::SETTLE_MS) delay(light::SETTLE_MS - since);
    afterOff = light::readMonitor();
    afterOffPending = false;
    readPads();
    checkPads(false);
  }

  void onTripped() {
    lit = false;
    tripped = true;
    offAt = millis();
    afterOffPending = true;
    const auto monitor = light::monitor();
    monitorNow = monitor;
    record(Verdict::Fail, "trip led=%u limit=%u trips=%u fails=trip (the LED monitor switched the light off)",
           static_cast<unsigned>(monitor.lastTripLedMv), static_cast<unsigned>(monitor.lastTripLimitMv),
           static_cast<unsigned>(monitor.trips - tripsAtOpen));
  }

  // --- switch run (BRING_UP section 7.3) ---------------------------------------

  void startSwitchRun() {
    if (Frontlight.stringMissing(false) || Frontlight.stringMissing(true)) {
      record(Verdict::Skip, "mode=colour_switch reason=one_string (the probe found a string missing)");
      return;
    }
    sw = SwitchRun{};
    sw.active = true;
    sw.trips0 = light::monitor().trips;
    sw.probeTrips0 = light::probe().trips;
    sw.warmNow = light::selectsWarm();
    // From Mix: the run switches whole strings, so hold the dominant one (lit, no probe).
    if (light::blending()) Frontlight.setWarmth(sw.warmNow ? 100 : 0);
    Frontlight.setBrightness(SWITCH_BRIGHTNESS[0]);  // lit: applies at once, no probe
    // A settled reading at the first duty, then the first switch a period later.
    sw.switchedAt = millis();
    sw.decidedAt = sw.switchedAt;
    sw.readPending = true;
    LOG_INF("TEST", "T9: switch run, %d colour changes at each of %d duties", SWITCHES_PER_DUTY, SWITCH_DUTIES);
    redraw();
  }

  void switchRunTick() {
    const unsigned long now = millis();
    // The main loop switches a tripped light off before this runs, so an in-use
    // trip shows as the monitor's count. A probe trip is a string read above
    // the trip level right after a change.
    if (light::monitor().trips != sw.trips0) {
      failSwitchRun("trip");
      return;
    }
    if (light::probe().trips != sw.probeTrips0) {
      failSwitchRun("probe_trip");
      return;
    }
    if (sw.deciding) {
      if (Frontlight.switchOnPending()) {
        if (now - sw.switchedAt > light::switchOnBoundMs()) failSwitchRun("probe_undecided");
        return;
      }
      sw.deciding = false;
      sw.decidedAt = now;
      sw.readPending = true;
    }
    if (!Frontlight.isOn()) {
      failSwitchRun(Frontlight.startupBlocked() ? "startup_check"
                    : Frontlight.noLightFound() ? "no_light"
                                                : "light_off");
      return;
    }
    if (sw.readPending) {
      if (now - sw.decidedAt < light::SETTLE_MS) return;
      sw.readPending = false;
      current = light::readMonitor(4);
      if (current.ledMv > light::LIT_MAX_LED_MV) {
        failSwitchRun("over_22v");
        return;
      }
      // At the dark rail while the carrier runs: U10 latched off by itself.
      if (current.ledMv < dark.ledMv + J6_MIN_RISE_MV) {
        failSwitchRun("latched_off");
        return;
      }
      if (sw.minLedMv == 0 || current.ledMv < sw.minLedMv) sw.minLedMv = current.ledMv;
      if (current.ledMv > sw.maxLedMv) sw.maxLedMv = current.ledMv;
      sw.vf[sw.duty][light::selectsWarm() ? 1 : 0] = current.ledMv;
    }
    if (now - drawnAt >= SWITCH_REDRAW_MS) redraw();
    if (now - sw.switchedAt < SWITCH_PERIOD_MS) return;
    if (sw.doneAtDuty >= SWITCHES_PER_DUTY) {
      if (sw.duty + 1 >= SWITCH_DUTIES) {
        finishSwitchRun();
        return;
      }
      ++sw.duty;
      sw.doneAtDuty = 0;
      Frontlight.setBrightness(SWITCH_BRIGHTNESS[sw.duty]);
      sw.switchedAt = now;
      sw.decidedAt = now;
      sw.readPending = true;
      return;
    }
    // The reader's colour change while lit: dark, COLOR_SEL, enable, load probe.
    sw.warmNow = !sw.warmNow;
    Frontlight.setWarmth(sw.warmNow ? 100 : 0);
    sw.deciding = true;
    sw.switchedAt = now;
    ++sw.doneAtDuty;
    ++sw.perDuty[sw.duty];
    ++sw.total;
  }

  void finishSwitchRun() {
    sw.stop = "done";
    endSwitchRun();
    if (!ask(QUESTION_SWITCH, SWITCH_HEADING, SWITCH_BODY, SWITCH_ANSWERS, 3)) recordSwitchRun(-1);
  }

  void failSwitchRun(const char* what) {
    addFailure(sw.fails, sizeof(sw.fails), what);
    LOG_ERR("TEST", "T9: switch run failed (%s) after %d switches", what, sw.total);
    sw.stop = "failed";
    endSwitchRun();
    recordSwitchRun(-1);
  }

  // Dark, with the screen's own step and colour back for the next light-up.
  void endSwitchRun() {
    sw.active = false;
    Frontlight.setOn(false);
    lit = false;
    offAt = millis();
    afterOffPending = true;
    Frontlight.setBrightness(STEPS[step]);
    Frontlight.setWarmth(COLOUR_WARMTH[colour]);
    redraw();
  }

  // answer: the operator's (SWITCH_ANSWERS), or -1 when not asked.
  void recordSwitchRun(const int answer) {
    char fails[80];
    snprintf(fails, sizeof(fails), "%s", sw.fails);
    if (answer == 1) addFailure(fails, sizeof(fails), "operator");
    const bool complete = strcmp(sw.stop, "done") == 0;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : complete && answer == 0 ? Verdict::Pass : Verdict::Info;
    char vf[SWITCH_DUTIES][24];
    for (int d = 0; d < SWITCH_DUTIES; ++d) formatPair(vf[d], sizeof(vf[d]), sw.vf[d][0], sw.vf[d][1]);
    const auto monitor = light::monitor();
    const auto probe = light::probe();
    monitorNow = monitor;
    probeNow = probe;
    record(
        verdict,
        "mode=colour_switch load=%s switches=%d at_duty_counts=%u:%d,%u:%d,%u:%d vf_%u=%s vf_%u=%s vf_%u=%s led=%u-%u "
        "trips=%u probe_trips=%u scope=%s stop=%s fails=%s",
        load == LightLoad::J3Panel ? "J3" : "J6", sw.total, SWITCH_DUTY[0], sw.perDuty[0], SWITCH_DUTY[1],
        sw.perDuty[1], SWITCH_DUTY[2], sw.perDuty[2], SWITCH_DUTY[0], vf[0], SWITCH_DUTY[1], vf[1], SWITCH_DUTY[2],
        vf[2], static_cast<unsigned>(sw.minLedMv), static_cast<unsigned>(sw.maxLedMv),
        static_cast<unsigned>(monitor.trips - sw.trips0), static_cast<unsigned>(probe.trips - sw.probeTrips0),
        answer >= 0 ? SWITCH_CODES[answer] : "none", sw.stop, fails[0] != '\0' ? fails : "none");
  }

  // --- dVf (BRING_UP section 7.2) ------------------------------------------------

  // "<cool>/<warm>" in mV, "-" for a value not measured.
  static void formatPair(char* out, const size_t size, const uint32_t first, const uint32_t second) {
    out[0] = '\0';
    appendMv(out, size, first);
    appendf(out, size, "/");
    appendMv(out, size, second);
  }
  static void appendMv(char* out, const size_t size, const uint32_t mv) {
    if (mv != 0) {
      appendf(out, size, "%lu", static_cast<unsigned long>(mv));
    } else {
      appendf(out, size, "-");
    }
  }

  // A settled table reading as LED_SW in mV; 0 when not measured.
  uint32_t tableLedMv(const int colour, const int s) const {
    return table[colour][s] != 0 ? light::ledFromPin(table[colour][s]) : 0;
  }

  // vf: cool/warm at 100% and 25%; dvf: warm - cool at those outputs. LED_SW in mV, "-" where a reading is missing.
  void formatVf(char* vf, const size_t vfSize, char* dvf, const size_t dvfSize) const {
    char cool[24];
    char warmPair[24];
    formatPair(cool, sizeof(cool), tableLedMv(0, STEP_FULL), tableLedMv(0, STEP_TENTH_DUTY));
    formatPair(warmPair, sizeof(warmPair), tableLedMv(1, STEP_FULL), tableLedMv(1, STEP_TENTH_DUTY));
    snprintf(vf, vfSize, "%s,%s", cool, warmPair);
    dvf[0] = '\0';
    for (const int s : {STEP_FULL, STEP_TENTH_DUTY}) {
      if (s != STEP_FULL) appendf(dvf, dvfSize, "/");
      const uint32_t c = tableLedMv(0, s);
      const uint32_t w = tableLedMv(1, s);
      if (c != 0 && w != 0) {
        appendf(dvf, dvfSize, "%ld", static_cast<long>(w) - static_cast<long>(c));
      } else {
        appendf(dvf, dvfSize, "-");
      }
    }
  }

  void sample() {
    sampledAt = millis();
    current = light::readMonitor();
    table[litColour()][step] = current.pinMv;  // the string lit (a missing one changes it), or the blend
    if (current.ledMv > maxLitLedMv) maxLitLedMv = current.ledMv;
    if (current.ledMv > light::LIT_MAX_LED_MV) {
      abortOverVoltage(current, millis() - changedAt);
      return;
    }
    readPads();
    checkPads(true);
  }

  void readPads() {
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    adim = light::readPad(fl.gpio);
    colorSel = light::readPad(fl.colorSelectGpio);
    monit = light::readPad(fl.monitorAdc);
  }

  // Pulls (fl-adim-no-pullup, fl-color-drive, fl-monit-pin) and the pads' drive
  // as the light is: dark, ADIM held inactive and COLOR_SEL at cool; lit, ADIM on
  // its carrier and COLOR_SEL at the chosen string, or on its blend carrier.
  void checkPads(const bool asLit) {
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    if (adim.valid && adim.pullUp) padFails |= PAD_ADIM_PULL;
    if (colorSel.valid && colorSel.pullUp) padFails |= PAD_COLOR_PULL;
    if (monit.valid && (monit.pullUp || monit.pullDown)) padFails |= PAD_MONIT_PULL;
    const bool darkLevel = !fl.activeHigh;
    const bool coolLevel = !fl.colorSelectWarmHigh;
    if (adim.valid && (asLit ? adim.fromGpioOut : !light::padHeldAt(adim, darkLevel))) {
      padFails |= PAD_ADIM_STATE;
    }
    if (asLit && light::blending()) {
      if (!colorSel.valid || !colorSel.outputEnabled || colorSel.fromGpioOut) padFails |= PAD_COLOR_STATE;
    } else if (colorSel.valid && fl.colorSelectGpio != BoardConfig::PIN_UNASSIGNED) {
      // Lit: the string the SDK picked. Dark: cool, unless a trip's charge on the
      // boost output still keeps it where it was (FrontlightConfig::railSafeV).
      const bool tripSeen = tripped || noLight || probeNow.trips != probeTripsAtOpen;
      const bool expectWarm = light::selectsWarm() && (asLit || tripSeen);
      const bool expect = expectWarm ? fl.colorSelectWarmHigh : coolLevel;
      if (!light::padHeldAt(colorSel, expect)) padFails |= PAD_COLOR_STATE;
    }
  }

  bool litInWindow(const uint32_t pinMv) const {
    const uint32_t led = light::ledFromPin(pinMv);
    if (led > light::LIT_MAX_LED_MV) return false;
    if (load == LightLoad::J3Panel) return led >= light::J3_MIN_LED_MV && led <= light::J3_MAX_LED_MV;
    return led >= dark.ledMv + J6_MIN_RISE_MV;
  }

  void formatSteps(char* out, const size_t size, const int colour) const {
    out[0] = '\0';
    for (int i = 0; i < STEP_COUNT; ++i) {
      if (table[colour][i] != 0) {
        appendf(out, size, "%s%u", i == 0 ? "" : "/", static_cast<unsigned>(table[colour][i]));
      } else {
        appendf(out, size, "%s-", i == 0 ? "" : "/");
      }
    }
  }

  bool colourMeasured(const int colour) const {
    for (int i = 0; i < STEP_COUNT; ++i) {
      if (table[colour][i] != 0) return true;
    }
    return false;
  }

  // answer: the operator's (SEEN_ANSWERS), or -1 when the light never ran.
  void recordResult(const int answer) {
    char fails[96] = "";
    if (darkTaken && dark.pinMv < light::DARK_MIN_PIN_MV) addFailure(fails, sizeof(fails), "dark_blind");
    if (darkTaken && dark.pinMv > light::DARK_MAX_PIN_MV) addFailure(fails, sizeof(fails), "dark_high");
    if (overVoltage || maxLitLedMv > light::LIT_MAX_LED_MV) addFailure(fails, sizeof(fails), "over_22v");
    const uint32_t trips = light::monitor().trips - tripsAtOpen;
    if (tripped || trips != 0) addFailure(fails, sizeof(fails), "trip");
    if (startupFailed) addFailure(fails, sizeof(fails), "startup_check");
    bool inWindow = true;
    for (const auto& colour : table) {
      for (const uint32_t mv : colour) {
        if (mv != 0 && !litInWindow(mv)) inWindow = false;
      }
    }
    if (!inWindow) addFailure(fails, sizeof(fails), "lit_range");
    const auto probe = light::probe();
    probeNow = probe;
    if (noLight) addFailure(fails, sizeof(fails), "no_light");
    if (answer >= 0) {
      // A J6 strip may carry one colour, so a string the probe found missing is no
      // failure there. The J3 panel light has both.
      const bool oneColourOk = load == LightLoad::J6Strip;
      const bool coolMissing = probe.record.cool == freeink::frontlight::StringState::Missing;
      const bool warmMissing = probe.record.warm == freeink::frontlight::StringState::Missing;
      if (!colourMeasured(0) && !(oneColourOk && coolMissing)) addFailure(fails, sizeof(fails), "cool_missing");
      if (Frontlight.hasColourSelect() && !colourMeasured(1) && !(oneColourOk && warmMissing)) {
        addFailure(fails, sizeof(fails), "warm_missing");
      }
    }
    if (afterOff.pinMv > light::DARK_MAX_PIN_MV) addFailure(fails, sizeof(fails), "after_off");
    if (padFails & PAD_ADIM_PULL) addFailure(fails, sizeof(fails), "io42_pullup");
    if (padFails & PAD_COLOR_PULL) addFailure(fails, sizeof(fails), "io40_pullup");
    if (padFails & PAD_MONIT_PULL) addFailure(fails, sizeof(fails), "io2_pull");
    if (padFails & PAD_ADIM_STATE) addFailure(fails, sizeof(fails), "io42_state");
    if (padFails & PAD_COLOR_STATE) addFailure(fails, sizeof(fails), "io40_state");
    if (answer > 0) addFailure(fails, sizeof(fails), "operator");
    char cool[64];
    char warmSteps[64];
    formatSteps(cool, sizeof(cool), 0);
    formatSteps(warmSteps, sizeof(warmSteps), 1);
    char mixSteps[64];
    formatSteps(mixSteps, sizeof(mixSteps), MIX);
    char vf[48];
    char dvf[24];
    formatVf(vf, sizeof(vf), dvf, sizeof(dvf));
    const auto monitor = light::monitor();
    monitorNow = monitor;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : answer == 0 ? Verdict::Pass : Verdict::Info;
    record(verdict,
           "load=%s probe=%s cool=%s warm=%s probe_trips=%u probe_peak=%u dark_pin=%u cool_pin=%s warm_pin=%s "
           "mix_pin=%s max_led=%u after_off_pin=%u trips=%u skipped=%u vf=%s dvf=%s seen=%s j6=%s fails=%s",
           !loadKnown                   ? "none"
           : load == LightLoad::J3Panel ? "J3"
                                        : "J6",
           light::loadToken(probe.record.load), light::stringToken(probe.record.cool),
           light::stringToken(probe.record.warm), static_cast<unsigned>(probe.trips - probeTripsAtOpen),
           static_cast<unsigned>(probe.lastPeakLedMv), static_cast<unsigned>(dark.pinMv), cool, warmSteps,
           colourMeasured(MIX) ? mixSteps : "n/a", static_cast<unsigned>(maxLitLedMv),
           static_cast<unsigned>(afterOff.pinMv), static_cast<unsigned>(trips),
           static_cast<unsigned>(monitor.skippedSamples), vf, dvf, answer >= 0 ? SEEN_CODES[answer] : "not_lit",
           options::cutDown() ? "n/a" : "ok", fails[0] != '\0' ? fails : "none");
  }

  static const char* padText(const light::PadState& pad, const bool isAdim) {
    if (!pad.valid) return "n/a";
    if (!pad.fromGpioOut) return isAdim ? "carrier" : "periph";
    return pad.level ? "HIGH" : "LOW";
  }

  void drawBody(TestPage& page) const override {
    if (!present) {
      page.paragraph(NOT_BUILT, 3);
      return;
    }
    if (sw.active) {
      drawSwitchRun(page);
      return;
    }
    page.paragraph(INTRO, 5);
    if (startupFailed) page.paragraph("Frontlight startup check failed; light off.", 1);
    page.gap(4);
    page.setCompact(true);
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    page.row("Load", !loadKnown ? "not named yet" : load == LightLoad::J3Panel ? "J3 panel light" : "J6 LED strip");
    if (options::cutDown()) page.row("J6 LED strip", "N/A: cut-down board (OPT)");
    page.rowf("Light", "%s, %s, %u%% (duty %u/1023)", lit ? "on" : "off", COLOUR_NAMES[lit ? litColour() : colour],
              static_cast<unsigned>(STEPS[step]),
              static_cast<unsigned>(freeink::frontlight::mappedDuty(STEPS[step], 1023, fl.maximumLegacyPercent)));
    const auto& probe = probeNow;
    page.rowf("Switch-on probe found", "load %s; cool %s, warm %s", light::loadToken(probe.record.load),
              light::stringToken(probe.record.cool), light::stringToken(probe.record.warm));
    if (probe.probes != 0) {
      page.rowf("Probe, last", "%s: %s, peak %u mV, %u reads", probe.lastWarm ? "warm" : "cool",
                probe.lastTripped ? "no load" : "lit", static_cast<unsigned>(probe.lastPeakLedMv),
                static_cast<unsigned>(probe.lastSamples));
    }
    if (lit && current.pinMv != 0) {
      page.rowf("LED_MONIT now", "%u mV = %u.%02u V LED_SW", static_cast<unsigned>(current.pinMv),
                static_cast<unsigned>(current.ledMv / 1000U), static_cast<unsigned>(current.ledMv % 1000U / 10U));
    }
    const auto& monitor = monitorNow;
    page.rowf("SDK monitor, last", "%u mV = %u.%02u V (%u read, %u skipped)", static_cast<unsigned>(monitor.lastPinMv),
              static_cast<unsigned>(monitor.lastLedMv / 1000U), static_cast<unsigned>(monitor.lastLedMv % 1000U / 10U),
              static_cast<unsigned>(monitor.samples), static_cast<unsigned>(monitor.skippedSamples));
    if (darkTaken) {
      page.rowf("Dark pin (expected 0.25-0.55 V)", "%u mV", static_cast<unsigned>(dark.pinMv));
    } else {
      page.row("Dark pin (expected 0.25-0.55 V)", "settling");
    }
    if (afterOff.pinMv != 0) page.rowf("After switching off", "%u mV", static_cast<unsigned>(afterOff.pinMv));
    page.rowf("Trips: this screen / boot", "%u / %u", static_cast<unsigned>(monitor.trips - tripsAtOpen),
              static_cast<unsigned>(monitor.trips));
    page.rowf("Probe trips: this screen / boot", "%u / %u", static_cast<unsigned>(probe.trips - probeTripsAtOpen),
              static_cast<unsigned>(probe.trips));
    if (monitor.trips != 0) {
      page.rowf("Last trip", "%u mV > %u mV limit", static_cast<unsigned>(monitor.lastTripLedMv),
                static_cast<unsigned>(monitor.lastTripLimitMv));
    }
    page.rowf("Trip level", "%u mV LED_SW = %u mV pin", fl.monitorTripMv,
              static_cast<unsigned>(light::pinFromLed(fl.monitorTripMv)));
    char steps[64];
    formatSteps(steps, sizeof(steps), 0);
    page.row("Cool 10/25/50/75/100%, pin mV", steps);
    if (Frontlight.hasColourSelect()) {
      formatSteps(steps, sizeof(steps), 1);
      page.row("Warm 10/25/50/75/100%, pin mV", steps);
      if (light::blends() || colourMeasured(MIX)) {
        formatSteps(steps, sizeof(steps), MIX);
        page.row("Mix 10/25/50/75/100%, pin mV", steps);
      }
      char vf[48];
      char dvf[24];
      formatVf(vf, sizeof(vf), dvf, sizeof(dvf));
      page.row("LED_SW mV, C / W at 100 / 25%", vf);
      page.row("dVf mV (W - C) at 100 / 25%", dvf);
    }
    page.rowf("Lit window", "%s", load == LightLoad::J6Strip ? "above dark +1 V, below 22 V" : "11-18 V (about 15 V)");
    page.rowf("IO42 / IO40 / pulls", "%s / %s / %s", padText(adim, true), padText(colorSel, false),
              (padFails & (PAD_ADIM_PULL | PAD_COLOR_PULL | PAD_MONIT_PULL)) != 0 ? "WRONG" : "none");
    page.rowf("Enable pulse / dwell / carrier", "%u us / %u ms / %lu Hz", fl.enablePulseUs, fl.offDwellMs,
              static_cast<unsigned long>(fl.pwmFrequency));
    if (light::blends()) {
      page.rowf("Blend carrier (IO40)", "%lu Hz", static_cast<unsigned long>(fl.colorBlendHz));
    } else if (Frontlight.hasColourSelect()) {
      page.row("Blend carrier (IO40)", fl.colorBlendHz != 0 ? "off: a string is missing" : "off: fixed selection");
    }
    page.setCompact(false);
    page.gap(4);
    if (darkTaken && dark.pinMv < light::DARK_MIN_PIN_MV) {
      page.paragraph(BLIND_NOTE, 4);
      return;
    }
    page.paragraph(SCOPE_NOTE, 4);
  }

  void drawSwitchRun(TestPage& page) const {
    page.paragraph(SWITCH_NOTE, 4);
    page.gap(4);
    page.setCompact(true);
    page.rowf("Duty / switch", "%u/1023 / %d of %d (%d of %d in all)", SWITCH_DUTY[sw.duty], sw.doneAtDuty,
              SWITCHES_PER_DUTY, sw.total, SWITCHES_PER_DUTY * SWITCH_DUTIES);
    page.rowf("Lit now", "%s", light::selectsWarm() ? "warm" : "cool");
    if (current.ledMv != 0) {
      page.rowf("LED_SW, last settled", "%u.%02u V", static_cast<unsigned>(current.ledMv / 1000U),
                static_cast<unsigned>(current.ledMv % 1000U / 10U));
    }
    for (int d = 0; d < SWITCH_DUTIES; ++d) {
      char label[32];
      char pair[24];
      snprintf(label, sizeof(label), "LED_SW mV C / W, duty %u/1023", SWITCH_DUTY[d]);
      formatPair(pair, sizeof(pair), sw.vf[d][0], sw.vf[d][1]);
      page.row(label, pair);
    }
    page.rowf("Trips / probe trips this run", "%u / %u", static_cast<unsigned>(monitorNow.trips - sw.trips0),
              static_cast<unsigned>(probeNow.trips - sw.probeTrips0));
    page.setCompact(false);
  }

  // The switch run's state (BRING_UP section 7.3).
  struct SwitchRun {
    bool active = false;
    int duty = 0;  // index into SWITCH_BRIGHTNESS
    int doneAtDuty = 0;
    int perDuty[SWITCH_DUTIES] = {};
    int total = 0;
    bool warmNow = false;  // the string asked for by the last change
    bool deciding = false;
    bool readPending = false;
    unsigned long switchedAt = 0;
    unsigned long decidedAt = 0;
    uint32_t trips0 = 0;
    uint32_t probeTrips0 = 0;
    uint32_t minLedMv = 0;
    uint32_t maxLedMv = 0;
    uint32_t vf[SWITCH_DUTIES][2] = {};  // last settled LED_SW per duty, [cool, warm]
    char fails[64] = "";
    const char* stop = "done";
  };
  SwitchRun sw;

  static constexpr uint8_t PAD_ADIM_PULL = 1U << 0;
  static constexpr uint8_t PAD_COLOR_PULL = 1U << 1;
  static constexpr uint8_t PAD_MONIT_PULL = 1U << 2;
  static constexpr uint8_t PAD_ADIM_STATE = 1U << 3;
  static constexpr uint8_t PAD_COLOR_STATE = 1U << 4;

  bool present = false;
  bool loadKnown = false;
  LightLoad load = LightLoad::J3Panel;
  bool lit = false;
  bool everLit = false;
  int colour = COOL;  // asked for: COOL, WARM or MIX
  bool tripped = false;
  bool startupFailed = false;
  bool noLight = false;  // a switch-on's probe found no load
  bool overVoltage = false;
  bool darkTaken = false;
  bool afterOffPending = false;
  int step = 0;
  unsigned long openedAt = 0;
  unsigned long changedAt = 0;
  unsigned long sampledAt = 0;
  unsigned long drawnAt = 0;
  unsigned long offAt = 0;
  uint32_t tripsAtOpen = 0;
  uint32_t probeTripsAtOpen = 0;
  uint32_t maxLitLedMv = 0;
  light::Reading dark;
  light::Reading current;
  light::Reading afterOff;
  FrontlightManager::MonitorDiagnostics monitorNow;  // the SDK record as of the last repaint request
  FrontlightManager::ProbeDiagnostics probeNow;      // the load-probe record, likewise
  uint32_t table[3][STEP_COUNT] = {};                // [cool, warm, mix][step]: pin mV after settling, 0 = not measured
  light::PadState adim;
  light::PadState colorSel;
  light::PadState monit;
  uint8_t padFails = 0;
};

}  // namespace

std::unique_ptr<Activity> createFrontlightTest(const TestInfo& info, GfxRenderer& renderer,
                                               MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FrontlightTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
