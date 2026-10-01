#if CROSSPOINT_TEST_SUITE
// T6: night-mode inversion (bring-up B2). Night mode is driven the way the
// reader drives it: SETTINGS.screenInverted, which the render task hands to the
// display before every render (ActivityManager::renderTaskLoop). The display
// inverts on the way to the panel, promotes the first refresh after a polarity
// change from FAST to HALF, and skips the gray pass while inverted
// (FreeInkDisplay::displayGrayBuffer); each step checks the mode that ran.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "GrayPass.h"
#include "PanelPatterns.h"
#include "PanelRun.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"

namespace testsuite {
namespace {

constexpr int QUESTION_LOOK = 1;

struct Step {
  const char* label;
  bool inverted;  // SETTINGS.screenInverted for this step
  bool frameB;
  bool grayPass;  // the reader's gray pass on the chart instead of a B/W frame
  HalDisplay::RefreshMode mode;
  HalDisplay::RefreshMode expect;  // the mode the driver must run
  bool pause;
};

constexpr Step STEPS[] = {
    {"Normal, HALF", false, false, false, HalDisplay::HALF_REFRESH, HalDisplay::HALF_REFRESH, false},
    {"Inverted: FAST asked, first after the switch runs HALF", true, true, false, HalDisplay::FAST_REFRESH,
     HalDisplay::HALF_REFRESH, true},
    {"Inverted, FAST", true, false, false, HalDisplay::FAST_REFRESH, HalDisplay::FAST_REFRESH, true},
    {"Inverted, HALF", true, true, false, HalDisplay::HALF_REFRESH, HalDisplay::HALF_REFRESH, true},
    {"Inverted, gray pass: skipped, crisp B/W", true, false, true, HalDisplay::HALF_REFRESH, HalDisplay::HALF_REFRESH,
     true},
    {"Normal: FAST asked, runs HALF", false, false, false, HalDisplay::FAST_REFRESH, HalDisplay::HALF_REFRESH, true},
};
constexpr int STEP_COUNT = static_cast<int>(sizeof(STEPS) / sizeof(STEPS[0]));

constexpr const char* LOOK_ANSWERS[] = {"All as expected", "Inverted image wrong", "Gray while inverted",
                                        "Ghost after returning"};
constexpr const char* LOOK_KEYS[] = {"as_expected", "inverted_wrong", "gray_while_inverted", "ghost_after"};

constexpr char INTRO[] =
    "Confirm runs night mode through the reader's setting: a normal frame, then inverted FAST and HALF frames, the "
    "gray chart while inverted, and a normal frame again. Look at each, then answer.";
constexpr char EXPECT[] =
    "Inverted frames are white on black and clean on FAST and on HALF. While inverted the gray pass is skipped: "
    "the chart shows black and white only. The HALF after switching back leaves no ghost.";
constexpr char CHART_FOOTER[] = "Any key or tap: continue.   Back: stop.";

class InversionTest final : public PanelRunTest {
 public:
  InversionTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : PanelRunTest(info, renderer, mappedInput) {}

 private:
  const char* slotLabel(const Slot slot) const override { return slot == Slot::Confirm ? "Run" : nullptr; }

  void onIdleSlot(const Slot slot) override {
    if (slot != Slot::Confirm) return;
    haveRun = false;
    stoppedAfter = -1;
    completedSteps = 0;
    startRun(STEP_COUNT);
  }

  void beforeStep(const int step) override { SETTINGS.screenInverted = STEPS[step].inverted ? 1 : 0; }

  void runStep(const int step) override {
    const Step& s = STEPS[step];
    char title[64];
    snprintf(title, sizeof(title), "%s  %d/%d  %s", info.id, step + 1, STEP_COUNT, s.label);
    if (s.grayPass) {
      // The reader's gate for its tiled route; a plain panel offers it, an inverted one does not.
      tiledOffered = renderer.grayscaleCapabilities().stripUploads;
      runGrayPass(renderer, GrayPath::Buffered, GrayBuffers{}, title, CHART_FOOTER, gray);
      samples[step] = gray.base;
      return;
    }
    renderer.clearScreen();
    patterns::drawBwFrame(renderer, s.frameB);
    char label[24];
    snprintf(label, sizeof(label), "%s  %d/%d", info.id, step + 1, STEP_COUNT);
    drawFrameLabel(label, s.label, s.pause);
    samples[step] = refreshTimed(renderer, s.mode);
  }

