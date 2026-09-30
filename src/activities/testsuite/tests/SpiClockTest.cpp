#if CROSSPOINT_TEST_SUITE
// T8: panel SPI clock trial (bring-up B2). The SSD1677 is specified to 20 MHz
// for writes (the profile's clock); this runs the T4 frames and the T5 chart at
// a faster clock set in RAM only (FreeInkDisplay::setSpiClockTrial, a
// FREEINK_DIAGNOSTICS control). The clock goes back to the profile's at the end
// of every run and when the screen closes, and any wake restores it anyway
// (display begin). The controller has no read-back on this board, so the
// operator compares the frames with a run at the profile clock; the host time
// per refresh (the SPI transfers) shows the clock's effect.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <memory>

#include "GrayPass.h"
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
      result.grayOk = gray.grayRan() && gray.gray.inWindow() && gray.base.inWindow();
      LOG_INF("TEST", "T8 gray pass: %s, BUSY %lu ms, strips %u ms", result.grayRan ? "ran" : "skipped",
              static_cast<unsigned long>(gray.gray.busyMs), gray.renderMs);
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
    if (result.promoted != 0) addFailure(fails, sizeof(fails), "promoted");
    if (result.missingBuffer) addFailure(fails, sizeof(fails), "scratch");
    if (!result.grayOk) addFailure(fails, sizeof(fails), "gray");
    if (!result.plain.inWindow() || result.plain.promoted()) addFailure(fails, sizeof(fails), "plain_page");
    if (answer != 0) addFailure(fails, sizeof(fails), "operator");
    const Verdict verdict = fails[0] == '\0' ? Verdict::Pass : Verdict::Fail;
    result.verdict = verdictName(verdict);
    const ClockResult& baseline = results[0];
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
           result.bad, result.promoted, result.grayRan ? "ran" : "skipped", LOOK_KEYS[answer],
           fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 6);
    page.gap(4);
    page.rowf("Clock", "%d/%d  %s (%lu kHz)", selected + 1, CLOCK_COUNT, CLOCKS[selected].name,
              static_cast<unsigned long>(clockHz(selected) / 1000));
    page.setCompact(true);
    page.rowf("Panel SPI clock now", "%lu kHz", static_cast<unsigned long>(panelSpiHz() / 1000));
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d of %d steps", stoppedAfter, STEP_COUNT);
    page.gap(4);
    page.heading("Mean BUSY + host time per refresh");
    for (int i = 0; i < CLOCK_COUNT; ++i) {
      const ClockResult& r = results[i];
      if (!r.valid) {
        page.row(CLOCKS[i].name, "not run");
        continue;
      }
      page.rowf(CLOCKS[i].name, "FAST %lu+%lu, HALF %lu+%lu ms, gray %s%s%s",
                static_cast<unsigned long>(r.busy[HalDisplay::FAST_REFRESH].meanMs()),
                static_cast<unsigned long>(r.hostMeanMs(HalDisplay::FAST_REFRESH)),
                static_cast<unsigned long>(r.busy[HalDisplay::HALF_REFRESH].meanMs()),
                static_cast<unsigned long>(r.hostMeanMs(HalDisplay::HALF_REFRESH)), r.grayRan ? "ran" : "skipped",
                r.verdict != nullptr ? ", " : "", r.verdict != nullptr ? r.verdict : "");
    }
  }

  std::unique_ptr<uint8_t[]> scratch;
  uint32_t profileHz = 0;
  int selected = CLOCK_COUNT - 1;  // the 40 MHz trial
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
