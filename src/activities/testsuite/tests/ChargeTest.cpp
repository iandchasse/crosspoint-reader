#if CROSSPOINT_TEST_SUITE
// T2: charge-status ladder (USB_STAT) and USB presence (bring-up B6). The ladder
// is sampled by the SDK (at most once per ChargeStatusLadder::minIntervalMs,
// usbstat-slow-sampling): this screen reads its record
// (BatteryMonitor::chargeLadderDiagnostics()). The one exception is stage 5's
// blink capture, a bounded 12 s run that reads the pin at 20 Hz through
// freeink::adc (no mode or pull is set) to time the charger's no-battery blink.
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <ChargeLadderDecoder.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SharedAdc.h>

#include <cstdio>
#include <cstring>

#include "TestFactories.h"
#include "TestNames.h"
#include "TextBuffer.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

using State = BoardConfig::ChargeState;
using Decoder = freeink::battery_detail::ChargeLadderDecoder<BoardConfig::ChargeLadderWindow>;

constexpr unsigned long REDRAW_MS = 5000;
constexpr int QUESTION_D2 = 1;
constexpr uint8_t HISTORY = 4;

// Stage 1 (HW-08, BRING_UP §4): on the cell the ladder sits at 2.20 V. Each
// sample since the stage was picked or the published state last changed (so the
// set-up's own transition is not counted) falls in one band:
// - at most 1350 mV, the charging or complete band: the unpowered charger's
//   status pins clamp the node, so firmware cannot tell the cell from USB (FAIL,
//   at any sample count: a clamp seen once is a clamp);
// - 1350-2100 mV: a partial clamp by the unpowered charger (WARN);
// - 2300-2600 mV: above nominal, ladder tolerance or a weak ST (WARN; a clamp
//   only pulls the node down);
// - above 2600 mV, about 3.3 V: ST is not asserting (U2.8/R17; WARN beside the
//   state's FAIL).
// PASS or WARN needs RECENT samples; fewer record INFO.
constexpr uint16_t ON_BATTERY_NOMINAL_MIN_MV = 2100;
constexpr uint16_t ON_BATTERY_NOMINAL_MAX_MV = 2300;
constexpr uint16_t CLAMP_MAX_MV = 1350;
constexpr uint16_t ST_HIGH_ABOVE_MV = 2600;
constexpr uint8_t RECENT = 10;

// Stage 5's capture: 20 Hz for 12 s, so the datasheet's slowest blink (4 s)
// still shows two whole periods. The node settles in about 13 ms between the
// blink's two levels (1 M || 510 k || 200 k with C23), so 50 ms resolves them.
constexpr unsigned long CAPTURE_PERIOD_MS = 50;
constexpr unsigned long CAPTURE_MS = 12000;
constexpr int CAPTURE_SAMPLES = static_cast<int>(CAPTURE_MS / CAPTURE_PERIOD_MS) + 8;
constexpr uint8_t CAPTURE_OVERSAMPLE = 4;
constexpr int MAX_EDGES = 48;

constexpr char HW08_NOTE[] =
    "HW-08: USB_STAT reads in the charging or complete band on the cell. The unpowered TP4056 clamps CHRG/STDBY (CE "
    "back-feed), so firmware cannot tell the cell from USB. BRING_UP section 4.";
constexpr char ST_NOTE[] =
    "USB_STAT at about 3.3 V on the cell: ST is not asserting. Check U2.8/R17 (BRING_UP section 4).";
constexpr char PARTIAL_NOTE[] =
    "USB_STAT at 1350-2100 mV on the cell: a partial clamp by the unpowered charger? Check USB_VBUS and D2.";
constexpr char ABOVE_NOTE[] =
    "USB_STAT at 2300-2600 mV on the cell: above nominal, not a clamp. Ladder tolerance, or a weak ST.";

// One bench state to set up and check. D2 lights whenever USB power is present,
// whatever the charger does (HARDWARE.md §3).
struct Stage {
  const char* title;
  const char* setup;
  State expect;
  State alt;             // also accepted (Unknown: none)
  bool usbConnected;     // HalGPIO::isUsbConnected()
  bool d2Lit;            // the operator's answer
  uint16_t maxSettleMs;  // first sample in the new window -> published state (expect)
  uint16_t altSettleMs;  // the same for alt
  bool neverComplete;    // Complete must not be reported during the stage
  bool clampCheck;       // stage 1's HW-08 levels
  bool capture;          // Record first captures USB_STAT at 20 Hz
};

