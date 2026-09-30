#if CROSSPOINT_TEST_SUITE
// T5: grayscale (anti-aliasing) passes (bring-up B2). A four-level chart goes
// through each route the reader's gray pass takes (GrayPass.h), then a plain
// page with a FAST refresh shows whether the pass left gray behind and whether
// the driver's state was cleaned up (a FAST that runs as HALF would mask it).
// The plane buffers and the strip scratch are allocated when the screen opens
// and freed when it closes, the same sizes the reader's tiled path uses.
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_memory_utils.h>

#include <cstdio>
#include <memory>

#include "CrossPointSettings.h"
#include "GrayPass.h"
#include "PanelPatterns.h"
#include "PanelRun.h"
#include "TestFactories.h"
#include "activities/testsuite/TestCatalog.h"

namespace testsuite {
namespace {

constexpr int QUESTION_LOOK = 1;
constexpr int STEPS = 2;  // the chart with its gray pass, then the plain page

struct Variant {
  const char* name;
  GrayPath path;
  bool fadingFix;  // run with SETTINGS.fadingFix on
  const char* what;
};

constexpr Variant VARIANTS[] = {
    {"Text page, planes buffered", GrayPath::TiledPlanes, false,
     "The reader's anti-aliased page: a FAST base fired async, both planes rendered into 48 KB buffers while it "
     "runs, then streamed to the controller."},
    {"Text page, 80-row strips", GrayPath::TiledStrips, false,
     "The reader's fallback when two plane buffers do not fit: an 8 KB scratch, band by band, after the base."},
    {"Text page, Fading Fix on", GrayPath::FastBase, true,
     "With the Sunlight Fading Fix: displayGrayscaleBase(FAST), no async base, and a power-down after each "
     "refresh."},
    {"Cleanup page, HALF base", GrayPath::HalfBase, false,
     "The page that takes the reader's periodic HALF: a HALF base, then the strips."},
    {"Image page, FAST base", GrayPath::FastBaseImage, false,
     "A page with images: a blocking FAST base, then the strips."},
    {"Buffered (sleep, images)", GrayPath::Buffered, false,
     "The sleep screen's and the image viewer's route: a HALF base, the B/W frame stored, both planes copied "
     "whole, the B/W frame restored."},
};
constexpr int VARIANT_COUNT = static_cast<int>(sizeof(VARIANTS) / sizeof(VARIANTS[0]));

constexpr const char* LOOK_ANSWERS[] = {"4 levels, then a clean page", "Fewer than 4 levels",
                                        "Gray left on the plain page"};
constexpr const char* LOOK_KEYS[] = {"4_levels_clean", "fewer_levels", "gray_left"};

constexpr char INTRO[] =
    "Left/Right pick a route, Confirm runs it: the chart with its gray pass, then a plain page with a FAST "
    "refresh. Look at each, then answer.";
constexpr char CHART_FOOTER[] = "Any key or tap: continue.   Back: stop.";
constexpr char PLAIN_TITLE[] = "T5  After the gray pass: FAST";
constexpr char PLAIN_NOTE[] =
    "Below the line the page must be white, with no gray left from the chart. Any key or tap: continue. Back: "
    "stop.";

class GrayscaleTest final : public PanelRunTest {
 public:
  GrayscaleTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : PanelRunTest(info, renderer, mappedInput) {}

 private:
  void onStart() override {
    // The reader's tiled path: two whole planes for the overlapped route, a band
    // for the strip routes. Held while this screen is open.
    planeBytes = static_cast<size_t>(renderer.getDisplayWidthBytes()) * renderer.getDisplayHeight();
    scratchBytes = static_cast<size_t>(renderer.getDisplayWidthBytes()) * STRIP_ROWS;
    lsbPlane = makeUniqueNoThrow<uint8_t[]>(planeBytes);
    msbPlane = lsbPlane ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    scratch = makeUniqueNoThrow<uint8_t[]>(scratchBytes);
    if (!lsbPlane || !msbPlane) {
      LOG_ERR("TEST", "T5: OOM for the plane buffers (2 x %u B); that route will be skipped",
              static_cast<unsigned>(planeBytes));
      lsbPlane.reset();
      msbPlane.reset();
    }
    if (!scratch) LOG_ERR("TEST", "T5: OOM for the strip scratch (%u B)", static_cast<unsigned>(scratchBytes));
    planesInPsram = lsbPlane && esp_ptr_external_ram(lsbPlane.get()) && esp_ptr_external_ram(msbPlane.get());
  }

