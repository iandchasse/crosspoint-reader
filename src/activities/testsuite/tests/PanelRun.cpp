#if CROSSPOINT_TEST_SUITE
#include "PanelRun.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/testsuite/TestCatalog.h"
#include "activities/testsuite/TestSuiteAccess.h"
#include "fontIds.h"

namespace testsuite {

// The driver reports modes in freeink::RefreshMode order (Full, Half, Fast).
static_assert(HalDisplay::FULL_REFRESH == 0 && HalDisplay::HALF_REFRESH == 1 && HalDisplay::FAST_REFRESH == 2,
              "RefreshSample compares HalDisplay modes with the driver's");

namespace {

// Every refresh must end inside this window. The completion wait gives up after
// 20 ms when BUSY never rises (no panel, since the pull-down reads idle, or BUSY
// stuck LOW) and after 30 s when it never falls (stuck HIGH); the quickest real
// waveform, FAST, takes about 500 ms.
constexpr uint32_t BUSY_MIN_MS = 100;
constexpr uint32_t BUSY_MAX_MS = 30000;

constexpr char PAUSE_HINT[] = "Any key or tap: continue.   Back: stop.";

}  // namespace

void fillFromDiagnostics(RefreshSample& sample, const freeink::EpdBusDiagnostics& before) {
  const auto now = freeink::EpdBus::diagnostics();
  sample.refreshes = static_cast<uint8_t>(std::min<uint32_t>(now.refreshes - before.refreshes, UINT8_MAX));
  if (sample.refreshes == 0) return;
  sample.ran = now.lastRefreshMode;
  sample.sequence = now.lastRefreshSequence;
  sample.async = now.lastRefreshAsync;
  sample.turnOff = now.lastRefreshTurnOff;
  sample.customLut = now.lastRefreshCustomLut;
  if (!now.refreshInFlight) {
    sample.busyMs = now.lastRefreshMs;
    sample.timedOut = now.lastRefreshTimedOut;
  }
}

bool RefreshSample::inWindow() const {
  return reported() && !timedOut && busyMs >= BUSY_MIN_MS && busyMs < BUSY_MAX_MS;
}

const char* refreshModeName(const uint8_t mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return "FULL";
    case HalDisplay::HALF_REFRESH:
      return "HALF";
    case HalDisplay::FAST_REFRESH:
      return "FAST";
    default:
      return "-";
  }
}

uint32_t panelSpiHz() { return TestSuiteAccess::eink(display).spiClockHz(); }

RefreshSample refreshTimed(const GfxRenderer& renderer, const HalDisplay::RefreshMode mode) {
  return measureRefresh(static_cast<uint8_t>(mode), [&renderer, mode] { renderer.displayBuffer(mode); });
}

RefreshSample refreshAsyncStart(const GfxRenderer& renderer, const HalDisplay::RefreshMode mode) {
  RefreshSample sample =
      measureRefresh(static_cast<uint8_t>(mode), [&renderer, mode] { renderer.displayBufferAsync(mode); });
  sample.busyAtReturn = TestSuiteAccess::eink(display).refreshBusy();
  return sample;
}

void refreshAsyncFinish(const GfxRenderer& renderer, RefreshSample& sample, const bool reseed) {
  if (reseed) {
    renderer.cleanupGrayscaleWithFrameBuffer();  // waits, then rebuilds the baseline
  } else {
    renderer.waitRefreshComplete();
  }
  if (!sample.reported()) return;
  const auto now = freeink::EpdBus::diagnostics();
  sample.busyMs = now.lastRefreshMs;
  sample.timedOut = now.lastRefreshTimedOut;
}

RefreshSample grayTimed(const GfxRenderer& renderer) {
  // The gray waveform has no refresh mode of its own: nothing to compare with.
  return measureRefresh(RefreshSample::NONE, [&renderer] { renderer.displayGrayBuffer(); });
}

void BusyStats::add(const uint32_t ms) {
  minMs = count == 0 ? ms : std::min(minMs, ms);
  maxMs = count == 0 ? ms : std::max(maxMs, ms);
  sumMs += ms;
  ++count;
}

// --- PanelRunTest --------------------------------------------------------------

void PanelRunTest::restoreDisplaySettings() {
  SETTINGS.fadingFix = 0;
  SETTINGS.screenInverted = 0;
}

void PanelRunTest::startRun(const int steps) {
  if (runOn() || steps <= 0) return;
  stepCount = steps;
  stepIndex = 0;
  paused = false;
  stepRan = false;
  stepQueued = false;
  closeRouting();  // the footer is not on the glass while the frames are
  phase = Phase::Arming;
  LOG_INF("TEST", "%s: run of %d steps", info.id, steps);
}