// Settle limits from the ladder's rules (BoardConfig ChargeStatusLadder): a
// state change needs 2 agreeing samples a second apart; Complete needs 8 stable
// samples; the no-battery blink needs 3 in-window samples that swing 70 mV,
// then the debounce.
constexpr Stage STAGES[] = {
    {"1 Battery only",
     "Unplug USB. On a meter USB_VBUS reads about 0 V and D2 is dark (HW-08). Wait 10 s, then Record.",
     State::OnBattery, State::Unknown, false, false, 3000, 0, false, true, false},
    {"2 Charging", "Plug a USB charger with a partly discharged cell (about 1.11 V).", State::Charging, State::Unknown,
     true, true, 3000, 0, false, false, false},
    {"3 Weak USB (optional)", "A weak USB source or a long thin cable (about 0.95 V).", State::Charging, State::Unknown,
     true, true, 3000, 0, false, false, false},
    {"4 Complete (optional, long)", "A nearly full cell on USB until the charger stops (about 0.55 V).",
     State::Complete, State::Unknown, true, true, 10000, 0, false, false, false},
    {"5 Cell pulled on USB (optional)",
     "Protected cell and insulated tools only: pull the cell with USB in. The 0.55/0.41 V blink, or a steady 3.3 V "
     "(charger switched off, also safe). Record times USB_STAT at 20 Hz for 12 s.",
     State::NoBattery, State::ExternalIdle, true, true, 8000, 3000, true, false, true},
    {"6 USB, no cell", "Cold boot from USB with no cell fitted (about 3.3 V).", State::ExternalIdle, State::Unknown,
     true, true, 3000, 0, false, false, false},
};
constexpr int STAGE_COUNT = static_cast<int>(sizeof(STAGES) / sizeof(STAGES[0]));

constexpr const char* D2_ANSWERS[] = {"D2 dark", "D2 lit"};
constexpr char D2_HEADING[] = "D2 (USB power LED)";
constexpr char D2_BODY[] = "Is D2 lit right now?";

uint8_t stateBit(const State state) { return static_cast<uint8_t>(1U << static_cast<uint8_t>(state)); }

Decoder::Params ladderParams() {
  const auto& ladder = BoardConfig::ACTIVE.chargeLadder;
  return {ladder.windows,         ladder.count,
          ladder.debounceSamples, ladder.completeStableSamples,
          ladder.blinkDeltaMv,    ladder.unknownHoldSamples};
}

// The profile's window for a state; the no-battery blink sits in the Complete window.
bool windowFor(State state, uint16_t& lo, uint16_t& hi) {
  if (state == State::NoBattery) state = State::Complete;
  const auto& ladder = BoardConfig::ACTIVE.chargeLadder;
  for (uint8_t i = 0; i < ladder.count; ++i) {
    if (ladder.windows[i].state == state) {
      lo = ladder.windows[i].minMv;
      hi = ladder.windows[i].maxMv;
      return true;
    }
  }
  return false;
}

bool accepts(const Stage& s, const State state) {
  return state == s.expect || (s.alt != State::Unknown && state == s.alt);
}

// What the capture saw: the blink's period and duty, or a steady level.
struct Blink {
  bool done = false;
  bool blinking = false;  // a swing of at least the ladder's blinkDeltaMv
  uint16_t samples = 0;
  uint16_t loMv = 0;
  uint16_t hiMv = 0;
  uint16_t meanMv = 0;
  uint8_t edges = 0;
  uint16_t periodMs = 0;  // mean of the whole periods; 0 when none was whole
  uint16_t periodMinMs = 0;
  uint16_t periodMaxMs = 0;
  bool dutyKnown = false;
  uint8_t lowDutyPct = 0;  // time at the low level (CHRG and STDBY both on) per period
  uint16_t highLevelMv = 0;
  uint16_t lowLevelMv = 0;
};

