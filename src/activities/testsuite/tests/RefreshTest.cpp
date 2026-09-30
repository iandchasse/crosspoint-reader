#if CROSSPOINT_TEST_SUITE
// T4: B/W refresh modes (bring-up B2). A run alternates two full-screen frames
// with one kind of refresh and logs, per refresh, the mode the driver actually
// ran (a FAST can be promoted to HALF), its update-sequence byte and its BUSY
// time; the operator judges the ghosting. The turn-off variants run the
// Sunlight Fading Fix through SETTINGS.fadingFix, which the loop hands to the
// renderer, so they exercise the reader's own path.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "PanelPatterns.h"
#include "PanelRun.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"

namespace testsuite {
namespace {

constexpr int FRAMES_PER_RUN = 6;
constexpr int MAX_STEPS = 32;  // the cadence: up to 30 page turns
constexpr int MODES = 3;       // FULL, HALF, FAST, in HalDisplay::RefreshMode order
constexpr int QUESTION_LOOK = 1;

// X4-class references for this panel and waveform set (Ssd1677Driver's default
// config: FULL about 1800 ms, FAST about 500 ms; HalDisplay.h: HALF about
// 1720 ms). A mean outside +/-50 % is flagged, not failed.
constexpr uint32_t REFERENCE_MS[MODES] = {1800, 1720, 500};
constexpr uint32_t REFERENCE_TOLERANCE_PCT = 50;

struct Variant {
  const char* name;  // on the screen
  const char* key;   // in results.csv
  HalDisplay::RefreshMode mode;
  bool async;
  bool turnOff;
  bool cadence;
  const char* expect;
};

constexpr Variant VARIANTS[] = {
    {"FULL", "full", HalDisplay::FULL_REFRESH, false, false, false,
     "No ghost of the previous frame after any refresh."},
    {"HALF", "half", HalDisplay::HALF_REFRESH, false, false, false, "At most a faint ghost of the previous frame."},
    {"FAST", "fast", HalDisplay::FAST_REFRESH, false, false, false,
     "Ghosting builds up from frame to frame; every frame stays readable."},
    {"FAST, async", "fast_async", HalDisplay::FAST_REFRESH, true, false, false,
     "As FAST. The call returns while the panel refreshes (the reader's overlapped chrome)."},
    {"FULL, turn-off", "full_off", HalDisplay::FULL_REFRESH, false, true, false,
     "Looks the same as FULL. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"HALF, turn-off", "half_off", HalDisplay::HALF_REFRESH, false, true, false,
     "Looks the same as HALF. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"FAST, turn-off", "fast_off", HalDisplay::FAST_REFRESH, false, true, false,
     "Looks the same as FAST. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"Cadence", "cadence", HalDisplay::FAST_REFRESH, false, false, true,
     "The reader's page turns: FAST ones build up ghosting, and every N-th turn is a HALF that clears it (N: the "
     "reader's refresh frequency)."},
};
constexpr int VARIANT_COUNT = static_cast<int>(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

constexpr const char* LOOK_ANSWERS[] = {"As expected", "Worse than expected", "Corrupt rows or noise"};
constexpr const char* LOOK_KEYS[] = {"as_expected", "worse", "corrupt"};

constexpr char INTRO[] =
    "Left/Right pick a refresh, Confirm runs it: 6 refreshes alternating two frames (Cadence: the reader's page "
    "turns). Watch every frame, then answer. Back stops a run.";

// Mean BUSY per mode the driver ran, over this boot's completed runs, for the
// FULL >= HALF > FAST check.
BusyStats bootStats[MODES];

// -1: not all three measured yet; 0: out of order; 1: in order.
int modeOrder() {
  for (const auto& stats : bootStats) {
    if (stats.count == 0) return -1;
  }
  const uint32_t full = bootStats[HalDisplay::FULL_REFRESH].meanMs();
  const uint32_t half = bootStats[HalDisplay::HALF_REFRESH].meanMs();
  const uint32_t fast = bootStats[HalDisplay::FAST_REFRESH].meanMs();
  return full >= half && half > fast ? 1 : 0;
}

bool nearReference(const int mode, const uint32_t meanMs) {
  const uint32_t reference = REFERENCE_MS[mode];
  const uint32_t tolerance = reference * REFERENCE_TOLERANCE_PCT / 100;
  return meanMs + tolerance >= reference && meanMs <= reference + tolerance;
}

// What a completed run measured.
struct RunSummary {
  bool valid = false;
  int variant = 0;
  int steps = 0;
  uint8_t ranCount[MODES] = {};
  uint8_t sequence[MODES] = {};  // the last update-sequence byte seen per mode
  BusyStats busy[MODES];
  uint32_t hostSumMs = 0;  // call time outside BUSY: SPI transfers and power-down
  int promoted = 0;
  int bad = 0;  // not reported, timed out or outside the BUSY window
  int asyncOk = 0;
  int turnOffSeen = 0;
};

class RefreshTest final : public PanelRunTest {
 public:
  RefreshTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : PanelRunTest(info, renderer, mappedInput) {}

 private:
  static int cadenceTurns() {
    const int frequency = SETTINGS.getRefreshFrequency();
    return frequency < 2 ? 2 : (frequency > MAX_STEPS ? MAX_STEPS : frequency);
  }
  static int stepsOf(const Variant& variant) { return variant.cadence ? cadenceTurns() : FRAMES_PER_RUN; }

  // The mode step `step` asks for: the cadence's last turn is its HALF.
  HalDisplay::RefreshMode modeOf(const int step) const {
    const Variant& variant = VARIANTS[runVariant];
    return variant.cadence && step == runSteps - 1 ? HalDisplay::HALF_REFRESH : variant.mode;
  }
  // The last frame, and the cadence's frame before its HALF, stay up for a look.
  bool pausesAfter(const int step) const {
    return step == runSteps - 1 || (VARIANTS[runVariant].cadence && step == runSteps - 2);
  }

  const char* slotLabel(const Slot slot) const override {
    switch (slot) {
      case Slot::Confirm:
        return "Run";
      case Slot::Left:
        return "Prev";
      case Slot::Right:
        return "Next";
      default:
        return nullptr;
    }
  }

  void onIdleSlot(const Slot slot) override {
    if (slot == Slot::Confirm) {
      start();
    } else {
      select(slot == Slot::Left ? -1 : +1);
    }
  }
  void onIdleStep(const int direction) override { select(-direction); }

  void select(const int delta) {
    selected = (selected + delta + VARIANT_COUNT) % VARIANT_COUNT;
    requestUpdate();
  }

  void start() {
    runVariant = selected;
    runSteps = stepsOf(VARIANTS[runVariant]);
    summary = RunSummary{};
    stoppedAfter = -1;
    completedSteps = 0;
    if (VARIANTS[runVariant].turnOff) SETTINGS.fadingFix = 1;
    startRun(runSteps);
  }

  void runStep(const int step) override {
    const Variant& variant = VARIANTS[runVariant];
    const HalDisplay::RefreshMode mode = modeOf(step);
    const bool frameB = step % 2 == 1;
    renderer.clearScreen();
    patterns::drawBwFrame(renderer, frameB);
    char label[48];
    snprintf(label, sizeof(label), "%s  %s  %d/%d  %s", info.id, variant.name, step + 1, runSteps,
             refreshModeName(mode));
    drawFrameLabel(label, frameB ? "Frame B" : "Frame A", pausesAfter(step));
    if (variant.async) {
      samples[step] = refreshAsyncStart(renderer, mode);
      refreshAsyncFinish(renderer, samples[step], /*reseed=*/true);
    } else {
      samples[step] = refreshTimed(renderer, mode);
    }
  }

  void afterStep(const int step) override {
    const RefreshSample& s = samples[step];
    completedSteps = step + 1;
    LOG_INF("TEST", "T4 %s %d/%d: asked %s, ran %s, 0x22=0x%02X, BUSY %lu ms%s, call %u ms%s%s%s",
            VARIANTS[runVariant].key, step + 1, runSteps, refreshModeName(s.requested), refreshModeName(s.ran),
            s.sequence, static_cast<unsigned long>(s.busyMs), s.timedOut ? " (timed out)" : "", s.callMs,
            s.async ? ", async" : "", s.async && s.busyAtReturn ? " (returned while BUSY)" : "",
            s.turnOff ? ", turn-off" : "");
    if (pausesAfter(step)) pauseRun();
  }

  void onRunEnd(const bool completed) override {
    if (!completed) {
      stoppedAfter = completedSteps;
      requestUpdate();
      return;
    }
    summarize();
    ask(QUESTION_LOOK, VARIANTS[runVariant].name, VARIANTS[runVariant].expect, LOOK_ANSWERS, 3);
  }

  void summarize() {
    RunSummary s;
    s.variant = runVariant;
    s.steps = runSteps;
    for (int i = 0; i < runSteps; ++i) {
      const RefreshSample& sample = samples[i];
      if (sample.promoted()) ++s.promoted;
      if (!sample.inWindow()) ++s.bad;
      if (sample.turnOff) ++s.turnOffSeen;
      if (sample.async && sample.busyAtReturn && sample.callMs < sample.busyMs) ++s.asyncOk;
      if (!sample.reported() || sample.ran >= MODES) continue;
      ++s.ranCount[sample.ran];
      s.sequence[sample.ran] = sample.sequence;
      s.hostSumMs += sample.hostMs();
      if (sample.inWindow()) {
        s.busy[sample.ran].add(sample.busyMs);
        bootStats[sample.ran].add(sample.busyMs);
      }
    }
    s.valid = true;
    summary = s;
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_LOOK || !summary.valid) return;
    if (answer < 0) {
      LOG_INF("TEST", "T4: no answer, nothing recorded");
      return;
    }
    const Variant& variant = VARIANTS[summary.variant];
    const int order = modeOrder();
    char fails[64] = "";
    if (summary.bad != 0) addFailure(fails, sizeof(fails), "busy_window");
    if (summary.promoted != 0) addFailure(fails, sizeof(fails), "promoted");
    if (variant.async && summary.asyncOk != summary.steps) addFailure(fails, sizeof(fails), "async");
    if (variant.turnOff && summary.turnOffSeen != summary.steps) addFailure(fails, sizeof(fails), "turn_off");
    if (order == 0) addFailure(fails, sizeof(fails), "mode_order");
    if (answer != 0) addFailure(fails, sizeof(fails), "operator");

    // "ran" is mode:count@update-sequence byte; busy_ms is min/mean/max per mode.
    char ran[48] = "";
    char busy[80] = "";
    char outside[24] = "";
    for (int mode = 0; mode < MODES; ++mode) {
      if (summary.ranCount[mode] == 0) continue;
      appendf(ran, sizeof(ran), "%s%s:%u@%02X", ran[0] != '\0' ? "," : "", refreshModeName(mode),
              summary.ranCount[mode], summary.sequence[mode]);
      const BusyStats& stats = summary.busy[mode];
      if (stats.count == 0) continue;
      appendf(busy, sizeof(busy), "%s%s:%lu/%lu/%lu", busy[0] != '\0' ? "," : "", refreshModeName(mode),
              static_cast<unsigned long>(stats.minMs), static_cast<unsigned long>(stats.meanMs()),
              static_cast<unsigned long>(stats.maxMs));
      if (!nearReference(mode, stats.meanMs())) addFailure(outside, sizeof(outside), refreshModeName(mode));
    }
    snprintf(resultLine, sizeof(resultLine), "variant=%s ran=%s busy_ms=%s host_ms=%lu promoted=%d bad=%d", variant.key,
             ran, busy[0] != '\0' ? busy : "-",
             static_cast<unsigned long>(summary.hostSumMs / static_cast<uint32_t>(summary.steps)), summary.promoted,
             summary.bad);
    if (variant.async) appendf(resultLine, sizeof(resultLine), " async=%d/%d", summary.asyncOk, summary.steps);
    if (variant.turnOff) appendf(resultLine, sizeof(resultLine), " turn_off=%d/%d", summary.turnOffSeen, summary.steps);
    appendf(resultLine, sizeof(resultLine), " x4_ref=%s%s order=%s spi_khz=%lu operator=%s fails=%s",
            outside[0] != '\0' ? "outside:" : "ok", outside,
            order < 0   ? "incomplete"
            : order > 0 ? "ok"
                        : "bad",
            static_cast<unsigned long>(panelSpiHz() / 1000), LOOK_KEYS[answer], fails[0] == '\0' ? "none" : fails);
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail, "%s", resultLine);
  }