  void afterStep(const int step) override {
    const RefreshSample& sample = samples[step];
    completedSteps = step + 1;
    LOG_INF("TEST", "T6 %d/%d %s: asked %s, ran %s (expect %s), seq=0x%02X, BUSY %lu ms%s", step + 1, STEP_COUNT,
            STEPS[step].inverted ? "inverted" : "normal", refreshModeName(sample.requested),
            refreshModeName(sample.ran), refreshModeName(STEPS[step].expect), sample.sequence,
            static_cast<unsigned long>(sample.busyMs),
            STEPS[step].grayPass ? (gray.gray.reported() ? ", gray waveform ran" : ", gray pass skipped") : "");
    if (STEPS[step].pause) pauseRun();
  }

  void onRunEnd(const bool completed) override {
    if (!completed) {
      stoppedAfter = completedSteps;
      requestUpdate();
      return;
    }
    haveRun = true;
    ask(QUESTION_LOOK, info.title, EXPECT, LOOK_ANSWERS, 4);
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_LOOK || !haveRun) return;
    if (answer < 0) {
      LOG_INF("TEST", "T6: no answer, nothing recorded");
      return;
    }
    char ran[48] = "";
    char expected[48] = "";
    int wrongMode = 0;
    int bad = 0;
    for (int i = 0; i < STEP_COUNT; ++i) {
      appendf(ran, sizeof(ran), "%s%s", i != 0 ? "," : "", refreshModeName(samples[i].ran));
      appendf(expected, sizeof(expected), "%s%s", i != 0 ? "," : "", refreshModeName(STEPS[i].expect));
      if (samples[i].ran != STEPS[i].expect) ++wrongMode;
      if (!samples[i].inWindow()) ++bad;
    }
    char fails[80] = "";
    if (wrongMode != 0) addFailure(fails, sizeof(fails), "mode");
    if (bad != 0) addFailure(fails, sizeof(fails), "busy_window");
    if (gray.gray.reported()) addFailure(fails, sizeof(fails), "gray_ran_inverted");
    if (gray.missingBuffer) addFailure(fails, sizeof(fails), "bw_store");
    if (tiledOffered) addFailure(fails, sizeof(fails), "tiled_offered_inverted");
    if (answer != 0) addFailure(fails, sizeof(fails), "operator");
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail, "ran=%s expected=%s gray=%s bad=%d operator=%s fails=%s",
           ran, expected, gray.gray.reported() ? "ran" : "skipped", bad, LOOK_KEYS[answer],
           fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    page.paragraph(INTRO, 5);
    page.gap(4);
    page.paragraph(EXPECT, 5);
    page.gap(4);
    page.setCompact(true);
    page.rowf("Night mode now (screenInverted)", "%s", SETTINGS.screenInverted != 0 ? "on" : "off");
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d of %d steps", stoppedAfter, STEP_COUNT);
    if (!haveRun) return;
    page.gap(4);
    page.heading("Last run: asked, ran (expected), BUSY");
    for (int i = 0; i < STEP_COUNT; ++i) {
      const RefreshSample& s = samples[i];
      char label[56];
      snprintf(label, sizeof(label), "%d %s", i + 1, STEPS[i].label);
      page.rowf(label, "%s, %s (%s), %lu ms", refreshModeName(s.requested), refreshModeName(s.ran),
                refreshModeName(STEPS[i].expect), static_cast<unsigned long>(s.busyMs));
    }
    page.row("Gray waveform while inverted", gray.missingBuffer     ? "B/W store failed"
                                             : gray.gray.reported() ? "RAN"
                                                                    : "skipped");
    page.row("Tiled gray route offered while inverted", tiledOffered ? "YES" : "no");
  }

  RefreshSample samples[STEP_COUNT];
  GrayRun gray;
  bool tiledOffered = false;
  bool haveRun = false;
  int completedSteps = 0;
  int stoppedAfter = -1;
};

}  // namespace

std::unique_ptr<Activity> createInversionTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<InversionTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