class ChargeTest final : public TestScreen {
 public:
  ChargeTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

 private:
  struct Change {
    unsigned long atMs;
    State from;
    State to;
    uint16_t mv;
    bool settleKnown;
    uint16_t settleMs;
  };

  struct CaptureSample {
    uint16_t atMs;  // from the start of the capture
    uint16_t mv;
  };

  // The capture runs the loop at its full pace for a bounded 12 s.
  bool preventAutoSleep() override { return capturing; }

  void onStart() override {
    openedAt = millis();
    drawnAt = openedAt;
    diag = BatteryMonitor::chargeLadderDiagnostics();
    seenSamples = diag.samples;
    published = diag.published;
    runWindow = Decoder::classify(ladderParams(), static_cast<int32_t>(diag.lastMv));
    runStartMs = diag.lastSampleMs;
    usbConnected = gpio.isUsbConnected();
    noCellNotice = powerManager.noChargeableCell();
    startStage();
  }

  void onTick() override {
    const unsigned long now = millis();
    diag = BatteryMonitor::chargeLadderDiagnostics();
    bool changed = false;
    if (diag.samples != seenSamples) {
      seenSamples = diag.samples;
      dirty = true;
      noteSample(static_cast<uint16_t>(diag.lastMv));
      // The run of samples in one window starts at its first sample.
      const State window = Decoder::classify(ladderParams(), static_cast<int32_t>(diag.lastMv));
      if (window != runWindow) {
        runWindow = window;
        runStartMs = diag.lastSampleMs;
      }
    }
    if (diag.published != published) {
      noteChange(now);
      changed = true;
    }
    if (gpio.wasUsbStateChanged()) {
      ++usbEdges;
      changed = true;
    }
    usbConnected = gpio.isUsbConnected();
    const bool noCell = powerManager.noChargeableCell();
    if (noCell != noCellNotice) {
      noCellNotice = noCell;
      changed = true;
    }
    if (capturing) {
      captureTick(now);
      return;  // no repaint mid-capture
    }
    if (changed || (dirty && now - drawnAt >= REDRAW_MS)) {
      dirty = false;
      drawnAt = now;
      requestUpdate();
    }
  }

  // Stage 1's recent samples, since the stage was picked or the state last changed.
  void noteSample(const uint16_t mv) {
    recent[recentHead] = mv;
    recentHead = static_cast<uint8_t>((recentHead + 1) % RECENT);
    if (recentCount < RECENT) ++recentCount;
  }

  void clearRecent() {
    recentHead = 0;
    recentCount = 0;
  }

  struct RecentLevels {
    uint16_t lo = 0;
    uint16_t hi = 0;
    uint8_t clamped = 0;  // at most 1350 mV: the charging or complete band
    uint8_t partial = 0;  // 1350-2100 mV
    uint8_t above = 0;    // 2300-2600 mV
    uint8_t stHigh = 0;   // above 2600 mV: ST not asserting
  };

  RecentLevels recentLevels() const {
    RecentLevels r;
    r.lo = UINT16_MAX;
    for (uint8_t i = 0; i < recentCount; ++i) {
      const uint16_t mv = recent[i];
      if (mv < r.lo) r.lo = mv;
      if (mv > r.hi) r.hi = mv;
      if (mv <= CLAMP_MAX_MV) {
        ++r.clamped;
      } else if (mv < ON_BATTERY_NOMINAL_MIN_MV) {
        ++r.partial;
      } else if (mv > ST_HIGH_ABOVE_MV) {
        ++r.stHigh;
      } else if (mv > ON_BATTERY_NOMINAL_MAX_MV) {
        ++r.above;
      }
    }
    if (recentCount == 0) r.lo = 0;
    return r;
  }

