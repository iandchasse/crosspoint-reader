#if CROSSPOINT_TEST_SUITE
// T9: front light (bring-up B8). The boost lights only after the operator names
// the LED load, every time it goes from dark to lit. Both strings step through
// five brightness levels while LED_MONIT (IO2) is read through freeink::adc and
// the SDK's monitor record is watched for trips: the trip handler in the main
// loop switches the light off before this screen could see fault(), so a trip
// shows here as the light dropping out and the record's count rising.
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
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr uint8_t STEPS[] = {10, 25, 50, 75, 100};  // brightness %, the SDK's perceptual curve
constexpr int STEP_COUNT = static_cast<int>(sizeof(STEPS) / sizeof(STEPS[0]));
constexpr unsigned long SAMPLE_MS = 1000;
constexpr unsigned long REDRAW_MS = 3000;
// Readings right after a light-up (fl-j3-mapping-risk): LED_SW over 22 V this
// soon is an open or reverse-wired load on its way to U10's OVP.
constexpr unsigned long EARLY_CHECK_MS[] = {50, 100, 200};
constexpr uint32_t J6_MIN_RISE_MV = 1000;  // lit LED_SW above the dark rail: the boost runs

constexpr int QUESTION_SEEN = 1;
constexpr const char* SEEN_ANSWERS[] = {"Both, brighter each step", "Only one string lit", "No rise, or flicker",
                                        "Never lit"};
constexpr const char* SEEN_CODES[] = {"both", "one_string", "no_rise_or_flicker", "never_lit"};
constexpr char SEEN_HEADING[] = "What did the light do?";
constexpr char SEEN_BODY[] =
    "Cool and warm through 10-100 %: did each string light, get brighter at every step, and stay free of flicker?";

constexpr char INTRO[] =
    "Confirm lights the boost once you name the LED load. Up/Down step the brightness (10, 25, 50, 75, 100 %), Left "
    "switches cool/warm. Take both strings through every step, then Record.";
