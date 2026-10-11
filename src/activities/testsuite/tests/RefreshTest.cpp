#if CROSSPOINT_TEST_SUITE
// T4: B/W refresh modes (bring-up B2). A run alternates two full-screen frames
// with one kind of refresh and logs, per refresh, the mode the driver actually
// ran (a FAST can be promoted to HALF), its sequence byte (SSD1677: 0x22;
// UC8279: TSSET; X3 drivers: the CDI byte) and its BUSY time, with the panel
// controller the boot resolved;
// the operator judges the ghosting. The turn-off variants run the
// Sunlight Fading Fix through SETTINGS.fadingFix, which the loop hands to the
// renderer, so they exercise the reader's own path. The scope loops repeat one
// refresh until any key or tap (BRING_UP section 8: PREVGH/PREVGL, VSH/VSL, GDR
// and RESE on a scope), for at most LOOP_MAX_MS.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "PanelFamily.h"
#include "PanelPatterns.h"
#include "PanelProbe.h"
#include "PanelRun.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"

namespace testsuite {
namespace {

constexpr int FRAMES_PER_RUN = 6;
constexpr int MAX_STEPS = 32;  // the cadence: up to 30 page turns
// A scope loop ends by itself after this long or this many refreshes (battery:
// it holds auto-sleep off while it runs).
constexpr unsigned long LOOP_MAX_MS = 5UL * 60UL * 1000UL;
constexpr int LOOP_MAX_REFRESHES = 1000;
constexpr int MODES = 3;  // FULL, HALF, FAST, in HalDisplay::RefreshMode order
constexpr int QUESTION_LOOK = 1;

// X4-class references for this panel and waveform set (Ssd1677Driver's default
// config: FULL about 1800 ms, FAST about 500 ms; HalDisplay.h: HALF about
// 1720 ms). A mean outside +/-50 % is flagged, not failed. A UC8279 panel runs
// other waveforms: its runs set the baseline instead. So do the X3 drivers of
// the Silkscreen 3.68" variant (X3_PANEL_BUILD, PanelFamily.h): no reference
// yet, record it, and neither the FULL >= HALF > FAST order nor a promotion
// is judged there (a HALF is a resync by design, the first paints run as FULL).
constexpr uint32_t REFERENCE_MS[MODES] = {1800, 1720, 500};
constexpr uint32_t REFERENCE_TOLERANCE_PCT = 50;
constexpr char X3_NOTE[] =
    "X3 drivers: a HALF is a resync and the first paints after boot run as FULL: recorded, not failed. No "
    "reference times yet: these runs set them.";

struct Variant {
  const char* name;  // on the screen
  const char* key;   // in results.csv
  HalDisplay::RefreshMode mode;
  bool async;
  bool turnOff;
  bool cadence;
  bool loop;  // a scope loop: until any key or tap, judged on the driver's record only
  const char* expect;
};

constexpr Variant VARIANTS[] = {
    {"FULL", "full", HalDisplay::FULL_REFRESH, false, false, false, false,
     "No ghost of the previous frame after any refresh."},
    {"HALF", "half", HalDisplay::HALF_REFRESH, false, false, false, false,
     "At most a faint ghost of the previous frame."},
    {"FAST", "fast", HalDisplay::FAST_REFRESH, false, false, false, false,
     "Ghosting builds up from frame to frame; every frame stays readable."},
    {"FAST, async", "fast_async", HalDisplay::FAST_REFRESH, true, false, false, false,
     "As FAST. The call returns while the panel refreshes (the reader's overlapped chrome)."},
    {"FULL, turn-off", "full_off", HalDisplay::FULL_REFRESH, false, true, false, false,
     "Looks the same as FULL. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"HALF, turn-off", "half_off", HalDisplay::HALF_REFRESH, false, true, false, false,
     "Looks the same as HALF. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"FAST, turn-off", "fast_off", HalDisplay::FAST_REFRESH, false, true, false, false,
     "Looks the same as FAST. The panel powers down after each refresh (Sunlight Fading Fix)."},
    {"Cadence", "cadence", HalDisplay::FAST_REFRESH, false, false, true, false,
     "The reader's page turns: FAST ones build up ghosting, and every N-th turn is a HALF that clears it (N: the "
     "reader's refresh frequency)."},
    {"FULL, scope loop", "full_loop", HalDisplay::FULL_REFRESH, false, false, false, true,
     "FULL refreshes back to back until any key or tap (5 min at most), for the panel rails on a scope."},
    {"HALF, scope loop", "half_loop", HalDisplay::HALF_REFRESH, false, false, false, true,
     "HALF refreshes back to back until any key or tap (5 min at most), for the panel rails on a scope."},
    {"FAST, scope loop", "fast_loop", HalDisplay::FAST_REFRESH, false, false, false, true,
     "FAST refreshes back to back until any key or tap (5 min at most), for the panel rails on a scope."},
};
constexpr int VARIANT_COUNT = static_cast<int>(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

constexpr const char* LOOK_ANSWERS[] = {"As expected", "Worse than expected", "Corrupt rows or noise"};
constexpr const char* LOOK_KEYS[] = {"as_expected", "worse", "corrupt"};

constexpr char INTRO[] =
    "Left/Right pick a refresh, Confirm runs it: 6 refreshes alternating two frames (Cadence: the reader's page "
    "turns; a scope loop: until any key). Watch every frame, then answer. Back stops a run.";
// BRING_UP section 8, with 10x probes (two rails are negative).
constexpr char SCOPE_NOTE[] =
    "Scope (10x probes): PREVGH C14.2 about +22 V, PREVGL C16.2 about -22 V, VSH1 C13.2 about +15 V, VSL C15.2 "
    "about -15 V; GDR at Q4.1 and RESE at Q4.2/R14.2: clean pulses, RESE peaks well under 2.4 V.";

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
  static int stepsOf(const Variant& variant) {
    if (variant.loop) return LOOP_MAX_REFRESHES;
    return variant.cadence ? cadenceTurns() : FRAMES_PER_RUN;
  }
  bool loopRun() const { return runOn() && VARIANTS[runVariant].loop; }
  bool stopsOnAnyInput() const override { return loopRun(); }
  // A scope loop is bounded (LOOP_MAX_MS) and gets no input to keep the board awake.
  bool preventAutoSleep() override { return loopRun(); }

  // The mode step `step` asks for: the cadence's last turn is its HALF.
  HalDisplay::RefreshMode modeOf(const int step) const {
    const Variant& variant = VARIANTS[runVariant];
    return variant.cadence && step == runSteps - 1 ? HalDisplay::HALF_REFRESH : variant.mode;
  }
  // The last frame, and the cadence's frame before its HALF, stay up for a look.
  bool pausesAfter(const int step) const {
    if (VARIANTS[runVariant].loop) return false;
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
    loopStats = LoopStats{};
    stoppedAfter = -1;
    completedSteps = 0;
    if (VARIANTS[runVariant].turnOff) SETTINGS.fadingFix = 1;
    loopStartedAt = millis();
    startRun(runSteps);
  }

  void runStep(const int step) override {
    const Variant& variant = VARIANTS[runVariant];
    const HalDisplay::RefreshMode mode = modeOf(step);
    const bool frameB = step % 2 == 1;
    renderer.clearScreen();
    patterns::drawBwFrame(renderer, frameB);
    char label[48];
    if (variant.loop) {
      snprintf(label, sizeof(label), "%s  %s  #%d", info.id, variant.name, step + 1);
      drawFrameLabel(label, "Any key or tap: stop (5 min at most)", false);
      loopSample = refreshTimed(renderer, mode);
      return;
    }
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
    completedSteps = step + 1;
    if (VARIANTS[runVariant].loop) {
      afterLoopStep(step);
      return;
    }
    const RefreshSample& s = samples[step];
    LOG_INF("TEST", "T4 %s %d/%d: asked %s, ran %s, seq=0x%02X, BUSY %lu ms%s, call %u ms%s%s%s",
            VARIANTS[runVariant].key, step + 1, runSteps, refreshModeName(s.requested), refreshModeName(s.ran),
            s.sequence, static_cast<unsigned long>(s.busyMs), s.timedOut ? " (timed out)" : "", s.callMs,
            s.async ? ", async" : "", s.async && s.busyAtReturn ? " (returned while BUSY)" : "",
            s.turnOff ? ", turn-off" : "");
    if (pausesAfter(step)) pauseRun();
  }

  // One scope-loop refresh, kept in running totals (a loop outlasts samples[]).
  void afterLoopStep(const int step) {
    const RefreshSample& s = loopSample;
    LoopStats& l = loopStats;
    ++l.refreshes;
    if (s.promoted()) ++l.promoted;
    if (!s.inWindow()) ++l.bad;
    if (s.reported() && s.ran < MODES) {
      ++l.ranCount[s.ran];
      l.sequence[s.ran] = s.sequence;
      l.hostSumMs += s.hostMs();
      if (s.inWindow()) {
        l.busy[s.ran].add(s.busyMs);
        bootStats[s.ran].add(s.busyMs);
      }
    }
    LOG_DBG("TEST", "T4 %s #%d: ran %s, BUSY %lu ms%s", VARIANTS[runVariant].key, step + 1, refreshModeName(s.ran),
            static_cast<unsigned long>(s.busyMs), s.timedOut ? " (timed out)" : "");
    if (millis() - loopStartedAt >= LOOP_MAX_MS) {
      l.timedOut = true;
      endAfterThisStep();
    }
  }

  // Every end of a scope loop records it: the refreshes it ran and what the
  // driver reported. FAIL only on a refresh outside the BUSY window or promoted.
  void recordLoop(const bool completed) {
    const LoopStats& l = loopStats;
    char fails[48] = "";
    if (l.bad != 0) addFailure(fails, sizeof(fails), "busy_window");
    if (l.promoted != 0) addFailure(fails, sizeof(fails), "promoted");
    char ran[48] = "";
    char busy[80] = "";
    for (int mode = 0; mode < MODES; ++mode) {
      if (l.ranCount[mode] == 0) continue;
      appendf(ran, sizeof(ran), "%s%s:%lu@%02X", ran[0] != '\0' ? "," : "", refreshModeName(mode),
              static_cast<unsigned long>(l.ranCount[mode]), l.sequence[mode]);
      const BusyStats& stats = l.busy[mode];
      if (stats.count == 0) continue;
      appendf(busy, sizeof(busy), "%s%s:%lu/%lu/%lu", busy[0] != '\0' ? "," : "", refreshModeName(mode),
              static_cast<unsigned long>(stats.minMs), static_cast<unsigned long>(stats.meanMs()),
              static_cast<unsigned long>(stats.maxMs));
    }
    char panel[40];
    formatPanelToken(panel, sizeof(panel));
    const char* stop = !completed ? "input" : l.timedOut ? "timeout" : "limit";
    record(fails[0] == '\0' ? Verdict::Info : Verdict::Fail,
           "variant=%s panel=%s refreshes=%lu stop=%s elapsed_s=%lu ran=%s busy_ms=%s host_ms=%lu promoted=%lu "
           "bad=%lu spi_khz=%lu fails=%s",
           VARIANTS[runVariant].key, panel, static_cast<unsigned long>(l.refreshes), stop,
           static_cast<unsigned long>((millis() - loopStartedAt) / 1000UL), ran[0] != '\0' ? ran : "-",
           busy[0] != '\0' ? busy : "-", static_cast<unsigned long>(l.refreshes != 0 ? l.hostSumMs / l.refreshes : 0),
           static_cast<unsigned long>(l.promoted), static_cast<unsigned long>(l.bad),
           static_cast<unsigned long>(panelSpiHz() / 1000), fails[0] != '\0' ? fails : "none");
  }

  void onRunEnd(const bool completed) override {
    if (VARIANTS[runVariant].loop) {
      recordLoop(completed);
      return;
    }
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
    if (!X3_PANEL_BUILD && summary.promoted != 0) addFailure(fails, sizeof(fails), "promoted");
    if (variant.async && summary.asyncOk != summary.steps) addFailure(fails, sizeof(fails), "async");
    if (variant.turnOff && summary.turnOffSeen != summary.steps) addFailure(fails, sizeof(fails), "turn_off");
    if (!X3_PANEL_BUILD && order == 0) addFailure(fails, sizeof(fails), "mode_order");
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
      if (!X3_PANEL_BUILD && !nearReference(mode, stats.meanMs())) {
        addFailure(outside, sizeof(outside), refreshModeName(mode));
      }
    }
    char panel[40];
    formatPanelToken(panel, sizeof(panel));
    snprintf(resultLine, sizeof(resultLine), "variant=%s panel=%s ran=%s busy_ms=%s host_ms=%lu promoted=%d bad=%d",
             variant.key, panel, ran, busy[0] != '\0' ? busy : "-",
             static_cast<unsigned long>(summary.hostSumMs / static_cast<uint32_t>(summary.steps)), summary.promoted,
             summary.bad);
    if (variant.async) appendf(resultLine, sizeof(resultLine), " async=%d/%d", summary.asyncOk, summary.steps);
    if (variant.turnOff) appendf(resultLine, sizeof(resultLine), " turn_off=%d/%d", summary.turnOffSeen, summary.steps);
    appendf(resultLine, sizeof(resultLine), " x4_ref=%s%s order=%s spi_khz=%lu operator=%s fails=%s",
            X3_PANEL_BUILD       ? "none"
            : outside[0] != '\0' ? "outside:"
                                 : "ok",
            outside,
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
    if (X3_PANEL_BUILD) page.paragraph(X3_NOTE, 3);
    if (variant.loop) page.paragraph(SCOPE_NOTE, 4);
    page.setCompact(true);
    if (variant.cadence) page.rowf("Reader refresh frequency (N)", "%d", cadenceTurns());
    page.rowf("Panel SPI clock", "%lu kHz", static_cast<unsigned long>(panelSpiHz() / 1000));
    drawPanelProbe(page, /*brief=*/true);
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d refreshes", stoppedAfter);
    if (summary.valid) {
      page.gap(4);
      char heading[48];
      snprintf(heading, sizeof(heading), "Last run: %s", VARIANTS[summary.variant].name);
      page.heading(heading);
      for (int mode = 0; mode < MODES; ++mode) {
        if (summary.ranCount[mode] == 0) continue;
        char label[40];
        snprintf(label, sizeof(label), "Ran %s x%u, seq 0x%02X", refreshModeName(mode), summary.ranCount[mode],
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
      if (X3_PANEL_BUILD) {
        if (stats.count == 0) {
          page.row(refreshModeName(mode), "not run (no reference yet)");
        } else {
          page.rowf(refreshModeName(mode), "%lu ms, n=%u (no reference yet: record it)",
                    static_cast<unsigned long>(stats.meanMs()), stats.count);
        }
        continue;
      }
      if (stats.count == 0) {
        page.rowf(refreshModeName(mode), "not run (X4: %lu ms)", static_cast<unsigned long>(REFERENCE_MS[mode]));
        continue;
      }
      page.rowf(refreshModeName(mode), "%lu ms, n=%u (X4 %lu: %s)", static_cast<unsigned long>(stats.meanMs()),
                stats.count, static_cast<unsigned long>(REFERENCE_MS[mode]),
                nearReference(mode, stats.meanMs()) ? "ok" : "outside +/-50%");
    }
    const int order = modeOrder();
    if (X3_PANEL_BUILD) {
      page.row("FULL >= HALF > FAST", order < 0   ? "run all three"
                                      : order > 0 ? "yes (recorded)"
                                                  : "no (recorded, not judged)");
    } else {
      page.row("FULL >= HALF > FAST", order < 0 ? "run all three" : order > 0 ? "yes" : "NO");
    }
  }

  int selected = 0;
  int runVariant = 0;
  int runSteps = 0;
  int completedSteps = 0;
  int stoppedAfter = -1;
  RefreshSample samples[MAX_STEPS];
  RunSummary summary;
  // A scope loop's running totals.
  struct LoopStats {
    uint32_t refreshes = 0;
    uint32_t ranCount[MODES] = {};
    uint8_t sequence[MODES] = {};
    BusyStats busy[MODES];
    uint32_t hostSumMs = 0;
    uint32_t promoted = 0;
    uint32_t bad = 0;
    bool timedOut = false;
  };
  RefreshSample loopSample;  // written by runStep() on the render task, read by afterStep()
  LoopStats loopStats;
  unsigned long loopStartedAt = 0;
  char resultLine[320] = "";  // the result being built (off the loop task's stack)
};

}  // namespace

std::unique_ptr<Activity> createRefreshTest(const TestInfo& info, GfxRenderer& renderer,
                                            MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<RefreshTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