  void noteChange(const unsigned long now) {
    Change& c = history[historyHead];
    historyHead = static_cast<uint8_t>((historyHead + 1) % HISTORY);
    if (historyCount < HISTORY) ++historyCount;
    c.atMs = now;
    c.from = published;
    c.to = diag.published;
    c.mv = static_cast<uint16_t>(diag.lastMv);
    // Known when the publishing sample closed the current run of the new
    // state's window (the blink's window is Complete's).
    const State runState = runWindow == State::Complete && c.to == State::NoBattery ? State::NoBattery : runWindow;
    c.settleKnown = runState == c.to && diag.lastSampleMs >= runStartMs;
    c.settleMs = c.settleKnown ? static_cast<uint16_t>(diag.lastSampleMs - runStartMs) : 0;
    published = diag.published;
    stageSeen |= stateBit(published);
    clearRecent();
    if (accepts(STAGES[stage], published)) {
      stageReached = true;
      stageSettle = c;
    }
  }

  void startStage() {
    // A Complete still published from the stage before (stage 4, then stage 5)
    // is not this stage's: noteChange() sets the bit for a change into Complete
    // during the stage, and a Complete still published at Record fails "state".
    const uint8_t notOwn = STAGES[stage].neverComplete ? stateBit(State::Complete) : 0;
    stageSeen = static_cast<uint8_t>(stateBit(published) & ~notOwn);
    stageReached = false;
    stageStartMs = millis();
    clearRecent();
    blink = Blink{};
  }

  const char* slotLabel(const Slot slot) const override {
    if (capturing) return nullptr;
    switch (slot) {
      case Slot::Confirm:
        return "Record";
      case Slot::Left:
        return "< Stage";
      case Slot::Right:
        return "Stage >";
      default:
        return nullptr;
    }
  }

  void onSlot(const Slot slot) override {
    if (capturing) return;
    if (slot == Slot::Confirm) {
      if (STAGES[stage].capture) {
        startCapture();
      } else {
        ask(QUESTION_D2, D2_HEADING, D2_BODY, D2_ANSWERS, 2);
      }
      return;
    }
    const int step = slot == Slot::Left ? -1 : +1;
    stage = (stage + step + STAGE_COUNT) % STAGE_COUNT;
    startStage();
    drawnAt = millis();
    requestUpdate();
  }

  // Back stops a capture: nothing is recorded.
  bool onBack() override {
    if (!capturing) return false;
    capturing = false;
    LOG_INF("TEST", "T2: USB_STAT capture stopped after %d samples", captureCount);
    requestUpdate();
    return true;
  }

  // --- stage 5's capture -------------------------------------------------------

  void startCapture() {
    capturing = true;
    captureCount = 0;
    captureStartMs = millis();
    captureLastMs = 0;
    blink = Blink{};
    LOG_INF("TEST", "T2: capturing USB_STAT at %lu Hz for %lu s", 1000UL / CAPTURE_PERIOD_MS, CAPTURE_MS / 1000UL);
    requestUpdate();
  }

  void captureTick(const unsigned long now) {
    if (captureCount > 0 && now - captureLastMs < CAPTURE_PERIOD_MS) return;
    const int8_t pin = BoardConfig::ACTIVE.chargeLadder.pin;
    if (pin >= 0 && captureCount < CAPTURE_SAMPLES) {
      uint32_t sum = 0;
      {
        const freeink::adc::Lock lock;
        for (uint8_t i = 0; i < CAPTURE_OVERSAMPLE; ++i) sum += analogReadMilliVolts(static_cast<uint8_t>(pin));
      }
      captureLastMs = now;
      capture[captureCount++] = {static_cast<uint16_t>(now - captureStartMs),
                                 static_cast<uint16_t>(sum / CAPTURE_OVERSAMPLE)};
    }
    if (pin < 0 || now - captureStartMs >= CAPTURE_MS || captureCount >= CAPTURE_SAMPLES) finishCapture();
  }

  void finishCapture() {
    capturing = false;
    analyseCapture();
    LOG_INF("TEST", "T2: capture %u samples, %u-%u mV, %s, %u edges, period %u ms, low %u %%", blink.samples,
            blink.loMv, blink.hiMv, blink.blinking ? "blink" : "steady", blink.edges, blink.periodMs, blink.lowDutyPct);
    drawnAt = millis();
    requestUpdate();
    ask(QUESTION_D2, D2_HEADING, D2_BODY, D2_ANSWERS, 2);
  }