  void onExit() override {
    lsbPlane.reset();
    msbPlane.reset();
    scratch.reset();
    PanelRunTest::onExit();
  }

  GrayBuffers buffers() const { return {lsbPlane.get(), msbPlane.get(), scratch.get()}; }

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
    haveRun = false;
    stoppedAfter = -1;
    completedSteps = 0;
    if (VARIANTS[runVariant].fadingFix) SETTINGS.fadingFix = 1;
    startRun(STEPS);
  }

  void runStep(const int step) override {
    if (step == 0) {
      char title[48];
      snprintf(title, sizeof(title), "%s  %s", info.id, VARIANTS[runVariant].name);
      runGrayPass(renderer, VARIANTS[runVariant].path, buffers(), title, CHART_FOOTER, pass);
      return;
    }
    renderer.clearScreen();
    patterns::drawPlainPage(renderer, PLAIN_TITLE, PLAIN_NOTE);
    nextPage = refreshTimed(renderer, HalDisplay::FAST_REFRESH);
  }

  void afterStep(const int step) override {
    completedSteps = step + 1;
    if (step == 0) {
      LOG_INF("TEST",
              "T5 %s: base asked %s ran %s 0x22=0x%02X BUSY %lu ms%s; gray %s 0x22=0x%02X BUSY %lu ms; render %u, "
              "wait %u, write %u, cleanup %u ms%s",
              grayPathName(VARIANTS[runVariant].path), refreshModeName(pass.base.requested),
              refreshModeName(pass.base.ran), pass.base.sequence, static_cast<unsigned long>(pass.base.busyMs),
              pass.base.async ? " async" : "", pass.grayRan() ? "ran" : "skipped", pass.gray.sequence,
              static_cast<unsigned long>(pass.gray.busyMs), pass.renderMs, pass.waitMs, pass.writeMs, pass.cleanupMs,
              pass.missingBuffer ? "; a buffer was missing" : "");
    } else {
      LOG_INF("TEST", "T5 plain page: asked FAST, ran %s, BUSY %lu ms", refreshModeName(nextPage.ran),
              static_cast<unsigned long>(nextPage.busyMs));
    }
    pauseRun();
  }

  void onRunEnd(const bool completed) override {
    if (!completed) {
      stoppedAfter = completedSteps;
      requestUpdate();
      return;
    }
    haveRun = true;
    if (pass.missingBuffer) {
      record(Verdict::Skip, "route=%s missing_buffer=1 planes=%s scratch=%s", grayPathName(VARIANTS[runVariant].path),
             lsbPlane ? "yes" : "no", scratch ? "yes" : "no");
      return;
    }
    ask(QUESTION_LOOK, VARIANTS[runVariant].name,
        "Did the chart show four distinct levels, and the plain page no gray?", LOOK_ANSWERS, 3);
  }

  void onAnswer(const int question, const int answer) override {
    if (question != QUESTION_LOOK || !haveRun) return;
    if (answer < 0) {
      LOG_INF("TEST", "T5: no answer, nothing recorded");
      return;
    }
    const Variant& variant = VARIANTS[runVariant];
    const bool tiledAsync = variant.path == GrayPath::TiledPlanes || variant.path == GrayPath::TiledStrips;
    char fails[80] = "";
    if (!pass.base.inWindow() || pass.base.promoted()) addFailure(fails, sizeof(fails), "base");
    if (tiledAsync && !(pass.base.async && pass.base.busyAtReturn)) addFailure(fails, sizeof(fails), "async_base");
    if (variant.fadingFix && !(pass.base.turnOff && pass.gray.turnOff)) addFailure(fails, sizeof(fails), "turn_off");
    if (!pass.grayRan()) {
      addFailure(fails, sizeof(fails), "gray_skipped");
    } else if (!pass.gray.inWindow()) {
      addFailure(fails, sizeof(fails), "gray_busy");
    }
    if (!nextPage.inWindow()) addFailure(fails, sizeof(fails), "next_page");
    if (nextPage.promoted()) addFailure(fails, sizeof(fails), "next_fast_promoted");
    if (answer != 0) addFailure(fails, sizeof(fails), "operator");
    record(fails[0] == '\0' ? Verdict::Pass : Verdict::Fail,
           "route=%s base=%s@%02X:%lu gray=%02X:%lu next=%s:%lu render_ms=%u wait_ms=%u write_ms=%u cleanup_ms=%u "
           "heap_used=%u psram_used=%u operator=%s fails=%s",
           grayPathName(variant.path), refreshModeName(pass.base.ran), pass.base.sequence,
           static_cast<unsigned long>(pass.base.busyMs), pass.grayRan() ? pass.gray.sequence : 0,
           static_cast<unsigned long>(pass.gray.busyMs), refreshModeName(nextPage.ran),
           static_cast<unsigned long>(nextPage.busyMs), pass.renderMs, pass.waitMs, pass.writeMs, pass.cleanupMs,
           static_cast<unsigned>(pass.internalFreeBefore - pass.internalFreeMin),
           static_cast<unsigned>(pass.psramFreeBefore - pass.psramFreeMin), LOOK_KEYS[answer],
           fails[0] == '\0' ? "none" : fails);
  }

