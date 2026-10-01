#if CROSSPOINT_TEST_SUITE
// T1: battery divider (BAT_MONIT) against a meter, and cell detection (bring-up
// B6). Read-only: it never calls BatteryMonitor::isBelowCutoff() or the HAL's
// cutoff latch (their state belongs to the main loop's cutoff), and never sets
// a mode or a pull on the divider pin. One reading a second; the screen repaints
// at most every REDRAW_MS, so the loop keeps its idle clock on battery.
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Memory.h>
#include <SharedAdc.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

constexpr unsigned long SAMPLE_MS = 1000;
constexpr unsigned long REDRAW_MS = 5000;
// No reading while a repaint may still run: its load (full clock, panel) would
// move P+ by the divider's load offset.
constexpr unsigned long REPAINT_SETTLE_MS = 900;
constexpr uint8_t OVERSAMPLE = 16;
constexpr uint8_t WINDOW = 30;  // samples in the spread window: 30 s
// |P+ - meter|: the ADC's +/-50 mV at the pin doubled by the divider, the 12-14 mV
// load offset (bat-load-offset) and the 1 % divider tolerance (~37 mV at 3.7 V).
constexpr int METER_TOLERANCE_MV = 150;
// The pin for a 3.0-4.2 V cell (bat-adc-range), judged with the ADC's +/-50 mV
// at the pin (adc1-only-11db): a full cell reads 2100 exactly.
constexpr uint16_t PIN_MIN_MV = 1500;
constexpr uint16_t PIN_MAX_MV = 2100;
constexpr uint16_t PIN_ADC_TOLERANCE_MV = 50;
// Spread at the pin over WINDOW samples at rest, off external power (proposal).
constexpr uint16_t SPREAD_MAX_PIN_MV = 20;
constexpr int STEP_FINE_MV = 10;
constexpr int STEP_COARSE_MV = 100;
constexpr int METER_MAX_MV = 5000;

constexpr char INTRO[] =
    "Measure P+ at J5 or across the cell and enter it: Left/Right -/+10 mV, Up/Down +/-100 mV. Confirm records. "
    "Repeat on battery, on USB with a cell, and on USB with no cell (cold boot).";
constexpr char CHARGING_NOTE[] = "Charging: P+ follows the charger's output, not the cell's state of charge.";