  // Edges cross the midpoint of the levels seen, with a quarter of the swing as
  // hysteresis; edges alternate, so edge j and j-2 bound one whole period.
  void analyseCapture() {
    Blink b;
    b.done = true;
    b.samples = static_cast<uint16_t>(captureCount);
    if (captureCount == 0) {
      blink = b;
      return;
    }
    uint32_t sum = 0;
    b.loMv = UINT16_MAX;
    for (int i = 0; i < captureCount; ++i) {
      const uint16_t mv = capture[i].mv;
      if (mv < b.loMv) b.loMv = mv;
      if (mv > b.hiMv) b.hiMv = mv;
      sum += mv;
    }
    b.meanMv = static_cast<uint16_t>(sum / static_cast<uint32_t>(captureCount));
    b.blinking = b.hiMv - b.loMv >= BoardConfig::ACTIVE.chargeLadder.blinkDeltaMv;
    if (!b.blinking) {
      blink = b;
      return;
    }
    const int mid = (b.loMv + b.hiMv) / 2;
    const int hysteresis = (b.hiMv - b.loMv) / 4;
    uint16_t edgeAt[MAX_EDGES];
    bool edgeRise[MAX_EDGES];
    int edges = 0;
    bool high = capture[0].mv >= mid;
    uint32_t highSum = 0;
    uint32_t lowSum = 0;
    uint16_t highN = 0;
    uint16_t lowN = 0;
    for (int i = 0; i < captureCount; ++i) {
      const int mv = capture[i].mv;
      if (mv >= mid + hysteresis) {
        highSum += static_cast<uint32_t>(mv);
        ++highN;
      } else if (mv <= mid - hysteresis) {
        lowSum += static_cast<uint32_t>(mv);
        ++lowN;
      }
      const bool crossed = high ? mv < mid - hysteresis : mv > mid + hysteresis;
      if (!crossed) continue;
      high = !high;
      if (edges < MAX_EDGES) {
        edgeAt[edges] = capture[i].atMs;
        edgeRise[edges] = high;
        ++edges;
      }
    }
    b.edges = static_cast<uint8_t>(edges);
    if (highN > 0) b.highLevelMv = static_cast<uint16_t>(highSum / highN);
    if (lowN > 0) b.lowLevelMv = static_cast<uint16_t>(lowSum / lowN);
    uint32_t periodSum = 0;
    int periods = 0;
    for (int j = 2; j < edges; ++j) {
      const uint16_t period = static_cast<uint16_t>(edgeAt[j] - edgeAt[j - 2]);
      if (periods == 0 || period < b.periodMinMs) b.periodMinMs = period;
      if (period > b.periodMaxMs) b.periodMaxMs = period;
      periodSum += period;
      ++periods;
    }
    if (periods > 0) b.periodMs = static_cast<uint16_t>(periodSum / static_cast<uint32_t>(periods));
    // Low time over the whole periods from the first falling edge to the last.
    int firstFall = -1;
    int lastFall = -1;
    for (int j = 0; j < edges; ++j) {
      if (edgeRise[j]) continue;
      if (firstFall < 0) firstFall = j;
      lastFall = j;
    }
    if (firstFall >= 0 && lastFall > firstFall) {
      uint32_t lowMs = 0;
      for (int j = firstFall; j < lastFall; j += 2) lowMs += static_cast<uint32_t>(edgeAt[j + 1] - edgeAt[j]);
      const uint32_t spanMs = static_cast<uint32_t>(edgeAt[lastFall] - edgeAt[firstFall]);
      if (spanMs > 0) {
        b.dutyKnown = true;
        b.lowDutyPct = static_cast<uint8_t>((lowMs * 100U + spanMs / 2U) / spanMs);
      }
    }
    blink = b;
  }

  // "cap=blink edges=8 period_ms=2000(1950-2050) low_duty=50 levels=550/410",
  // "cap=steady cap_mv=3290-3302", or "cap=none".
  void formatCapture(char* out, const size_t size) const {
    out[0] = '\0';
    if (!blink.done || blink.samples == 0) {
      snprintf(out, size, "cap=none");
      return;
    }
    if (!blink.blinking) {
      snprintf(out, size, "cap=steady cap_mv=%u-%u", blink.loMv, blink.hiMv);
      return;
    }
    snprintf(out, size, "cap=blink edges=%u", blink.edges);
    if (blink.periodMs != 0) {
      appendf(out, size, " period_ms=%u(%u-%u)", blink.periodMs, blink.periodMinMs, blink.periodMaxMs);
    } else {
      appendf(out, size, " period_ms=n/a");
    }
    if (blink.dutyKnown) {
      appendf(out, size, " low_duty=%u", blink.lowDutyPct);
    } else {
      appendf(out, size, " low_duty=n/a");
    }
    appendf(out, size, " levels=%u/%u", blink.highLevelMv, blink.lowLevelMv);
  }