  void drawBody(TestPage& page) const override {
    const Variant& variant = VARIANTS[selected];
    page.paragraph(INTRO, 3);
    page.gap(4);
    page.rowf("Route", "%d/%d  %s", selected + 1, VARIANT_COUNT, variant.name);
    page.paragraph(variant.what, 4);
    page.setCompact(true);
    const auto caps = renderer.grayscaleCapabilities();
    page.rowf("Panel: strips / async base", "%s / %s", caps.stripUploads ? "yes" : "no", caps.asyncBase ? "yes" : "no");
    if (lsbPlane) {
      page.rowf("Plane buffers", "2 x %u B, %s", static_cast<unsigned>(planeBytes),
                planesInPsram ? "PSRAM" : "internal RAM");
    } else {
      page.row("Plane buffers", "not allocated (route 1 skips)");
    }
    page.rowf("Strip scratch", scratch ? "%u B" : "not allocated (%u B)", static_cast<unsigned>(scratchBytes));
    if (stoppedAfter >= 0) page.rowf("Last run", "stopped after %d of %d steps", stoppedAfter, STEPS);
    if (!haveRun) return;
    page.gap(4);
    char heading[48];
    snprintf(heading, sizeof(heading), "Last run: %s", VARIANTS[runVariant].name);
    page.heading(heading);
    page.rowf("Base: asked / ran", "%s / %s, 0x22 = 0x%02X, BUSY %lu ms%s", refreshModeName(pass.base.requested),
              refreshModeName(pass.base.ran), pass.base.sequence, static_cast<unsigned long>(pass.base.busyMs),
              pass.base.async ? (pass.base.busyAtReturn ? ", async" : ", async (not busy)") : "");
    if (pass.missingBuffer) {
      page.row("Gray pass", "skipped: a buffer was missing");
    } else if (pass.grayRan()) {
      page.rowf("Gray waveform", "0x22 = 0x%02X, BUSY %lu ms", pass.gray.sequence,
                static_cast<unsigned long>(pass.gray.busyMs));
    } else {
      page.row("Gray waveform", "did not run");
    }
    page.rowf("Planes: render / wait / write", "%u / %u / %u ms", pass.renderMs, pass.waitMs, pass.writeMs);
    page.rowf(variant.path == GrayPath::Buffered ? "B/W frame restored" : "Baseline rebuilt", "%u ms", pass.cleanupMs);
    page.rowf("Plain page: asked FAST, ran", "%s, BUSY %lu ms", refreshModeName(nextPage.ran),
              static_cast<unsigned long>(nextPage.busyMs));
    page.rowf("Internal heap free: before / lowest", "%u / %u B", static_cast<unsigned>(pass.internalFreeBefore),
              static_cast<unsigned>(pass.internalFreeMin));
    page.rowf("PSRAM free: before / lowest", "%u / %u B", static_cast<unsigned>(pass.psramFreeBefore),
              static_cast<unsigned>(pass.psramFreeMin));
  }

  std::unique_ptr<uint8_t[]> lsbPlane;
  std::unique_ptr<uint8_t[]> msbPlane;
  std::unique_ptr<uint8_t[]> scratch;
  size_t planeBytes = 0;
  size_t scratchBytes = 0;
  bool planesInPsram = false;
  int selected = 0;
  int runVariant = 0;
  int completedSteps = 0;
  int stoppedAfter = -1;
  bool haveRun = false;
  GrayRun pass;
  RefreshSample nextPage;
};

}  // namespace

std::unique_ptr<Activity> createGrayscaleTest(const TestInfo& info, GfxRenderer& renderer,
                                              MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<GrayscaleTest>(info, renderer, mappedInput);
}

}  // namespace testsuite
#endif