class BatteryTest final : public TestScreen {
 public:
  BatteryTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    openedAt = millis();
    sample();
    meterMv = roundTo10(pPlusMv);
    drawnAt = openedAt;  // TestScreen paints the first frame
  }

  void onTick() override {
    const unsigned long now = millis();
    if (now - sampledAt < SAMPLE_MS || now - drawnAt < REPAINT_SETTLE_MS) return;
    const BoardConfig::ChargeState before = charge;
    sample();
    // A state change repaints at once; readings wait for the slow repaint.
    if (charge != before || now - drawnAt >= REDRAW_MS) redraw();
  }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return "Record";
      case Slot::Left:
        return "-10 mV";
      case Slot::Right:
        return "+10 mV";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (slot == Slot::Confirm) recordReading();
    if (slot == Slot::Left) adjustMeter(-STEP_FINE_MV);
    if (slot == Slot::Right) adjustMeter(+STEP_FINE_MV);
  }

  void onStep(const int direction) override { adjustMeter(direction * STEP_COARSE_MV); }

  static int roundTo10(const int mv) { return (mv + 5) / 10 * 10; }

  void adjustMeter(const int deltaMv) {
    // The first step starts from the firmware's own reading.
    if (!meterEntered) meterMv = roundTo10(pPlusMv);
    meterEntered = true;
    meterMv += deltaMv;
    if (meterMv < 0) meterMv = 0;
    if (meterMv > METER_MAX_MV) meterMv = METER_MAX_MV;
    redraw();
  }

  void redraw() {
    drawnAt = millis();
    requestUpdate();
  }

  void sample() {
    const BatteryMonitor battery;
    sampledAt = millis();
    pPlusMv = battery.readMillivoltsAveraged(OVERSAMPLE);
    const int8_t pin = BoardConfig::ACTIVE.batteryAdc;
    singlePinMv = pin >= 0 ? static_cast<uint16_t>(freeink::adc::readMilliVolts(static_cast<uint8_t>(pin))) : 0;
    percent = BatteryMonitor::percentageFromMillivolts(pPlusMv, percent);
    charge = BatteryMonitor::chargeStatus();
    externalPower = battery.isExternalPowerPresent(&externalKnown);
    radioOn = WiFi.getMode() != WIFI_MODE_NULL;
    noCellNotice = powerManager.noChargeableCell();
    window[windowHead] = pinMv();
    windowHead = static_cast<uint8_t>((windowHead + 1) % WINDOW);
    if (windowCount < WINDOW) ++windowCount;
  }

  // The pin's average, back through the divider.
  uint16_t pinMv() const {
    const float divider = BoardConfig::ACTIVE.batteryDividerMultiplier;
    return divider > 0.0f ? static_cast<uint16_t>(static_cast<float>(pPlusMv) / divider + 0.5f) : pPlusMv;
  }

  void windowRange(uint16_t& lo, uint16_t& hi) const {
    lo = UINT16_MAX;
    hi = 0;
    for (uint8_t i = 0; i < windowCount; ++i) {
      if (window[i] < lo) lo = window[i];
      if (window[i] > hi) hi = window[i];
    }
    if (windowCount == 0) lo = 0;
  }

  bool cellPresent() const { return pPlusMv >= BatteryMonitor::CELL_PRESENT_MIN_MV; }
  bool onExternalPower() const {
    return (externalKnown && externalPower) || charge == BoardConfig::ChargeState::NoBattery;
  }

  // What the main loop's cutoff would compare this reading with (without taking
  // a reading of its own): writes the text, returns the margin in mV when judged.
  bool cutoffMargin(char* text, const size_t size, int& margin) const {
    const auto& policy = BoardConfig::powerPolicy();
    if (policy.lowBatteryCutoffMv == 0) {
      snprintf(text, size, "none on this board");
      return false;
    }
    if (onExternalPower()) {
      snprintf(text, size, "not judged: external power");
      return false;
    }
    if (!cellPresent()) {
      snprintf(text, size, "not judged: no cell");
      return false;
    }
    const int allowance = radioOn ? policy.cutoffRadioAllowanceMv : 0;
    const int threshold = static_cast<int>(policy.lowBatteryCutoffMv) - allowance;
    margin = static_cast<int>(pPlusMv) - threshold;
    snprintf(text, size, "%d mV%s: %+d mV", threshold, radioOn ? " (radio)" : "", margin);
    return true;
  }

  void recordReading() {
    char fails[64] = "";
    const auto fail = [&fails](const char* what) { addFailure(fails, sizeof(fails), what); };
    const int error = static_cast<int>(pPlusMv) - meterMv;
    if (meterEntered && abs(error) > METER_TOLERANCE_MV) fail("meter");
    const uint16_t pin = pinMv();
    // While charging or at Complete, P+ is the charger's output (CV 4.2 V +/-1 %
    // plus the path's I*R), not the cell's: the range is not judged then.
    constexpr uint16_t pinLo = PIN_MIN_MV - PIN_ADC_TOLERANCE_MV;
    constexpr uint16_t pinHi = PIN_MAX_MV + PIN_ADC_TOLERANCE_MV;
    const bool rangeJudged =
        cellPresent() && charge != BoardConfig::ChargeState::Charging && charge != BoardConfig::ChargeState::Complete;
    if (rangeJudged && (pin < pinLo || pin > pinHi)) fail("pin_range");
    uint16_t lo = 0;
    uint16_t hi = 0;
    windowRange(lo, hi);
    const uint16_t spread = static_cast<uint16_t>(hi - lo);
    const bool spreadJudged = windowCount >= WINDOW && !onExternalPower();
    if (spreadJudged && spread > SPREAD_MAX_PIN_MV) fail("spread");
    char range[16] = "n/a";
    if (rangeJudged) snprintf(range, sizeof(range), "%u-%u", pinLo, pinHi);
    char cutoff[40];
    int margin = 0;
    char marginText[12];
    if (cutoffMargin(cutoff, sizeof(cutoff), margin)) {
      snprintf(marginText, sizeof(marginText), "%d", margin);
    } else {
      snprintf(marginText, sizeof(marginText), "n/a");
    }
    // The real check, once per record: it logs an error when it blocks.
    const bool writeBlocked = powerManager.firmwareUpdateBlockedByBattery();
    char meter[16];
    if (meterEntered) {
      snprintf(meter, sizeof(meter), "%d", meterMv);
    } else {
      snprintf(meter, sizeof(meter), "none");
    }
    const Verdict verdict = fails[0] != '\0' ? Verdict::Fail : meterEntered ? Verdict::Pass : Verdict::Info;
    record(verdict,
           "pplus=%u pin=%u pin_range=%s single=%u meter=%s err=%d pct=%u cell=%d usbstat=%s ext=%s radio=%d "
           "spread=%u n=%u spread_judged=%d cutoff_margin=%s fw_write=%s no_cell_notice=%d onscreen=%lus fails=%s",
           pPlusMv, pin, range, singlePinMv, meter, meterEntered ? error : 0, percent, cellPresent() ? 1 : 0,
           chargeStateName(charge),
           !externalKnown  ? "unknown"
           : externalPower ? "yes"
                           : "no",
           radioOn ? 1 : 0, spread, windowCount, spreadJudged ? 1 : 0, marginText, writeBlocked ? "blocked" : "allowed",
           noCellNotice ? 1 : 0, (millis() - openedAt) / 1000UL,
           fails[0] == '\0' ? (meterEntered ? "none" : "no_meter_value") : fails);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 4);
    page.gap(4);
    const uint16_t pin = pinMv();
    page.rowf("P+ (divider x2, 16 samples)", "%u mV", pPlusMv);
    page.rowf("Pin: average / one sample", "%u / %u mV", pin, singlePinMv);
    if (meterEntered) {
      const int error = static_cast<int>(pPlusMv) - meterMv;
      page.rowf("Meter / error", "%d / %+d mV%s", meterMv, error, abs(error) > METER_TOLERANCE_MV ? " (over)" : "");
    } else {
      page.rowf("Meter", "not entered (%d)", meterMv);
    }
    page.rowf("Percent (10 % steps)", "%u %%", percent);
    page.row("Cell", cellPresent() ? "present (P+ >= 2500 mV)" : "none (P+ < 2500 mV)");
    // The reader's header and status bar on external power with no chargeable cell.
    if (noCellNotice) {
      page.rowf("Reader's battery label", "%s (no cell, or one too flat to charge)", tr(STR_NO_BATTERY));
    } else {
      page.row("Reader's battery label", "the percentage");
    }
    page.rowf("USB_STAT / external power", "%s / %s", chargeStateName(charge),
              !externalKnown  ? "unknown"
              : externalPower ? "yes"
                              : "no");
    uint16_t lo = 0;
    uint16_t hi = 0;
    windowRange(lo, hi);
    page.rowf("Pin, last 30 s", "%u-%u mV (spread %u, n=%u)", lo, hi, static_cast<unsigned>(hi - lo), windowCount);
    char cutoff[40];
    int margin = 0;
    cutoffMargin(cutoff, sizeof(cutoff), margin);
    page.row("Cutoff", cutoff);
    // Mirrors HalPowerManager::firmwareUpdateBlockedByBattery() without its log line.
    const auto& policy = BoardConfig::powerPolicy();
    if (policy.firmwareUpdateMinMv != 0) {
      const int allowance = radioOn ? policy.cutoffRadioAllowanceMv : 0;
      const bool blocked = !onExternalPower() && cellPresent() && pPlusMv + allowance < policy.firmwareUpdateMinMv;
      page.rowf("Firmware writes", "%s (%u mV floor)", blocked ? "blocked" : "allowed", policy.firmwareUpdateMinMv);
    }
    const unsigned long onScreen = (millis() - openedAt) / 1000UL;
    page.rowf("On this screen", "%lu:%02lu", onScreen / 60UL, onScreen % 60UL);
    if (charge == BoardConfig::ChargeState::Charging) {
      page.gap(4);
      page.paragraph(CHARGING_NOTE, 2);
    }
  }

  unsigned long openedAt = 0;
  unsigned long sampledAt = 0;
  unsigned long drawnAt = 0;
  uint16_t pPlusMv = 0;
  uint16_t singlePinMv = 0;
  uint16_t percent = 101;  // > 100: no history for the deadband
  BoardConfig::ChargeState charge = BoardConfig::ChargeState::Unknown;
  bool externalPower = false;
  bool externalKnown = false;
  bool radioOn = false;
  bool noCellNotice = false;  // HalPowerManager::noChargeableCell()
  int meterMv = 0;
  bool meterEntered = false;
  uint16_t window[WINDOW] = {};
  uint8_t windowHead = 0;
  uint8_t windowCount = 0;
};

}  // namespace

std::unique_ptr<Activity> createBatteryTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<BatteryTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