  // --- the record --------------------------------------------------------------

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_D2 || answer < 0) return;
    // The ladder kept going while the question was open: judge it as it is now.
    onTick();
    const Stage& s = STAGES[stage];
    const bool d2Lit = answer == 1;
    char fails[64] = "";
    char warns[48] = "";
    const auto fail = [&fails](const char* what) { addFailure(fails, sizeof(fails), what); };
    if (!accepts(s, published)) fail("state");
    // The window of the state the stage reached, or of the one it expects.
    const State judged = accepts(s, published) ? published : s.expect;
    uint16_t lo = 0;
    uint16_t hi = 0;
    if (!diag.sampled || !windowFor(judged, lo, hi) || diag.lastMv < lo || diag.lastMv > hi) fail("window");
    if (usbConnected != s.usbConnected) fail("usb");
    if (d2Lit != s.d2Lit) fail("d2");
    const uint16_t settleLimit = s.alt != State::Unknown && stageSettle.to == s.alt ? s.altSettleMs : s.maxSettleMs;
    if (stageReached && stageSettle.settleKnown && stageSettle.settleMs > settleLimit) fail("slow");
    if (s.neverComplete && (stageSeen & stateBit(State::Complete)) != 0) fail("reported_complete");
    const RecentLevels recentNow = recentLevels();
    if (s.clampCheck) {
      if (recentNow.clamped > 0) fail("hw08_clamp");
      if (recentNow.stHigh > 0) addFailure(warns, sizeof(warns), "st_not_asserting");
      if (recentNow.partial > 0) addFailure(warns, sizeof(warns), "partial_clamp");
      if (recentNow.above > 0) addFailure(warns, sizeof(warns), "above_nominal");
    }
    // Stage 1 passes or warns only on RECENT samples since the last change.
    const bool tooFew = s.clampCheck && recentCount < RECENT;
    char settle[16];
    if (stageReached && stageSettle.settleKnown) {
      snprintf(settle, sizeof(settle), "%ums", stageSettle.settleMs);
    } else {
      snprintf(settle, sizeof(settle), "n/a");
    }
    char extra[112] = "";
    if (s.clampCheck) {
      snprintf(extra, sizeof(extra), " recent=%u-%u/%u clamped=%u partial=%u above=%u st_high=%u", recentNow.lo,
               recentNow.hi, recentCount, recentNow.clamped, recentNow.partial, recentNow.above, recentNow.stHigh);
      if (tooFew) appendf(extra, sizeof(extra), " too_few_samples=%u/%u", recentCount, RECENT);
    } else if (s.capture) {
      const int n = snprintf(extra, sizeof(extra), " outcome=%s ",
                             published == State::NoBattery      ? "blink"
                             : published == State::ExternalIdle ? "steady"
                                                                : "other");
      if (n > 0 && static_cast<size_t>(n) < sizeof(extra)) formatCapture(extra + n, sizeof(extra) - n);
    }
    const Verdict verdict = fails[0] != '\0'   ? Verdict::Fail
                            : tooFew           ? Verdict::Info
                            : warns[0] != '\0' ? Verdict::Warn
                                               : Verdict::Pass;
    record(verdict,
           "stage=%d stage_s=%lu state=%s decoded=%s mv=%lu usb=%d d2=%s settle=%s lost=%d disproved=%d "
           "samples=%lu complete_seen=%d no_cell=%d%s warns=%s fails=%s",
           stage + 1, (millis() - stageStartMs) / 1000UL, chargeStateName(published), chargeStateName(diag.decoded),
           static_cast<unsigned long>(diag.lastMv), usbConnected ? 1 : 0, d2Lit ? "lit" : "dark", settle,
           diag.lost ? 1 : 0, diag.disproved ? 1 : 0, static_cast<unsigned long>(diag.samples),
           (stageSeen & stateBit(State::Complete)) != 0 ? 1 : 0, noCellNotice ? 1 : 0, extra,
           warns[0] == '\0' ? "none" : warns, fails[0] == '\0' ? "none" : fails);
  }

  // --- the screen --------------------------------------------------------------

  void drawCapture(TestPage& page) const {
    if (capturing) {
      const unsigned long elapsed = millis() - captureStartMs;
      page.heading("Capturing USB_STAT at 20 Hz");
      page.rowf("Time", "%lu of %lu s; Back stops", elapsed / 1000UL, CAPTURE_MS / 1000UL);
      return;
    }
    if (!blink.done) return;
    if (blink.samples == 0) {
      page.row("Capture", "no samples");
    } else if (!blink.blinking) {
      page.rowf("Capture: steady", "%u-%u mV over %u samples", blink.loMv, blink.hiMv, blink.samples);
    } else {
      if (blink.periodMs != 0) {
        page.rowf("Capture: blink period", "%.2f s (%.2f-%.2f), %u edges", static_cast<double>(blink.periodMs) / 1000.0,
                  static_cast<double>(blink.periodMinMs) / 1000.0, static_cast<double>(blink.periodMaxMs) / 1000.0,
                  blink.edges);
      } else {
        page.rowf("Capture: blink period", "longer than the capture (%u edges)", blink.edges);
      }
      if (blink.dutyKnown) {
        page.rowf("High / low level, time low", "%u / %u mV, %u %%", blink.highLevelMv, blink.lowLevelMv,
                  blink.lowDutyPct);
      } else {
        page.rowf("High / low level", "%u / %u mV", blink.highLevelMv, blink.lowLevelMv);
      }
    }
  }

  void drawBody(TestPage& page) const override {
    const Stage& s = STAGES[stage];
    const auto& ladder = BoardConfig::ACTIVE.chargeLadder;
    page.heading(s.title);
    page.paragraph(s.setup, 3);
    uint16_t lo = 0;
    uint16_t hi = 0;
    windowFor(s.expect, lo, hi);
    page.setCompact(true);
    if (s.alt != State::Unknown) {
      uint16_t altLo = 0;
      uint16_t altHi = 0;
      windowFor(s.alt, altLo, altHi);
      page.rowf("Expect", "%s %u-%u or %s >%u mV, D2 %s", chargeStateName(s.expect), lo, hi, chargeStateName(s.alt),
                altLo - 1U, s.d2Lit ? "lit" : "dark");
    } else {
      page.rowf("Expect", "%s, %u-%u mV, USB %s, D2 %s", chargeStateName(s.expect), lo, hi,
                s.usbConnected ? "yes" : "no", s.d2Lit ? "lit" : "dark");
    }
    page.gap(4);
    if (!diag.sampled) {
      page.row("USB_STAT", "not sampled yet");
    } else {
      const State window = Decoder::classify(ladderParams(), static_cast<int32_t>(diag.lastMv));
      page.rowf("USB_STAT (16 samples, 1 Hz)", "%lu mV, window %s", static_cast<unsigned long>(diag.lastMv),
                chargeStateName(window));
    }
    page.rowf("Published / decoded", "%s / %s", chargeStateName(published), chargeStateName(diag.decoded));
    page.rowf("Candidate, complete, outside", "%s %u/%u, %u/%u, %u/%u", chargeStateName(diag.candidate),
              diag.candidateRun, ladder.debounceSamples, diag.completeRun, ladder.completeStableSamples,
              diag.unknownRun, ladder.unknownHoldSamples);
    page.rowf("isUsbConnected / changes", "%s / %u", usbConnected ? "yes" : "no", usbEdges);
    // The reader's header and status bar on external power with no chargeable cell.
    if (noCellNotice) {
      page.rowf("Reader's battery label", "%s (no cell, or one too flat to charge)", tr(STR_NO_BATTERY));
    } else {
      page.row("Reader's battery label", "the percentage");
    }
    char trend[32];
    if (diag.trendSamples > 0) {
      snprintf(trend, sizeof(trend), "%u of 5 from %u mV", diag.trendSamples, diag.trendStartMv);
    } else {
      snprintf(trend, sizeof(trend), "idle");
    }
    page.rowf("Lost / P+ cross-check", "%s / %s%s", diag.lost ? "YES" : "no", diag.disproved ? "DISPROVED, " : "",
              trend);
    page.rowf("Samples / last", "%lu / %.1f s ago", static_cast<unsigned long>(diag.samples),
              diag.sampled ? static_cast<double>(millis() - diag.lastSampleMs) / 1000.0 : 0.0);
    if (s.neverComplete) {
      page.row("Complete reported this stage", (stageSeen & stateBit(State::Complete)) != 0 ? "YES (fail)" : "no");
    }
    RecentLevels recentNow{};
    if (s.clampCheck) {
      recentNow = recentLevels();
      if (recentCount == 0) {
        page.rowf("Recent samples (2100-2300 mV)", "0 of %u since the change, wait", RECENT);
      } else {
        page.rowf("Recent samples (2100-2300 mV)", "%u of %u%s, %u-%u mV", recentCount, RECENT,
                  recentCount < RECENT ? ", wait" : "", recentNow.lo, recentNow.hi);
        page.rowf("Clamped / partial / above / 3.3 V", "%u / %u / %u / %u", recentNow.clamped, recentNow.partial,
                  recentNow.above, recentNow.stHigh);
      }
    }
    if (s.capture) drawCapture(page);
    page.setCompact(false);
    const char* note = nullptr;
    if (s.clampCheck) {
      note = recentNow.clamped > 0   ? HW08_NOTE
             : recentNow.stHigh > 0  ? ST_NOTE
             : recentNow.partial > 0 ? PARTIAL_NOTE
             : recentNow.above > 0   ? ABOVE_NOTE
                                     : nullptr;
    }
    if (note != nullptr) {
      page.gap(4);
      page.paragraph(note, 4);
    }
    page.gap(4);
    page.setCompact(true);
    page.heading("Changes (time on this screen)");
    if (historyCount == 0) page.row("none yet", "");
    for (uint8_t i = 0; i < historyCount; ++i) {
      // Newest first.
      const Change& c = history[(historyHead + HISTORY - 1 - i) % HISTORY];
      const unsigned long t = (c.atMs - openedAt) / 1000UL;
      char label[48];
      snprintf(label, sizeof(label), "%lu:%02lu %s > %s", t / 60UL, t % 60UL, chargeStateName(c.from),
               chargeStateName(c.to));
      if (c.settleKnown) {
        page.rowf(label, "%u mV, %.1f s", c.mv, static_cast<double>(c.settleMs) / 1000.0);
      } else {
        page.rowf(label, "%u mV", c.mv);
      }
    }
    page.setCompact(false);
  }

  BatteryMonitor::ChargeLadderDiagnostics diag;
  unsigned long openedAt = 0;
  unsigned long drawnAt = 0;
  uint32_t seenSamples = 0;
  State published = State::Unknown;
  State runWindow = State::Unknown;
  uint32_t runStartMs = 0;
  bool dirty = false;
  bool usbConnected = false;
  bool noCellNotice = false;  // HalPowerManager::noChargeableCell()
  unsigned usbEdges = 0;
  int stage = 0;
  unsigned long stageStartMs = 0;
  uint8_t stageSeen = 0;
  bool stageReached = false;
  Change stageSettle{};
  Change history[HISTORY] = {};
  uint8_t historyHead = 0;
  uint8_t historyCount = 0;
  uint16_t recent[RECENT] = {};
  uint8_t recentHead = 0;
  uint8_t recentCount = 0;
  // Stage 5's capture, in the heap-allocated screen (about 1 KB).
  bool capturing = false;
  unsigned long captureStartMs = 0;
  unsigned long captureLastMs = 0;
  int captureCount = 0;
  CaptureSample capture[CAPTURE_SAMPLES] = {};
  Blink blink;
};

}  // namespace

std::unique_ptr<Activity> createChargeTest(const TestInfo& info, GfxRenderer& renderer,
                                           MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ChargeTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
