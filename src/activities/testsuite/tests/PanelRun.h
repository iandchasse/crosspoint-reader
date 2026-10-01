#pragma once

// Shared by the display tests T4-T8 (CROSSPOINT_TEST_SUITE builds only): what a
// refresh did, as the panel driver reported it, and the run of full-screen
// frames a test steps through.
#if CROSSPOINT_TEST_SUITE
#include <Arduino.h>
#include <HalDisplay.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "TextBuffer.h"
#include "activities/testsuite/TestScreen.h"

class GfxRenderer;

namespace testsuite {

// One refresh as the driver reported it (EpdBus diagnostics): the mode it ran
// after any promotion of the requested one, and its BUSY time.
struct RefreshSample {
  static constexpr uint8_t NONE = 0xFF;
  uint32_t busyMs = 0;       // activation to the end of its completion wait
  uint16_t callMs = 0;       // the display call: SPI transfers, BUSY (unless async), power-down
  uint8_t requested = NONE;  // HalDisplay::RefreshMode
  uint8_t ran = NONE;        // the same order (Full, Half, Fast); NONE: no refresh reported
  uint8_t sequence = 0;      // the update-sequence byte (SSD1677 0x22)
  uint8_t refreshes = 0;     // activations the call reported
  bool timedOut = false;
  bool async = false;         // fired without waiting
  bool turnOff = false;       // power-down requested (the Sunlight Fading Fix)
  bool customLut = false;     // the grayscale waveform
  bool busyAtReturn = false;  // async: BUSY still asserted when the call returned

  bool reported() const { return ran != NONE; }
  bool promoted() const { return reported() && requested != NONE && ran != requested; }
  // Within the window every refresh must meet (see PanelRun.cpp).
  bool inWindow() const;
  uint32_t hostMs() const { return callMs > busyMs ? callMs - busyMs : 0; }
};

const char* refreshModeName(uint8_t mode);  // "FULL", "HALF", "FAST", "-"
uint32_t panelSpiHz();                      // the clock the panel's SPI transactions run at now

// What the driver reported since `before` (EpdBus diagnostics).
void fillFromDiagnostics(RefreshSample& sample, const freeink::EpdBusDiagnostics& before);

// Render task, RenderLock held. Times one display call (`call` refreshes) and
// fills in what the driver reported for it.
template <typename Call>
RefreshSample measureRefresh(const uint8_t requested, Call&& call) {
  RefreshSample sample;
  sample.requested = requested;
  const auto before = freeink::EpdBus::diagnostics();
  const unsigned long start = millis();
  call();
  const unsigned long elapsed = millis() - start;
  sample.callMs = static_cast<uint16_t>(elapsed < UINT16_MAX ? elapsed : UINT16_MAX);
  fillFromDiagnostics(sample, before);
  return sample;
}

// Render task, RenderLock held. Blocking refresh of the framebuffer.
RefreshSample refreshTimed(const GfxRenderer& renderer, HalDisplay::RefreshMode mode);
// The reader's deferred refresh (displayBufferAsync()); finish it with
// refreshAsyncFinish() before touching the framebuffer or the panel.
RefreshSample refreshAsyncStart(const GfxRenderer& renderer, HalDisplay::RefreshMode mode);
// Waits out the refresh; `reseed` rebuilds the differential baseline from the
// framebuffer as the reader does after its shadow-free async refresh.
void refreshAsyncFinish(const GfxRenderer& renderer, RefreshSample& sample, bool reseed);
// displayGrayBuffer(): the gray waveform, or no refresh when the pass is skipped.
RefreshSample grayTimed(const GfxRenderer& renderer);

// Minimum, mean and maximum of BUSY times.
struct BusyStats {
  uint16_t count = 0;
  uint32_t minMs = 0;
  uint32_t maxMs = 0;
  uint32_t sumMs = 0;
  void add(uint32_t ms);
  uint32_t meanMs() const { return count != 0 ? sumMs / count : 0; }
};

// A display test that steps through full-screen frames: each step draws one
// frame and refreshes it on the render task, one loop pass apart, so Back ends
// a run between steps. A step can hold its frame on the glass until any key or
// a tap (pause). The run ends with a HALF repaint of the test's own screen.
// While a run is on, the touch footer is not routed (it is not on the glass).
class PanelRunTest : public TestScreen {
 protected:
  PanelRunTest(const TestInfo& info, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TestScreen(info, renderer, mappedInput) {}

  // --- run contract ----------------------------------------------------------
  // Loop task, before step `step` is queued: SETTINGS a step depends on (the
  // render task applies screenInverted to every render).
  virtual void beforeStep(int /*step*/) {}
  // Render task, RenderLock held: draw the step's frame and refresh it.
  virtual void runStep(int step) = 0;
  // Loop task, after the step ran: store what it measured, pauseRun() to hold it.
  virtual void afterStep(int /*step*/) {}
  // RenderLock held: put the panel back as the reader has it (clock, ...).
  // Runs at the end of every run and on exit. SETTINGS are restored separately.
  virtual void restorePanel() {}
  // Loop task, after the HALF repaint that ends a run: judge and record.
  virtual void onRunEnd(bool /*completed*/) {}
  // Idle input and ticks (no run on).
  virtual void onIdleSlot(Slot) {}
  virtual void onIdleStep(int) {}
  virtual void onIdleTick() {}
  // True while any key or tap ends the run as Back does (a run with no frame to
  // hold, such as T4's scope loop).
  virtual bool stopsOnAnyInput() const { return false; }

  // --- helpers ---------------------------------------------------------------
  // Loop task. Starts a run of `steps` steps. SETTINGS.fadingFix set here reach
  // the renderer before step 0 (the loop applies them at the top of a pass).
  void startRun(int steps);
  void pauseRun() { paused = true; }
  // From afterStep(): the step that just ran is the run's last, and the run
  // completes after it.
  void endAfterThisStep() { stepCount = stepIndex + 1; }
  bool runOn() const { return phase.load() != Phase::Idle; }
  // Back to the pinned values (TestSuite.cpp): Fading Fix off, normal polarity.
  static void restoreDisplaySettings();
  // A labelled box in the middle of the frame, with the pause hint when asked.
  void drawFrameLabel(const char* line1, const char* line2, bool pauseHint) const;

  // Puts the panel and SETTINGS back; a test that frees buffers here calls it last.
  void onExit() override;

 private:
  enum class Phase : uint8_t { Idle, Arming, Running };

  void onSlot(Slot slot) final;
  void onStep(int direction) final;
  void onTick() final;
  bool onBack() final;
  void render(RenderLock&& lock) override;

  void resume();
  void endRun(bool completed);

  std::atomic<Phase> phase{Phase::Idle};
  std::atomic<bool> stepQueued{false};
  std::atomic<bool> stepRan{false};
  int stepIndex = 0;  // written by the loop task before stepQueued is set
  int stepCount = 0;
  bool paused = false;
  bool glassDirty = false;  // a frame is on the glass that the next screen must clean
};

}  // namespace testsuite
#endif
