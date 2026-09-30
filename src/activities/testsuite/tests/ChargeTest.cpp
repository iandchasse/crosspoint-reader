#if CROSSPOINT_TEST_SUITE
// T2: charge-status ladder (USB_STAT) and USB presence (bring-up B6). The ladder
// is sampled by the SDK alone (at most once per ChargeStatusLadder::minIntervalMs,
// usbstat-slow-sampling): this screen reads its record
// (BatteryMonitor::chargeLadderDiagnostics()) and never touches the pin.
#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <ChargeLadderDecoder.h>
#include <HalGPIO.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "TestFactories.h"
#include "TestNames.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestScreen.h"

namespace testsuite {
namespace {

using State = BoardConfig::ChargeState;
using Decoder = freeink::battery_detail::ChargeLadderDecoder<BoardConfig::ChargeLadderWindow>;

constexpr unsigned long REDRAW_MS = 5000;
constexpr int QUESTION_D2 = 1;
constexpr uint8_t HISTORY = 4;

// One bench state to set up and check. D2 lights whenever USB power is present,
// whatever the charger does (HARDWARE.md §3).
struct Stage {
  const char* title;
  const char* setup;
  State expect;
  bool usbConnected;     // HalGPIO::isUsbConnected()
  bool d2Lit;            // the operator's answer
  uint16_t maxSettleMs;  // first sample in the new window -> published state
  bool neverComplete;    // Complete must not be reported during the stage
};

// Settle limits from the ladder's rules (BoardConfig ChargeStatusLadder): a
// state change needs 2 agreeing samples a second apart; Complete needs 8 stable
// samples; the no-battery blink needs 3 in-window samples that swing 70 mV,
// then the debounce.
constexpr Stage STAGES[] = {
    {"1 Battery only", "Unplug USB. On a meter USB_VBUS reads about 0 V (HW-08).", State::OnBattery, false, false, 3000,
     false},
    {"2 Charging", "Plug a USB charger with a partly discharged cell (about 1.11 V).", State::Charging, true, true,
     3000, false},
    {"3 Weak USB (optional)", "A weak USB source or a long thin cable (about 0.95 V).", State::Charging, true, true,
     3000, false},
    {"4 Complete (optional, long)", "A nearly full cell on USB until the charger stops (about 0.55 V).",
     State::Complete, true, true, 10000, false},
    {"5 Cell pulled on USB (optional)",
     "Protected cell and insulated tools only: pull the cell with USB in. 0.55/0.41 V blink.", State::NoBattery, true,
     true, 8000, true},
    {"6 USB, no cell", "Cold boot from USB with no cell fitted (about 3.3 V).", State::ExternalIdle, true, true, 3000,
     false},
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

  void onStart() override {
    openedAt = millis();
    drawnAt = openedAt;
    diag = BatteryMonitor::chargeLadderDiagnostics();
    seenSamples = diag.samples;
    published = diag.published;
    runWindow = Decoder::classify(ladderParams(), static_cast<int32_t>(diag.lastMv));
    runStartMs = diag.lastSampleMs;
    usbConnected = gpio.isUsbConnected();
    startStage();
  }

  void onTick() override {
    const unsigned long now = millis();
    diag = BatteryMonitor::chargeLadderDiagnostics();
    bool changed = false;
    if (diag.samples != seenSamples) {
      seenSamples = diag.samples;
      dirty = true;
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
    if (changed || (dirty && now - drawnAt >= REDRAW_MS)) {
      dirty = false;
      drawnAt = now;
      requestUpdate();
    }
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
    if (published == STAGES[stage].expect) {
      stageReached = true;
      stageSettle = c;
    }
  }

  void startStage() {
    stageSeen = stateBit(published);
    stageReached = false;
    stageStartMs = millis();
  }

  const char* slotLabel(const Slot slot) const override {
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
    if (slot == Slot::Confirm) {
      ask(QUESTION_D2, D2_HEADING, D2_BODY, D2_ANSWERS, 2);
      return;
    }
    const int step = slot == Slot::Left ? -1 : +1;
    stage = (stage + step + STAGE_COUNT) % STAGE_COUNT;
    startStage();
    drawnAt = millis();
    requestUpdate();
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_D2 || answer < 0) return;
    // The ladder kept going while the question was open: judge it as it is now.
    onTick();
    const Stage& s = STAGES[stage];
    const bool d2Lit = answer == 1;
    char fails[64] = "";
    const auto fail = [&fails](const char* what) {
      if (fails[0] != '\0') strncat(fails, "+", sizeof(fails) - strlen(fails) - 1);
      strncat(fails, what, sizeof(fails) - strlen(fails) - 1);
    };
    if (published != s.expect) fail("state");
    uint16_t lo = 0;
    uint16_t hi = 0;
    if (!diag.sampled || !windowFor(s.expect, lo, hi) || diag.lastMv < lo || diag.lastMv > hi) fail("window");
    if (usbConnected != s.usbConnected) fail("usb");
    if (d2Lit != s.d2Lit) fail("d2");
    if (stageReached && stageSettle.settleKnown && stageSettle.settleMs > s.maxSettleMs) fail("slow");
    if (s.neverComplete && (stageSeen & stateBit(State::Complete)) != 0) fail("reported_complete");
    char settle[16];
    if (stageReached && stageSettle.settleKnown) {
      snprintf(settle, sizeof(settle), "%ums", stageSettle.settleMs);
    } else {
      snprintf(settle, sizeof(settle), "n/a");
    }
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "stage=%d stage_s=%lu state=%s decoded=%s mv=%lu usb=%d d2=%s settle=%s lost=%d disproved=%d "
           "samples=%lu complete_seen=%d fails=%s",
           stage + 1, (millis() - stageStartMs) / 1000UL, chargeStateName(published), chargeStateName(diag.decoded),
           static_cast<unsigned long>(diag.lastMv), usbConnected ? 1 : 0, d2Lit ? "lit" : "dark", settle,
           diag.lost ? 1 : 0, diag.disproved ? 1 : 0, static_cast<unsigned long>(diag.samples),
           (stageSeen & stateBit(State::Complete)) != 0 ? 1 : 0, fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    const Stage& s = STAGES[stage];
    const auto& ladder = BoardConfig::ACTIVE.chargeLadder;
    page.heading(s.title);
    page.paragraph(s.setup, 2);
    uint16_t lo = 0;
    uint16_t hi = 0;
    windowFor(s.expect, lo, hi);
    page.setCompact(true);
    page.rowf("Expect", "%s, %u-%u mV, USB %s, D2 %s", chargeStateName(s.expect), lo, hi, s.usbConnected ? "yes" : "no",
              s.d2Lit ? "lit" : "dark");
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
    page.gap(4);
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
  unsigned usbEdges = 0;
  int stage = 0;
  unsigned long stageStartMs = 0;
  uint8_t stageSeen = 0;
  bool stageReached = false;
  Change stageSettle{};
  Change history[HISTORY] = {};
  uint8_t historyHead = 0;
  uint8_t historyCount = 0;
};

}  // namespace

std::unique_ptr<Activity> createChargeTest(const TestInfo& info, GfxRenderer& renderer,
                                           MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ChargeTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