  void drawBody(TestPage& page) const override {
    const Variant& variant = VARIANTS[selected];
    page.paragraph(INTRO, 4);
    page.gap(4);
    page.rowf("Refresh", "%d/%d  %s", selected + 1, VARIANT_COUNT, variant.name);
    page.paragraph(variant.expect, 3);
    page.setCompact(true);
    if (variant.cadence) page.rowf("Reader refresh frequency (N)", "%d", cadenceTurns());
    page.rowf("Panel SPI clock", "%lu kHz", static_cast<unsigned long>(panelSpiHz() / 1000));
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d refreshes", stoppedAfter);
    if (summary.valid) {
      page.gap(4);
      char heading[48];
      snprintf(heading, sizeof(heading), "Last run: %s", VARIANTS[summary.variant].name);
      page.heading(heading);
      for (int mode = 0; mode < MODES; ++mode) {
        if (summary.ranCount[mode] == 0) continue;
        char label[40];
        snprintf(label, sizeof(label), "Ran %s x%u, 0x22 = 0x%02X", refreshModeName(mode), summary.ranCount[mode],
                 summary.sequence[mode]);
        const BusyStats& stats = summary.busy[mode];
        page.rowf(label, "BUSY %lu / %lu / %lu ms", static_cast<unsigned long>(stats.minMs),
                  static_cast<unsigned long>(stats.meanMs()), static_cast<unsigned long>(stats.maxMs));
      }
      page.rowf("Host time per refresh (mean)", "%lu ms",
                static_cast<unsigned long>(summary.hostSumMs / static_cast<uint32_t>(summary.steps)));
      page.rowf("Promoted / outside the BUSY window", "%d / %d", summary.promoted, summary.bad);
      if (VARIANTS[summary.variant].async) {
        page.rowf("Returned while BUSY", "%d / %d", summary.asyncOk, summary.steps);
      }
      if (VARIANTS[summary.variant].turnOff) {
        page.rowf("Turn-off requested", "%d / %d", summary.turnOffSeen, summary.steps);
      }
    }
    page.gap(4);
    page.heading("Mean BUSY this boot");
    for (int mode = 0; mode < MODES; ++mode) {
      const BusyStats& stats = bootStats[mode];
      if (stats.count == 0) {
        page.rowf(refreshModeName(mode), "not run (X4: %lu ms)", static_cast<unsigned long>(REFERENCE_MS[mode]));
        continue;
      }
      page.rowf(refreshModeName(mode), "%lu ms, n=%u (X4 %lu: %s)", static_cast<unsigned long>(stats.meanMs()),
                stats.count, static_cast<unsigned long>(REFERENCE_MS[mode]),
                nearReference(mode, stats.meanMs()) ? "ok" : "outside +/-50%");
    }
    const int order = modeOrder();
    page.row("FULL >= HALF > FAST", order < 0 ? "run all three" : order > 0 ? "yes" : "NO");
  }

  int selected = 0;
  int runVariant = 0;
  int runSteps = 0;
  int completedSteps = 0;
  int stoppedAfter = -1;
  RefreshSample samples[MAX_STEPS];
  RunSummary summary;
  char resultLine[256] = "";  // the result being built (off the loop task's stack)
};

}  // namespace

std::unique_ptr<Activity> createRefreshTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RefreshTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
