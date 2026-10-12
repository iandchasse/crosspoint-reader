#if CROSSPOINT_TEST_SUITE
// T8: panel SPI clock trial (bring-up B2). The SSD1677 is specified to 20 MHz
// for writes (the profile's clock); this runs the T4 frames and the T5 chart at
// a faster clock set in RAM only (FreeInkDisplay::setSpiClockTrial, a
// FREEINK_DIAGNOSTICS control). The clock goes back to the profile's at the end
// of every run and when the screen closes, and any wake restores it anyway
// (display begin). The controller has no read-back on this board, so the
// operator compares the frames with a run at the profile clock; the host time
// per refresh (the SPI transfers) shows the clock's effect. On the Silkscreen
// 3.68" variant (X3_PANEL_BUILD, PanelFamily.h) the panel is the X3's, at the
// profile's 10 MHz: no reference exists for a faster clock yet, record it, and
// the X3 UC8279d driver reports no gray pass, so that run is judged by its base
// refresh and the operator's look. The screen opens on the profile clock there (a
// first bench run is the reference the trials are compared with), and a mode no
// refresh ran in reads n/a: the UC8279d runs a HALF turn on its one GC bank and
// reports it HALF, and a FAST that runs GC (the first paints after begin(), the
// paint after a resync) is reported FULL, so a FULL column can hold such turns.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <memory>

#include "GrayPass.h"
#include "PanelFamily.h"
#include "PanelPatterns.h"
#include "PanelRun.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestSuiteAccess.h"

namespace testsuite {
namespace {

constexpr int BW_REFRESHES = 20;
constexpr int STEP_GRAY = BW_REFRESHES;
constexpr int STEP_PLAIN = BW_REFRESHES + 1;
constexpr int STEP_COUNT = BW_REFRESHES + 2;
constexpr int HALF_EVERY = 5;  // four FAST, then a HALF, as the reader's page turns do
constexpr int MODES = 3;
constexpr int QUESTION_LOOK = 1;

struct Clock {
  const char* name;
  const char* key;
  uint32_t hz;  // 0: the profile's
};
constexpr Clock CLOCKS[] = {
    {"Profile clock", "profile", 0},
    {"26.7 MHz", "26m7", 26666667},
    {"40 MHz", "40m", 40000000},
};
constexpr int CLOCK_COUNT = static_cast<int>(sizeof(CLOCKS) / sizeof(CLOCKS[0]));

constexpr const char* LOOK_ANSWERS[] = {"Same as at the profile clock", "Corrupt, missing or shifted rows",
                                        "Wrong gray levels"};
constexpr const char* LOOK_KEYS[] = {"same", "corrupt_rows", "wrong_grays"};

constexpr char INTRO[] =
    "Left/Right pick a clock, Confirm runs it: 20 B/W refreshes (4 FAST, then a HALF), the gray chart and a plain "
    "page. The panel is back at its own clock afterwards. The SSD1677 is specified to 20 MHz: faster is a trial. "
    "Run the profile clock first, then compare.";
constexpr char INTRO_X3[] =
    "Left/Right pick a clock, Confirm runs it: 20 B/W refreshes (4 FAST, then a HALF), the gray chart and a plain "
    "page. The panel is back at its own clock afterwards. The profile's is the X3's: faster is a trial, and no "
    "reference exists for this panel yet, so record it. Run the profile clock first, then compare. The UC8279d "
    "driver does not report the gray pass.";
constexpr char CHART_FOOTER[] = "Any key or tap: continue.   Back: stop.";
constexpr char PLAIN_TITLE[] = "T8  After the gray pass: FAST";
constexpr char PLAIN_NOTE[] =
    "Below the line the page must be white, with no gray left from the chart. Any key or tap: continue. Back: "
    "stop.";

// What a completed run at one clock measured; kept for the boot, so a trial
// can be compared with the profile clock's run.
struct ClockResult {
  bool valid = false;
  uint32_t appliedHz = 0;
  BusyStats busy[MODES];
  uint32_t hostSumMs[MODES] = {};
  int bad = 0;
  int promoted = 0;
  bool grayRan = false;
  bool grayOk = false;
  bool missingBuffer = false;
  RefreshSample plain;
  const char* verdict = nullptr;  // after the operator's answer
  uint32_t hostMeanMs(const int mode) const { return busy[mode].count != 0 ? hostSumMs[mode] / busy[mode].count : 0; }
};
ClockResult results[CLOCK_COUNT];

#if FREEINK_SILKSCREEN_368
// The mean BUSY and host time of one mode of a run, or n/a when no refresh ran in it: a 0 there would be a
// number nobody measured. The UC8279d reports a HALF as HALF (its one GC bank), and a FAST that ran GC as FULL.
void meansColumns(char* out, const size_t size, const char* key, const ClockResult& r, const int mode) {
  if (r.busy[mode].count == 0) {
    snprintf(out, size, "%s_busy=n/a %s_host=n/a", key, key);
    return;
  }
  snprintf(out, size, "%s_busy=%lu %s_host=%lu", key, static_cast<unsigned long>(r.busy[mode].meanMs()), key,
           static_cast<unsigned long>(r.hostMeanMs(mode)));
}

// "<busy>+<host>", or n/a, for the screen.
void meansText(char* out, const size_t size, const ClockResult& r, const int mode) {
  if (r.busy[mode].count == 0) {
    snprintf(out, size, "n/a");
    return;
  }
  snprintf(out, size, "%lu+%lu", static_cast<unsigned long>(r.busy[mode].meanMs()),
           static_cast<unsigned long>(r.hostMeanMs(mode)));
}
#endif

class SpiClockTest final : public PanelRunTest {
 public:
  SpiClockTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : PanelRunTest(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    profileHz = panelSpiHz();
    scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(renderer.getDisplayWidthBytes()) * STRIP_ROWS);
    if (!scratch) LOG_ERR("TEST", "T8: OOM for the strip scratch; the gray pass will be skipped");
  }