constexpr char SCOPE_NOTE[] =
    "Scope (optional): IO42 LOW for at least 3 ms, HIGH for the enable pulse, then the carrier. IO40 (HIGH = warm) "
    "moves only while IO42 is LOW, at least 3 ms after it fell.";
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
    Frontlight.setOn(false);
    Frontlight.setWarmth(0);
    Frontlight.setBrightness(STEPS[step]);
  }

  void onTick() override {
    if (!present) return;
    const unsigned long now = millis();
    // The dark reading waits for LED_MONIT to settle from anything lit before.
    if (!darkTaken && now - openedAt >= light::SETTLE_MS) takeDarkReading();
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
    switch (slot) {
      case Slot::Confirm:
        if (!present) return "Record";
        return lit ? "Off" : "Light on";
      case Slot::Left:
        if (!present || !Frontlight.warmthIsSelection()) return nullptr;
        return warm ? "Cool" : "Warm";
      case Slot::Right:
        return present ? "Record" : nullptr;
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
        warm = !warm;
        Frontlight.setWarmth(warm ? 100 : 0);
        changedAt = millis();
        redraw();
        break;
      case Slot::Right:
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
    if (!present) return;
    const int next = step + direction;
    if (next < 0 || next >= STEP_COUNT) return;
    step = next;
    Frontlight.setBrightness(STEPS[step]);
    changedAt = millis();
    redraw();
  }

  void onAnswer(const int question, const int answer) override {
    if (question == QUESTION_SEEN && answer >= 0) recordResult(answer);
  }

  void onLightLoadConfirmed(const LightLoad chosen) override {
    load = chosen;
    loadKnown = true;
    lightUp();
  }

  void redraw() {
    drawnAt = millis();
    monitorNow = light::monitor();
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

  void lightUp() {
    Frontlight.setBrightness(STEPS[step]);
    Frontlight.setWarmth(warm ? 100 : 0);
    Frontlight.setOn(true);
    const unsigned long start = millis();
    lit = true;
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
    LOG_INF("TEST", "T9: lit %s at %u %%", warm ? "warm" : "cool", STEPS[step]);
    redraw();
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

  void sample() {
    sampledAt = millis();
    current = light::readMonitor();
    const int colour = warm ? 1 : 0;
    table[colour][step] = current.pinMv;
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
  // its carrier and COLOR_SEL at the chosen string.
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
    if (colorSel.valid && fl.colorSelectGpio != BoardConfig::PIN_UNASSIGNED) {
      const bool expect = asLit && warm ? fl.colorSelectWarmHigh : coolLevel;
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
    bool inWindow = true;
    for (const auto& colour : table) {
      for (const uint32_t mv : colour) {
        if (mv != 0 && !litInWindow(mv)) inWindow = false;
      }
    }
    if (!inWindow) addFailure(fails, sizeof(fails), "lit_range");
    if (answer >= 0) {
      if (!colourMeasured(0)) addFailure(fails, sizeof(fails), "cool_missing");
      if (Frontlight.warmthIsSelection() && !colourMeasured(1)) addFailure(fails, sizeof(fails), "warm_missing");
    }
    if (afterOff.pinMv > light::DARK_MAX_PIN_MV) addFailure(fails, sizeof(fails), "after_off");
    if (padFails & PAD_ADIM_PULL) addFailure(fails, sizeof(fails), "io42_pullup");
    if (padFails & PAD_COLOR_PULL) addFailure(fails, sizeof(fails), "io40_pullup");
    if (padFails & PAD_MONIT_PULL) addFailure(fails, sizeof(fails), "io2_pull");
    if (padFails & PAD_ADIM_STATE) addFailure(fails, sizeof(fails), "io42_state");
    if (padFails & PAD_COLOR_STATE) addFailure(fails, sizeof(fails), "io40_state");
    if (answer > 0) addFailure(fails, sizeof(fails), "operator");
    char cool[40];
    char warmSteps[40];
    formatSteps(cool, sizeof(cool), 0);
    formatSteps(warmSteps, sizeof(warmSteps), 1);
    const auto monitor = light::monitor();
    monitorNow = monitor;
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : answer == 0 ? Verdict::Pass : Verdict::Info;
    record(verdict,
           "load=%s dark_pin=%u cool_pin=%s warm_pin=%s max_led=%u after_off_pin=%u trips=%u skipped=%u seen=%s "
           "fails=%s",
           !loadKnown                   ? "none"
           : load == LightLoad::J3Panel ? "J3"
                                        : "J6",
           static_cast<unsigned>(dark.pinMv), cool, warmSteps, static_cast<unsigned>(maxLitLedMv),
           static_cast<unsigned>(afterOff.pinMv), static_cast<unsigned>(trips),
           static_cast<unsigned>(monitor.skippedSamples), answer >= 0 ? SEEN_CODES[answer] : "not_lit",
           fails[0] != '\0' ? fails : "none");
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
    page.paragraph(INTRO, 4);
    page.gap(4);
    page.setCompact(true);
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    page.row("Load", !loadKnown ? "not named yet" : load == LightLoad::J3Panel ? "J3 panel light" : "J6 LED strip");
    page.rowf("Light", "%s, %s, %u %% (level %u of 255)", lit ? "on" : "off", warm ? "warm" : "cool",
              static_cast<unsigned>(STEPS[step]), static_cast<unsigned>(STEPS[step]) * 255U / 100U);
    if (lit && current.pinMv != 0) {
      page.rowf("LED_MONIT now", "%u mV = %u.%02u V LED_SW", static_cast<unsigned>(current.pinMv),
                static_cast<unsigned>(current.ledMv / 1000U), static_cast<unsigned>(current.ledMv % 1000U / 10U));
    }
    const auto& monitor = monitorNow;
    page.rowf("SDK monitor, last", "%u mV = %u.%02u V (%u read, %u skipped)", static_cast<unsigned>(monitor.lastPinMv),
              static_cast<unsigned>(monitor.lastLedMv / 1000U), static_cast<unsigned>(monitor.lastLedMv % 1000U / 10U),
              static_cast<unsigned>(monitor.samples), static_cast<unsigned>(monitor.skippedSamples));
    if (darkTaken) {
      page.rowf("Dark (healthy 0.25-0.55 V)", "%u mV", static_cast<unsigned>(dark.pinMv));
    } else {
      page.row("Dark (healthy 0.25-0.55 V)", "settling");
    }
    if (afterOff.pinMv != 0) page.rowf("After switching off", "%u mV", static_cast<unsigned>(afterOff.pinMv));
    page.rowf("Trips: this screen / boot", "%u / %u", static_cast<unsigned>(monitor.trips - tripsAtOpen),
              static_cast<unsigned>(monitor.trips));
    if (monitor.trips != 0) {
      page.rowf("Last trip", "%u mV > %u mV limit", static_cast<unsigned>(monitor.lastTripLedMv),
                static_cast<unsigned>(monitor.lastTripLimitMv));
    }
    page.rowf("Trip level", "%u mV LED_SW = %u mV pin", fl.monitorTripMv,
              static_cast<unsigned>(light::pinFromLed(fl.monitorTripMv)));
    char steps[40];
    formatSteps(steps, sizeof(steps), 0);
    page.row("Cool 10/25/50/75/100 %, pin mV", steps);
    if (Frontlight.warmthIsSelection()) {
      formatSteps(steps, sizeof(steps), 1);
      page.row("Warm 10/25/50/75/100 %, pin mV", steps);
    }
    page.rowf("Lit window", "%s", load == LightLoad::J6Strip ? "above dark +1 V, below 22 V" : "11-18 V (about 15 V)");
    page.rowf("IO42 / IO40 / pulls", "%s / %s / %s", padText(adim, true), padText(colorSel, false),
              (padFails & (PAD_ADIM_PULL | PAD_COLOR_PULL | PAD_MONIT_PULL)) != 0 ? "WRONG" : "none");
    page.rowf("Enable pulse / dwell / carrier", "%u us / %u ms / %lu Hz", fl.enablePulseUs, fl.offDwellMs,
              static_cast<unsigned long>(fl.pwmFrequency));
    page.setCompact(false);
    page.gap(4);
    if (darkTaken && dark.pinMv < light::DARK_MIN_PIN_MV) {
      page.paragraph(BLIND_NOTE, 3);
      return;
    }
    page.paragraph(SCOPE_NOTE, 3);
  }

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
  bool warm = false;
  bool tripped = false;
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
  uint32_t maxLitLedMv = 0;
  light::Reading dark;
  light::Reading current;
  light::Reading afterOff;
  FrontlightManager::MonitorDiagnostics monitorNow;  // the SDK record as of the last repaint request
  uint32_t table[2][STEP_COUNT] = {};                // [cool, warm][step]: pin mV after settling, 0 = not measured
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