void PanelRunTest::onTick() {
  switch (phase.load()) {
    case Phase::Idle:
      onIdleTick();
      return;
    case Phase::Arming:
      // Step 0 waits a pass: the loop hands SETTINGS.fadingFix to the renderer at
      // the top of the next one.
      phase = Phase::Running;
      return;
    case Phase::Running:
      break;
  }
  if (paused) {
    // Back never gets here (TestScreen::loop leaves through onBack() first).
    int x = 0;
    int y = 0;
    if (gpio.wasAnyReleased() || mappedInput.wasScreenTapped(x, y)) resume();
    return;
  }
  if (stopsOnAnyInput()) {
    int x = 0;
    int y = 0;
    if (gpio.wasAnyReleased() || mappedInput.wasScreenTapped(x, y)) {
      LOG_INF("TEST", "%s: run stopped by input after %d steps", info.id, stepIndex);
      endRun(false);
      return;
    }
  }
  if (!stepQueued) {
    beforeStep(stepIndex);
    glassDirty = true;
    stepRan = false;
    stepQueued = true;
  }
  requestUpdateAndWait();
  // A render already under way answered the wait; the step runs on the next one.
  if (!stepRan) return;
  stepQueued = false;
  afterStep(stepIndex);
  ++stepIndex;
  if (!paused && stepIndex >= stepCount) endRun(true);
}

void PanelRunTest::resume() {
  paused = false;
  if (stepIndex >= stepCount) endRun(true);
}

void PanelRunTest::onSlot(const Slot slot) {
  if (!runOn()) {
    onIdleSlot(slot);
    return;
  }
  if (paused) {
    resume();
  } else if (stopsOnAnyInput()) {
    endRun(false);
  }
}

void PanelRunTest::onStep(const int direction) {
  if (!runOn()) {
    onIdleStep(direction);
    return;
  }
  if (paused) {
    resume();
  } else if (stopsOnAnyInput()) {
    endRun(false);
  }
}

bool PanelRunTest::onBack() {
  if (!runOn()) return false;
  LOG_INF("TEST", "%s: run stopped after %d of %d steps", info.id, stepIndex, stepCount);
  endRun(false);
  return true;
}

void PanelRunTest::endRun(const bool completed) {
  {
    RenderLock lock;
    restorePanel();
  }
  restoreDisplaySettings();
  paused = false;
  stepQueued = false;
  phase = Phase::Idle;
  requestCleanRepaint();
  requestUpdateAndWait();
  glassDirty = false;
  onRunEnd(completed);
}

void PanelRunTest::onExit() {
  // The ActivityManager holds the RenderLock here.
  restorePanel();
  restoreDisplaySettings();
  phase = Phase::Idle;
  // Whatever comes next paints over a test frame: its first refresh is a clean one.
  if (glassDirty) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  TestScreen::onExit();
}

void PanelRunTest::render(RenderLock&& lock) {
  if (phase.load() == Phase::Idle) {
    TestScreen::render(std::move(lock));
    return;
  }
  // While a run is on, only its steps draw: a stray repaint would cover the frame.
  if (!stepQueued.load() || stepRan.load()) return;
  runStep(stepIndex);
  stepRan = true;
}

void PanelRunTest::drawFrameLabel(const char* line1, const char* line2, const bool pauseHint) const {
  if (renderer.getRenderMode() != GfxRenderer::BW) return;  // a white box would mark the gray planes
  const int width = renderer.getScreenWidth();
  const int line12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
  int textWidth = renderer.getTextWidth(UI_12_FONT_ID, line1, EpdFontFamily::BOLD);
  if (line2 != nullptr) textWidth = std::max(textWidth, renderer.getTextWidth(UI_10_FONT_ID, line2));
  if (pauseHint) textWidth = std::max(textWidth, renderer.getTextWidth(UI_10_FONT_ID, PAUSE_HINT));
  const int boxWidth = std::min(textWidth + 32, width - 16);
  const int boxHeight = line12 + (line2 != nullptr ? line10 : 0) + (pauseHint ? line10 : 0) + 20;
  const int x = (width - boxWidth) / 2;
  const int y = (renderer.getScreenHeight() - boxHeight) / 2;
  renderer.fillRect(x, y, boxWidth, boxHeight, false);
  renderer.drawRect(x, y, boxWidth, boxHeight, 2, true);
  int top = y + 10;
  renderer.drawCenteredText(UI_12_FONT_ID, top, line1, true, EpdFontFamily::BOLD);
  top += line12;
  if (line2 != nullptr) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, line2);
    top += line10;
  }
  if (pauseHint) renderer.drawCenteredText(UI_10_FONT_ID, top, PAUSE_HINT);
}

}  // namespace testsuite
#endif