  void onExit() override {
    scratch.reset();
    PanelRunTest::onExit();
  }

  void restorePanel() override {
    if (panelSpiHz() == profileHz) return;
    TestSuiteAccess::eink(display).setSpiClockTrial(0);
    LOG_INF("TEST", "T8: panel SPI clock back to %lu kHz", static_cast<unsigned long>(panelSpiHz() / 1000));
  }

  uint32_t clockHz(const int index) const { return CLOCKS[index].hz != 0 ? CLOCKS[index].hz : profileHz; }

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
      runClock = selected;
      results[runClock] = ClockResult{};
      stoppedAfter = -1;
      completedSteps = 0;
      startRun(STEP_COUNT);
      return;
    }
    select(slot == Slot::Left ? -1 : +1);
  }
  void onIdleStep(const int direction) override { select(-direction); }

  void select(const int delta) {
    selected = (selected + delta + CLOCK_COUNT) % CLOCK_COUNT;
    requestUpdate();
  }

  static HalDisplay::RefreshMode modeOf(const int step) {
    return step % HALF_EVERY == HALF_EVERY - 1 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  }
  static bool pausesAfter(const int step) { return step >= BW_REFRESHES - 1; }

  void runStep(const int step) override {
    ClockResult& result = results[runClock];
    char title[48];
    snprintf(title, sizeof(title), "%s  %s  %lu kHz", info.id, CLOCKS[runClock].name,
             static_cast<unsigned long>(clockHz(runClock) / 1000));
    if (step == 0) {
      TestSuiteAccess::eink(display).setSpiClockTrial(clockHz(runClock));
      result.appliedHz = panelSpiHz();
      LOG_INF("TEST", "T8: panel SPI clock %lu kHz for this run", static_cast<unsigned long>(result.appliedHz / 1000));
    }
    if (step == STEP_GRAY) {
      runGrayPass(renderer, GrayPath::TiledStrips, GrayBuffers{nullptr, nullptr, scratch.get()}, title, CHART_FOOTER,
                  gray);
      return;
    }
    if (step == STEP_PLAIN) {
      renderer.clearScreen();
      patterns::drawPlainPage(renderer, PLAIN_TITLE, PLAIN_NOTE);
      result.plain = refreshTimed(renderer, HalDisplay::FAST_REFRESH);
      return;
    }
    const bool frameB = step % 2 == 1;
    renderer.clearScreen();
    patterns::drawBwFrame(renderer, frameB);
    char label[48];
    snprintf(label, sizeof(label), "%d/%d  %s", step + 1, BW_REFRESHES, refreshModeName(modeOf(step)));
    drawFrameLabel(title, label, pausesAfter(step));
    samples[step] = refreshTimed(renderer, modeOf(step));
  }

  void afterStep(const int step) override {
    completedSteps = step + 1;
    ClockResult& result = results[runClock];
    if (step < BW_REFRESHES) {
      const RefreshSample& s = samples[step];
      LOG_INF("TEST", "T8 %lu kHz %d/%d: asked %s, ran %s, BUSY %lu ms, host %lu ms",
              static_cast<unsigned long>(result.appliedHz / 1000), step + 1, BW_REFRESHES, refreshModeName(s.requested),
              refreshModeName(s.ran), static_cast<unsigned long>(s.busyMs), static_cast<unsigned long>(s.hostMs()));
      if (s.promoted()) ++result.promoted;
      if (!s.inWindow()) ++result.bad;
      if (s.reported() && s.ran < MODES && s.inWindow()) {
        result.busy[s.ran].add(s.busyMs);
        result.hostSumMs[s.ran] += s.hostMs();
      }
    } else if (step == STEP_GRAY) {
      result.missingBuffer = gray.missingBuffer;
      result.grayRan = gray.grayRan();
      if (X3_PANEL_BUILD) {
        // No record of the gray waveform: the base refresh and the operator's look judge the run.
        result.grayOk = gray.base.inWindow();
        LOG_INF("TEST", "T8 gray pass: not reported by the UC8279d driver, strips %u ms", gray.renderMs);
      } else {
        result.grayOk = gray.grayRan() && gray.gray.inWindow() && gray.base.inWindow();
        LOG_INF("TEST", "T8 gray pass: %s, BUSY %lu ms, strips %u ms", result.grayRan ? "ran" : "skipped",
                static_cast<unsigned long>(gray.gray.busyMs), gray.renderMs);
      }
    }
    if (pausesAfter(step)) pauseRun();
  }

  void onRunEnd(const bool completed) override {
    if (!completed) {
      stoppedAfter = completedSteps;
      results[runClock] = ClockResult{};
      requestUpdate();
      return;
    }
    results[runClock].valid = true;
    ask(QUESTION_LOOK, CLOCKS[runClock].name,
        "Did every frame, the gray chart and the plain page look as they do at the profile clock?", LOOK_ANSWERS, 3);
  }

  void onAnswer(const int question, const int answer) override {
    ClockResult& result = results[runClock];
    if (question != QUESTION_LOOK || !result.valid) return;
    if (answer < 0) {
      LOG_INF("TEST", "T8: no answer, nothing recorded");
      return;
    }
    char fails[80] = "";
    if (result.bad != 0) addFailure(fails, sizeof(fails), "busy_window");
    if (!X3_PANEL_BUILD && result.promoted != 0) addFailure(fails, sizeof(fails), "promoted");
    if (result.missingBuffer) addFailure(fails, sizeof(fails), "scratch");
    if (!result.grayOk) addFailure(fails, sizeof(fails), "gray");
    // A promotion of the plain page is recorded, not judged, on the UC8279d driver (as T5 does).
    if (!result.plain.inWindow() || (!X3_PANEL_BUILD && result.plain.promoted())) {
      addFailure(fails, sizeof(fails), "plain_page");
    }
    if (answer != 0) addFailure(fails, sizeof(fails), "operator");
    const Verdict verdict = fails[0] == '\0' ? Verdict::Pass : Verdict::Fail;
    result.verdict = verdictName(verdict);
    const ClockResult& baseline = results[0];
#if FREEINK_SILKSCREEN_368
    char fast[48];
    char half[48];
    char full[48];
    meansColumns(fast, sizeof(fast), "fast", result, HalDisplay::FAST_REFRESH);
    meansColumns(half, sizeof(half), "half", result, HalDisplay::HALF_REFRESH);
    meansColumns(full, sizeof(full), "full", result, HalDisplay::FULL_REFRESH);
    record(verdict,
           "clock=%s clock_khz=%lu profile_khz=%lu %s %s %s baseline_fast_host=%s%lu bad=%d promoted=%d "
           "gray=not_reported operator=%s fails=%s",
           CLOCKS[runClock].key, static_cast<unsigned long>(result.appliedHz / 1000),
           static_cast<unsigned long>(profileHz / 1000), fast, half, full,
           baseline.valid ? "" : "none:", static_cast<unsigned long>(baseline.hostMeanMs(HalDisplay::FAST_REFRESH)),
           result.bad, result.promoted, LOOK_KEYS[answer], fails[0] == '\0' ? "none" : fails);
#else
    record(verdict,
           "clock=%s clock_khz=%lu profile_khz=%lu fast_busy=%lu fast_host=%lu half_busy=%lu half_host=%lu "
           "baseline_fast_host=%s%lu bad=%d promoted=%d gray=%s operator=%s fails=%s",
           CLOCKS[runClock].key, static_cast<unsigned long>(result.appliedHz / 1000),
           static_cast<unsigned long>(profileHz / 1000),
           static_cast<unsigned long>(result.busy[HalDisplay::FAST_REFRESH].meanMs()),
           static_cast<unsigned long>(result.hostMeanMs(HalDisplay::FAST_REFRESH)),
           static_cast<unsigned long>(result.busy[HalDisplay::HALF_REFRESH].meanMs()),
           static_cast<unsigned long>(result.hostMeanMs(HalDisplay::HALF_REFRESH)),
           baseline.valid ? "" : "none:", static_cast<unsigned long>(baseline.hostMeanMs(HalDisplay::FAST_REFRESH)),
           result.bad, result.promoted,
           X3_PANEL_BUILD   ? "not_reported"
           : result.grayRan ? "ran"
                            : "skipped",
           LOOK_KEYS[answer], fails[0] == '\0' ? "none" : fails);
#endif
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(X3_PANEL_BUILD ? INTRO_X3 : INTRO, 6);
    page.gap(4);
    page.rowf("Clock", "%d/%d  %s (%lu kHz)", selected + 1, CLOCK_COUNT, CLOCKS[selected].name,
              static_cast<unsigned long>(clockHz(selected) / 1000));
    page.setCompact(true);
    page.rowf("Panel SPI clock now", "%lu kHz", static_cast<unsigned long>(panelSpiHz() / 1000));
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d of %d steps", stoppedAfter, STEP_COUNT);
    page.gap(4);
#if FREEINK_SILKSCREEN_368
    page.heading("Mean BUSY + host ms (gray: not reported)");
#else
    page.heading("Mean BUSY + host time per refresh");
#endif
    for (int i = 0; i < CLOCK_COUNT; ++i) {
      const ClockResult& r = results[i];
      if (!r.valid) {
        page.row(CLOCKS[i].name, "not run");
        continue;
      }
#if FREEINK_SILKSCREEN_368
      char fast[24];
      char half[24];
      char full[24];
      meansText(fast, sizeof(fast), r, HalDisplay::FAST_REFRESH);
      meansText(half, sizeof(half), r, HalDisplay::HALF_REFRESH);
      meansText(full, sizeof(full), r, HalDisplay::FULL_REFRESH);
      page.rowf(CLOCKS[i].name, "FAST %s, HALF %s, FULL %s ms%s%s", fast, half, full, r.verdict != nullptr ? ", " : "",
                r.verdict != nullptr ? r.verdict : "");
#else
      page.rowf(CLOCKS[i].name, "FAST %lu+%lu, HALF %lu+%lu ms, gray %s%s%s",
                static_cast<unsigned long>(r.busy[HalDisplay::FAST_REFRESH].meanMs()),
                static_cast<unsigned long>(r.hostMeanMs(HalDisplay::FAST_REFRESH)),
                static_cast<unsigned long>(r.busy[HalDisplay::HALF_REFRESH].meanMs()),
                static_cast<unsigned long>(r.hostMeanMs(HalDisplay::HALF_REFRESH)),
                X3_PANEL_BUILD ? "not reported"
                : r.grayRan    ? "ran"
                               : "skipped",
                r.verdict != nullptr ? ", " : "", r.verdict != nullptr ? r.verdict : "");
#endif
    }
  }

  std::unique_ptr<uint8_t[]> scratch;
  uint32_t profileHz = 0;
  // The 40 MHz trial on the 4.26" board. A first bench run of the 3.68" variant opens on the profile clock:
  // the reference the trials are compared with, over R29-R34 and a longer flex.
  int selected = X3_PANEL_BUILD ? 0 : CLOCK_COUNT - 1;
  int runClock = 0;
  int completedSteps = 0;
  int stoppedAfter = -1;
  RefreshSample samples[BW_REFRESHES];
  GrayRun gray;
};

}  // namespace

std::unique_ptr<Activity> createSpiClockTest(const TestInfo& info, GfxRenderer& renderer,
                                             MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SpiClockTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
